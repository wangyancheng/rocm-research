/*
 * hsa_vector_add.cpp  —  经典 HSA 向量加法（纯 HSA Runtime C API）
 *
 * 演示 AMD HSA（Heterogeneous System Architecture）运行时的完整调用链：
 *
 *   hsa_init()
 *     └─ hsa_iterate_agents()              # 枚举 CPU/GPU Agent
 *         └─ hsa_amd_agent_iterate_memory_pools()  # 枚举内存池
 *
 *   hsa_amd_memory_pool_allocate()         # 在 GPU VRAM 分配设备数组
 *   hsa_memory_copy()                      # Host → Device 数据搬运
 *
 *   hsa_queue_create()                     # 创建 AQL 命令队列
 *   hsa_code_object_reader_create_from_memory()
 *   hsa_executable_create/load/freeze()   # 加载并链接 .hsaco
 *   hsa_executable_get_symbol_by_name()   # 查找内核入口符号
 *   hsa_executable_symbol_get_info()      # 读取内核元数据
 *
 *   hsa_signal_create()                   # 完成信号（初值 = 1）
 *   [构造 hsa_kernel_dispatch_packet_t]   # 填写 AQL Dispatch Packet
 *   hsa_signal_store(doorbell_signal)     # 敲 Doorbell，触发 GPU 执行
 *   hsa_signal_wait_scacquire()           # 等待 GPU 写 signal → 0
 *
 *   hsa_memory_copy()                     # Device → Host 结果回传
 *   [验证结果]
 *   [清理所有 HSA 对象]
 *   hsa_shut_down()
 *
 * 内存模型（显式 Host ↔ Device 搬运）：
 *   Host malloc  ──hsa_memory_copy──>  GPU VRAM  ──[GPU kernel]
 *                                      GPU VRAM  ──hsa_memory_copy──>  Host malloc
 *
 * 目标硬件  : AMD MI50 (gfx906 / Vega20), PCIe 离散 GPU
 * ROCm 版本 : 5.6
 * HSA 规范  : 1.2  (hsa_ext_amd.h 扩展)
 */

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>

/* ══════════════════════════════════════════════════════════════
 * 宏：HSA 调用结果检查
 * ══════════════════════════════════════════════════════════════ */
#define HSA_CHECK(expr)                                                    \
    do {                                                                   \
        hsa_status_t _s = (expr);                                          \
        if (_s != HSA_STATUS_SUCCESS) {                                    \
            const char* _msg = nullptr;                                    \
            hsa_status_string(_s, &_msg);                                  \
            std::cerr << "[HSA ERROR] " << __FILE__ << ":" << __LINE__    \
                      << "  " << #expr << "\n"                             \
                      << "  -> " << (_msg ? _msg : "unknown") << "\n";    \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

/* ══════════════════════════════════════════════════════════════
 * 向量参数
 * ══════════════════════════════════════════════════════════════ */
static constexpr uint32_t N       = 1024;   /* 元素个数                   */
static constexpr uint32_t WG_SIZE = 64;     /* workgroup 大小 = gfx906 wavefront 宽度 */

/* ══════════════════════════════════════════════════════════════
 * Agent 与内存池发现
 *
 * HSA 运行时把系统里所有处理器（CPU / GPU / DSP …）抽象为 Agent。
 * 每个 Agent 拥有若干 MemoryPool，描述它能访问的物理内存区域。
 * ══════════════════════════════════════════════════════════════ */

/* ── Agent 枚举回调 ─────────────────────────────────────────── */
struct AgentCtx {
    hsa_agent_t cpu;        /* 主机 CPU Agent：用于发现系统内存池 */
    hsa_agent_t gpu;        /* GPU Agent：执行内核的目标设备       */
    bool cpu_found = false;
    bool gpu_found = false;
};

static hsa_status_t cb_find_agents(hsa_agent_t agent, void* data)
{
    auto* ctx = static_cast<AgentCtx*>(data);

    hsa_device_type_t type;
    hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &type);

    char name[64] = {};
    hsa_agent_get_info(agent, HSA_AGENT_INFO_NAME, name);

    if (type == HSA_DEVICE_TYPE_CPU && !ctx->cpu_found) {
        ctx->cpu = agent;
        ctx->cpu_found = true;
        std::cout << "[Agent] CPU: " << name << "\n";
    }
    if (type == HSA_DEVICE_TYPE_GPU && !ctx->gpu_found) {
        ctx->gpu = agent;
        ctx->gpu_found = true;
        std::cout << "[Agent] GPU: " << name << "\n";
    }
    /* 返回 SUCCESS 继续枚举；若已找齐可提前返回 INFO 终止迭代 */
    return HSA_STATUS_SUCCESS;
}

/* ── 内存池枚举回调：在 GPU Agent 上找 VRAM 全局内存池 ─────── */
/*
 * GPU Agent 的内存池类型（典型 MI50）：
 *   GLOBAL  + COARSE_GRAINED  → VRAM 主显存（最大，用于数据）
 *   GROUP                     → LDS（本地共享内存，每 CU 64 KB）
 *   PRIVATE                   → 每线程寄存器溢出 scratch 空间
 */
struct PoolCtx {
    hsa_amd_memory_pool_t gpu_global;   /* GPU VRAM 全局内存池            */
    hsa_amd_memory_pool_t cpu_kernarg;  /* CPU 侧 Kernarg 专用内存池      */
    bool gpu_global_found  = false;
    bool cpu_kernarg_found = false;
};

static hsa_status_t cb_find_gpu_pool(hsa_amd_memory_pool_t pool, void* data)
{
    auto* ctx = static_cast<PoolCtx*>(data);

    hsa_amd_segment_t seg;
    hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &seg);

    /* 只关心 GLOBAL segment（对应 VRAM / Device Memory） */
    if (seg == HSA_AMD_SEGMENT_GLOBAL && !ctx->gpu_global_found) {
        /* 检查内存池是否允许分配（部分 GLOBAL pool 是只读的） */
        bool alloc_allowed = false;
        hsa_amd_memory_pool_get_info(pool,
            HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED, &alloc_allowed);
        if (alloc_allowed) {
            ctx->gpu_global = pool;
            ctx->gpu_global_found = true;
        }
    }
    return HSA_STATUS_SUCCESS;
}

static hsa_status_t cb_find_cpu_pool(hsa_amd_memory_pool_t pool, void* data)
{
    auto* ctx = static_cast<PoolCtx*>(data);

    hsa_amd_segment_t seg;
    hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &seg);

    if (seg != HSA_AMD_SEGMENT_GLOBAL) return HSA_STATUS_SUCCESS;

    uint32_t flags = 0;
    hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flags);

    /*
     * HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT = 1
     *   → 专为内核参数（kernarg）设计的细粒度系统内存池
     *     CPU 可直接写入，GPU 通过 PCIe / IOMMU 可见。
     *     比普通 VRAM 的 hsa_memory_copy 写法更高效（零拷贝）。
     */
    if ((flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT) &&
        !ctx->cpu_kernarg_found) {
        ctx->cpu_kernarg = pool;
        ctx->cpu_kernarg_found = true;
    }
    return HSA_STATUS_SUCCESS;
}

/* ══════════════════════════════════════════════════════════════
 * Code Object 加载与 Executable 创建
 *
 *  HSA Code Object (.hsaco) = LLVM/ELF 格式的设备端二进制，
 *  内含 GPU machine code、内核描述符（KD）和元数据（YAML）。
 *
 *  加载流程：
 *    文件 → 内存缓冲 → CodeObjectReader → Executable
 *    Executable 相当于动态链接后的可运行程序，类比 dlopen()。
 * ══════════════════════════════════════════════════════════════ */
static hsa_executable_t load_hsaco(hsa_agent_t gpu,
                                   const char* path,
                                   hsa_code_object_reader_t& out_reader)
{
    /* ── 读取 .hsaco 文件到内存 ─────────────────────────────── */
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        std::cerr << "无法打开内核文件: " << path << "\n";
        std::exit(1);
    }
    std::streamsize sz = f.tellg();
    f.seekg(0);

    /*
     * HSA Code Object Loader 要求缓冲区至少 4096 字节对齐，
     * 否则映射 ELF section 时会产生 alignment fault。
     */
    void* buf = nullptr;
    if (posix_memalign(&buf, 4096, static_cast<size_t>(sz)) != 0) {
        std::cerr << "posix_memalign 失败\n";
        std::exit(1);
    }
    f.read(static_cast<char*>(buf), sz);
    f.close();

    /* ── CodeObjectReader：包装 ELF 内存缓冲 ───────────────── */
    HSA_CHECK(hsa_code_object_reader_create_from_memory(buf, sz, &out_reader));

    /* ── Executable：HSA "进程"，持有加载后的内核代码 ─────── */
    hsa_executable_t exe;
    HSA_CHECK(hsa_executable_create(
        HSA_PROFILE_BASE,               /* BASE = 离散 GPU（区别于 FULL = APU）*/
        HSA_EXECUTABLE_STATE_UNFROZEN,  /* 未冻结：允许继续 load code object   */
        nullptr, &exe));

    /* 把 code object 绑定到指定 GPU Agent（可多次调用支持多 GPU） */
    HSA_CHECK(hsa_executable_load_agent_code_object(
        exe, gpu, out_reader, nullptr, nullptr));

    /*
     * Freeze：完成符号解析与重定位，类比 dlopen() 之后 ld.so 的链接步骤。
     * Freeze 之后 executable 不可再修改，但可并发读取。
     */
    HSA_CHECK(hsa_executable_freeze(exe, nullptr));

    return exe;
}

/* ══════════════════════════════════════════════════════════════
 * Kernarg 参数布局
 *
 * 必须与 kernel.cl 的参数列表严格对应（HSA OpenCL C ABI）：
 *   __global const float* a  → 8 B（设备虚拟地址）
 *   __global const float* b  → 8 B
 *   __global       float* c  → 8 B
 *   unsigned int n           → 4 B
 *   [隐式填充 4 B]
 * ══════════════════════════════════════════════════════════════ */
struct KernelArgs {
    uint64_t a_ptr;     /* &a[0] 的 GPU 虚拟地址 */
    uint64_t b_ptr;     /* &b[0] 的 GPU 虚拟地址 */
    uint64_t c_ptr;     /* &c[0] 的 GPU 虚拟地址 */
    uint32_t n;         /* 元素个数              */
    uint32_t _pad = 0;  /* ABI 隐式填充          */
};

/* ══════════════════════════════════════════════════════════════
 * AQL Kernel Dispatch Packet 构造与提交
 *
 * AQL (Architected Queuing Language) 是 HSA 规范定义的命令格式。
 * 队列是一个环形缓冲区（ring buffer），CPU 写 packet、GPU 消费。
 *
 *  CPU 侧写入流程：
 *   1. add_write_index()  → 原子申请写槽位 idx
 *   2. 填写 packet 字段（header 最后写，保证可见性顺序）
 *   3. 原子 store header  → 激活 packet（对 GPU "可见"）
 *   4. store doorbell     → 敲响门铃，通知 CP（Command Processor）
 * ══════════════════════════════════════════════════════════════ */
static void dispatch_kernel(hsa_queue_t*   queue,
                            uint64_t       kernel_object,
                            uint32_t       private_seg_size,
                            uint32_t       group_seg_size,
                            void*          kernarg,
                            hsa_signal_t   done_signal,
                            uint32_t       grid_x,
                            uint32_t       wg_x)
{
    /*
     * 原子增加队列写指针，获取独占写槽位。
     * 返回的 idx 是全局单调递增计数器；取模得到 ring buffer 中的位置。
     * screlease 语义：保证之前的所有写操作在 idx 对其他核可见之前完成。
     */
    uint64_t idx = hsa_queue_add_write_index_screlease(queue, 1);

    /* 等待队列未满（ring buffer 溢出保护） */
    while (idx - hsa_queue_load_read_index_scacquire(queue) >= queue->size)
        ; /* 忙等；生产代码建议加 CPU pause 指令 */

    /* 计算 packet 在 ring buffer 中的实际槽位（编译器可优化为 AND） */
    uint64_t slot = idx & (queue->size - 1);
    auto* pkt = &reinterpret_cast<hsa_kernel_dispatch_packet_t*>(
                      queue->base_address)[slot];

    /* 先清零整个 64 字节 packet，再按字段填写 */
    std::memset(pkt, 0, sizeof(*pkt));

    /*
     * ── Dispatch 维度 ─────────────────────────────────────────
     * setup 的 DIMENSIONS 字段告诉 CP 用几维 grid。
     * 向量加法只需 1D（X 维），Y/Z 均为 1。
     */
    pkt->setup             = 1u << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
    pkt->workgroup_size_x  = static_cast<uint16_t>(wg_x);
    pkt->workgroup_size_y  = 1;
    pkt->workgroup_size_z  = 1;
    pkt->grid_size_x       = grid_x;   /* 总线程数（>=N，向上对齐到 wg_x） */
    pkt->grid_size_y       = 1;
    pkt->grid_size_z       = 1;

    /*
     * ── 内存段大小（来自内核 ELF 元数据）──────────────────────
     * private_segment_size：每线程栈/spill 寄存器大小（字节）
     * group_segment_size  ：该 workgroup 所需 LDS 大小（字节）
     *   本内核无 LDS 用法，两者均为 0。
     */
    pkt->private_segment_size = private_seg_size;
    pkt->group_segment_size   = group_seg_size;

    /* kernel_object：内核代码入口的 GPU 虚拟地址（由符号表查得） */
    pkt->kernel_object    = kernel_object;

    /* kernarg_address：指向 GPU 可见的内核参数区域（KernelArgs 结构体） */
    pkt->kernarg_address  = kernarg;

    /* completion_signal：GPU 执行完毕后将其值从 1 原子递减到 0 */
    pkt->completion_signal = done_signal;

    /*
     * ── Header：最后原子写入，激活 packet ─────────────────────
     *
     * 必须最后写 header！若先写 header GPU 可能在 packet
     * 其余字段填写完毕之前就开始执行，产生 UB。
     *
     * ACQUIRE fence：确保 GPU 在读取 kernarg / 全局内存之前，
     *                能看到 CPU 之前所有的写操作（Store → Load 排序）。
     * RELEASE fence：确保 GPU 写入全局内存之后，
     *                CPU 能看到这些写操作（用于 hsa_memory_copy 之前）。
     * SYSTEM scope：栅栏范围覆盖整个系统（包括 PCIe 对端的 CPU）。
     * BARRIER bit ：等待队列中前面的所有 packet 完成后再执行本 packet。
     */
    uint16_t header =
        (static_cast<uint16_t>(HSA_PACKET_TYPE_KERNEL_DISPATCH)
            << HSA_PACKET_HEADER_TYPE) |
        (1u << HSA_PACKET_HEADER_BARRIER) |
        (static_cast<uint16_t>(HSA_FENCE_SCOPE_SYSTEM)
            << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
        (static_cast<uint16_t>(HSA_FENCE_SCOPE_SYSTEM)
            << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);

    /* __ATOMIC_RELEASE：保证 packet 其余字段在 header 之前对 GPU 可见 */
    __atomic_store_n(&pkt->header, header, __ATOMIC_RELEASE);

    /*
     * 敲 Doorbell：将当前写指针 idx 写入 doorbell_signal。
     * CP 硬件监听 doorbell，收到后开始从队列取 packet 执行。
     * relaxed 语义足够：header 的 RELEASE 已建立了必要的 happens-before。
     */
    hsa_signal_store_relaxed(queue->doorbell_signal,
                             static_cast<hsa_signal_value_t>(idx));
}

/* ══════════════════════════════════════════════════════════════
 * main
 * ══════════════════════════════════════════════════════════════ */
int main()
{
    /* ────────────────────────────────────────────────────────────
     * 步骤 1：初始化 HSA Runtime
     *
     * hsa_init() 内部流程（追踪路径）：
     *   libhsa-runtime64.so
     *     → ROCr::Runtime::Acquire()
     *       → thunk (ROCT): hsaKmtOpenKFD()      // 打开 /dev/kfd
     *         → KFD 内核驱动: kfd_open()
     *   此后 Runtime 内部 topology 线程枚举所有 GPU 节点。
     * ────────────────────────────────────────────────────────────*/
    HSA_CHECK(hsa_init());
    std::cout << "[1/9] HSA Runtime 初始化完成\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 2：枚举 Agent（处理器）
     *
     * HSA 把系统所有处理器抽象为 Agent，通过回调逐一遍历。
     * 底层：ROCr 从 /sys/class/kfd/kfd/topology/ 读取拓扑信息。
     * ────────────────────────────────────────────────────────────*/
    AgentCtx agents;
    HSA_CHECK(hsa_iterate_agents(cb_find_agents, &agents));
    if (!agents.gpu_found) {
        std::cerr << "未找到 GPU Agent，请检查 ROCm 驱动\n";
        return 1;
    }

    /* 查询 wavefront 大小（gfx906 = 64，部分新架构支持 32） */
    uint32_t wavefront_size = 0;
    hsa_agent_get_info(agents.gpu, HSA_AGENT_INFO_WAVEFRONT_SIZE, &wavefront_size);
    std::cout << "       GPU Wavefront size: " << wavefront_size << "\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 3：枚举内存池
     *
     * 对 GPU Agent 找 VRAM global pool（存放数组数据）。
     * 对 CPU Agent 找 kernarg pool（存放内核参数，细粒度系统内存）。
     * ────────────────────────────────────────────────────────────*/
    PoolCtx pools;
    HSA_CHECK(hsa_amd_agent_iterate_memory_pools(agents.gpu, cb_find_gpu_pool, &pools));
    HSA_CHECK(hsa_amd_agent_iterate_memory_pools(agents.cpu, cb_find_cpu_pool, &pools));

    if (!pools.gpu_global_found) {
        std::cerr << "未找到 GPU VRAM 全局内存池\n";
        return 1;
    }
    std::cout << "[2/9] Agent & 内存池发现完成\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 4：在 GPU VRAM 分配设备数组
     *
     * hsa_amd_memory_pool_allocate() 底层调用：
     *   → thunk: hsaKmtAllocMemory()
     *     → KFD ioctl: KFD_IOC_ALLOC_MEMORY_OF_GPU（分配 BO）
     *       → amdgpu_gem_object_create()
     *
     * 分配后只有 GPU 默认可访问；hsa_amd_agents_allow_access()
     * 建立额外 Agent 的 IOMMU/GPUVM 页表映射。
     * ────────────────────────────────────────────────────────────*/
    const size_t arr_bytes = N * sizeof(float);

    float *a_dev, *b_dev, *c_dev;
    HSA_CHECK(hsa_amd_memory_pool_allocate(pools.gpu_global, arr_bytes, 0, (void**)&a_dev));
    HSA_CHECK(hsa_amd_memory_pool_allocate(pools.gpu_global, arr_bytes, 0, (void**)&b_dev));
    HSA_CHECK(hsa_amd_memory_pool_allocate(pools.gpu_global, arr_bytes, 0, (void**)&c_dev));

    /*
     * 授权 CPU Agent 访问 GPU VRAM（通过 BAR（Base Address Register）映射）：
     *   底层：GPUVM 页表添加 CPU entity 的映射，允许 PCIe 读写。
     * 不调用此函数则 CPU 端不能使用 hsa_memory_copy / 直接指针读写。
     */
    hsa_agent_t both[2] = { agents.cpu, agents.gpu };
    HSA_CHECK(hsa_amd_agents_allow_access(2, both, nullptr, a_dev));
    HSA_CHECK(hsa_amd_agents_allow_access(2, both, nullptr, b_dev));
    HSA_CHECK(hsa_amd_agents_allow_access(2, both, nullptr, c_dev));

    std::cout << "[3/9] GPU VRAM 数组已分配 (3 × " << arr_bytes << " B)\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 5：在 Host 侧准备数据，然后拷贝到 GPU
     *
     * hsa_memory_copy() 底层是 DMA 引擎传输（PCIe DMA）：
     *   → ROCr::DmaBlit::CopyMemory()
     *     → thunk: hsaKmtCopyMemory()（或 blit 内核）
     *
     * 若使用细粒度系统内存（CPU 侧 fine-grained pool），
     * CPU 直接写入、无需此步骤——这是更"HSA式"的编程模型，
     * 但 VRAM 的显式拷贝对性能调优更透明。
     * ────────────────────────────────────────────────────────────*/
    /* Host 侧临时缓冲 */
    float* h_a = new float[N];
    float* h_b = new float[N];
    float* h_c = new float[N]();

    for (uint32_t i = 0; i < N; ++i) {
        h_a[i] = static_cast<float>(i);
        h_b[i] = static_cast<float>(N - i);   /* a[i] + b[i] 应等于 N=1024 */
    }

    /* Host → Device：PCIe DMA 将 h_a/h_b 的内容搬到 VRAM */
    HSA_CHECK(hsa_memory_copy(a_dev, h_a, arr_bytes));
    HSA_CHECK(hsa_memory_copy(b_dev, h_b, arr_bytes));
    std::cout << "[4/9] 数据已传输至 GPU VRAM\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 6：创建 AQL 队列（软件 ring buffer）
     *
     * AQL 队列是 CPU 与 GPU CP（Command Processor）之间的通信通道。
     * 底层：
     *   → KFD ioctl: KFD_IOC_CREATE_QUEUE
     *     → kfd_ioctl_create_queue() → pqm_create_queue()
     *     → 分配 MQD（Message Queue Descriptor）和 doorbell 寄存器
     *
     * queue->base_address : ring buffer 基址（CPU 可写，GPU 可读）
     * queue->doorbell_signal : CPU 写入触发 CP 取 packet
     * ────────────────────────────────────────────────────────────*/
    hsa_queue_t* queue;
    HSA_CHECK(hsa_queue_create(
        agents.gpu,
        64,                     /* ring buffer 深度：64 个 packet（必须 2 的幂）*/
        HSA_QUEUE_TYPE_MULTI,   /* MULTI：允许多 CPU 线程并发提交                */
        nullptr, nullptr,
        UINT32_MAX, UINT32_MAX, /* 优先级：使用默认                              */
        &queue));
    std::cout << "[5/9] AQL 队列创建完成 (size=" << queue->size << ")\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 7：加载 .hsaco，创建 Executable，查找内核符号
     * ────────────────────────────────────────────────────────────*/
    hsa_code_object_reader_t reader;
    hsa_executable_t exe = load_hsaco(agents.gpu, "kernel.hsaco", reader);

    /*
     * OpenCL C 内核 "vector_add" 编译后符号名为 "vector_add.kd"
     * .kd = Kernel Descriptor，位于 .hsaco ELF 的 .rodata 段。
     * Kernel Descriptor 包含：
     *   - 代码入口偏移（kernel_code_entry_byte_offset）
     *   - 所需 VGPR/SGPR 数量
     *   - LDS / scratch 大小
     *   - compute_pgm_rsrc1/2 寄存器（GPU 硬件直接读取）
     */
    hsa_executable_symbol_t sym;
    HSA_CHECK(hsa_executable_get_symbol_by_name(
        exe, "vector_add.kd", &agents.gpu, &sym));

    /* 内核对象：GPU 执行所需的代码入口物理地址（由 GPUVM 映射后得到） */
    uint64_t kernel_obj = 0;
    HSA_CHECK(hsa_executable_symbol_get_info(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_obj));

    /* 每线程私有内存（spill registers / stack），本内核应为 0 */
    uint32_t private_seg = 0;
    HSA_CHECK(hsa_executable_symbol_get_info(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &private_seg));

    /* 整个 workgroup 共享的 LDS 大小，本内核应为 0 */
    uint32_t group_seg = 0;
    HSA_CHECK(hsa_executable_symbol_get_info(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &group_seg));

    /* 内核元数据规定的 kernarg 区域大小（由编译器根据参数列表计算） */
    uint32_t kernarg_size = 0;
    HSA_CHECK(hsa_executable_symbol_get_info(
        sym, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernarg_size));

    std::cout << "[6/9] Executable: kernel_obj=0x" << std::hex << kernel_obj << std::dec
              << "  private=" << private_seg
              << "  group="   << group_seg
              << "  kernarg=" << kernarg_size << " B\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 7b：分配 Kernarg 区域（存放内核参数）
     *
     * Kernarg 必须在 GPU 可见的内存中。
     * 若找到 CPU Kernarg pool（细粒度系统内存），CPU 可直接写入，
     * GPU 通过 PCIe/IOMMU 直接读取，无需额外 hsa_memory_copy。
     * 否则退回到 GPU VRAM（需 hsa_memory_copy 写入）。
     * ────────────────────────────────────────────────────────────*/
    void* kernarg_ptr = nullptr;
    bool  kernarg_in_cpu_pool = pools.cpu_kernarg_found;

    if (kernarg_in_cpu_pool) {
        /* CPU Kernarg pool：细粒度系统内存，CPU 可直接 memcpy */
        HSA_CHECK(hsa_amd_memory_pool_allocate(
            pools.cpu_kernarg,
            std::max(kernarg_size, static_cast<uint32_t>(sizeof(KernelArgs))),
            0, &kernarg_ptr));
        /* 授权 GPU 访问 CPU 侧的 Kernarg 内存（IOMMU 映射） */
        HSA_CHECK(hsa_amd_agents_allow_access(1, &agents.gpu, nullptr, kernarg_ptr));
    } else {
        /* 降级：在 GPU VRAM 分配，后续用 hsa_memory_copy 写入 */
        HSA_CHECK(hsa_amd_memory_pool_allocate(
            pools.gpu_global,
            std::max(kernarg_size, static_cast<uint32_t>(sizeof(KernelArgs))),
            0, &kernarg_ptr));
        HSA_CHECK(hsa_amd_agents_allow_access(1, &agents.cpu, nullptr, kernarg_ptr));
    }

    /* 填写参数：GPU 虚拟地址（在 GPU VRAM 中由 ROCr 分配的地址） */
    KernelArgs args;
    args.a_ptr = reinterpret_cast<uint64_t>(a_dev);
    args.b_ptr = reinterpret_cast<uint64_t>(b_dev);
    args.c_ptr = reinterpret_cast<uint64_t>(c_dev);
    args.n     = N;

    if (kernarg_in_cpu_pool) {
        /* CPU 直接写入细粒度系统内存（无需 DMA） */
        std::memcpy(kernarg_ptr, &args, sizeof(args));
    } else {
        /* 通过 DMA 写入 VRAM */
        HSA_CHECK(hsa_memory_copy(kernarg_ptr, &args, sizeof(args)));
    }
    std::cout << "[7/9] Kernarg 已写入 ("
              << (kernarg_in_cpu_pool ? "CPU fine-grained pool" : "GPU VRAM") << ")\n";

    /* ────────────────────────────────────────────────────────────
     * 步骤 8：创建完成信号 & Dispatch 内核
     *
     * 信号是 CPU/GPU 之间同步的原语：
     *   hsa_signal_t 内部是 volatile int64_t，
     *   GPU 执行完内核后自动将其从初始值 1 原子递减为 0。
     * ────────────────────────────────────────────────────────────*/
    hsa_signal_t done;
    HSA_CHECK(hsa_signal_create(1, 0, nullptr, &done));

    /*
     * grid_size 向上取整到 WG_SIZE 的整数倍：
     *   N=1024, WG_SIZE=64 → grid_size=1024 (正好整除)
     *   N=1000, WG_SIZE=64 → grid_size=1024 (末尾 workgroup 有 24 个活跃 lane)
     * 内核内部用 if (gid >= n) return 保护边界。
     */
    uint32_t grid_size = ((N + WG_SIZE - 1) / WG_SIZE) * WG_SIZE;

    dispatch_kernel(queue, kernel_obj,
                    private_seg, group_seg,
                    kernarg_ptr, done,
                    grid_size, WG_SIZE);

    std::cout << "[8/9] AQL Dispatch 已提交 (grid=" << grid_size
              << "  wg=" << WG_SIZE << "  wavefronts=" << grid_size / wavefront_size << ")\n";

    /* ────────────────────────────────────────────────────────────
     * 等待 GPU 完成
     *
     * hsa_signal_wait_scacquire()：
     *   - scacquire：memory_order_acquire，保证之后的 CPU 读操作
     *     能看到 GPU dispatch packet RELEASE fence 之前的所有写操作。
     *   - HSA_WAIT_STATE_ACTIVE：CPU 忙等（spin），延迟最低。
     *     生产环境可改用 HSA_WAIT_STATE_BLOCKED（睡眠，节省 CPU）。
     * ────────────────────────────────────────────────────────────*/
    hsa_signal_wait_scacquire(done,
                              HSA_SIGNAL_CONDITION_LT, 1,
                              UINT64_MAX,
                              HSA_WAIT_STATE_ACTIVE);

    /* ────────────────────────────────────────────────────────────
     * 步骤 9：结果回传 & 验证
     * ────────────────────────────────────────────────────────────*/
    /* Device → Host：PCIe DMA 将 c_dev 拷回 h_c */
    HSA_CHECK(hsa_memory_copy(h_c, c_dev, arr_bytes));

    uint32_t errors = 0;
    for (uint32_t i = 0; i < N; ++i) {
        /* a[i] + b[i] = i + (N-i) = N = 1024.0f（精确，无浮点误差） */
        float expected = static_cast<float>(N);
        if (std::fabs(h_c[i] - expected) > 1e-4f) {
            if (++errors <= 3)
                std::cerr << "  FAIL c[" << i << "]=" << h_c[i]
                          << " expected=" << expected << "\n";
        }
    }

    if (errors == 0) {
        std::cout << "[9/9] PASS  — 所有 " << N << " 个结果正确\n"
                  << "       c[0]=" << h_c[0]
                  << "  c[512]="   << h_c[512]
                  << "  c[1023]="  << h_c[1023] << "\n";
    } else {
        std::cerr << "[9/9] FAIL  — " << errors << " 个结果错误\n";
    }

    /* ════════════════════════════════════════════════════════════
     * 清理：按创建的逆序释放所有 HSA 对象
     * ════════════════════════════════════════════════════════════*/
    delete[] h_a;
    delete[] h_b;
    delete[] h_c;

    hsa_memory_free(a_dev);
    hsa_memory_free(b_dev);
    hsa_memory_free(c_dev);
    hsa_memory_free(kernarg_ptr);

    hsa_signal_destroy(done);
    hsa_queue_destroy(queue);
    hsa_executable_destroy(exe);
    hsa_code_object_reader_destroy(reader);

    /*
     * hsa_shut_down()：引用计数递减；到 0 时：
     *   → Runtime 析构所有内部对象
     *   → thunk: hsaKmtCloseKFD()   // 关闭 /dev/kfd fd
     */
    HSA_CHECK(hsa_shut_down());
    std::cout << "[Done] HSA Runtime 已关闭\n";

    return errors ? 1 : 0;
}

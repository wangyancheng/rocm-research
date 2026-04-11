/*
 * kernel.cl  —  HSA 向量加法 OpenCL C 内核
 *
 * 编译目标 : amdgcn-amd-amdhsa / gfx906 (MI50 / Vega20)
 * 编译工具 : clang -target amdgcn-amd-amdhsa -mcpu=gfx906
 *            输出 ELF-based HSA Code Object (.hsaco)
 *
 * ─── HSA Kernarg 参数布局 (OpenCL C ABI) ───────────────────
 *  offset  0 (8 B) : a   — __global const float*  输入向量 A
 *  offset  8 (8 B) : b   — __global const float*  输入向量 B
 *  offset 16 (8 B) : c   — __global float*        输出向量 C
 *  offset 24 (4 B) : n   — unsigned int           元素总数
 *  offset 28 (4 B) :       隐式填充（ABI 对齐要求）
 *  total  : 32 B
 * ────────────────────────────────────────────────────────────
 *
 * OpenCL C 编译到 HSACO 时，编译器把每个 __global 指针当成
 * 64-bit 设备虚拟地址嵌入 kernarg 区域；host 侧填写真实的
 * GPU-visible 指针值，驱动不做任何翻译。
 */

__kernel void vector_add(
    __global const float* restrict a,   /* 只读：输入向量 A        */
    __global const float* restrict b,   /* 只读：输入向量 B        */
    __global       float* restrict c,   /* 读写：输出向量 C        */
    unsigned int n                      /* 数组元素个数            */
)
{
    /*
     * get_global_id(0) 返回当前 work-item 在第 0 维的全局线性 ID。
     * 在 gfx906 上，每个 wavefront 含 64 个 lane；
     * 本内核按 1D 方式 dispatch，workgroup_size_x = 64。
     *
     * 线程到数组元素的映射：
     *   workgroup i, lane j  →  global_id = i * 64 + j  →  c[global_id]
     */
    unsigned int gid = get_global_id(0);

    /*
     * 边界保护：grid_size 向上取整到 workgroup_size 的整数倍，
     * 末尾 workgroup 中超出数组范围的 lane 直接退出，避免越界写。
     */
    if (gid >= n) return;

    /* 核心计算：一个 lane 完成一个浮点加法 */
    c[gid] = a[gid] + b[gid];

    /*
     * 硬件缓存一致性注意：
     *   gfx906 L2 cache 对 GPU 内部是共享的；
     *   写入 c[] 后数据暂留 L2，host 侧读取前需要通过
     *   AQL packet 的 RELEASE fence 或 hsa_memory_copy 刷出 L2。
     *   本示例在 AQL dispatch packet 中设置
     *   HSA_FENCE_SCOPE_SYSTEM，保证写操作对 CPU 可见。
     */
}

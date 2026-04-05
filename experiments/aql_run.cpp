#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <malloc.h>
#include <atomic>

#define CHECK(x) if ((x) != HSA_STATUS_SUCCESS) { \
    const char* err; hsa_status_string(x, &err); \
    std::cout << "HSA Error at line " << __LINE__ << ": " << err << std::endl; \
    exit(1); }

hsa_agent_t gpu_agent;
hsa_amd_memory_pool_t global_pool; // VRAM 显存池

hsa_status_t find_gpu(hsa_agent_t agent, void*) {
    hsa_device_type_t type;
    hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &type);
    if (type == HSA_DEVICE_TYPE_GPU) gpu_agent = agent;
    return HSA_STATUS_SUCCESS;
}

hsa_status_t find_pool(hsa_amd_memory_pool_t pool, void*) {
    hsa_amd_segment_t segment;
    hsa_amd_memory_pool_get_info(pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment);
    if (segment == HSA_AMD_SEGMENT_GLOBAL) {
        global_pool = pool;
    }
    return HSA_STATUS_SUCCESS;
}

int main() {
    CHECK(hsa_init());
    CHECK(hsa_iterate_agents(find_gpu, nullptr));
    CHECK(hsa_amd_agent_iterate_memory_pools(gpu_agent, find_pool, nullptr));

    hsa_queue_t* queue;
    CHECK(hsa_queue_create(gpu_agent, 64, HSA_QUEUE_TYPE_SINGLE, nullptr, nullptr, UINT32_MAX, UINT32_MAX, &queue));

    hsa_signal_t signal;
    CHECK(hsa_signal_create(1, 0, nullptr, &signal));

    // 1. 加载 Code Object (需 4096 对齐)
    std::ifstream f("kernel.hsaco", std::ios::binary | std::ios::ate);
    size_t size = f.tellg();
    f.seekg(0);
    void* code_buf;
    posix_memalign(&code_buf, 4096, size);
    f.read((char*)code_buf, size);
    f.close();

    hsa_code_object_reader_t reader;
    CHECK(hsa_code_object_reader_create_from_memory(code_buf, size, &reader));

    hsa_executable_t exe;
    CHECK(hsa_executable_create(HSA_PROFILE_BASE, HSA_EXECUTABLE_STATE_UNFROZEN, nullptr, &exe));
    CHECK(hsa_executable_load_agent_code_object(exe, gpu_agent, reader, nullptr, nullptr));
    CHECK(hsa_executable_freeze(exe, nullptr));

    hsa_executable_symbol_t symbol;
    CHECK(hsa_executable_get_symbol_by_name(exe, "my_kernel.kd", &gpu_agent, &symbol));

    uint64_t kernel_object;
    CHECK(hsa_executable_symbol_get_info(symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_object));

    // 2. 分配输出地址 (out)
    int* out_gpu;
    CHECK(hsa_amd_memory_pool_allocate(global_pool, sizeof(int), 0, (void**)&out_gpu));
    CHECK(hsa_amd_agents_allow_access(1, &gpu_agent, nullptr, out_gpu));

    // 3. 分配参数段 (kernarg) - 关键修复点！
    // 必须分配在 GPU 可访问的内存池中
    void* kernarg_gpu;
    CHECK(hsa_amd_memory_pool_allocate(global_pool, 64, 0, &kernarg_gpu));
    CHECK(hsa_amd_agents_allow_access(1, &gpu_agent, nullptr, kernarg_gpu));

    // 在 Host 侧准备数据，然后拷贝过去
    uint64_t host_args = (uint64_t)out_gpu;
    CHECK(hsa_memory_copy(kernarg_gpu, &host_args, sizeof(uint64_t)));

    // 4. 写 AQL Packet
    uint64_t index = hsa_queue_add_write_index_relaxed(queue, 1);
    hsa_kernel_dispatch_packet_t* pkt = &((hsa_kernel_dispatch_packet_t*)queue->base_address)[index % queue->size];
    
    memset(pkt, 0, sizeof(*pkt));
    pkt->setup = 1 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
    pkt->workgroup_size_x = 1; pkt->workgroup_size_y = 1; pkt->workgroup_size_z = 1;
    pkt->grid_size_x = 1;      pkt->grid_size_y = 1;      pkt->grid_size_z = 1;
    pkt->kernel_object = kernel_object;
    pkt->kernarg_address = kernarg_gpu; // 指向 GPU 可视的参数地址
    pkt->completion_signal = signal;

    // 原子写入 Header 激活任务
    uint16_t header = (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE) |
                     (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
                     (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE);
    __atomic_store_n(&(pkt->header), header, 3); // __ATOMIC_RELEASE

    // 5. 敲门并等待结果
    hsa_signal_store_release(queue->doorbell_signal, index);
    while (hsa_signal_wait_acquire(signal, HSA_SIGNAL_CONDITION_LT, 1, UINT64_MAX, HSA_WAIT_STATE_ACTIVE) != 0);

    // 将结果拷回 CPU 打印
    int result = 0;
    CHECK(hsa_memory_copy(&result, out_gpu, sizeof(int)));
    std::cout << "MI50 Success! GPU wrote: " << result << std::endl;

    hsa_signal_destroy(signal);
    hsa_queue_destroy(queue);
    hsa_executable_destroy(exe);
    hsa_code_object_reader_destroy(reader);
    hsa_shut_down();
    return 0;
}
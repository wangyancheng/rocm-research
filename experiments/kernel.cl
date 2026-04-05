__kernel void my_kernel(__global int* out) {
    // 0x7ea = 2026
    out[0] = 2026;
    // 硬件级同步：确保数据刷出 L2 缓存
    __builtin_amdgcn_s_waitcnt(0);
}
#include <hip/hip_runtime.h>
#include <iostream>

__global__ void hello_kernel() {
    int tid = (int)threadIdx.x; 
    printf("Hello from MI50 (gfx906) Thread %d\n", tid);
}

int main() {
    hello_kernel<<<1, 10>>>();
    hipDeviceSynchronize();
    return 0;
}
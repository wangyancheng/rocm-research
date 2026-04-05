#include <hip/hip_runtime.h>
#include <iostream>

__global__ void vector_add(int* a, int* b, int* c, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        c[idx] = a[idx] + b[idx];
    }
}

int main() {
    const int N = 16;
    int a[N], b[N], c[N];

    for (int i = 0; i < N; i++) {
        a[i] = i;
        b[i] = i * 2;
    }

    int *d_a, *d_b, *d_c;

    hipMalloc(&d_a, N * sizeof(int));
    hipMalloc(&d_b, N * sizeof(int));
    hipMalloc(&d_c, N * sizeof(int));

    hipMemcpy(d_a, a, N * sizeof(int), hipMemcpyHostToDevice);
    hipMemcpy(d_b, b, N * sizeof(int), hipMemcpyHostToDevice);

    hipLaunchKernelGGL(vector_add, dim3(1), dim3(16), 0, 0, d_a, d_b, d_c, N);

    hipMemcpy(c, d_c, N * sizeof(int), hipMemcpyDeviceToHost);

    for (int i = 0; i < N; i++) {
        std::cout << a[i] << " + " << b[i] << " = " << c[i] << std::endl;
    }

    hipFree(d_a);
    hipFree(d_b);
    hipFree(d_c);

    return 0;
}
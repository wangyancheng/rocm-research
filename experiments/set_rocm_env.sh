#!/bin/bash
# ROCm 5.6 环境变量设置脚本

export ROCM_PATH=/opt/rocm-5.6
export HIP_PATH=/opt/rocm-5.6
export PATH=$ROCM_PATH/bin:$PATH
export LD_LIBRARY_PATH=$ROCM_PATH/lib:$LD_LIBRARY_PATH
export HIP_CLANG_PATH=$ROCM_PATH/bin
export DEVICE_LIB_PATH=$ROCM_PATH/amdgcn/bitcode

echo "ROCm 5.6 environment loaded."

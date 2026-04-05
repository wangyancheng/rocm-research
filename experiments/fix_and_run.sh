#!/bin/bash
set -e

# 1. 编译 Kernel (确保针对 gfx906)
/opt/rocm-5.6/bin/clang -target amdgcn-amd-amdhsa -mcpu=gfx906 -mcode-object-version=4 \
    -fPIC -shared kernel.cl -o kernel.hsaco

# 2. 强制修正二进制 Flags (从 Wave32 修正为 Wave64)
python3 -c "
import struct
with open('kernel.hsaco', 'r+b') as f:
    f.seek(48)
    f.write(struct.pack('<I', 0x02F))
"
echo "ELF Flags patched to 0x02F."

# 3. 编译 Host
g++ aql_run.cpp -o aql_run -I/opt/rocm-5.6/include -L/opt/rocm-5.6/lib -lhsa-runtime64

# 4. 运行 (加载 ROCm 库)
export LD_LIBRARY_PATH=/opt/rocm-5.6/lib:$LD_LIBRARY_PATH
./aql_run
ROCM_PATH ?= /opt/rocm-5.6
CXX       := g++
CLANG     := $(ROCM_PATH)/bin/clang

# 架构配置
GPU_ARCH  := gfx906
COD_VER   := 4

# 编译选项
CLANG_FLAGS := -target amdgcn-amd-amdhsa -mcpu=$(GPU_ARCH) -mcode-object-version=$(COD_VER) -fPIC -shared
CXX_FLAGS   := -I$(ROCM_PATH)/include -L$(ROCM_PATH)/lib -lhsa-runtime64 -std=c++17

# 自动获取当前目录下的 .cpp 文件名作为目标程序名
TARGET := $(patsubst %.cpp,%,$(wildcard *.cpp))
HSACO  := kernel.hsaco

all: $(TARGET) $(HSACO)

# 通用 C++ 编译规则
%: %.cpp
	$(CXX) $< -o $@ $(CXX_FLAGS)

# 通用 OpenCL 内核编译规则
%.hsaco: %.cl
	$(CLANG) $(CLANG_FLAGS) $< -o $@
	@# 自动执行我们之前的 Flags 修正逻辑
	@python3 -c "import struct; f=open('$@','r+b'); f.seek(48); f.write(struct.pack('<I', 0x02F))"
	@echo "Built and patched $@ for $(GPU_ARCH)"

clean:
	rm -f $(TARGET) *.hsaco

.PHONY: all clean

# Makefile — HSA 向量加法示例
#
# 构建目标：
#   kernel.hsaco      — OpenCL C 内核编译为 AMDGCN ELF Code Object
#   hsa_vector_add    — 主机端 C++ 程序（链接 HSA Runtime）
#
# 用法：
#   make              — 编译所有目标
#   make run          — 编译并运行
#   make clean        — 删除编译产物
#
# 前提：
#   export ROCM_PATH=/opt/rocm-5.6   （或修改下方默认值）

ROCM_PATH ?= /opt/rocm-5.6

# ── 工具链 ───────────────────────────────────────────────────
# clang：LLVM 前端，支持 OpenCL C → amdgcn-amd-amdhsa 交叉编译
# g++  ：编译 host C++ 程序（链接 ROCr 动态库）
CLANG := $(ROCM_PATH)/bin/clang
CXX   := g++

# ── 目标 GPU 架构 ─────────────────────────────────────────────
# gfx906 = AMD MI50 / RX 5700 系列 (Vega20)
# 修改此处可适配其他 GPU（gfx908、gfx90a …）
GPU_ARCH := gfx906

# Code Object 版本：ROCm 5.x 默认 v4（v3 已废弃）
COD_VER := 4

# ── 编译选项 ─────────────────────────────────────────────────

# clang 编译 OpenCL C → HSACO 的选项：
#   -target amdgcn-amd-amdhsa   : 目标三元组，告知 clang 输出 AMDGCN 代码
#   -mcpu=gfx906                : 指定具体 GPU 微架构（影响指令选择）
#   -mcode-object-version=4     : 生成 Code Object V4 格式 ELF
#   -fPIC -shared               : 生成可重定位共享对象（.hsaco 本质上是 shared lib）
#   -cl-std=CL2.0               : 使用 OpenCL 2.0 语言标准
#   -O2                         : 标准优化级别
CLANG_FLAGS := \
    -target amdgcn-amd-amdhsa  \
    -mcpu=$(GPU_ARCH)           \
    -mcode-object-version=$(COD_VER) \
    -fPIC -shared               \
    -cl-std=CL2.0               \
    -O2

# g++ 编译 host 程序的选项：
#   -I$(ROCM_PATH)/include      : HSA 头文件路径（hsa/hsa.h 等）
#   -L$(ROCM_PATH)/lib          : ROCr 动态库搜索路径
#   -lhsa-runtime64             : 链接 HSA Runtime（libhsa-runtime64.so）
#   -Wl,-rpath,...              : 运行时直接从 ROCM_PATH 加载 .so，免 LD_LIBRARY_PATH
#   -std=c++17                  : 需要 C++17 特性（if constexpr 等）
#   -O2 -g                      : 优化 + 保留调试信息
CXX_FLAGS := \
    -I$(ROCM_PATH)/include        \
    -L$(ROCM_PATH)/lib            \
    -lhsa-runtime64               \
    -Wl,-rpath,$(ROCM_PATH)/lib   \
    -std=c++17                    \
    -O2 -g

# ── 文件名 ────────────────────────────────────────────────────
TARGET := hsa_vector_add
HSACO  := kernel.hsaco
SRC    := hsa_vector_add.cpp
KERNEL := kernel.cl

# ═════════════════════════════════════════════════════════════
# 构建规则
# ═════════════════════════════════════════════════════════════

.PHONY: all clean run

all: $(TARGET) $(HSACO)

# ── 内核编译：OpenCL C → AMDGCN ELF (.hsaco) ─────────────────
#
# 编译流程（clang 内部）：
#   kernel.cl
#     → Clang Frontend（解析 OpenCL C 2.0）
#     → LLVM IR（amdgcn target）
#     → AMDGCN MC（SelectionDAG / MachineIR）
#     → ELF Code Object（含 .text / .rodata / .note 段）
#
# 输出 .hsaco 包含：
#   .text            : GPU 机器码（AMDGCN ISA）
#   .rodata          : Kernel Descriptor（KD）— GPU 执行前读取
#   .note            : YAML 元数据（内核名、参数、寄存器用量等）
#   .symtab/.strtab  : 符号表（host 侧用于 get_symbol_by_name）
$(HSACO): $(KERNEL)
	$(CLANG) $(CLANG_FLAGS) $< -o $@
	@# ── ELF Flags 修正（gfx906 特定）────────────────────────
	@# clang 在某些版本（ROCm 5.x）生成的 .hsaco ELF e_flags 字段
	@# mach 位域不正确，导致 ROCr hsa_executable_load_agent_code_object()
	@# 在运行时报 "incompatible ISA" 错误。
	@#
	@# ELF header offset 48 = e_flags（4 字节，little-endian）。
	@# EF_AMDGPU_MACH_AMDGCN_GFX906 = 0x02F（来自 LLVM BinaryFormat/ELF.h）。
	@# 此补丁强制将 mach 字段设置为 gfx906 正确值。
	@python3 -c "import struct; \
	    f = open('$@', 'r+b'); \
	    f.seek(48); \
	    f.write(struct.pack('<I', 0x02F)); \
	    f.close()"
	@echo "  [HSACO] $@ 编译并修正完成 ($(GPU_ARCH), COV$(COD_VER))"

# ── Host 程序编译：C++ → 可执行文件 ──────────────────────────
$(TARGET): $(SRC)
	$(CXX) $< -o $@ $(CXX_FLAGS)
	@echo "  [BIN]   $@ 编译完成"

# ── 运行 ─────────────────────────────────────────────────────
run: all
	@echo "════════════════════════════════════"
	./$(TARGET)
	@echo "════════════════════════════════════"

# ── 清理 ─────────────────────────────────────────────────────
clean:
	rm -f $(TARGET) $(HSACO)

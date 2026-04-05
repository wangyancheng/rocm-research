ROCM_PATH ?= /opt/rocm-5.6
HIPCC     := $(ROCM_PATH)/bin/hipcc

# 自动获取当前目录下唯一的 .cpp 文件名（不带后缀）作为目标
TARGET := $(patsubst %.cpp,%,$(wildcard *.cpp))

# 编译选项（如有需要可添加，例如 -g 或 -O3）
HIPCC_FLAGS := 

all: $(TARGET)

# 通用模式规则：将任何 .cpp 编译为同名的可执行文件
%: %.cpp
	$(HIPCC) $(HIPCC_FLAGS) $< -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean

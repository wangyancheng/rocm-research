# 列出所有包含 Makefile 的子目录
SUBDIRS := aql_run hello_world vector_add

# 默认目标：编译所有子目录
all:
	@for dir in $(SUBDIRS); do \
		echo "Building in $$dir..."; \
		$(MAKE) -C $$dir; \
	done

# 清理目标：递归清理所有子目录
clean:
	@for dir in $(SUBDIRS); do \
		echo "Cleaning in $$dir..."; \
		$(MAKE) -C $$dir clean; \
	done

.PHONY: all clean

#!/bin/bash
set -e

echo "=========================================="
echo "🚀 开始一键安全分步推送..."
echo "=========================================="

# 1. 处理子模块的本地提交
git submodule foreach '
    echo "------------------------------------------"
    echo "正在检查子模块: $name"
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    if [ "$CURRENT_BRANCH" = "HEAD" ]; then
        git checkout rocm-5.6.0 2>/dev/null || git checkout -b rocm-5.6.0
    fi
    git add .
    git commit -m "update submodule: $name" 2>/dev/null || echo "子模块 $name 无文件需要提交"
'

# 2. 处理主项目的本地提交
echo "------------------------------------------"
echo "📦 正在处理主项目改动..."
git add .
git commit -m "Update research notes and experiments" 2>/dev/null || echo "主项目无文件需要提交"

# 3. 终极一击：不再依赖联动，我们手动按顺序把它们一个一个精准送上服务器
echo "------------------------------------------"
echo "📤 正在独立向远程同步【子模块：ROCR-Runtime】..."
cd ~/rocm-5.6/rocm-research/ROCR-Runtime
# 针对我们之前的标签/分支重名冲突，显式精准推送到远程分支
git push origin HEAD:refs/heads/rocm-5.6.0

echo "📤 正在独立向远程同步【子模块：ROCT-Thunk-Interface】..."
cd ~/rocm-5.6/rocm-research/ROCT-Thunk-Interface
git push origin HEAD:refs/heads/rocm-5.6.0 2>/dev/null || echo "ROCT 无新提交需要推送"

echo "📤 正在向远程同步【主项目：rocm-research】..."
cd ~/rocm-5.6/rocm-research
git push origin HEAD

echo "=========================================="
echo "🎉 全盘一键独立推送成功，404 彻底解决！"
echo "=========================================="

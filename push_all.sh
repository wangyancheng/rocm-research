#!/bin/bash
set -e # 遇到任何致命错误立刻停止，防止污染后续提交

echo "=========================================="
echo "🚀 开始一键推送主项目及所有子模块..."
echo "=========================================="

# 1. 智能处理子模块：先检查并尝试让它附着在正确的远程分支上，再进行提交
git submodule foreach '
    echo "正在检查子模块: $name"
    # 如果处于分离头指针状态，强制让它感知当前所处的 rocm-5.6.0 跟踪分支（或者根据你的实际远程分支微调）
    # 这样能给它一个合法的本地引用，彻底避免 refs/heads 找不到的致命错误
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    if [ "$CURRENT_BRANCH" = "HEAD" ]; then
        echo "检测到分离头指针，正在建立临时本地关联..."
        # 尝试切换回对应的开发分支，如果本地没有则基于当前点拉一个
        git checkout rocm-5.6.0 2>/dev/null || git checkout -b rocm-5.6.0
    fi
    
    # 执行添加和提交
    git add .
    git commit -m "update submodule: $name" 2>/dev/null || echo "子模块 $name 无文件需要提交"
'

# 2. 处理主项目的改动
echo "------------------------------------------"
echo "📦 正在处理主项目改动..."
git add .
git commit -m "Update research notes and experiments" 2>/dev/null || echo "主项目无文件需要提交"

# 3. 终极一击：安全推送
echo "------------------------------------------"
echo "📤 正在向远程同步代码..."
# 显式指定推送主项目的当前分支，并让子模块在推送时自动寻找对应的上游分支
git push origin HEAD --recurse-submodules=on-demand

echo "=========================================="
echo "🎉 全盘一键推送成功！"
echo "=========================================="

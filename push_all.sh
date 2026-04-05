#!/bin/bash
# 1. 先处理子模块的改动
git submodule foreach 'git add . && git commit -m "update submodule" || true'

# 2. 再处理主项目的改动
git add .
git commit -m "Update research notes and experiments"

# 3. 推送（包含子模块内容）
git push --recurse-submodules=on-demand

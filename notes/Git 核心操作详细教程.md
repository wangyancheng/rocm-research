# Git 核心操作详细教程

> Git 是目前世界上最先进的分布式版本控制系统。本教程涵盖了从环境配置到日常协作的核心命令。

---

## 一、 环境配置

在开始之前，你需要配置自己的身份标识，以便 Git 记录是谁提交了修改。

- **配置用户名**：`git config --global user.name "你的名字"`
- **配置邮箱**：`git config --global user.email "你的邮箱"`
- **查看配置列表**：`git config --list`

---

## 二、 基础操作：从零开始

### 1. 初始化与克隆

* **新建本地仓库**：在项目目录下运行 `git init`，会生成一个隐藏的 `.git` 文件夹。
* **克隆远程仓库**：`git clone [url]`，直接将远程项目镜像到本地。

### 2. 提交修改

Git 的提交分为“三部曲”：

1. **查看状态**：`git status`（随时查看哪些文件被修改了）。
2. **添加到暂存区**：
   - `git add <file>`：添加指定文件。
   - `git add .`：添加当前目录下的所有修改。
3. **提交到本地仓库**：
   - `git commit -m "提交说明"`：一定要写有意义的说明。

---

## 三、 远程协作

当你需要与他人协作或备份代码到 GitHub/Gitee 时：

| 命令                            | 说明            |
|:----------------------------- |:------------- |
| `git remote add origin [url]` | 关联本地仓库与远程仓库   |
| `git push -u origin master`   | 第一次推送并建立追踪关系  |
| `git push`                    | 推送最新修改到远程     |
| `git pull`                    | 拉取远程更新并合并到本地  |
| `git fetch`                   | 仅下载远程更新，不自动合并 |

---

## 四、 分支管理（核心）

分支可以让多人在不同的功能线上同时工作，互不干扰。

* **创建分支**：`git branch <name>`
* **切换分支**：`git checkout <name>` 或新版 `git switch <name>`
* **创建并切换**：`git checkout -b <name>`
* **合并分支**：先切回主分支 `git checkout master`，再执行 `git merge <name>`。
* **删除分支**：`git branch -d <name>`

---

## 五、 撤销与“后悔药”

* **撤销暂存区文件**：`git reset HEAD <file>`（文件回到工作区）。
* **丢弃工作区修改**：`git checkout -- <file>`（恢复到上一次提交的状态）。
* **版本回退**：
  - `git log`：查看提交历史，获取 `commit_id`。
  - `git reset --hard <commit_id>`：强制回退到指定版本。

---

## 六、 常用小技巧

- **查看精简日志**：`git log --oneline --graph`（图形化查看分支合并情况）。
- **忽略文件**：在根目录创建 `.gitignore` 文件，写入不需要追踪的文件名（如 `.DS_Store` 或 `node_modules/`）。
- **临时存放修改**：如果你正在写代码但需要紧急切换分支，使用 `git stash` 暂存当前进度，切换回来后再用 `git stash pop` 恢复。

---

**学习建议：**
初学者推荐阅读 [廖雪峰 Git 教程](https://liaoxuefeng.com) 进行深度学习。

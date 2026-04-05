# Claude Code 加速 ROCm 5.6 源码研究指南

> 硬件：MI50 (gfx906)  
> 研究范围：ROCr → ROCT/thunk → KFD 内核驱动  
> 前提：已按 rocm56_research_guide.md 搭建研究环境

---

## 目录

1. [环境搭建](#1-环境搭建)
2. [工作流设计](#2-工作流设计)
3. [Phase 1：动态追踪分析](#3-phase-1动态追踪分析)
4. [Phase 2：ROCr 源码对照](#4-phase-2rocr-源码对照)
5. [Phase 3：thunk 层分析](#5-phase-3thunk-层分析)
6. [Phase 4：KFD 内核驱动](#6-phase-4kfd-内核驱动)
7. [实验代码生成](#7-实验代码生成)
8. [知识沉淀](#8-知识沉淀)
9. [不适合用 Claude Code 的场景](#9-不适合用-claude-code-的场景)

---

## 1. 环境搭建

### 1.1 目录结构

所有仓库放在同一父目录，Claude Code 从父目录启动，可跨仓库分析。

```bash
mkdir ~/rocm_research && cd ~/rocm_research

# ROCr 运行时
git clone --branch rocm-5.6.0 \
  https://github.com/RadeonOpenCompute/ROCR-Runtime

# thunk
git clone --branch rocm-5.6.0 \
  https://github.com/RadeonOpenCompute/ROCT-Thunk-Interface

# 内核源码：用系统自带，不要 clone 主线
# 确认版本对齐
uname -r
apt install linux-source
# 源码解压到 /usr/src/linux-source-x.x.x/

# 创建内核源码软链接，方便 Claude Code 访问
ln -s /usr/src/linux-source-$(uname -r | cut -d- -f1) \
  ~/rocm_research/linux-kernel

# 把你自己的 HSA 实验程序也放进来
mkdir ~/rocm_research/my_experiments
cp your_hsa_app.cpp ~/rocm_research/my_experiments/
```

最终目录结构：

```
~/rocm_research/
├── ROCR-Runtime/          # ROCr 源码
├── ROCT-Thunk-Interface/  # thunk 源码
├── linux-kernel/          # 内核源码软链接（系统自带）
└── my_experiments/        # 你的实验程序
    ├── hsa_basic.cpp      # 基础 HSA dispatch 程序
    └── traces/            # roctracer / strace 输出
```

### 1.2 生成编译数据库（让 Claude Code 理解代码结构）

```bash
# ROCr
cd ~/rocm_research/ROCR-Runtime
mkdir build && cd build
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cp compile_commands.json ../

# thunk
cd ~/rocm_research/ROCT-Thunk-Interface
mkdir build && cd build
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
cp compile_commands.json ../
```

### 1.3 启动 Claude Code

```bash
cd ~/rocm_research
claude
```

**重要**：从父目录启动，三个仓库都在工作区范围内，Claude Code 可以跨仓库追踪调用链。

---

## 2. 工作流设计

### 核心原则

```
动态追踪（观察行为）→ Claude Code（定位源码）→ 自己理解 → 实验验证
```

不要跳过动态追踪直接读源码。先知道"发生了什么"，再用 Claude Code 找"为什么这么实现"。

### 两种使用模式

**模式 A：导航模式**（最常用）
已知一个函数名或行为，让 Claude Code 追踪完整路径。

```
你：hsa_queue_create 到 ioctl 的完整调用链是什么？
Claude Code：[读源码] → 给出逐层路径 + 关键代码片段
```

**模式 B：解释模式**
看不懂某段代码，让 Claude Code 解释设计意图。

```
你：[粘贴代码片段] 这段 mmap 的 offset 计算是什么含义？
Claude Code：解释 doorbell page 的映射机制
```

### 把追踪结果喂给 Claude Code

这是最高效的用法。把 roctracer / strace 的输出直接粘贴给 Claude Code：

```
你：这是我程序的 strace 输出（粘贴）
    帮我找出每个 ioctl 对应 ROCR-Runtime 里的哪个函数调用
```

---

## 3. Phase 1：动态追踪分析

**目标**：先跑实验，收集原始数据，再用 Claude Code 解读。

### 3.1 收集追踪数据

```bash
cd ~/rocm_research/my_experiments

# roctracer：HSA API 调用链
export HSA_TOOLS_LIB=libroctracer64.so
export ROCTRACER_DOMAIN=hsa
./hsa_basic 2>&1 | tee traces/hsa_trace.log

# strace：IOCTL 边界
strace -e trace=ioctl -T -v ./hsa_basic 2>&1 \
  | tee traces/ioctl_trace.log

# 内存映射（需在程序里 hsa_queue_create 后加 sleep(60)）
cat /proc/$(pgrep hsa_basic)/maps > traces/maps.log
```

### 3.2 用 Claude Code 解读数据

启动 Claude Code 后执行以下任务：

---

**任务 1.1：解读 HSA API 调用时序**

```
我运行了 HSA 程序，roctracer 输出如下：
[粘贴 traces/hsa_trace.log 内容]

请帮我：
1. 把每个 API 调用映射到 ROCR-Runtime/src/core/runtime/ 下的对应函数
2. 标出哪些调用会触发 ioctl，哪些是纯用户态操作
3. 画出调用的层次关系
```

---

**任务 1.2：解读 IOCTL 序列**

```
这是 strace 捕获的 ioctl 调用序列：
[粘贴 traces/ioctl_trace.log 中 ioctl 相关行]

请帮我：
1. 根据 ROCT-Thunk-Interface/include/hsakmt/hsakmt.h 
   和 linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_ioctl.h
   识别每个 ioctl 的命令名和用途
2. 建立一张表：HSA API → thunk 函数 → IOCTL 命令 → 内核处理函数
```

---

**任务 1.3：解读内存映射**

```
这是程序运行时的 /proc/maps 输出：
[粘贴 traces/maps.log 内容]

结合 ROCR-Runtime 源码，帮我识别每段内存映射的用途：
哪段是 AQL ring buffer？哪段是 doorbell MMIO？
哪段是 signal 共享内存？地址是怎么确定的？
```

---

## 4. Phase 2：ROCr 源码对照

**目标**：逐模块理解 ROCr 的实现，Claude Code 负责导航和解释。

### 4.1 初始化链路

---

**任务 2.1：hsa_init 完整初始化链路**

```
请追踪 hsa_init() 的完整初始化流程：
入口：ROCR-Runtime/src/core/runtime/runtime.cpp

需要包括：
1. Runtime::Load() 的执行顺序
2. GPU Agent 是如何从 KFD topology 发现并初始化的
3. 第一个 ioctl 是哪个，在哪里调用
4. hsa_agent_t 这个 handle 是如何从内部对象转换来的

列出每步的文件路径和行号
```

---

**任务 2.2：Agent 抽象层次**

```
分析 ROCR-Runtime 中 Agent 相关的类层次：
GpuAgent、CpuAgent 继承自什么基类？
GpuAgent 的关键成员变量有哪些（重点：isa_、queues_、pools_）？
gfx906 相关的特殊初始化在哪里？
```

---

### 4.2 Queue 创建与 Doorbell

---

**任务 2.3：hsa_queue_create 完整路径**

```
追踪 hsa_queue_create() 从 API 到 ioctl 的完整路径：

重点关注：
1. ring buffer 的 mmap 在哪里，size 怎么计算
2. AMDKFD_IOC_CREATE_QUEUE 调用时传了哪些参数
3. doorbell 的 mmap offset 从哪里来，如何映射到 MMIO
4. hsa_queue_t（C结构体）和 AqlQueue（C++类）的对应关系

输出格式：调用链 + 每步关键代码片段
```

---

**任务 2.4：AQL Packet 激活机制**

```
分析 ROCR-Runtime 中 AQL packet 的写入和激活：

我的代码里这样激活 packet：
  __atomic_store_n(&(pkt->header), header, 3);

请解释：
1. 为什么必须最后写 header 字段
2. ROCr 内部是怎么封装这个原子操作的（找对应函数）
3. HSA_FENCE_SCOPE_SYSTEM 和 HSA_FENCE_SCOPE_AGENT 
   在 packet header 里如何编码，区别是什么
4. GPU CP 读到 header 后的处理流程（在源码中能追踪到哪一层）
```

---

**任务 2.5：Doorbell 写入机制**

```
分析 doorbell 的完整机制：

我的代码：hsa_signal_store_release(queue->doorbell_signal, index)

请追踪：
1. doorbell_signal 和普通 completion signal 的区别
2. 这个写操作最终写到哪个地址
3. 在 ROCR-Runtime 源码中，doorbell 写入是哪个函数
4. 对应 kfd_ioctl_create_queue 返回的哪个字段
```

---

### 4.3 Signal 两种实现

---

**任务 2.6：Signal 实现对比**

```
对比 BusyWaitSignal 和 InterruptSignal 的实现：
文件：
  ROCR-Runtime/src/core/runtime/default_signal.cpp
  ROCR-Runtime/src/core/runtime/interrupt_signal.cpp

请对比：
1. WaitAcquire() 的实现差异（自旋 vs syscall）
2. InterruptSignal 走哪个 ioctl 进入睡眠
3. GPU 完成后如何唤醒等待的 InterruptSignal
4. hsa_signal_create 的第二个参数如何决定选择哪种实现
```

---

### 4.4 内存池

---

**任务 2.7：内存分配完整路径**

```
追踪 hsa_amd_memory_pool_allocate() 的完整路径：

我的代码分配了两块内存：
  hsa_amd_memory_pool_allocate(global_pool, sizeof(int), 0, &out_gpu)
  hsa_amd_memory_pool_allocate(global_pool, 64, 0, &kernarg_gpu)

请追踪：
1. 从 ROCr API 到 thunk 的调用路径
2. ROCT 的 fmm.c 中实际分配逻辑（fmm_allocate_device_mem）
3. 触发哪个 ioctl，传递哪些关键参数
4. 返回的指针是什么类型的地址（GPUVM VA？系统 VA？）

同时解释 hsa_amd_agents_allow_access() 做了什么（页表层面）
```

---

**任务 2.8：MI50 内存孔径布局**

```
分析 gfx906 (MI50) 的 GPUVM 地址空间布局：

在以下文件中找出地址空间的划分：
  ROCT-Thunk-Interface/src/fmm.c
  linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_flat_memory.c（如果存在）

需要了解：
1. GPUVM 用户空间的起始和结束地址
2. Scratch 孔径、LDS 孔径的位置
3. kernarg 内存落在哪个区域
4. 为什么我的 out_gpu 和 kernarg_gpu 地址值很大（通常 > 0x7f...）
```

---

## 5. Phase 3：thunk 层分析

**目标**：理解 thunk 作为用户态和内核的语义桥接层。

---

**任务 3.1：建立完整 IOCTL 对照表**

```
对照以下两个文件：
  ROCT-Thunk-Interface/include/hsakmt/hsakmt.h
  linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_ioctl.h

生成完整的对照表，格式：
| thunk 函数 | IOCTL 命令 | 内核处理函数 | 主要参数 | 用途 |

重点包括：队列管理、内存管理、事件机制相关的所有 ioctl
```

---

**任务 3.2：fmm.c 内存管理器**

```
分析 ROCT-Thunk-Interface/src/fmm.c 的整体设计：

1. fmm 管理哪些类型的内存区域（aperture 类型有哪些）
2. fmm_allocate_device_mem() 的分配策略
3. fmm_map_to_gpu() 如何触发页表建立
4. 内存释放时的清理顺序

重点：MI50 (gfx906) 走哪个分支
```

---

**任务 3.3：topology.c 拓扑发现**

```
分析 ROCT-Thunk-Interface/src/topology.c：

1. 如何读取 /sys/class/kfd/kfd/topology/ 下的节点信息
2. CPU 节点和 GPU 节点的区分逻辑
3. 内存池（memory bank）信息如何解析
4. 这些信息如何传递给 ROCr 的 GpuAgent 初始化
```

---

## 6. Phase 4：KFD 内核驱动

**目标**：理解内核态的队列和内存管理，完成全栈理解。

---

**任务 4.1：/dev/kfd 字符设备**

```
分析 linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_chardev.c：

1. kfd_open() 做了什么（进程注册流程）
2. kfd_ioctl() 如何根据命令号 dispatch
3. kfd_mmap() 处理哪些 mmap 请求
   （doorbell / signal / MMIO 各走哪个分支）
4. 进程退出时的清理逻辑在哪里
```

---

**任务 4.2：Queue 创建内核侧**

```
追踪 AMDKFD_IOC_CREATE_QUEUE 在内核的完整处理：

入口：linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_chardev.c
      → kfd_ioctl_create_queue()

需要追踪到：
1. MQD（Memory Queue Descriptor）是什么结构，在哪个文件定义
2. gfx906 专用的 MQD manager 文件是哪个（kfd_mqd_manager_v9.c？）
3. MQD 初始化填了哪些字段（ring_base、doorbell_off 等）
4. GTT 内存如何分配给 MQD
5. Queue 最终如何提交给 GPU 调度器（HWS）
```

---

**任务 4.3：内存管理内核侧**

```
追踪 AMDKFD_IOC_ALLOC_MEMORY_OF_GPU 在内核的完整处理：

从 kfd_ioctl_alloc_memory_of_gpu() 开始，追踪到：
1. amdgpu_amdkfd.c 中的桥接函数
2. amdgpu_vm.c 中的 GPUVM 操作
3. Buffer Object（BO）的创建和管理
4. MAP_MEMORY_TO_GPU 时页表是如何建立的
5. TLB 刷新在哪里触发
```

---

**任务 4.4：中断与事件机制**

```
分析 KFD 的事件和中断机制：
文件：linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_events.c

1. GPU 执行完成后如何触发中断
2. 中断如何路由到 kfd_events.c
3. AMDKFD_IOC_WAIT_EVENTS 的内核侧实现
4. 如何唤醒等待的用户态进程
5. 这和 InterruptSignal 的 WaitAcquire() 是如何串联的
```

---

**任务 4.5：KFD ↔ GFX 驱动接口**

```
分析 amdgpu_amdkfd.c 这个桥接层：

1. KFD 和 GFX 驱动分工是什么
2. 哪些操作必须走这个桥接（内存分配？命令提交？）
3. amdgpu_amdkfd_gpuvm_* 系列函数的作用
4. gfx906 特有的初始化在这里有哪些
```

---

## 7. 实验代码生成

让 Claude Code 直接生成验证实验，加速每个阶段的理解确认。

### 7.1 Signal 延迟对比实验

```
基于我的 hsa_basic.cpp，生成一个对比实验程序：

实验目的：对比 BusyWaitSignal 和 InterruptSignal 的延迟

要求：
1. 两种 signal 各跑 1000 次 dispatch
2. Kernel 用 busy loop 模拟 10μs 执行时间
3. 统计：平均延迟、P50、P95、P99、最大值
4. 用 std::chrono::high_resolution_clock 计时
5. 最后打印对比表格

同时生成对应的 kernel.cl 或 kernel.s
```

### 7.2 内存地址空间探测实验

```
生成一个程序，探测 MI50 的 GPUVM 地址空间布局：

要求：
1. 分配以下类型的内存，打印每种的虚拟地址：
   - hsa_amd_memory_pool_allocate（VRAM global pool）
   - hsa_amd_memory_pool_allocate（fine-grained system memory）
   - 普通 malloc（CPU 内存）
   - hsa_signal_create 返回的 signal value 地址

2. 打印每个地址的：
   - 十六进制地址
   - 高位字段（判断所在孔径）
   - /proc/self/maps 中对应的行

3. 在分配后 sleep(30) 让我能手动查看 /proc/maps
```

### 7.3 Doorbell MMIO 观测实验

```
生成一个程序，观测 doorbell 写入前后的状态：

要求：
1. 创建 queue 后，打印 doorbell_signal 的地址
2. 打印 /proc/self/maps 中 doorbell 对应的映射行
3. 在写 doorbell 前后各读一次 doorbell 地址的值并打印
4. 加入 fence：__sync_synchronize() 位置要正确

目的：验证 doorbell 就是一个 MMIO 映射的内存写操作
```

### 7.4 AQL Packet 字段验证实验

```
生成一个程序，打印 AQL packet 写入前后的内存内容：

要求：
1. hsa_queue_add_write_index 后，在写 packet 前 hexdump 那个 slot
2. 写完所有字段（除 header）后再 hexdump
3. 写完 header 后再 hexdump
4. 用注释标注每个字节对应 hsa_kernel_dispatch_packet_t 的哪个字段

目的：验证 packet 的内存布局和原子写 header 的效果
```

### 7.5 fence scope 正确性实验

```
生成一个实验，验证 fence scope 对正确性的影响：

实验设计：
1. 基准：HSA_FENCE_SCOPE_SYSTEM（你现在的代码）
2. 对比：HSA_FENCE_SCOPE_AGENT
3. 对比：无 fence（header 的 acquire/release bits 全为0）

每种配置各跑 10000 次，检查结果正确性
记录错误率（如果有）和吞吐量差异

注意：无 fence 版本可能导致错误结果，这是预期行为
```

---

## 8. 知识沉淀

研究过程中用 Claude Code 把理解转化为文档。

### 8.1 给关键函数加注释

```
读取 ROCR-Runtime/src/core/runtime/amd_aql_queue.cpp 
的 AqlQueue::Create() 函数

用中文给每个关键步骤加注释，重点解释：
- 每个 mmap 调用的参数含义和返回值用途
- ring buffer 大小计算逻辑
- doorbell offset 的来源
- 为什么需要两次内存屏障

输出：加了注释的完整函数代码
```

### 8.2 生成模块设计说明

```
基于 ROCT-Thunk-Interface/src/fmm.c 的源码分析，
用中文写一份 fmm 模块的设计说明文档：

包括：
1. 模块职责（一句话）
2. 核心数据结构（aperture、vm_area 等）
3. 三种内存分配路径的流程图（文字版）
4. 与 KFD ioctl 的交互点
5. MI50 (gfx906) 的特殊处理

格式：Markdown，500字以内，精炼
```

### 8.3 生成端到端调用链文档

```
基于对 ROCR-Runtime、ROCT-Thunk-Interface、linux-kernel 
三个仓库的分析，生成一份端到端调用链文档：

覆盖：从 hsa_queue_create() 到 GPU 开始消费 AQL packet
格式：
  函数名 (文件:行号)
    → 函数名 (文件:行号)
       参数：xxx
       关键操作：xxx
    → ...

要求精确到文件和行号
```

---

## 9. 不适合用 Claude Code 的场景

以下任务必须自己做，Claude Code 无法替代：

| 任务 | 原因 | 正确工具 |
|------|------|---------|
| 观察动态行为 | Claude Code 看不到运行时数据 | roctracer / strace / bpftrace |
| 测量实际延迟 | 必须在 MI50 硬件上实测 | rocprof / std::chrono |
| 验证 IOCTL 参数值 | 需要真实运行 | strace -v |
| GPU 寄存器行为 | 微码级行为超出源码范围 | AMD ISA 手册 + rocprof HW counter |
| 内核 bug 触发复现 | 需要实际运行 | rocgdb + ftrace |
| 性能火焰图 | 需要 profiling 数据 | perf + flamegraph |

---

## 附：Claude Code 提问模板

每次提问包含以下要素，回答质量更高：

```
[文件范围] 告诉 Claude Code 看哪些文件
[具体问题] 精确到函数名或行为
[输出格式] 调用链 / 对比表 / 注释代码 / 设计说明
[关联背景] 粘贴相关的追踪日志或代码片段
```

示例：

```
文件范围：ROCR-Runtime/src/core/runtime/amd_aql_queue.cpp
           ROCT-Thunk-Interface/src/queues.c
           linux-kernel/drivers/gpu/drm/amd/amdkfd/kfd_chardev.c

问题：hsa_queue_create 触发的 AMDKFD_IOC_CREATE_QUEUE ioctl，
      在用户态传入的参数结构体里，doorbell_offset 字段是怎么
      在内核侧赋值并返回的？

输出格式：调用链 + 关键代码片段

背景：我的 strace 输出里看到 ioctl 返回后程序执行了 mmap，
      怀疑 doorbell_offset 就是这个 mmap 的 offset 参数
```

---

*文档版本：2026-04  配套文档：rocm56_research_guide.md*

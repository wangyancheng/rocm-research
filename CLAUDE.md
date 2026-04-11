## 研究背景
研究 ROCm 5.6 运行时与驱动架构
硬件：AMD MI50 (gfx906 / Vega20)
研究范围：ROCr → ROCT/thunk → KFD 内核驱动

## 工作区结构
- ROCR-Runtime/              ROCr 运行时源码（fork 自 AMD 官方，rocm-5.6.0 分支）
- ROCT-Thunk-Interface/      thunk 源码（同上）
- kfd/                       Ubuntu 内核 amdkfd 驱动源码
- kfd_amdgpu/                Ubuntu 内核 amdgpu 驱动源码
- experiments/               实验程序（HIP/HSA）
- notes/                     研究笔记

## 关键约定
- 追踪调用链时列出文件路径和行号
- 涉及 gfx906 专用代码路径时单独标注
- 生成笔记保存到 notes/ 对应子目录
- 实验代码保存到 experiments/

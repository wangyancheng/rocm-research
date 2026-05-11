===============
AMDGPU Glossary
===============

Here you can find some generic acronyms used in the amdgpu driver. Notice that
we have a dedicated glossary for Display Core at
'Documentation/gpu/amdgpu/display/dc-glossary.rst'.

.. glossary::

    active_cu_number
      The number of CUs that are active on the system.  The number of active
      CUs may be less than SE * SH * CU depending on the board configuration.
      活跃计算单元数量（每个粗包括多个流处理器（如64个））。GPU中实际启用的CU（Compute Unit)数量。可能小于物   理设计的总数（SE × SH × CU）。厂商会根据芯片良率、功耗限制或市场定位，屏蔽部分CU，最终驱动检测到的就是   active_cu_number，常用于计算着色器负载和性能调优。我的MI50显卡的active_cu_number是60.
    BACO
      Bus Alive, Chip Off
      总线活动、芯片关闭。一种深度电源状态：GPU 的 PCIe 总线接口保持通电、可响应链路层信号，但核心计算与渲染   逻辑完全断电。用于混合显卡笔记本等场景，可以在保持总线可见的前提下大幅降低功耗，比普通 D3 冷更省电，但   唤醒延迟也更长。
    
    BOCO
      Bus Off, Chip Off
    
    CE
      Constant Engine
      常数引擎。负责将着色器程序中的常量数据高效地广播到各个计算单元，避免用普通访存方式反复读取常数，减轻 L1   缓存的压力。在 AMD GCN 及 RDNA 架构中，CE 在命令处理器附近，对固定参数和统一变量的供给至关重要。
    
    CIK
      Sea Islands
      “海岛”家族代号，对应 AMD 的 Radeon HD 8000/7000 系列部分 GPU，如 Bonaire、Hawaii 等，属于 GCN   （Graphics Core Next）架构的第二代。驱动中普遍用 CIK 来指代这个代际的 IP 配置与初始代码路径。
    
    CB
      Color Buffer
      颜色缓冲区。渲染管线生成像素颜色后写入的缓冲区，属于渲染目标（Render Target）的一种。包含像素的 RGB   或 RGBA 数据，后续可能经过混合、后处理，最终输出到显示器或用作贴图。
    
    CP
      Command Processor
      命令处理器。位于 GPU 前端的核心模块，负责解析来自驱动的命令流、进行上下文管理，并将图形或计算任务分发给   相应的引擎（如 ME、MEC 等）。可以说 CP 是 GPU 的“控制中枢”。
    
    CPC
      Command Processor Compute
      计算命令处理器。专注于处理计算队列中的命令（如 dispatch 内核），为 MEC 和其他计算引擎提供指令。
    
    CPF
      Command Processor Fetch
      命令处理器取指单元。负责从内存中的命令缓冲区预取命令，供后续的 CPG/CPC 解析。
    
    CPG
      Command Processor Graphics
      图形命令处理器。负责处理图形管线相关的命令，如绘制命令、状态设置等，并将它们发送到图形微引擎（ME）。
    
    CPLIB
      Content Protection Library
      内容保护库。用于处理受保护视频内容的加密/解密和认证，比如 HDCP、DPCP。驱动通过此库与 PSP（平台安全处	   理器）以及显示控制器交互，确保合规播放。
    
    CS
      Command Submission
      命令提交。通常指用户态驱动（如 Mesa 的 amdgpu）向内核驱动提交命令缓冲区的过程。内核经过验证后，将命令   写入 HIQ/KIQ 等环形缓冲，由 GPU 的 CP 开始执行。
    
    CSB
      Clear State Indirect Buffer
      清除状态间接缓冲区。在上下文切换或抢占时，用于快速保存或恢复 GPU 的部分状态，以保证多条命令流互不干扰。
    
    CU
      Compute Unit
      计算单元。GPU 的基本计算核心。每个 CU 包含多组流处理器、向量寄存器（VGPR）、标量寄存器（SGPR）、本地数   据共享（LDS）等，能独立运行一个工作组（workgroup）。通常多个 CU 共享一个 SE（Shader Engine）。
    
    DB
      Depth Buffer
      深度缓冲区。存储每个像素的深度值，用于深度测试（Z 测试），决定片元是否可见。通常与 CB 协同工作，实现正确   遮挡。
    
    DFS
      Digital Frequency Synthesizer
      数字频率合成器。用于生成芯片内部所需的各种时钟信号。在 GPU 中常用来动态调整核心频率（与 SMU 配合），支   持精细的功率和性能调节。
    
    ECP
      Enhanced Content Protection
      增强内容保护。可以视作标准内容保护方案的升级版，提供更强的加密和密钥保护能力，常见于高级别的媒体内容保护   要求。
    
    EOP
      End Of Pipe/Pipeline
      流水线末端。一种同步事件，表示某条管线（图形、计算或 DMA）上的工作已经全部完成。驱动用 EOP 来协调 CPU   与 GPU 之间的任务依赖，或用于确认队列中已无命令。
    
    FLR
      Function Level Reset
      功能级复位。PCIe 定义的一种重设机制，可以复位指定功能（Function）的硬件状态而不影响整个设备。用于虚拟   化或驱动恢复，避免全芯片复位带来过大的影响。
    
    GART
      Graphics Address Remapping Table.  This is the name we use for the GPUVM
      page table used by the GPU kernel driver.  It remaps system resources
      (memory or MMIO space) into the GPU's address space so the GPU can access
      them.  The name GART harkens back to the days of AGP when the platform
      provided an MMU that the GPU could use to get a contiguous view of
      scattered pages for DMA.  The MMU has since moved on to the GPU, but the
      name stuck.
      图形地址重映射表。这是 AMD 内核驱动中 GPU 页表的称呼。它将系统内存或 MMIO 空间连续映射到 GPU 虚拟地   址空间，让 GPU 能通过 DMA 访问这些资源。早期 AGP 时代的 MMU 在芯片组中，被称作 GART，现在 GPU 内置   MMU，但名称沿用至今。GART 表主要用于内核驱动访问系统资源。
    
    GC
      Graphics and Compute
      图形与计算模块。它是一个较大的硬件 IP 集合，包含渲染管线、计算单元、光栅化器、RLC 等，是现代 AMD GPU   的核心处理区块。
    
    GDS
      Global Data Share
      全局数据共享。一个容量不大但带宽极高的全局内存区域，可在多个计算单元之间高效共享变量，适合执行 atomic   操作或跨工作组的数据排序等。
    
    GE
      Geometry Engine
      几何引擎。负责顶点获取、图元装配、几何着色器和曲面细分等几何管线阶段的处理，位于光栅化之前。
    
    GMC
      Graphic Memory Controller
      图形内存控制器。管理 GPU 对本地显存（VRAM）和系统内存的访问，提供虚拟地址到物理地址的转换（含           TC/UTC/TLB），并保证内存访问的连贯性和分块。
    
    GPR
      General Purpose Register
      通用寄存器。在着色器核心中泛指所有可用的寄存器，通常分为 VGPR（向量）和 SGPR（标量）。编译器会尽可能将   变量分配到这些寄存器，减少访问显存的延迟。
    
    GPUVM
      GPU Virtual Memory.  This is the GPU's MMU.  The GPU supports multiple
      virtual address spaces that can be in flight at any given time.  These
      allow the GPU to remap VRAM and system resources into GPU virtual address
      spaces for use by the GPU kernel driver and applications using the GPU.
      These provide memory protection for different applications using the GPU.
      GPU 虚拟内存（即 GPU 侧的 MMU）。GPU 支持多套虚拟地址空间同时存在（每个进程或驱动自己一个），可将 VRAM   和系统内存映射到各自的虚拟空间，并实行内存保护。每个虚拟地址空间通过 VMID 区分。
    
    GTT
      Graphics Translation Tables.  This is a memory pool managed through TTM
      which provides access to system resources (memory or MMIO space) for
      use by the GPU. These addresses can be mapped into the "GART" GPUVM page
      table for use by the kernel driver or into per process GPUVM page tables
      for application usage.
      图形转换表（内存池）。由 TTM（Translation Table Manager）管理，用于暂存系统内存中需要被 GPU 访问的   部分，例如应用的顶点缓冲区或命令流。GTT 内的区域会被映射到 GART 页表或进程专用页表中，让 GPU 用虚拟地   址访问系统 RAM。
    
    GWS
      Global Wave Sync
      全局波同步。一种硬件支持的同步机制，可让不同计算单元上的波（wave）等待某一全局事件，常用于需要跨工作组   严格同步的计算任务。
    
    IH
      Interrupt Handler
      中断处理器。收集 GPU 内部各引擎产生的中断（如页面错误、完成信号、温度报警），汇总后通过 PCIe/MSI 发送   给主机 CPU，是 GPU 向驱动报告事件的主要渠道。
    
    IV
      Interrupt Vector
      中断向量。标识具体中断类型，用于 IH 和驱动区分中断源，如显示 VSync、编解码完成、GPU 复位请求等。
    
    HQD
      Hardware Queue Descriptor
      硬件队列描述符。一种特定的队列描述符结构，由 MEC 解析，描述了一条硬件队列的基址、大小、读写指针等信息，   硬件据此取出命令执行。
    
    IB
      Indirect Buffer
      间接缓冲区。一种命令提交方式，CP 可以通过一个间接指令跳转到另一块内存中的命令列表继续执行，从而实现命令   复用或生成。
    
    IMU
      Integrated Management Unit (Power Management support)
      集成管理单元。主要负责电源管理方面的微观控制，和 SMU 协同监控温度、电流、功耗，调节时钟和电压。
    
    IP
        Intellectual Property blocks
        知识产权模块，即硬件 IP 块。指 GPU 中某独立功能模块，比如 VCN、DCN（显示核心）、SDMA 等，各 IP 有     独立的版本号和功能集，驱动通过 IP 版本来适配。
    
    KCQ
      Kernel Compute Queue
      内核计算队列。驱动为内核态计算任务（如 ROCm 的 HSA 内核）创建的队列，由 MEC 负责调度执行。
    
    KFD
      Kernel Fusion Driver
      内核融合驱动。AMD 用于异构计算（CPU+GPU）的内核子系统，主要负责 ROCm 栈中的内存管理、用户态队列创建和   HSA 规范的实现，让 GPU 参与系统级别的共享虚拟内存。
    
    KGQ
      Kernel Graphics Queue
      内核图形队列。驱动在内核态创建的专门处理图形命令的队列，用于显示合成或低层图形操作，通常对应 ME 上的硬   件队列。
    
    KIQ
      Kernel Interface Queue
      内核接口队列。一条特殊的队列，驱动借此与 MEC 通信，例如更新队列的运行/停止状态，或挂起/恢复硬件上的任   务。
    
    MC
      Memory Controller
      内存控制器。位于 GPU 内部，直接连接 VRAM 颗粒，调度和优化显存的读写时序，是 GMC 子模块之一。
    
    MCBP
      Mid Command Buffer Preemption
      中命令缓冲抢占。一种图形管线抢占技术，允许 GPU 在 IB 中间暂停当前绘制任务，切换到更高优先级的任务，随   后再恢复。
    
    ME
      MicroEngine (Graphics)
      图形微引擎。负责执行图形队列上的命令，将各种绘制和状态操作分发到后端的几何引擎、光栅化器等。
    
    MEC
      MicroEngine Compute
      计算微引擎。专门处理计算队列的微处理器，通常每个 MEC 可以包含多个硬件队列（基于 HQD/MQD），调度计算任   务的 wave 到 CU 上。
    
    MES
      MicroEngine Scheduler
      微引擎调度器。在较新一代 GPU 中出现，负责对队列和任务进行更高级别的调度，比如管理用户模式队列（UMSCH     相关），协调多个 MEC 和图形队列的资源分配。
    
    MMHUB
      Multi-Media HUB
      多媒体集线器。连接多媒体引擎（如 VCN、VPE）到内存系统的中枢，通过自己的 UTCL/MMU 访问显存，实现编解码   器高效读写。
    
    MQD
      Memory Queue Descriptor
      内存队列描述符。描述计算队列驻留在内存中的控制结构，包含队列地址、长度、写指针等。MEC 读取 MQD 后，将   其载入到内部管理，实现硬件行走环。
    
    PA
      Primitive Assembler / Physical Address
      图元装配器：光栅化前，将顶点组装成三角形、线等图元。
      物理地址：正文中常指真实的硬件地址，与虚拟地址相对。
    
    PDE
      Page Directory Entry
      页目录项。GPU 多级页表中的一项，指向下一级页表，是 GPUVM 管理大段地址映射的结构。
    
    PFP
      Pre-Fetch Parser (Graphics)
      预取解析器。图形前端的一个单元，负责从命令流中提前取指令并做初步解析，然后移交 ME 进一步处理。
    
    PPLib
      PowerPlay Library - PowerPlay is the power management component.
      PowerPlay 电源管理库。驱动中用来管理 GPU 动态频率电压调节（DVFS）的软件组件，与 SMU 交互以实现功耗   与性能的平衡。
    
    PRT
      Partially Resident Texture (also known as sparse residency)
      部分驻留纹理，也称稀疏纹理。允许应用创建比显存更大的纹理，只将正在使用的部分物理页实际分配，对巨型虚拟纹   理极为有用。
    
    PSP
        Platform Security Processor
        平台安全处理器。一个专用的 ARM 或类似核心，用于在芯片启动和运行时执行安全固件，处理密钥管理、验证固件     签名、内容保护等可信操作。
    
    PTE
      Page Table Entry
      页表项。最基础的内存映射单元，将某一段虚拟地址映射到具体物理页（显存或系统内存）。GPU 的 TLB（UTC）会缓   存这些 PTE。
    
    RB
      Render Backends. Some people called it ROPs.
      渲染后端，也称 ROPs（光栅操作单元）。负责对光栅化生成的片元进行深度/模板测试、混合，并将最终像素写入     CB，同时处理 MSAA 的解析等。
    
    RLC
      RunList Controller. This name is a remnant of past ages and doesn't have
      much meaning today. It's a group of general-purpose helper engines for
      the GFX block. It's involved in GFX power management and SR-IOV, among
      other things.
      运行列表控制器。名称是历史遗留，现代 GPU 中它是一组辅助微引擎的总称，协助 GFX 模块进行电源管理、SR-     IOV 下的上下文切换和保存恢复状态等，并非只运行“列表”。
    
    SC
      Scan Converter
      扫描转换器。光栅化的一部分，将图元离散化生成片元，并插值属性，是经典图形管线中的名称延续。
    
    SDMA
      System DMA
      系统 DMA 引擎。提供独立的异步拷贝能力，能在内存之间（系统<->显存、显存内）快速搬移数据，不占用 3D/计算   引擎。
    
    SE
      Shader Engine
      着色器引擎。一个较大的模块，内含多个 CU、TC、SPI、RB 等，负责从 SPI 接收波前并在其内部的 CU 上调度执   行。高端 GPU 通过堆叠多个 SE 提升并行度。
    
    SGPR
      Scalar General-Purpose Registers
      标量通用寄存器。CU 内运行的所有波（wave）共享同一份标量寄存器数据，适合存储常数、分支条件等每个工作项都   一致的值。
    
    SH
      SHader array
      着色器阵列。一个 SE 可以包含多个 SH，每个 SH 内有一组 CU 和相关的缓存，是 CU 管理的逻辑分组。
    
    SI
      Southern Islands
      “南岛”家族代号，对应 Radeon HD 7000 系列（如 Tahiti、Cape Verde 等），是 GCN 第一代架构，驱动代   码中大量使用 SI 标识该代支持。
    
    SMU/SMC
      System Management Unit / System Management Controller
      系统管理单元/控制器。一个专用微控制器，负责整个 GPU 的电源状态、电压调整、风扇控制和热管理，是芯片功耗   与性能的管家。
    
    SPI (AMDGPU)
      Shader Processor Input
      着色器处理器输入。负责接收已装配的顶点或图元，根据着色器类型分配 wave 到 CU，并传递输入参数，是几何管   线与计算管道的分发站。
    
    SRLC
      Save/Restore List Control
      保存/恢复列表控制。管理 RLC 用于电源状态切换或 GPU 抢占时需要保存和恢复的寄存器的列表。
    
    SRLG
      Save/Restore List GPM_MEM
      与图形功率管理相关的保存/恢复列表，GPM_MEM 指专用的电源管理内存区域。
    
    SRLS
      Save/Restore List SRM_MEM
      与 SR-IOV 或安全相关的保存/恢复列表。
    
    SS
      Spread Spectrum
      展频技术。通过调制时钟频率，将尖峰的电磁干扰能量分散到一定带宽，降低 EMI 认证时的超标风险。
    
    SX
      Shader Export
      着色器导出阶段。将着色器计算得到的结果（如变换后的顶点、像素颜色）写入目标缓冲区或传递给下一个管线阶段。
    
    TA
      Trusted Application
      可信应用程序。在 PSP 的安全环境中运行的应用程序，用于执行敏感操作，如密钥协商、验证等。
    
    TC
      Texture Cache
      纹理缓存。传统上专为纹理读取优化的缓存，在 GCN/RDNA 中通常指 L2 缓存，服务于读写请求，纹理流水线通过   TC 获取数据。
    
    TCP (AMDGPU)
      Texture Cache per Pipe. Even though the name "Texture" is part of this
      acronym, the TCP represents the path to memory shaders; i.e., it is not
      related to texture. The name is a leftover from older designs where shader
      stages had different cache designs; it refers to the L1 cache in older
      architectures.
      每条管道（Pipe）的纹理缓存，但实际上在现代架构中，TCP 是着色器访问内存的主 L1 缓存，不只是纹理。命名是   历史遗留，早期 GPU 为着色器引擎的不同阶段设计了独立的缓存，现在它是通用读取 L1。
    
    TMR
      Trusted Memory Region
      可信内存区域。通过硬件隔离的内存区，只有安全处理器（PSP）或经过授权的 TA 才能访问，用于存储密钥等机密。
    
    TMZ
      Trusted Memory Zone
      可信内存区。提供硬件加密的内存区间，在数据进出显存时自动加解密，用于保护安全应用的数据。
    
    TOC
      Table of Contents
      目录表。多用于固件或安全组件，描述其内各个子模块的偏移和大小，以方便加载和验证。
    
    UMC
      Unified Memory Controller
      统一内存控制器。管理 HBM 或 GDDR 等显存的统一通道，协调读写和刷新等，在物理层直接连接内存颗粒。
    
    UMSCH
      User Mode Scheduler
      用户模式调度器。允许用户态驱动直接参与队列调度，减少内核态陷出，在较新的硬件（如 RDNA3）中用于提高提交   效率。
    
    UTC (AMDGPU)
      Unified Translation Cache. UTC is equivalent to TLB. You might see a
      variation of this acronym with L at the end, i.e., UTCL followed by a
      number; L means the cache level (e.g., UTCL1 and UTCL2).
      统一翻译缓存，即 GPU 的 TLB（旁路转换缓冲）。缓存虚拟地址到物理地址的映射结果。UTCL1 靠近计算单元或引   擎，UTCL2 位于内存控制器后端，两者共同降低页表遍历延迟。
    
    UVD
      Unified Video Decoder
      统一视频解码器。早期的硬件视频解码模块，支持 H.264、VC-1 等解码，后被 VCN 取代。
    
    VCE
      Video Compression Engine
      视频压缩引擎。早期的硬件视频编码模块，用于 H.264 编码等，同样被 VCN 取代。
    
    VCN
      Video Codec Next
      下一代视频编解码器。UVD 和 VCE 的继任者，统一负责硬件视频的编解码，支持 H.265、AV1 等更新的格式，集   成度更高。
    
    VGPR
      Vector General-Purpose Registers
      向量通用寄存器。每个工作项（线程）私有的寄存器，存储向量单元的计算结果，是着色器中最主要的运算数存储地，   占用面积大，决定了 wave 的并行度。
    
    VMID
      Virtual Memory ID
      虚拟内存标识符。GPU 可以在不同应用程序或驱动之间快速切换虚拟地址空间，每个空间都有一个唯一的 VMID，硬   件通过 VMID 隔离并保护页表。
    
    VPE
      Video Processing Engine
      视频处理引擎。专门用于视频后处理的硬件，如缩放、色彩转换、去隔行等，常与 VCN 配合。
    
    XCC
      Accelerator Core Complex
      加速器核心复合体。用于多芯片封装（如 MI200 系列的 Aldebaran）的 GPU 芯片，一个 GPU 可以包含多个     XCC，每个 XCC 是一个基本完整的计算和内存子系统，通过 Infinity Fabric 互联。
    
    XCP
      Accelerator Core Partition
      加速器核心分区。基于 XCC 的逻辑分区，允许将一个物理 GPU 划分为几个独立的实例（如 1/2 GPU），向不同虚   拟机或容器暴露，实现硬件级的隔离和多租户。

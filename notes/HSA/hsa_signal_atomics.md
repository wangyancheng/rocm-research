# HSA 信号如何保障原子操作

> 基于 ROCm 5.6 ROCR-Runtime 源码分析，硬件：MI50 (gfx906)

## 整体层次

```
hsa_signal_xxx() API
    ↓
Signal 虚函数（BusyWaitSignal / InterruptSignal）
    ↓
atomic:: 命名空间（atomic_helpers.h）
    ↓
GCC __atomic_* builtins + x86 fence 指令
    ↓
CPU ↔ GPU 跨设备内存可见性（PCIe + WC 内存）
```

---

## 第一层：数据布局保障

**文件：`src/inc/amd_hsa_signal.h:61`**

```c
#define AMD_SIGNAL_ALIGN_BYTES 64
typedef struct AMD_SIGNAL_ALIGN amd_signal_s {
    amd_signal_kind64_t kind;
    union {
        volatile int64_t value;   // 信号值
        volatile uint32_t* legacy_hardware_doorbell_ptr;
        volatile uint64_t* hardware_doorbell_ptr;
    };
    uint64_t event_mailbox_ptr;
    uint32_t event_id;
    ...
} amd_signal_t;
```

- `volatile int64_t value`：`volatile` 防止编译器将值缓存在寄存器，保证每次都走内存
- **64 字节对齐**（一个完整 cache line）：杜绝跨 cache line 的撕裂读/写
- `SharedSignal` 整体大小固定为 128 字节（编译期 `static_assert` 保证）

---

## 第二层：atomic_helpers.h — x86 WC 内存的关键处理

**文件：`src/core/util/atomic_helpers.h`**

GPU 写信号值时，该内存区域往往以 **Write-Combining (WC)** 方式映射（PCIe BAR 或 GTT 页）。WC 内存不遵循普通 TSO（Total Store Order），C++ 标准原子的 memory_order 语义不足以覆盖。

ROCr 的解法：在 `X64_ORDER_WC` 模式下，绕开 C++ memory_order 到 GCC flag 的映射（统一用 `__ATOMIC_RELAXED`），改为在 `__atomic_*` 前后**手动插入 x86 SSE fence**：

```cpp
static __forceinline void PreFence(std::memory_order order) {
    switch (order) {
        case std::memory_order_release:
        case std::memory_order_acq_rel:
        case std::memory_order_seq_cst:
            _mm_sfence();   // 确保之前所有 store 对其他处理器可见
    }
}

static __forceinline void PostFence(std::memory_order order) {
    switch (order) {
        case std::memory_order_seq_cst:
            _mm_mfence();   // 全屏障
        case std::memory_order_acq_rel:
        case std::memory_order_acquire:
            _mm_lfence();   // 阻止投机性 load 越过此点
    }
}
```

所有 atomic::Load / Store / Cas / Exchange / Add / Sub / And / Or / Xor 都遵循 `PreFence → __atomic_op → PostFence` 的三段式结构。

### 调用路径示例（CasAcqRel）

```
hsa_signal_cas_scacq_screl()
  → BusyWaitSignal::CasAcqRel()          // default_signal.cpp:280
    → atomic::Cas(&signal_.value, ...)    // atomic_helpers.h:227
      1. PreFence(acq_rel) → _mm_sfence()
      2. __atomic_compare_exchange(ptr, &expected, &val, false,
                                   __ATOMIC_RELAXED, __ATOMIC_RELAXED)
      3. PostFence(acq_rel) → _mm_lfence()
```

---

## 第三层：两种信号实现

### BusyWaitSignal（DefaultSignal）

**文件：`src/core/inc/default_signal.h`，`src/core/runtime/default_signal.cpp`**

- 纯用户态轮询，无内核介入
- 等待策略：先用 `_mm_mwaitx` / `_mm_monitorx` 监视内存地址（AMD 专有指令），超过 200μs 后 `os::uSleep(20)` 让出 CPU
- 所有 atomic 操作直接调用 `atomic::` 模板，无额外动作

### InterruptSignal

**文件：`src/core/inc/interrupt_signal.h`，`src/core/runtime/interrupt_signal.cpp`**

- 每次写操作后追加调用 `SetEvent()`：

```cpp
// interrupt_signal.h:208
__forceinline void SetEvent() {
    std::atomic_signal_fence(std::memory_order_seq_cst);  // 纯编译器屏障，防重排
    if (InWaiting()) hsaKmtSetEvent(event_);              // 有等待者才触发 KFD 事件
}
```

- `atomic_signal_fence` 只阻止**编译器**重排（不是 CPU 重排），CPU 重排由前一步的 atomic 操作中的 fence 指令保证
- 等待策略：先自旋 200μs → 若仍未满足条件，调用 `hsaKmtWaitOnEvent(event_, wait_ms)` 陷入内核，由 KFD 驱动在信号到来时唤醒

### 对比

| 特性 | BusyWaitSignal | InterruptSignal |
|------|---------------|----------------|
| 等待机制 | 自旋（mwaitx 优化） | 自旋 200μs → 内核睡眠 |
| 写操作后 | 无额外动作 | SetEvent → hsaKmtSetEvent |
| CPU 消耗 | 高（持续占用核心） | 低（可睡眠释放 CPU） |
| 唤醒延迟 | 低 | 稍高（内核调度开销） |
| 使用场景 | 短等待、高频同步 | 长等待、CPU 需要做其他工作 |

---

## 第四层：CPU ↔ GPU 跨设备可见性

GPU kernel（gfx906）通过 SDMA 或 shader 写信号值：

```
GPU: buffer_atomic_add / s_store_dword → 写到 GTT/GART 物理页
  → PCIe TLP Write 到 CPU 侧内存（signal_.value 地址）
  → CPU cache 失效（由 PCIe snooping + _mm_lfence 保证读到最新值）
```

CPU 侧等待循环（`interrupt_signal.cpp:173`）：

```cpp
value = atomic::Load(&signal_.value, std::memory_order_relaxed);
```

`relaxed` Load 在 WC 模式下不插 PostFence，依赖 `volatile` + mwaitx 的 cache monitoring 感知 GPU 的写入。

`WaitAcquire` 在返回前还会加一道 fence：

```cpp
// interrupt_signal.cpp:234
std::atomic_thread_fence(std::memory_order_acquire);
```

这与写端的 `StoreRelease`（含 `_mm_sfence`）共同构成**完整的 release-acquire 同步对**，保证 GPU kernel 写入的数据对 CPU 可见。

---

## 完整信号流

```
GPU kernel 写信号（buffer_atomic / s_store）
              ↓ PCIe
signal_.value 内存被更新
              ↓
CPU 侧 atomic::Load 读到新值（volatile 防寄存器缓存）
              ↓ (InterruptSignal)
GPU firmware 写 event_mailbox_ptr → KFD 触发中断
              ↓
hsaKmtWaitOnEvent 返回
              ↓
WaitAcquire: atomic_thread_fence(acquire)  ← release-acquire 对完成
              ↓
应用程序看到完整同步后的结果
```

---

## 关键保障点总结

| 机制 | 作用 |
|------|------|
| 64 字节对齐 | 防止 cache line 撕裂读写 |
| `volatile int64_t` | 防止编译器寄存器缓存 |
| x86 `_mm_sfence` / `_mm_lfence` / `_mm_mfence` | 补偿 WC 内存不走 TSO 的问题 |
| GCC `__atomic_*` builtins | 保证单条指令的原子性（LOCK 前缀 / XCHG 等） |
| `atomic_signal_fence` | 仅防编译器重排，作为轻量级屏障使用 |
| `SetEvent` → KFD 事件 | 避免 InterruptSignal 等待者永久自旋 |
| release-acquire 语义配对 | 保证 GPU 写的数据对 CPU 完全可见 |

---

## 相关源文件

- `src/inc/amd_hsa_signal.h` — `amd_signal_t` 结构定义
- `src/core/inc/signal.h` — `Signal` 抽象基类，`SharedSignal`
- `src/core/inc/default_signal.h` — `BusyWaitSignal` / `DefaultSignal`
- `src/core/inc/interrupt_signal.h` — `InterruptSignal`，`SetEvent()`
- `src/core/runtime/default_signal.cpp` — BusyWaitSignal 各 atomic 操作实现
- `src/core/runtime/interrupt_signal.cpp` — InterruptSignal 实现，WaitRelaxed 流程
- `src/core/util/atomic_helpers.h` — PreFence / PostFence / 所有 atomic 模板

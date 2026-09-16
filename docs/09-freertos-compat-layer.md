# 09 — FreeRTOS API 兼容层设计（路线 A）

> **目标**：让 `platform/T5AI/tuyaos/tuyaos_adapter/src/system/` 那 6 个 TKL 文件
> **一行都不改**就能在 XiZi 上跑起来。TuyaOpen 升级时那 6 个文件还能直接 merge。
>
> 依据见 `docs/08`：Tuya 全栈对 FreeRTOS 的依赖收敛在 TKL 系统层的
> **6 个文件 / 47 处调用 / 30 个 API**。

---

## 1. 为什么是「兼容层」而不是「重写 TKL」

| | A. 兼容层（本文） | B. 重写 TKL |
|---|---|---|
| 改 Tuya 代码 | **0 行** | 6 个文件 |
| TuyaOpen 升级 | **可直接 merge** | 每次升级要重新 rebase |
| 要复刻的语义 | FreeRTOS 的（超时、优先级、ISR 变体） | TKL 头文件的 |
| 工作量 | 30 个 API | 30 个 API 的行为 |
| 风险 | 语义偏差会静默传播 | 偏差会当场暴露 |

选 A 的核心理由是**不改上游**：owner 的要求是「仍然留在涂鸦平台」，
fork 掉 Tuya 的 6 个文件会让后续每次 TuyaOpen 升级都变成手工合并。

---

## 2. 交付物形态

```
board/bk7258/third_party_driver/freertos_compat/
├── README.md
├── FreeRTOSConfig.h          必须：FreeRTOS.h 会无条件包含它
├── Makefile
├── include/                  ← 从 TuyaOpen 自带的同一份 FreeRTOS 拷头文件
│   ├── FreeRTOS.h  task.h  semphr.h  queue.h  timers.h  event_groups.h
│   └── portable.h  projdefs.h  StackMacros.h ...
└── src/
    ├── frc_handle.c         控制块分配 / 类型标签
    ├── frc_task.c           xTask*
    ├── frc_sem.c            xSemaphore*
    ├── frc_queue.c          xQueue*
    ├── frc_notify.c         xTaskNotify*（模拟）
    └── frc_heap.c           pvPortMalloc / vPortFree / xPortGet*HeapSize
```

**头文件必须用 TuyaOpen 自带的那一份**（`platform/T5AI/t5_os/ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/include/`），
不要另找版本 —— 这样函数签名由构造保证一致，而不是靠人工核对。

---

## 3. 逐 API 映射表（全部对着 XiUOS 源码核过）

### 3.1 任务

| FreeRTOS | XiZi | 证据 / 说明 |
|---|---|---|
| `xTaskCreate(fn,name,stack,arg,prio,handle)` | `KTaskCreate(name,fn,arg,stack,prio)` → `StartupKTask(id)` | `xs_ktask.h:153,181`。**两步**：创建与启动分开 |
| `xTaskCreateStatic(...)` | 同上（忽略静态缓冲，改用内核栈） | TKL 的 `tkl_thread.c` 未用，但表里留着 |
| `xTaskCreateInPsram(...)` | `KTaskCreate` | Beken 专有变体，语义等同 |
| `xTaskCreatePinnedToCore(...,core)` | `KTaskCreate` + `KTaskCoreCombine(id,core)` | `xs_ktask.h:188`。`tskNO_AFFINITY` → 不调 combine |
| `vTaskDelete(h)` | `KTaskDelete(id)` | `xs_ktask.h:182` |
| `xTaskGetCurrentTaskHandle()` | `GetKTaskDescriptor()` | `xs_ktask.h:179` ✅ 直接对应 |
| `xTaskGetTickCount()` | `CurrentTicksGain()` | `xs_ktick.h:27` |
| `vTaskDelay(ticks)` | `DelayKTask(ticks)` | `xs_ktask.h:184` |
| `pdMS_TO_TICKS(ms)` | `CalculateTickFromTimeMs(ms)` | `xs_ktick.h:29` |
| `tskNO_AFFINITY` | 宏，值为 `0x7FFFFFFF` | — |
| `vTaskStartScheduler()` | **空操作** | XiUOS 的调度器由 `XiUOSStartup()` 起，早于 Tuya 代码 |
| `xTaskGetSchedulerState()` | 恒返回 `taskSCHEDULER_RUNNING` | 能跑到这里说明调度器已在跑 |

### 3.2 互斥量与信号量

**关键发现：XiZi 的互斥量本来就是递归的。**
`kernel/thread/mutex.c:95` 写着 `if (mutex->holder == task) { mutex->recursive_cnt++; }`，
`struct Mutex` 也有 `recursive_cnt` 字段（`xs_mutex.h:36`）。
所以 `xSemaphoreCreateRecursiveMutex` **不需要额外模拟**。

| FreeRTOS | XiZi | 说明 |
|---|---|---|
| `xSemaphoreCreateMutex()` | `KMutexCreate()` | `xs_mutex.h:50` |
| `xSemaphoreCreateRecursiveMutex()` | `KMutexCreate()` | **同一实现**，XiZi 互斥量天然递归 |
| `xSemaphoreCreateCounting(max,init)` | `KSemaphoreCreate(init)` | ⚠️ **上限要自己管**，见 §4.3 |
| `xSemaphoreCreateBinary()` | `KSemaphoreCreate(0)` | 二元信号量 = 初值 0、上限 1 的计数信号量 |
| `xSemaphoreTake(h,ticks)` | `KMutexObtain` 或 `KSemaphoreObtain` | 按控制块类型标签分派 |
| `xSemaphoreTakeRecursive(h,ticks)` | `KMutexObtain` | 同上 |
| `xSemaphoreGive(h)` | `KMutexAbandon` 或 `KSemaphoreAbandon` | |
| `xSemaphoreGiveRecursive(h)` | `KMutexAbandon` | |
| `xSemaphoreGiveFromISR(h,woken)` | 同上 + ISR 处理 | 见 §4.4 |
| `vSemaphoreDelete(h)` | `KMutexDelete` / `KSemaphoreDelete` | 按标签分派 |
| `xSemaphoreTakeFromISR` | 同上 | TKL 未用，表里留着 |

`wait_time` 的 `portMAX_DELAY` → `WAITING_FOREVER`（XiUOS 的常量，`0xFFFFFFFF`）。

### 3.3 队列

`struct Semaphore` 与 `struct MsgQueue` 都不暴露内部计数，所以**占用数自己记**（§4.2）。

| FreeRTOS | XiZi |
|---|---|
| `xQueueCreate(len,item)` | `KCreateMsgQueue(item, len)` — `xs_msg.h:58`，注意**参数顺序相反** |
| `xQueueSend(q,item,ticks)` | `KMsgQueueSendwait(id, buf, size, timeout)` — `xs_msg.h:59` |
| `xQueueSendToBack` / `xQueueSendToFront` | `KMsgQueueSendwait` / `KMsgQueueUrgentSend` — `xs_msg.h:60` |
| `xQueueReceive(q,buf,ticks)` | `KMsgQueueRecv(id, buf, size, timeout)` — `xs_msg.h:62` |
| `xQueueSendFromISR` | `KMsgQueueSend`（非阻塞版）— `xs_msg.h:61` |
| `uxQueueMessagesWaiting(q)` | 控制块里自己记的数（§4.2） |

### 3.4 堆

| FreeRTOS | XiZi | 证据 |
|---|---|---|
| `pvPortMalloc(n)` | `x_malloc(n)` | `xs_memory.h:92` |
| `vPortFree(p)` | `x_free(p)` | `xs_memory.h:93` |
| `xPortGetFreeHeapSize()` | `MemoryInfo(&total,&used,&max)` → `total-used` | `xs_memory.h:105` |
| `xPortGetMinimumEverFreeHeapSize()` | 同上 → `total-max_used` | 同上 |

### 3.5 任务通知（唯一需要真正模拟的）

`xTaskNotify` / `xTaskNotifyFromISR` / `xTaskNotifyGive` / `vTaskNotifyGiveFromISR` /
`ulTaskNotifyTake` / `xTaskNotifyWait` / `xTaskNotifyStateClear` —— XiZi **没有对应原语**。

设计：**每任务一个通知槽**（§4.5），用一张以任务句柄为键的定长表实现。
不用 `KEventCreate` 是因为 FreeRTOS 的 task notification 语义是
「每任务一个 32 位值 + 一个挂起标志」，比事件组更窄也更精确，直接实现更简单。

---

## 4. 五个必须处理的语义差异

### 4.1 句柄形态：XiZi 是 id，FreeRTOS 是句柄

XiZi 全部用 `int32 id`，FreeRTOS 用不透明指针。**不能直接把 id 强转成指针**，因为：

1. `xSemaphoreGive` 拿到句柄后**必须知道它是互斥量还是计数信号量**才能分派
2. 计数信号量要记上限（§4.3）、队列要记占用数（§4.2）
3. `xTaskNotify` 需要每任务状态

**方案**：每个对象分配一个控制块，返回其指针作为句柄。

```c
struct frc_cb {
    uint32_t magic;      /* 0x46524342 "FRCB"，用于校验句柄合法性 */
    uint16_t kind;       /* FRC_KIND_MUTEX / SEM / QUEUE / ... */
    uint16_t flags;
    int32_t  id;         /* 对应的 XiZi id */
    /* 以下按 kind 复用 */
    uint32_t max;        /* 计数信号量上限 */
    uint32_t count;      /* 队列占用数 */
    uint32_t item_size;  /* 队列元素大小 */
};
```

控制块本身用 `x_malloc` 分配（**不是** `pvPortMalloc` —— 那会递归）。

### 4.2 队列占用数

XiZi 没暴露。但**所有收发路径都经过本兼容层**，所以在 `frc_cb.count` 里自己记：
发送成功 `count++`，接收成功 `count--`。TKL 不会绕过兼容层直接调 `KMsgQueueSend`，
所以这个计数是完备的。

### 4.3 计数信号量的上限

`struct Semaphore` 只有 `value` **没有上限**（`xs_sem.h:34-40`），
而 FreeRTOS 的计数信号量是有界的（到顶后 give 不生效）。

→ 在 `frc_cb` 里存 `max` 与当前值，`xSemaphoreGive` 先判上限。
判上限与 give 之间必须**关中断**，否则 ISR 与任务可能同时通过检查。

### 4.4 ISR 安全

`xSemaphoreGiveFromISR` / `xQueueSendFromISR` / `vTaskNotifyGiveFromISR` 会在中断里调。
XiUOS 的 `KMutexAbandon` 等**没有文档保证 ISR 安全**。

第一版策略：**在 ISR 变体里用 `DISABLE_INTERRUPT()` / `ENABLE_INTERRUPT(lock)` 包住**
（`xs_isr.h` 提供，`connect_uart.c` 就是这么用的）。
这不是最优解，但语义上安全，且**可验证**。
真正的问题——`KMsgQueueSend` 在 ISR 里唤醒任务是否会触发 PendSV——留到上板后实测。

### 4.5 任务通知的模拟

```
struct frc_notify {
    void    *task;        /* 任务句柄 */
    uint32_t value;       /* FreeRTOS 的通知值 */
    uint8_t  pending;     /* 是否有未取的通知 */
    uint8_t  waiting;     /* 是否有任务在等 */
    int32_t  wait_sem;    /* 等待者阻塞用的信号量 id */
};
```

- `xTaskNotify(h, v, eSetValueWithOverwrite)` → 覆盖 value，置 pending，唤醒等待者
- `xTaskNotify(h, v, eSetBits)` → `value |= v`
- `xTaskNotify(h, v, eIncrement)` → `value += v`
- `ulTaskNotifyTake(clearOnExit, ticks)` → 等 pending，取值，按需清零
- `xTaskNotifyWait(...)` → 同上但更复杂（`ulBitsToClearOnEntry` / `OnExit`）

表用定长数组（比如 16 项）线性查找。TKL 里通知只用在少数几个任务上，
线性查找的代价可以忽略，换来的是**无动态分配、无锁**。

---

## 5. 覆盖范围与不覆盖的部分

**覆盖**：`docs/08` §3 实测出的 6 个 TKL 文件所需的全部 30 个 API。

**不覆盖**（TKL 系统层没用到，要用时再补）：

- `xTimer*`（软件定时器）—— TKL 未用
- `xEventGroup*` —— TKL 未用（Beken SDK 其他地方用了，但那些走 `bk_rtos`）
- `xStreamBuffer*` —— TKL 未用
- `xTaskNotifyWait` 的全部选项组合 —— 先实现 TKL 实际用到的
- `configASSERT` / trace 钩子 —— 空实现

**明确不做**：不实现 FreeRTOS 内核本身。本层只提供 API 形状，调度、上下文切换、
优先级全归 XiZi。

---

## 6. 验证方式

因为这一层可以**独立于 Beken SDK 编译和运行**，所以能先用 XiUOS 自己的板子验证：

1. 把 `frc_*` 编进 `board/bk7258`（或任一块 XiUOS 板子）
2. 写一个自检任务，依次调用 30 个 API 并打印结果 —— 就像
   `board/*/third_party_driver/test/` 里的既有 `TOOL_TEST_*` 那样
3. 用 `make BOARD=... ` 构建，在现有能跑的板子上确认行为

**这比等所有东西都上板再调要快得多**，也是本路线相对 B 的一个额外好处：
兼容层与硬件无关。

---

## 7. 实现记录（2026-09-16）

已全部落地在 `xiuos-overlay/board/bk7258/third_party_driver/freertos_compat/`：

```
include/  FreeRTOS.h  portmacro.h  portable.h  FreeRTOSConfig.h
          task.h  semphr.h  queue.h
src/      frc_internal.h  frc_handle.c  frc_misc.c  frc_heap.c
          frc_task.c  frc_sem.c  frc_queue.c  frc_notify.c
```

**最终核验（`tools/verify-bk7258.sh`）**

```
构建日志 error 行数: 0
向量表 16 项不匹配: 0
可用堆: 0x28016894..0x28063800 = 307 KiB (92% of AP_RAM)
FreeRTOS API 兼容层: 33/33 全部链入
未定义符号: 0
```

### 实现时撞到的四个坑（都已修，且都值得记住）

**坑 1：上游 `make` 会把编译失败吞掉。**

顶层 `Makefile:87` 写的是

```make
@for dir in $(SRC_DIR);do $(MAKE) -C $$dir; done
```

shell 的 `for` 循环退出码 = **最后一条命令**的状态。所以中间任何一个子目录编译失败，
只要最后一个目录成功，`make` 就返回 0。**第一次构建就是这样：`rc=0`，但 `frc_task.c`
其实编译失败了。**

→ `tools/xizi-build.sh` 现在会自己扫日志里的 `error:` 行数，`rc=0` 但错误数非零时
判定为失败（并打印提示）。

> 这条也影响了更早的结论：**「阶段① 链接通过」当时用的判定标准（只看 `make` 的 rc）
> 不可靠**。已用新的判定重跑，结论不变（0 错误），但过程本身是个教训。

**坑 2：`-Wl,--gc-sections` 会让「链接通过」变成假象。**

XiUOS 的 `LFLAGS` 带 `--gc-sections`。兼容层刚写完时没有任何调用方，
于是**整层被回收** —— `make` 成功、ELF 尺寸不变，但一个符号都没进去。
未定义符号要等到真的接 Tuya 那天才暴露。

→ 加了 `frc_api_table[]` 把所有对外 API 的地址收进来，由 `FreeRTOSCompatInit()`
引用，强制整层参与链接。

**坑 3：编译器常量折叠会把你刚加的链接校验再变成假象。**

加了覆盖表之后仍然只有一个符号进得去。原因：`frc_api_table` 是 `const`，
元素是常量地址，GCC 编译期就知道 `frc_api_table[0]` 非空，于是把
`if (frc_api_table[0] == NULL)` 整个折叠掉 —— **对表的引用随之消失**，
表被回收，表里引用的几十个 API 跟着被回收。

→ 索引必须走 `volatile`：

```c
static volatile unsigned frc_api_probe_idx = 0;
...
if (frc_api_table[frc_api_probe_idx] == NULL) { ... }
```

这条的教训是一般性的：**用「引用了某个表」来对抗 `--gc-sections` 时，
那个引用必须对优化器不透明。**

**坑 4：`KTaskCoreCombine` 只在 `ARCH_SMP` 下存在。**

`xs_ktask.h:188` 无条件声明了它，但实现体在 `#ifdef ARCH_SMP` 里。
本板 `.defconfig` 是 `# CONFIG_ARCH_SMP is not set`，直接调会 undefined reference。

→ 用 `#ifdef ARCH_SMP` 包住（单核下绑核本来就无意义）。
单核构建下 `xTaskCreatePinnedToCore` 会忽略 `xCoreID`。

### 另外两个「必须最先包含 xizi.h」的包含顺序陷阱

XiUOS 上游有些头单独包含会失败，例如 `kernel/include/xs_msg.h:34` 的
`struct IdNode id;` 依赖 `xs_id.h`，而 `xs_msg.h` 自己并不 include 它。
上游靠 `xizi.h` 的包含顺序兜住。

→ 兼容层所有 `.c` 都把 `#include <xizi.h>` 放在**最前**。

### 尚未验证的部分（诚实列出）

| 项 | 状态 |
|---|---|
| 编译 + 链接进 XiUOS 镜像 | ✅ 已验证（33/33） |
| **与真实 TKL 源码的编译兼容** | ✅ **已验证（9/10，见 §8）** |
| 运行时行为 | ❌ **未验证** —— 要等上板 |
| ISR 变体的安全性 | ❌ 未实测。`xSemaphoreGiveFromISR` / `xQueueSendFromISR` / `vTaskNotifyGiveFromISR` 在 ISR 里调 XiZi 原语是否安全，`docs/09 §4.4` 已说明第一版策略是关中断包住，但没跑过 |
| 超时精度 | ❌ 未实测。`ulTaskNotifyTake` 的等待只做一次、不重算剩余时间（见 `frc_notify.c` 文件头） |
| `vTaskPrioritySet` | ⚠️ 故意不实现 —— 上游 `xs_ktask.h:186` 写着 `KTaskPrioSet is bugged, dont use this` |

---

## 8. 用真实 TKL 源码验证（2026-09-16）

§7 的表格里，「与真实 TKL 的编译兼容」原本是未验证项。这一节把它补上 ——
**这是整个兼容层最有价值的一次验证**，因为它测的不是我的理解，而是真实调用方。

**做法**（`tools/tkl-compat-test.sh`）：只**编译**、不链接。
把 `platform/T5AI/tuyaos/tuyaos_adapter/src/system/` 下的真实 TKL 源码
用我的 compat include 编译一遍。编译通过就足以证明签名与调用方一致 ——
链接需要整个 Beken SDK + TuyaOpen 的构建打通，那是下一步的事。

关键细节：**compat 的 include 必须排在 Beken 前面**，否则测的就不是我的兼容层，
因为 Beken 自己也带 `FreeRTOS.h` / `task.h` / `semphr.h` / `queue.h` /
`FreeRTOSConfig.h` / `portmacro.h`。脚本末尾会打印实际命中的头文件路径来核对这一点。

**结果**

```
[OK] tkl_thread.c      [OK] tkl_mutex.c       [OK] tkl_semaphore.c
[OK] tkl_queue.c       [OK] tkl_system.c      [OK] tkl_task_notify.c
[OK] tkl_memory.c      [OK] tkl_atomic.c      [OK] tkl_output.c
[!!] tkl_sleep.c   -> system_hw.h: No such file or directory

9 通过 / 1 失败
```

命中的头文件确认是我的：

```
<repo>/freertos_compat/include/FreeRTOS.h
<repo>/freertos_compat/include/FreeRTOSConfig.h
<repo>/freertos_compat/include/portmacro.h
<repo>/freertos_compat/include/task.h
```

### 这次测试发现了三个真缺口 —— 都是「只看文档看不出来」的

**缺口 1：TKL 不只调用 FreeRTOS 的公开 API，还直接包含它的内部实现头。**

实测：

| 文件 | 包含的内部头 |
|---|---|
| `tkl_atomic.c:17`、`tkl_system.c:21` | `atomic.h` |
| `tkl_thread.c:17` | `mpu_wrappers.h` |
| `tkl_thread.c:19`、`tkl_semaphore.c:15` | `projdefs.h` |

`atomic.h` 是**实现头**，不是声明头 —— `tkl_atomic.c` 直接用
`Atomic_Increment_u32` / `Atomic_Decrement_u32` / `Atomic_Add_u32` /
`Atomic_Subtract_u32` / `Atomic_SwapPointers_p32` /
`Atomic_CompareAndSwap_u32` / `Atomic_CompareAndSwapPointers_p32` 这 7 个操作。

→ 补了 `include/atomic.h`。**没有搬 FreeRTOS 那一整套 port 原子宏**，
而是按本平台直接实现：BK7258 AP 核在 XiZi 下是单核（`.defconfig` 里
`CONFIG_ARCH_SMP` 未开），所以「关中断 + 读改写」就是正确的原子实现。
**若日后开 SMP，这里必须换成 LDREX/STREX**，否则关本核中断挡不住另一个核 ——
这条已写在 `atomic.h` 的文件头里。

→ 另补 `include/projdefs.h`（布尔常量与 `pdMS_TO_TICKS`，并把 `FreeRTOS.h`
里重复的部分移过去，与原版分工一致）和 `include/mpu_wrappers.h`（空实现，
本平台不开 FreeRTOS 的 MPU）。

**缺口 2：两个废弃别名不能省。**

| 报错 | 原因 | 修法 |
|---|---|---|
| `'portTICK_RATE_MS' undeclared`（3 处） | FreeRTOS V10.4 后改名 `portTICK_PERIOD_MS`，TKL 仍用旧名 | `portmacro.h` 里加别名 |
| `unknown type name 'xQueueHandle'`（`tkl_queue.c`） | 旧名 `xQueueHandle`，现名 `QueueHandle_t` | `queue.h` 里加 typedef 别名，顺带给 `semphr.h` 也加了 `xSemaphoreHandle` |

**缺口 3（不是兼容层的问题）：`tkl_sleep.c` 编译不过。**

报 `system_hw.h: No such file or directory` —— 那是 **Beken SDK 的电源管理头链**，
不是 FreeRTOS 相关的。而且 `tkl_sleep.c` **本来就不在那 6 个使用 FreeRTOS 的文件里**
（`analysis/tkl-system-freertos-surface.txt` 里它一次都没出现）。

→ **明确不追**。它是 Beken 的功耗适配器，追它的头链属于「打通完整构建」那件事，
不属于「验证兼容层」。这也是为什么 9/10 而不是 10/10 是**可接受的结论**。

### 这次验证把不确定性缩到了什么程度

| 之前的不确定性 | 现在的状态 |
|---|---|
| 「我猜的签名对不对」 | ✅ **消除了** —— 真实调用方编译通过 |
| 「是不是漏了某些 API」 | ✅ **基本消除** —— 缺的三个头都是编译期硬报错，不是静默问题 |
| 「Beken 自带的 FreeRTOS 头会不会干扰」 | ✅ 已确认不会（compat 在 include 顺序上优先，且脚本会核对命中路径） |
| 「运行时行为对不对」 | ❌ **仍未验证** —— 只能上板 |

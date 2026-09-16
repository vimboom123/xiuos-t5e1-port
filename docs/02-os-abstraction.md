# Beken OS 抽象层剖析

> 本文是整个移植可行性的核心证据。所有结论均来自对 TuyaOpen SDK 的**实测**，非推断。

---

## 1. 结论先行

**闭源无线 / 音频库不调用 FreeRTOS。** 它们只调用 Beken 自己的一层薄抽象
（`rtos_*` / `os_*`），而这一层**已经有三套可替换后端**。

因此移植的本质是：

> **新增第四套后端 `bk_rtos/xizi/`，把约 95 个 `rtos_*` 函数映射到 XiZi 原语。**

- 闭源库：**一行不改**
- FreeRTOS：**源码可见**，且本来就要被 XiZi 替换掉
- 「用 XiZi 替换 FreeRTOS」的叙事：**成立且干净** —— 那层抽象是 Beken 原有的，不是我们加的

---

## 2. 实测一：闭源库的 RTOS 依赖只有 23 个符号

方法：用 SDK 自带工具链导出静态库的未定义符号。

```bash
arm-none-eabi-nm --undefined-only <lib>.a      # 见 tools/nm_closed_libs.py
```

| 库 | 路径 | 大小 | 未定义符号总数 | **其中 RTOS 相关** |
|---|---|---|---|---|
| `libwifi.a` | `cp/components/bk_libs/bk7258/libs/` | 5.9 MB | 1,915 | **15** |
| `libwifi_csi.a` | 同上 | 6.2 MB | 2,009 | **19** |
| `libbluetooth_controller_dual.a` | 同上 | 6.6 MB | 2,352 | **6** |
| `libbluetooth_host_dm_dual.a` | 同上 | 7.2 MB | 3,152 | **0** |
| `libbluetooth_host_dm_dual_ap.a` | `ap/components/bk_libs/bk7258_ap/libs/` | 7.2 MB | 3,151 | **0** |
| `libfdk_aac_enc.a` | 同上 | 9.5 MB | 1,048 | **0** |

**并集 = 23 个符号**（原始输出见 `analysis/closed-lib-rtos-symbols.txt`）：

```
内存(6)    os_memcpy   os_memset   os_memcmp   os_memmove
           os_malloc_debug   os_free_debug

线程(2)    rtos_create_thread          rtos_delete_thread

队列(4)    rtos_init_queue             rtos_deinit_queue
           rtos_push_to_queue          rtos_pop_from_queue

信号量(4)  rtos_init_semaphore         rtos_deinit_semaphore
           rtos_get_semaphore          rtos_set_semaphore

互斥量(4)  rtos_init_mutex             rtos_deinit_mutex
           rtos_lock_mutex             rtos_unlock_mutex

延时(1)    rtos_delay_milliseconds

中断(2)    rtos_disable_int            rtos_enable_int
```

**注意：没有任何一个 FreeRTOS 原生符号**（`xTaskCreate` / `xQueueSend` / `xSemaphoreTake` …）。

---

## 3. 实测二：FreeRTOS 是源码，不是二进制

```
platform/T5AI/t5_os/
├── ap/components/os_source/freertos_smp_v2p0/FreeRTOS-Kernel/   ← AP 核，FreeRTOS SMP v2.0
└── cp/components/os_source/freertos_v10/                        ← CP 核，FreeRTOS v10
```

两边都是**完整可读可改的源码**。

SDK 全局扫描（15616 个 `.c/.h/.cpp`）：

| 指标 | 值 |
|---|---|
| 命中 FreeRTOS API 的文件 | 239 |
| API 调用总次数 | 3,568 |
| 不同 API 种类 | 78 |

**但其中绝大部分是 FreeRTOS 自身的实现**——按调用密度排序的头部全是内核自己的文件：

```
195  cp/.../freertos_v10/include/task.h
177  ap/.../FreeRTOS-Kernel/include/freertos/task.h
123  cp/.../freertos_v10/tasks.c
121  ap/.../FreeRTOS-Kernel/tasks.c
116  ap/.../FreeRTOS-Kernel/queue.c
```

目录分布也印证：`platform/T5AI` 独占 3,494 次，而 `src/`（TuyaOpen 自己的代码）加起来不到 70 次。

> **含义**：TuyaOpen 的应用层几乎不直接碰 FreeRTOS —— 它走 Beken 的 `rtos_*` 抽象。
> **这进一步缩小了移植面。**

---

## 4. 实测三（决定性）：`bk_rtos` 已有多套可替换后端

```
ap/components/bk_rtos/
├── freertos/          FreeRTOS 后端
│   ├── FreeRTOSConfig.h
│   ├── FreeRTOSConfig_SMP_V2P0.h
│   ├── mem_arch.c   mmgmt.c   str_arch.c
│   └── v10/
│       ├── rtos_pub.c        (48 处 FreeRTOS 调用)
│       └── rtos_pub_smp.c    (53 处)
├── non_os/            ★ 无 OS 后端
│   ├── rtos_pub.c      8,122 B
│   ├── port.c         11,440 B
│   ├── portmacro.h    11,579 B
│   ├── mem_arch.c      3,097 B
│   ├── str_arch.c      2,643 B
│   └── heap_4.c       43,080 B
└── include/
    ├── mmgmt.h   bk_rtos_debug.h   sys_rtos.h
```

### `non_os` 为什么是关键证据

它的 `rtos_create_thread` 长这样：

```c
bk_err_t rtos_create_thread(beken_thread_t* thread, uint8_t priority, const char* name,
                            beken_thread_function_t function, uint32_t stack_size,
                            beken_thread_arg_t arg)
{
    if (function) {
        function(arg);        /* 直接同步调用 —— 根本没有线程 */
    }
    return kNoErr;
}
```

**它能在完全没有 RTOS 的情况下链接成功**（行为退化为同步执行）。
这证明这层抽象**在设计上就是为"换 OS / 无 OS"准备的**，不是 FreeRTOS 的专属垫片。

### 于是移植 = 加第四套

```
bk_rtos/freertos/       FreeRTOS        （已有）
bk_rtos/freertos/v10/   FreeRTOS v10    （已有）
bk_rtos/non_os/         无 OS           （已有）
bk_rtos/xizi/           XiZi            ← 新增这一个
```

---

## 5. `bk_rtos` 公共接口规模：约 95 个函数

来源：`ap/include/os/os.h`（1,397 行）。按类统计：

| 类别 | 数量 | 函数 |
|---|---|---|
| 线程 | 14 | `rtos_create_thread` / `_sram_` / `_psram_` / `_static`、`delete`、`suspend`、`suspend_all`、`resume`、`resume_all`、`thread_join`、`thread_force_awake`、`is_current_thread`、`get_current_thread`、`thread_sleep`、`thread_msleep`、`delay_milliseconds`、`print_thread_status` |
| 定时器 | 16 | `rtos_init_timer` / `start` / `stop` / `reload` / `deinit` / `is_timer_init` / `is_timer_running` / `change_period` / `get_timer_expiry_time` + **oneshot × 8** |
| 互斥量 | 10 | `init` / `trylock` / `lock` / `lock_timeout` / `unlock` / `deinit` + **递归版 × 4** |
| 队列 | 8 | `init` / `push_to_queue` / `push_to_queue_front` / `pop_from_queue` / `deinit` / `is_queue_empty` / `is_queue_full` / `reset_queue` |
| 信号量 | 6 | `init` / `init_ex` / `set` / `get` / `get_semaphore_count` / `deinit` |
| 事件标志 | 6 | `init_event_flags` / `wait_for_event_flags` / `set_event_flags` / `clear_event_flags` / `sync_event_flags` / `deinit_event_flags` |
| 堆信息 | 6 | `get_{total,free,minimum_free}_heap_size` ×（内部 + PSRAM） |
| 中断 | 4 | `rtos_disable_int` / `rtos_enable_int` / `rtos_before_sleep` / `rtos_after_sleep` |
| 状态查询 | 3 | `rtos_is_in_interrupt_context` / `rtos_local_irq_disabled` / `rtos_is_scheduler_suspended` |
| 时间 | 3 | `rtos_get_time` / `beken_time_get_time` / `beken_ms_per_tick` |
| 临界区 | 2 | `rtos_enter_critical` / `rtos_exit_critical` |
| 调度器 | 2 | `rtos_start_scheduler` / `rtos_is_scheduler_started` |
| **SMP / 多核** | **5** | `rtos_smp_create_thread` / `rtos_core0_create_thread` / `rtos_core0_create_psram_thread` / `rtos_core1_create_thread` / `rtos_core1_create_psram_thread` |
| 其它 | 5 | `rtos_get_name` / `rtos_get_version` / `rtos_shutdown` / `rtos_get_tick_count` / `rtos_thread_status` |

**对比：闭源库只需要其中 23 个。** 多出来的 70 多个是给 SDK 其它部分用的，
但每个都只是**简单原语映射**。

---

## 6. 由此得出的实现形态

```
ap/components/bk_rtos/xizi/
├── rtos_pub.c        ~95 个函数 → XiZi 原语
├── port.c            架构层（从 non_os/port.c 出发，改 OS 无关部分）
├── portmacro.h
├── mem_arch.c        内存（os_memcpy / os_memset / os_malloc_debug …）
├── str_arch.c        字符串
└── heap_4.c          可直接复用（若有 PSRAM 需求需评估）
```

### 可复用性预判

| 文件 | 复用度 | 说明 |
|---|---|---|
| `heap_4.c` | **高** | 纯分配器算法，与 OS 无关 |
| `port.c` | **中高** | 其中 `platform_is_in_interrupt_context`、`platform_cpsr_content`、`bk_fake_clock`、中断/上下文切换**是芯片私有而非 OS 相关**，应可复用；需逐函数分类 |
| `str_arch.c` | **高** | 字符串实现 |
| `mem_arch.c` | **中** | 取决于 XiZi 的堆接口 |
| `rtos_pub.c` | **全部重写** | 这就是本项目的主体工作 |

---

## 7. 复现方法

```bash
# 依赖：TuyaOpen SDK（含 gcc-arm-none-eabi 工具链）
python tools/nm_closed_libs.py        # 产出 analysis/closed-lib-rtos-symbols.txt
python tools/scan_freertos_surface.py # 产出 analysis/freertos-surface.txt
```

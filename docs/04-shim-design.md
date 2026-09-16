# 04 · `bk_rtos/xizi/` 设计

目标目录：`<TuyaOpenSDK>/platform/T5AI/t5_os/ap/components/bk_rtos/xizi/`
骨架来源：`bk_rtos/non_os/`（可替换后端的现成模板）

> **状态：待实现。** 下表是接口清单与映射骨架。
> 「XiZi 侧」一列的取值必须是**读 XiZi 源码后填**，不得凭猜测填写。
> 标记 `?` 的一律为未验证。

---

## 文件布局

```
bk_rtos/xizi/
├── CMakeLists.txt
├── Kconfig
├── rtos_pub.c        ★ 主体：~95 个函数 → XiZi 原语
├── port.c            架构层（从 non_os/port.c 出发）
├── portmacro.h
├── mem_arch.c        内存（os_memcpy / os_memset / os_malloc_debug / os_free_debug …）
├── str_arch.c
└── heap_4.c          分配器（评估是否直接复用）
```

---

## A. 闭源库硬依赖（23 个）—— 优先级最高

这 23 个符号是 `libwifi.a` / `libbluetooth_*.a` / `libfdk_aac_enc.a` 的**未定义符号**，
**缺一个就链接不过**，因此必须是第一批实现的。

| # | 符号 | 语义要点 | XiZi 侧 |
|---|---|---|---|
| 1 | `os_memcpy` | `memcpy` 语义 | `?` |
| 2 | `os_memset` | `memset` 语义 | `?` |
| 3 | `os_memcmp` | `memcmp` 语义 | `?` |
| 4 | `os_memmove` | `memmove` 语义 | `?` |
| 5 | `os_malloc_debug` | 带调试信息的分配（size/file/line 参数？需读 `non_os` 版签名） | `?` |
| 6 | `os_free_debug` | 配对释放 | `?` |
| 7 | `rtos_create_thread` | 参数：`thread, priority(uint8), name, function, stack_size, arg`；**返回值 `bk_err_t`** | `?` |
| 8 | `rtos_delete_thread` | 传 `NULL` 是否为"删除自己"？需读实现 | `?` |
| 9 | `rtos_init_queue` | `queue, name, message_size, number_of_messages` | `?` |
| 10 | `rtos_deinit_queue` | | `?` |
| 11 | `rtos_push_to_queue` | `queue, message, timeout_ms`；**能否在 ISR 中调用？** | `?` |
| 12 | `rtos_pop_from_queue` | `queue, message, timeout_ms` | `?` |
| 13 | `rtos_init_semaphore` | `semaphore, max_count` | `?` |
| 14 | `rtos_deinit_semaphore` | | `?` |
| 15 | `rtos_get_semaphore` | `semaphore, timeout_ms` | `?` |
| 16 | `rtos_set_semaphore` | | `?` |
| 17 | `rtos_init_mutex` | | `?` |
| 18 | `rtos_deinit_mutex` | | `?` |
| 19 | `rtos_lock_mutex` | | `?` |
| 20 | `rtos_unlock_mutex` | | `?` |
| 21 | `rtos_delay_milliseconds` | `num_ms` | `?` |
| 22 | `rtos_disable_int` | **返回中断屏蔽状态**（用于后续恢复） | `?` |
| 23 | `rtos_enable_int` | 参数为 #22 的返回值 | `?` |

### 必须在动手前确认的语义契约

- [ ] `rtos_disable_int` / `rtos_enable_int` 的**配对语义**：
      是否可嵌套？返回值是 PRIMASK 还是自定义层级？（直接决定中断安全）
- [ ] 队列 / 信号量的 push/set 是否**允许在中断上下文调用**？
      闭源无线库大概率在 ISR 里投递事件，**这一条错了会死机**
- [ ] `timeout_ms` 的 `0` / `RTOS_WAIT_FOREVER` 取值约定
- [ ] `priority` 的**取值范围与方向**（数值大 = 优先级高 还是 低）
- [ ] `stack_size` 单位是字节还是字

---

## B. `bk_rtos` 完整接口（~95 个）—— 按类映射

### 线程（14）

| 函数 | XiZi 侧 |
|---|---|
| `rtos_create_thread` | `?` |
| `rtos_create_sram_thread` | `?`（内存来源差异） |
| `rtos_create_psram_thread` | `?`（需 PSRAM 堆） |
| `rtos_create_thread_static` | `?`（静态栈） |
| `rtos_delete_thread` | `?` |
| `rtos_suspend_thread` / `rtos_suspend_all_thread` | `?` |
| `rtos_resume_thread` / `rtos_resume_all_thread` | `?` |
| `rtos_thread_join` | `?` |
| `rtos_thread_force_awake` | `?` |
| `rtos_is_current_thread` / `rtos_get_current_thread` | `?` |
| `rtos_thread_sleep` / `rtos_thread_msleep` | `?` |
| `rtos_delay_milliseconds` | `?` |
| `rtos_print_thread_status` | `?`（可退化实现） |

### 队列（8）

`rtos_init_queue` / `rtos_push_to_queue` / `rtos_push_to_queue_front` /
`rtos_pop_from_queue` / `rtos_deinit_queue` /
`rtos_is_queue_empty` / `rtos_is_queue_full` / `rtos_reset_queue`

> `push_to_queue_front` 需要 XOR 双链表或等价结构 —— **XiZi 的队列是否支持插队？** `?`

### 信号量（6）

`rtos_init_semaphore` / `rtos_init_semaphore_ex`（带初值）/ `rtos_set_semaphore` /
`rtos_get_semaphore` / `rtos_get_semaphore_count` / `rtos_deinit_semaphore`

### 互斥量（10）

`rtos_init_mutex` / `rtos_trylock_mutex` / `rtos_lock_mutex` / `rtos_lock_mutex_timeout` /
`rtos_unlock_mutex` / `rtos_deinit_mutex`
+ 递归版 4 个：`rtos_init_recursive_mutex` / `rtos_lock_recursive_mutex` /
`rtos_unlock_recursive_mutex` / `rtos_deinit_recursive_mutex`

> **优先级继承**：FreeRTOS mutex 默认带优先级继承，XiZi 的互斥量有吗？`?`
> 无继承时会引入优先级反转 —— 本地有先例（`demos/os/smp/inherit/priority_inversion.c`）。

### 事件标志（6）—— **最可能缺失的一组**

`rtos_init_event_flags` / `rtos_wait_for_event_flags` / `rtos_set_event_flags` /
`rtos_clear_event_flags` / `rtos_sync_event_flags` / `rtos_deinit_event_flags`

> **EventFlags 是 FreeRTOS 特色的「多条件等待任一/全部」原语。**
> XiZi 未必有对应物。若无：需用「信号量 + 位图」自建，或改造调用方。`?`

### 定时器（16）

`rtos_init_timer` / `rtos_start_timer` / `rtos_stop_timer` / `rtos_reload_timer` /
`rtos_deinit_timer` / `rtos_is_timer_init` / `rtos_is_timer_running` /
`rtos_change_period` / `rtos_get_timer_expiry_time`
+ oneshot 8 个：`rtos_init_oneshot_timer` / `rtos_start_oneshot_timer` /
`rtos_stop_oneshot_timer` / `rtos_deinit_oneshot_timer` /
`rtos_is_oneshot_timer_running` / `rtos_is_oneshot_timer_init` /
`rtos_oneshot_reload_timer` / `rtos_oneshot_reload_timer_ex`

> 回调执行上下文是**定时器服务任务**还是**中断**？影响 XiZi 侧实现方式。`?`

### 临界区（2）

`rtos_enter_critical` / `rtos_exit_critical`
> 注意签名与 `rtos_disable_int` **不同**（这里 `exit` 带 `flags` 参数）。
> SMP 下这两者的语义差异是关键。`?`

### 中断（4）

`rtos_disable_int` / `rtos_enable_int` / `rtos_before_sleep` / `rtos_after_sleep`

### 调度器与状态（7）

`rtos_start_scheduler` / `rtos_is_scheduler_started` /
`rtos_is_in_interrupt_context` / `rtos_local_irq_disabled` / `rtos_is_scheduler_suspended` /
`rtos_suspend_all_thread` / + `rtos_shutdown`

### 时间与 tick（4）

`rtos_get_time` / `beken_time_get_time` / `beken_ms_per_tick` / `rtos_get_tick_count`

### 堆信息（6）

`rtos_get_total_heap_size` / `rtos_get_free_heap_size` / `rtos_get_minimum_free_heap_size`
+ PSRAM 版 3 个

### SMP / 多核（5）—— **最大未决项，见 05**

`rtos_smp_create_thread` / `rtos_core0_create_thread` / `rtos_core0_create_psram_thread` /
`rtos_core1_create_thread` / `rtos_core1_create_psram_thread`

### 其它（5）

`rtos_get_name` / `rtos_get_version` / `rtos_thread_status` + 少量辅助

---

## C. 实现顺序建议

```
第 1 批（能让固件链接过）
  A 组 23 个符号 + heap + 临界区 + tick

第 2 批（能让调度跑起来）
  线程全部 + 队列 + 信号量 + 互斥量 + 定时器

第 3 批（补齐）
  事件标志（若缺则自建）+ 堆信息 + 状态查询 + SMP

第 4 批（可选）
  PSRAM 相关、调试打印、递归互斥
```

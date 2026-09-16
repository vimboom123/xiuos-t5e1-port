# bk_rtos/xizi/ —— 待实现

Beken `bk_rtos` 的 **第四套后端**（现有：`freertos/`、`freertos/v10/`、`non_os/`）。

目标路径：
`<TuyaOpenSDK>/platform/T5AI/t5_os/ap/components/bk_rtos/xizi/`

骨架来源：`bk_rtos/non_os/`

## 文件布局

```
xizi/
├── CMakeLists.txt
├── Kconfig
├── rtos_pub.c        ★ 主体
├── port.c
├── portmacro.h
├── mem_arch.c
├── str_arch.c
└── heap_4.c
```

## 硬性要求

**闭源库（`libwifi.a` / `libbluetooth_*.a` / `libfdk_aac_enc.a`）只引用 23 个符号，
缺一个就链接不过。** 这 23 个是第一批必须实现的：

```
os_memcpy  os_memset  os_memcmp  os_memmove  os_malloc_debug  os_free_debug
rtos_create_thread  rtos_delete_thread
rtos_init_queue  rtos_deinit_queue  rtos_push_to_queue  rtos_pop_from_queue
rtos_init_semaphore  rtos_deinit_semaphore  rtos_get_semaphore  rtos_set_semaphore
rtos_init_mutex  rtos_deinit_mutex  rtos_lock_mutex  rtos_unlock_mutex
rtos_delay_milliseconds
rtos_disable_int  rtos_enable_int
```

完整接口约 95 个，逐函数映射表见 [`docs/04-shim-design.md`](../../../docs/04-shim-design.md)。

## 实现前必须确认的语义契约

- [ ] `rtos_disable_int` / `rtos_enable_int` 的配对与嵌套语义
- [ ] 队列 / 信号量的 push / set **能否在中断上下文调用**（闭源无线库大概率在 ISR 里投递）
- [ ] `priority` 的取值范围与方向
- [ ] `timeout_ms` 的 `0` / 永久等待约定
- [ ] `stack_size` 单位
- [ ] 互斥量是否带**优先级继承**
- [ ] 定时器回调的执行上下文

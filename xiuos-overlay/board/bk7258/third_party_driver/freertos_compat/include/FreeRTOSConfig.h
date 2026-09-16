/*
 * FreeRTOSConfig.h —— 给「FreeRTOS API 兼容层」用的配置
 *
 * 【注意】这不是一个 FreeRTOS 内核配置。本兼容层**不包含 FreeRTOS 内核**，
 * 只提供 API 形状，调度/上下文切换/优先级全部归 XiZi。
 * 所以这里配置项的作用是：告诉调用方「这个平台是什么样子」，
 * 而不是去配置一个并不存在的内核。
 *
 * 依据：docs/09-freertos-compat-layer.md
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---- 调度器规模 ----
 * TKL 与 TuyaOpen 需要创建的任务不多。这些值只影响调用方的判断逻辑
 * （比如 TKL 可能据此算栈大小），不影响 XiZi 的调度。 */
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 0
#define configMAX_PRIORITIES                    32
#define configMINIMAL_STACK_SIZE                512
#define configMAX_TASK_NAME_LEN                 16
#define configUSE_16_BIT_TICKS                  0
#define configIDLE_SHOULD_YIELD                 1
#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             1
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_TASK_NOTIFICATIONS            1
#define configQUEUE_REGISTRY_SIZE               8
#define configUSE_QUEUE_SETS                    0
#define configUSE_TRACE_FACILITY                0
#define configUSE_STATS_FORMATTING_FUNCTIONS    0

/* ---- tick ----
 * 与 board/bk7258/.defconfig 里的 CONFIG_TICK_PER_SECOND=1000 保持一致。
 * pdMS_TO_TICKS 走这条换算。 */
#define configTICK_RATE_HZ                      1000
#define configTICK_RATE_HZ_MIN                  1000

/* ---- 堆 ----
 * 我们不提供 FreeRTOS 的 heap_1..5，pvPortMalloc/vPortFree 直接转 XiZi 的
 * x_malloc/x_free（见 frc_heap.c）。所以这几个宏只是让 FreeRTOS.h 里的
 * 条件编译成立。 */
#define configSUPPORT_DYNAMIC_ALLOCATION        1
#define configSUPPORT_STATIC_ALLOCATION         0
#define configTOTAL_HEAP_SIZE                   (0)     /* 堆由 XiZi 管，见 HEAP_BEGIN/END */
#define configAPPLICATION_ALLOCATED_HEAP        0

/* ---- 钩子：全部关掉 ----
 * 开了就要实现对应的回调，而我们不需要。 */
#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/* ---- 断言与临界区 ----
 * configASSERT 走 XiUOS 的 KPrintf 后停机。
 * 真正的临界区实现见 portmacro.h（那里用 XiZi 的 PRIMASK 操作）。 */
#define configASSERT_DEFINED                    1
#define configCHECK_FOR_STACK_OVERFLOW          0

/* ---- 可选 API ----
 * 只开 TKL 系统层实际用到的那些。开多了就要实现更多函数。 */
#define INCLUDE_vTaskPrioritySet                1
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     1
#define INCLUDE_vTaskSuspend                    1
#define INCLUDE_vTaskDelayUntil                 1
#define INCLUDE_vTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1
#define INCLUDE_uxTaskGetStackHighWaterMark     1
#define INCLUDE_xTaskGetIdleTaskHandle          1
#define INCLUDE_eTaskGetState                   1
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  1
#define INCLUDE_xQueueGetMutexHolder            0
#define INCLUDE_xSemaphoreGetMutexHolder        0

#define configUSE_TIMERS                        0
#define configUSE_EVENT_GROUPS                  0
#define configUSE_STREAM_BUFFERS                0
#define configUSE_TASK_NOTIFICATIONS            1

/* ---- Beken 扩展 ----
 * tkl_thread.c 会调 xTaskCreateInPsram / xTaskCreatePinnedToCore，
 * 这两个是 Beken 在 FreeRTOS 上的私有扩展，原版 FreeRTOS 没有。 */
#define configTASK_CREATE_IN_PSRAM_SUPPORTED    1
#define configTASK_CREATE_PINNED_TO_CORE        1
#define configNUM_CORES                         2

#endif /* FREERTOS_CONFIG_H */

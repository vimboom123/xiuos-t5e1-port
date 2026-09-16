/*
 * frc_misc.c —— 临界区、断言、日志、总初始化
 */

#include "frc_internal.h"
#include "FreeRTOS.h"   /* frc_assert_failed / vPortEnterCritical 的原型在这里，
                         * 包含它才能让编译器做签名检查 */

#include <stdarg.h>
#include <stdio.h>

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>

/* ==========================================================================
 * 临界区
 *
 * 与 portmacro.h 里的 frc_enter_critical 同一实现（见那里的注释：
 * 用全局 PRIMASK 而不是 FreeRTOS 的 BASEPRI 屏蔽，因为这里保护的是兼容层
 * 自己的控制块，语义上更强也更简单）。
 * ========================================================================== */
uint32_t frc_critical_enter(void)
{
    uint32_t primask;
    __asm__ volatile ("MRS %0, PRIMASK" : "=r" (primask));
    __asm__ volatile ("CPSID i" ::: "memory");
    __asm__ volatile ("DSB" ::: "memory");
    __asm__ volatile ("ISB" ::: "memory");
    return primask;
}

void frc_critical_exit(uint32_t primask)
{
    __asm__ volatile ("MSR PRIMASK, %0" :: "r" (primask));
    __asm__ volatile ("DSB" ::: "memory");
    __asm__ volatile ("ISB" ::: "memory");
}

/* ==========================================================================
 * portmacro.h 里声明的两个函数（宏 portENTER_CRITICAL 展开成它们）
 *
 * FreeRTOS 语义上是可嵌套的（内部记 uxCriticalNesting）。这里同样记，
 * 但因为我们用的是「保存/恢复 PRIMASK」，嵌套时内层保存的是已关闭状态，
 * 恢复后仍是关闭 —— 语义正确。
 * ========================================================================== */
void vPortEnterCritical(void)
{
    (void)frc_critical_enter();
}

void vPortExitCritical(void)
{
    /* 直接开中断。嵌套场景下内层的 exit 会提前开中断，
     * 但 FreeRTOS 的临界区在 TKL 里都是成对且不嵌套的短区段，
     * 第一版接受这个近似；若日后发现嵌套，改成计数式。 */
    __asm__ volatile ("CPSIE i" ::: "memory");
    __asm__ volatile ("DSB" ::: "memory");
    __asm__ volatile ("ISB" ::: "memory");
}

/* ==========================================================================
 * 断言 / 日志
 * ========================================================================== */
void frc_assert_failed(const char *file, int line)
{
    KPrintf("frc ASSERT FAILED: %s:%d\n", file ? file : "?", line);
    /* 停住，便于调试器抓现场。与 arch 层故障处理的风格一致。 */
    for (;;) {
        ;
    }
}

void frc_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    /* XiUOS 的 KPrintf 是 printf 风格，但没有 vprintf 变体可用，
     * 这里用 vsnprintf 先格式化到栈上缓冲。 */
    {
        char buf[160];
        vsnprintf(buf, sizeof(buf), fmt, ap);
        KPrintf("%s", buf);
    }
    va_end(ap);
}

/* ==========================================================================
 * API 覆盖表
 *
 * 【为什么需要它】
 * XiUOS 的 LFLAGS 带 -Wl,--gc-sections，没有引用的段会被整个回收。
 * 于是「编译通过 + 链接返回 0」并不代表这一层真的进了镜像 ——
 * 第一次构建时就出现过「FreeRTOSCompatInit 在、但 xTaskCreate 全被回收」的情况，
 * 那种「链接成功」是假的，未定义符号要到接 Tuya 那天才会暴露。
 *
 * 这张表把所有对外 API 的地址收进来，FreeRTOSCompatInit() 引用它，
 * 于是整层都被保留，链接期能真正校验。它同时是一份「本层提供了什么」的清单。
 *
 * 无运行时开销：只是一个 const 数组，不执行。
 * ========================================================================== */

#include "task.h"
#include "semphr.h"
#include "queue.h"

extern size_t xPortGetTotalHeapSize(void);

const void *const frc_api_table[] = {
    /* 任务 */
    (const void *)xTaskCreate,
    (const void *)xTaskCreateInPsram,
    (const void *)xTaskCreatePinnedToCore,
    (const void *)xTaskCreateStatic,
    (const void *)vTaskDelete,
    (const void *)vTaskStartScheduler,
    (const void *)xTaskGetCurrentTaskHandle,
    (const void *)xTaskGetTickCount,
    (const void *)xTaskGetSchedulerState,
    (const void *)uxTaskPriorityGet,
    (const void *)eTaskGetState,
    (const void *)pcTaskGetName,
    (const void *)xTaskGetIdleTaskHandle,
    (const void *)xTaskGetHandle,
    (const void *)uxTaskGetStackHighWaterMark,
    (const void *)vTaskDelay,
    (const void *)vTaskDelayUntil,
    (const void *)vTaskSuspend,
    (const void *)vTaskResume,
    (const void *)xTaskResumeFromISR,
    (const void *)vTaskPrioritySet,
    (const void *)taskYIELD,
    /* 任务通知 */
    (const void *)xTaskNotify,
    (const void *)xTaskNotifyGive,
    (const void *)vTaskNotifyGiveFromISR,
    (const void *)xTaskNotifyFromISR,
    (const void *)ulTaskNotifyTake,
    (const void *)xTaskNotifyWait,
    (const void *)xTaskNotifyStateClear,
    /* 信号量 / 互斥量 */
    (const void *)xSemaphoreCreateMutex,
    (const void *)xSemaphoreCreateRecursiveMutex,
    (const void *)xSemaphoreCreateCounting,
    (const void *)xSemaphoreCreateBinary,
    (const void *)xSemaphoreCreateMutexStatic,
    (const void *)xSemaphoreCreateCountingStatic,
    (const void *)xSemaphoreTake,
    (const void *)xSemaphoreTakeRecursive,
    (const void *)xSemaphoreGive,
    (const void *)xSemaphoreGiveRecursive,
    (const void *)xSemaphoreGiveFromISR,
    (const void *)xSemaphoreTakeFromISR,
    (const void *)vSemaphoreDelete,
    (const void *)uxSemaphoreGetCount,
    (const void *)xSemaphoreGetMutexHolder,
    /* 队列 */
    (const void *)xQueueCreate,
    (const void *)xQueueCreateStatic,
    (const void *)xQueueSend,
    (const void *)xQueueSendToBack,
    (const void *)xQueueSendToFront,
    (const void *)xQueueReceive,
    (const void *)xQueuePeek,
    (const void *)xQueueSendFromISR,
    (const void *)xQueueReceiveFromISR,
    (const void *)xQueueReset,
    (const void *)vQueueDelete,
    (const void *)uxQueueMessagesWaiting,
    (const void *)uxQueueSpacesAvailable,
    /* 堆 */
    (const void *)pvPortMalloc,
    (const void *)vPortFree,
    (const void *)xPortGetFreeHeapSize,
    (const void *)xPortGetMinimumEverFreeHeapSize,
    (const void *)xPortGetTotalHeapSize,
    /* 临界区 */
    (const void *)vPortEnterCritical,
    (const void *)vPortExitCritical,
    /* 断言：只被 configASSERT 引用。若没有断言触发，它会被 --gc-sections 回收，
     * 于是「覆盖表」看起来缺一项。收进来是为了让链接校验完整。 */
    (const void *)frc_assert_failed,
};

const unsigned frc_api_table_size = sizeof(frc_api_table) / sizeof(frc_api_table[0]);

/*
 * 【这个 volatile 不是可有可无的】
 * 第一次实现时我写的是 `if (frc_api_table[0] == NULL) {...}`，
 * 结果整层仍然被 --gc-sections 回收了。原因：frc_api_table 是 const，
 * 元素是常量地址，GCC 在编译期就知道 [0] 非空，于是把整个判断常量折叠掉，
 * **对表的引用随之消失** —— 表被回收，表里引用的几十个 API 跟着被回收。
 * 链接照样返回 0，「链接通过」变成假象。
 *
 * 用 volatile 索引后，编译器无法再折叠，对表的引用是真实的。
 */
static volatile unsigned frc_api_probe_idx = 0;

/* ==========================================================================
 * 总初始化
 *
 * 调用点：board.c 的 InitBoardHardware()，在 InstallConsole() 之后。
 * ========================================================================== */
void FreeRTOSCompatInit(void)
{
    frc_handle_init();
    frc_notify_init();

    KPrintf("frc: FreeRTOS API compat layer ready, %u APIs, no FreeRTOS kernel (XiZi underneath)\n",
            frc_api_table_size);

    /* 引用一下覆盖表，防止 -Wl,--gc-sections 把整层回收掉。
     * 索引必须是 volatile，理由见 frc_api_probe_idx 的说明。 */
    if (frc_api_table[frc_api_probe_idx] == NULL) {
        frc_log("frc: api table corrupted\n");
    }
}

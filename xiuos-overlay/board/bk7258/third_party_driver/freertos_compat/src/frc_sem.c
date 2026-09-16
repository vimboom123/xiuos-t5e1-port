/*
 * frc_sem.c —— 互斥量与信号量 API 转 XiZi
 *
 * 【本平台最大的一个简化】
 * XiZi 的互斥量**本来就是递归的**：
 *   kernel/thread/mutex.c:95   if (mutex->holder == task) { mutex->recursive_cnt++; }
 *   kernel/include/xs_mutex.h:36   struct Mutex { ... uint8 recursive_cnt; ... }
 * 所以 xSemaphoreCreateMutex 与 xSemaphoreCreateRecursiveMutex 走同一个原语，
 * Take/TakeRecursive、Give/GiveRecursive 同理。
 * FreeRTOS 那边要记 owner + count 的一整套模拟，在这里全省掉了。
 *
 * 【必须自己补的一处】
 * XiZi 的计数信号量**没有上限**（xs_sem.h:34-40 的 struct Semaphore 只有 value），
 * 而 FreeRTOS 的计数信号量是有界的。上限在控制块里管，见下面 sem_give()。
 */

#include "frc_internal.h"
#include "FreeRTOS.h"
#include "semphr.h"

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>
#include <xs_sem.h>
#include <xs_mutex.h>
#include <xs_ktick.h>

/* FreeRTOS tick → XiUOS 毫秒。portMAX_DELAY 走 WAITING_FOREVER(-1)。 */
static int32 frc_ticks_to_ms(TickType_t ticks)
{
    if (ticks == portMAX_DELAY) {
        return WAITING_FOREVER;
    }
    if (ticks == 0) {
        return 0;
    }
    return (int32)CalculateTimeMsFromTick((x_ticks_t)ticks);
}

/* ==========================================================================
 * 创建
 * ========================================================================== */

static SemaphoreHandle_t frc_create_mutex(void)
{
    struct frc_cb *cb = frc_cb_alloc(FRC_KIND_MUTEX);
    if (cb == NULL) {
        return NULL;
    }

    cb->id = KMutexCreate();
    if (cb->id < 0) {
        frc_log("frc: KMutexCreate failed\n");
        frc_cb_free(cb);
        return NULL;
    }
    return (SemaphoreHandle_t)cb;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return frc_create_mutex();
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    /* 与普通互斥量同一个原语 —— XiZi 的互斥量天然递归。 */
    return frc_create_mutex();
}

static SemaphoreHandle_t frc_create_counting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
    struct frc_cb *cb;

    if (uxMaxCount == 0 || uxInitialCount > uxMaxCount) {
        frc_log("frc: bad counting sem params max=%u init=%u\n",
                (unsigned)uxMaxCount, (unsigned)uxInitialCount);
        return NULL;
    }

    cb = frc_cb_alloc(FRC_KIND_SEM);
    if (cb == NULL) {
        return NULL;
    }

    /* XiZi 只收初值，上限我们自己记 */
    cb->id = KSemaphoreCreate((uint16)uxInitialCount);
    if (cb->id < 0) {
        frc_log("frc: KSemaphoreCreate failed\n");
        frc_cb_free(cb);
        return NULL;
    }
    cb->max   = (uint32)uxMaxCount;
    cb->count = (uint32)uxInitialCount;
    return (SemaphoreHandle_t)cb;
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount)
{
    return frc_create_counting(uxMaxCount, uxInitialCount);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    /* 二元信号量 = 上限 1、初值 0 的计数信号量 */
    return frc_create_counting(1, 0);
}

SemaphoreHandle_t xSemaphoreCreateMutexStatic(void *pxMutexBuffer)
{
    (void)pxMutexBuffer;    /* XiZi 无静态创建，转向动态 */
    return frc_create_mutex();
}

SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount,
                                                 UBaseType_t uxInitialCount,
                                                 void *pxSemaphoreBuffer)
{
    (void)pxSemaphoreBuffer;
    return frc_create_counting(uxMaxCount, uxInitialCount);
}

/* ==========================================================================
 * 获取 / 释放
 * ========================================================================== */

static BaseType_t frc_sem_take(void *handle, TickType_t xTicksToWait, int is_isr)
{
    struct frc_cb *cb = frc_cb_check(handle, FRC_KIND_NONE);
    int32 ret;
    int32 wait_ms;

    if (cb == NULL) {
        return pdFALSE;
    }
    wait_ms = is_isr ? 0 : frc_ticks_to_ms(xTicksToWait);

    if (cb->kind == FRC_KIND_MUTEX) {
        ret = KMutexObtain(cb->id, wait_ms);
    } else if (cb->kind == FRC_KIND_SEM) {
        ret = KSemaphoreObtain(cb->id, wait_ms);
    } else {
        frc_log("frc: xSemaphoreTake on non-semaphore kind=%u\n", (unsigned)cb->kind);
        return pdFALSE;
    }

    if (ret == EOK) {
        uint32_t primask = frc_critical_enter();
        if (cb->count > 0) {
            cb->count--;
        }
        frc_critical_exit(primask);
        return pdTRUE;
    }
    return pdFALSE;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
    return frc_sem_take(xSemaphore, xTicksToWait, 0);
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait)
{
    /* 递归获取就是普通获取 —— XiZi 互斥量自己记 recursive_cnt */
    return frc_sem_take(xMutex, xTicksToWait, 0);
}

BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return frc_sem_take(xSemaphore, 0, 1);
}

static BaseType_t frc_sem_give(void *handle, int is_isr)
{
    struct frc_cb *cb = frc_cb_check(handle, FRC_KIND_NONE);
    uint32_t primask;
    int32 ret;

    if (cb == NULL) {
        return pdFALSE;
    }

    if (cb->kind == FRC_KIND_MUTEX) {
        /* 互斥量不判上限：递归释放由 XiZi 的 recursive_cnt 处理 */
        ret = KMutexAbandon(cb->id);
        return (ret == EOK) ? pdTRUE : pdFALSE;
    }

    if (cb->kind != FRC_KIND_SEM) {
        frc_log("frc: xSemaphoreGive on non-semaphore kind=%u\n", (unsigned)cb->kind);
        return pdFALSE;
    }

    /*
     * 计数信号量的上限在这里管 —— XiZi 的 KSemaphoreAbandon 不会拦住超过初值的释放，
     * 而 FreeRTOS 语义是「到顶后 give 不生效（返回 pdFALSE）」。
     *
     * 判上限与 give 之间必须关中断：否则 ISR 与任务可能同时通过检查，
     * 把计数顶过 max。见 docs/09 §4.3。
     */
    primask = frc_critical_enter();
    if (cb->count >= cb->max) {
        frc_critical_exit(primask);
        return pdFALSE;
    }
    ret = KSemaphoreAbandon(cb->id);
    if (ret == EOK) {
        cb->count++;
    }
    frc_critical_exit(primask);

    (void)is_isr;
    return (ret == EOK) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
    return frc_sem_give(xSemaphore, 0);
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex)
{
    return frc_sem_give(xMutex, 0);
}

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return frc_sem_give(xSemaphore, 1);
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
    struct frc_cb *cb = frc_cb_check(xSemaphore, FRC_KIND_NONE);

    if (cb == NULL) {
        return;
    }

    if (cb->kind == FRC_KIND_MUTEX) {
        KMutexDelete(cb->id);
    } else if (cb->kind == FRC_KIND_SEM) {
        KSemaphoreDelete(cb->id);
    }
    frc_cb_free(cb);
}

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore)
{
    struct frc_cb *cb = frc_cb_check(xSemaphore, FRC_KIND_NONE);
    if (cb == NULL) {
        return 0;
    }
    return (UBaseType_t)cb->count;
}

TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex)
{
    (void)xMutex;
    return NULL;    /* XiZi 未暴露 holder，第一版返回 NULL */
}

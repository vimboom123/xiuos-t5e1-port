/*
 * semphr.h —— 信号量 API（FreeRTOS 形状）
 *
 * 【本平台的一个重要简化】
 * 原版 FreeRTOS 里信号量是队列的特例（semphr.h 全是宏，展开成 xQueue*）。
 * 但 **XiZi 有原生的互斥量与计数信号量**，而且：
 *
 *   kernel/thread/mutex.c:95
 *       if (mutex->holder == task) { mutex->recursive_cnt++; ... }
 *   kernel/include/xs_mutex.h:36
 *       struct Mutex { ... uint8 recursive_cnt; ... }
 *
 * —— XiZi 的互斥量**本来就是递归的**。所以：
 *   xSemaphoreCreateMutex() 与 xSemaphoreCreateRecursiveMutex() 走同一个 XiZi 原语，
 *   xSemaphoreTake() 与 xSemaphoreTakeRecursive() 也是。
 *   这省掉了 FreeRTOS 那边「递归互斥量要记 owner + count」的一整套模拟。
 *
 * 【另一个差异】XiZi 的计数信号量没有上限（xs_sem.h:34-40 只有 value 没有 max），
 * 而 FreeRTOS 的有界。上限在兼容层的控制块里自己管，见 frc_handle.h 与
 * docs/09-freertos-compat-layer.md §4.3。
 */

#ifndef INC_SEMPHR_H
#define INC_SEMPHR_H

#include "FreeRTOS.h"
#include "queue.h"      /* 兼容 FreeRTOS 的包含链：调用方常只 include semphr.h */
#include "task.h"       /* xSemaphoreGetMutexHolder 的返回类型 TaskHandle_t 来自这里 */

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SemaphoreHandle_t;

/* FreeRTOS 原版把 MutexHandle_t 定义为 SemaphoreHandle_t 的别名 */
typedef SemaphoreHandle_t MutexHandle_t;

/* 废弃别名，与 queue.h 里的 xQueueHandle 同理 */
typedef SemaphoreHandle_t xSemaphoreHandle;

/* ---- 创建 ---- */
SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount, UBaseType_t uxInitialCount);
SemaphoreHandle_t xSemaphoreCreateBinary(void);

/* 静态变体：XiZi 无对应物，转向动态创建并忽略调用方给的缓冲 */
SemaphoreHandle_t xSemaphoreCreateMutexStatic(void *pxMutexBuffer);
SemaphoreHandle_t xSemaphoreCreateCountingStatic(UBaseType_t uxMaxCount,
                                                 UBaseType_t uxInitialCount,
                                                 void *pxSemaphoreBuffer);

/* ---- 获取 / 释放 ----
 * 返回值：pdTRUE 成功、pdFALSE 超时。与 FreeRTOS 一致。 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t xMutex, TickType_t xTicksToWait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t xMutex);

BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xSemaphoreTakeFromISR(SemaphoreHandle_t xSemaphore,
                                 BaseType_t *pxHigherPriorityTaskWoken);

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

UBaseType_t uxSemaphoreGetCount(SemaphoreHandle_t xSemaphore);
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t xMutex);

#ifdef __cplusplus
}
#endif

#endif /* INC_SEMPHR_H */

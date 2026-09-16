/*
 * FreeRTOS.h —— FreeRTOS API 兼容层的总头
 *
 * 【这不是 FreeRTOS】本仓库不含 FreeRTOS 内核。这里只声明 API 形状，
 * 实现在 frc_*.c 里，全部转调 XiZi 原语。
 *
 * 为什么要自己写这份头而不是 vendor TuyaOpen 自带的那 640 KB：
 *   1. TuyaOpen 那份 FreeRTOS.h 会连带 portable.h → portmacro.h → 具体芯片的
 *      port 实现，而我们**不用 FreeRTOS 的 port**（上下文切换归 XiZi 的 pendsv.S）
 *   2. vendor 进来还要写 portmacro.h，两头不省
 *   3. 最终验证手段是「用真实的 TKL 源码编译一遍」——编译器会当场暴露签名不匹配，
 *      比人工比对可靠
 *
 * 依据：docs/09-freertos-compat-layer.md
 */

#ifndef FREERTOS_H
#define FREERTOS_H

#include <stdint.h>
#include <stddef.h>

#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include "portable.h"   /* 堆与 port 接口（与原版 FreeRTOS.h 的包含结构一致） */

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 布尔与状态常量
 * ========================================================================== */
#define pdTRUE          ((BaseType_t)1)
#define pdFALSE         ((BaseType_t)0)
#define pdPASS          pdTRUE
#define pdFAIL          pdFALSE

#define pdTICKS_TO_MS(xTicks)   ((TickType_t)(((TickType_t)(xTicks)) / (TickType_t)configTICK_RATE_HZ * (TickType_t)1000))
#define pdMS_TO_TICKS(xTimeInMs) ((TickType_t)(((TickType_t)(xTimeInMs)) * (TickType_t)configTICK_RATE_HZ / (TickType_t)1000))

/* Beken 私有：任务不绑核 */
#define tskNO_AFFINITY  ((BaseType_t)0x7FFFFFFF)

/* 调度器状态 */
#define taskSCHEDULER_SUSPENDED     ((BaseType_t)0)
#define taskSCHEDULER_NOT_STARTED   ((BaseType_t)1)
#define taskSCHEDULER_RUNNING       ((BaseType_t)2)

/* 任务状态（eTaskGetState 用） */
typedef enum {
    eRunning = 0,
    eReady,
    eBlocked,
    eSuspended,
    eDeleted,
    eInvalid
} eTaskState;

/* 通知的置位动作（xTaskNotify 的 eAction 参数） */
typedef enum {
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

/* ==========================================================================
 * 栈深度类型
 * ========================================================================== */
#ifndef configSTACK_DEPTH_TYPE
#define configSTACK_DEPTH_TYPE uint32_t
#endif

/* ==========================================================================
 * 统计结构（vTaskGetInfo / uxTaskGetSystemState 用；本项目只声明不实现）
 * ========================================================================== */
typedef struct xTASK_STATUS {
    void *xHandle;
    const char *pcTaskName;
    UBaseType_t xTaskNumber;
    eTaskState eCurrentState;
    UBaseType_t uxCurrentPriority;
    UBaseType_t uxBasePriority;
    uint32_t ulRunTimeCounter;
    StackType_t *pxStackBase;
    uint32_t usStackHighWaterMark;
} TaskStatus_t;

/* ==========================================================================
 * 断言
 * ========================================================================== */
void frc_assert_failed(const char *file, int line);
#define configASSERT(x) \
    do { if (!(x)) { frc_assert_failed(__FILE__, __LINE__); } } while (0)

/* ==========================================================================
 * 兼容层自己的初始化（**不是 FreeRTOS 的 API**）
 *
 * 必须在任何 frc_* API 被调用之前调一次，用来初始化句柄控制块表。
 * 典型位置：board.c 的 InitBoardHardware()。
 * ========================================================================== */
void FreeRTOSCompatInit(void);

#ifdef __cplusplus
}
#endif

#endif /* FREERTOS_H */

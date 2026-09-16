/*
 * projdefs.h —— FreeRTOS 的基础定义
 *
 * TKL 有两个文件直接 include 它（tkl_semaphore.c:15、tkl_thread.c:19），
 * 所以哪怕是「内部头」也必须提供。
 *
 * 原版 projdefs.h 就是这些内容（布尔常量、pdMS_TO_TICKS、pdTICKS_TO_MS），
 * 只是它需要先有 portmacro.h 给的 TickType_t。
 */

#ifndef PROJDEFS_H
#define PROJDEFS_H

#include "FreeRTOSConfig.h"
#include "portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

#define pdTRUE          ((BaseType_t)1)
#define pdFALSE         ((BaseType_t)0)
#define pdPASS          pdTRUE
#define pdFAIL          pdFALSE

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(xTimeInMs) \
    ((TickType_t)(((TickType_t)(xTimeInMs)) * (TickType_t)configTICK_RATE_HZ / (TickType_t)1000))
#endif
#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTicks) \
    ((TickType_t)(((TickType_t)(xTicks)) / (TickType_t)configTICK_RATE_HZ * (TickType_t)1000))
#endif

#ifdef __cplusplus
}
#endif

#endif /* PROJDEFS_H */

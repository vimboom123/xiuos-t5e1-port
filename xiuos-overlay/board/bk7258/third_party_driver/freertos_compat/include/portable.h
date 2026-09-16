/*
 * portable.h —— FreeRTOS 的「可移植层」接口
 *
 * 原版 FreeRTOS 里这个头声明堆管理与 port 相关的接口，并由 FreeRTOS.h 包含。
 * 我们保留同样的结构（虽然大部分内容用不上），这样调用方按原版习惯
 * `#include "portable.h"` 也能work，而且堆函数的声明位置与原版一致。
 *
 * 本层**不提供 FreeRTOS 的 heap_1..5**：堆完全归 XiZi 管，
 * 由 board.c 的 InitBoardMemory(HEAP_BEGIN, HEAP_END) 划出来，
 * 下面这几个函数只是转调 x_malloc / x_free / MemoryInfo。
 */

#ifndef PORTABLE_H
#define PORTABLE_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 堆 ----
 * pvPortMalloc/vPortFree 的语义与 FreeRTOS 一致（失败返回 NULL、free(NULL) 安全）。 */
void *pvPortMalloc(size_t xWantedSize);
void  vPortFree(void *pv);

/* 历史最低剩余堆。注意这是**低水位**，不是当前值 ——
 * 用 MemoryInfo() 的峰值已用量算出来，与 FreeRTOS 同语义。 */
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);

/* 本层扩展（FreeRTOS 没有）：堆总量，便于上板后核对 board.h 的 HEAP_BEGIN/END */
size_t xPortGetTotalHeapSize(void);

/* ---- 临界区 ---- */
void vPortEnterCritical(void);
void vPortExitCritical(void);

#ifdef __cplusplus
}
#endif

#endif /* PORTABLE_H */

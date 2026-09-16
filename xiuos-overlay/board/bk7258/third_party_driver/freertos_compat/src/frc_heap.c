/*
 * frc_heap.c —— 堆 API 转 XiZi
 *
 * FreeRTOS: pvPortMalloc / vPortFree / xPortGetFreeHeapSize / xPortGetMinimumEverFreeHeapSize
 * XiZi:     x_malloc     / x_free     / MemoryInfo(&total,&used,&max_used)
 *
 * 我们**不提供 FreeRTOS 的 heap_1..5** —— 堆完全归 XiZi 管
 * （由 board.c 的 InitBoardMemory(HEAP_BEGIN, HEAP_END) 划出来）。
 */

#include "frc_internal.h"
#include "portable.h"   /* 拿到 pvPortMalloc 等的原型，让编译器做签名检查 */

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>
#include <xs_memory.h>

void *pvPortMalloc(size_t xWantedSize)
{
    if (xWantedSize == 0) {
        return NULL;
    }
    return x_malloc((x_size_t)xWantedSize);
}

void vPortFree(void *pv)
{
    if (pv == NULL) {
        return;
    }
    x_free(pv);
}

/*
 * MemoryInfo(uint32 *total, uint32 *used, uint32 *max_used)
 *   total    —— 堆总字节数
 *   used     —— 当前已用
 *   max_used —— 历史峰值
 *
 * 映射关系：
 *   xPortGetFreeHeapSize()              = total - used
 *   xPortGetMinimumEverFreeHeapSize()   = total - max_used   （历史最小剩余）
 *
 * 注意「最小剩余」用峰值已用来算，这正是 FreeRTOS 那个函数的语义
 * （历史最低水位），不是当前值。
 */
size_t xPortGetFreeHeapSize(void)
{
    uint32 total = 0, used = 0, max_used = 0;
    MemoryInfo(&total, &used, &max_used);
    return (size_t)((used <= total) ? (total - used) : 0);
}

size_t xPortGetMinimumEverFreeHeapSize(void)
{
    uint32 total = 0, used = 0, max_used = 0;
    MemoryInfo(&total, &used, &max_used);
    return (size_t)((max_used <= total) ? (total - max_used) : 0);
}

size_t xPortGetTotalHeapSize(void)
{
    uint32 total = 0, used = 0, max_used = 0;
    MemoryInfo(&total, &used, &max_used);
    return (size_t)total;
}

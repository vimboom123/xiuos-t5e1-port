/*
 * frc_internal.h —— 兼容层内部共用定义
 *
 * 【为什么需要控制块】
 * XiZi 的对象全是 `int32 id`，FreeRTOS 是不透明指针。不能直接把 id 强转成指针：
 *   1. 拿到一个句柄后，xSemaphoreGive 必须知道它是互斥量还是计数信号量 —— 两者
 *      走 XiZi 的不同原语（KMutexAbandon vs KSemaphoreAbandon）
 *   2. 计数信号量要记上限（XiZi 无上限概念）
 *   3. 队列要记占用数（XiZi 不暴露）
 *
 * 所以每个对象配一个控制块，句柄 = 控制块指针。
 * magic 字段用来在运行时挡住野句柄 —— TKL 那边若有 bug 传了错句柄，
 * 我们在这里拦下来，而不是拿着一个乱数字去调 XiZi 内核。
 */

#ifndef FRC_INTERNAL_H
#define FRC_INTERNAL_H

#include <stdint.h>
#include <stddef.h>

#define FRC_MAGIC   0x46524342u   /* "FRCB" */
#define FRC_MAGIC_DEAD  0xDEAD4242u

enum frc_kind {
    FRC_KIND_NONE = 0,
    FRC_KIND_MUTEX,     /* xSemaphoreCreateMutex / RecursiveMutex */
    FRC_KIND_SEM,       /* xSemaphoreCreateCounting / Binary */
    FRC_KIND_QUEUE,
    FRC_KIND_TASK
};

struct frc_cb {
    uint32_t magic;
    uint16_t kind;
    uint16_t flags;
    int32_t  id;            /* 对应的 XiZi id */
    uint32_t max;           /* 计数信号量上限（FRC_KIND_SEM）*/
    uint32_t count;         /* 计数信号量当前值 / 队列占用数 */
    uint32_t item_size;     /* 队列元素字节数（FRC_KIND_QUEUE）*/
    void    *aux;           /* 备用（队列的内部缓冲指针等）*/
};

/* ---- 控制块生命周期（frc_handle.c）---- */

/* 初始化控制块子系统。幂等；由 FreeRTOSCompatInit() 调用。 */
void frc_handle_init(void);

/* 分配一个控制块。失败返回 NULL。 */
struct frc_cb *frc_cb_alloc(uint16_t kind);

/* 释放控制块（先置 magic 为 DEAD 再 free，便于抓 use-after-free）。 */
void frc_cb_free(struct frc_cb *cb);

/* 校验句柄。kind 传 0 表示不校验类型。
 * 返回 NULL 表示句柄非法 —— 调用方应直接返回失败，不要继续。 */
struct frc_cb *frc_cb_check(void *handle, uint16_t kind);

/* ---- 临界区（frc_misc.c）----
 * 与 portmacro.h 里的 frc_enter/exit_critical 同源，这里再暴露一次给内部用。 */
uint32_t frc_critical_enter(void);
void     frc_critical_exit(uint32_t primask);

/* ---- 断言（frc_misc.c）---- */
void frc_assert_failed(const char *file, int line);

/* ---- 内部日志 ----
 * 走 XiUOS 的 KPrintf。级别低于内核 panic，但比静默失败强。 */
void frc_log(const char *fmt, ...);

/* ---- 任务通知子系统（frc_notify.c）---- */
void frc_notify_init(void);

/* ---- 两个必须做的单位换算 ----
 *
 * 【1】栈深度：FreeRTOS 的 usStackDepth 单位是**字**，
 *      XiUOS 的 KTaskCreate 的 stack_depth 单位是**字节**
 *      —— 证据 kernel/thread/ktask.c:699,703 直接把它传给 malloc，
 *         并在 :713 用 memset(stack_start, '#', stack_depth)。
 *      → 必须乘 sizeof(StackType_t)。
 *
 * 【2】超时：FreeRTOS 全部是 **tick**，
 *      XiUOS 的 KMutexObtain/KSemaphoreObtain/KMsgQueueRecv 的 wait_time 是 **毫秒**。
 *      → 用 CalculateTimeMsFromTick() 换算；portMAX_DELAY 走 WAITING_FOREVER。
 *
 * 这两处若漏了，症状分别是「任务一跑就栈溢出」和「超时早 1000 倍」——
 * 都属于上板后极难查的类型，所以在这里显式写出来。
 */
#define FRC_STACK_BYTES(words)  ((uint32)((words) * sizeof(uint32_t)))
#define FRC_MAX_DELAY_MS        (-1)    /* 与 XiUOS 的 WAITING_FOREVER 同为「永久等待」 */

#endif /* FRC_INTERNAL_H */

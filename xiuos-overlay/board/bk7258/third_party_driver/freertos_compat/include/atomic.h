/*
 * atomic.h —— FreeRTOS 的原子操作实现
 *
 * 【为什么必须有这个头】
 * 这是「用真实 TKL 源码编译」测试发现的第一个真缺口：
 * TKL 不只调用 FreeRTOS 的**公开 API**，还直接用它的**内部实现头**。
 *   tkl_atomic.c:17   #include "atomic.h"   → Atomic_Increment_u32 等 7 个操作
 *   tkl_system.c:21   #include "atomic.h"
 *
 * 原版 atomic.h 依赖 portmacro.h 提供的 portATOMIC_* 宏（不同 port 实现不同）。
 * 与其把 FreeRTOS 那一整套 port 宏搬过来，不如**按本平台直接实现**：
 * BK7258 的 AP 核在 XiZi 下是**单核**（.defconfig 里 CONFIG_ARCH_SMP 未开），
 * 所以「关中断 + 读改写」就是正确的原子实现 —— 没有别的核能同时改这块内存。
 *
 * 若日后开了 SMP，这里必须换成 LDREX/STREX 或 __atomic 内建，
 * 否则关本核中断挡不住另一个核。
 *
 * 语义按 FreeRTOS 原版（返回值是新值还是旧值，逐个对着用法的期望写）。
 */

#ifndef ATOMIC_H
#define ATOMIC_H

#include "FreeRTOS.h"
#include "portmacro.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ATOMIC_COMPARE_AND_SWAP_SUCCESS     0x1U
#define ATOMIC_COMPARE_AND_SWAP_FAILED      0x0U

/* ---- 加减法：返回**新值**（与 FreeRTOS 原版一致）---- */

static inline uint32_t Atomic_Increment_u32(volatile uint32_t *pulDestination)
{
    uint32_t primask = frc_enter_critical();
    uint32_t v = ++(*pulDestination);
    frc_exit_critical(primask);
    return v;
}

static inline uint32_t Atomic_Decrement_u32(volatile uint32_t *pulDestination)
{
    uint32_t primask = frc_enter_critical();
    uint32_t v = --(*pulDestination);
    frc_exit_critical(primask);
    return v;
}

static inline uint32_t Atomic_Add_u32(volatile uint32_t *pulDestination, uint32_t ulValue)
{
    uint32_t primask = frc_enter_critical();
    uint32_t v = (*pulDestination += ulValue);
    frc_exit_critical(primask);
    return v;
}

static inline uint32_t Atomic_Subtract_u32(volatile uint32_t *pulDestination, uint32_t ulValue)
{
    uint32_t primask = frc_enter_critical();
    uint32_t v = (*pulDestination -= ulValue);
    frc_exit_critical(primask);
    return v;
}

/* ---- 指针交换：返回**旧值** ---- */

static inline void *Atomic_SwapPointers_p32(void *volatile *ppvDestination, void *pvExchange)
{
    uint32_t primask = frc_enter_critical();
    void *pvOld = *ppvDestination;
    *ppvDestination = pvExchange;
    frc_exit_critical(primask);
    return pvOld;
}

static inline void *Atomic_CompareAndSwapPointers_p32(void *volatile *ppvDestination,
                                                      void *pvExchange,
                                                      void *pvComparand)
{
    uint32_t primask = frc_enter_critical();
    void *pvOld = *ppvDestination;
    if (pvOld == pvComparand) {
        *ppvDestination = pvExchange;
    }
    frc_exit_critical(primask);
    return pvOld;
}

/* ---- 比较并交换：返回 SUCCESS / FAILED ---- */

static inline BaseType_t Atomic_CompareAndSwap_u32(volatile uint32_t *pulDestination,
                                                   uint32_t ulExchange,
                                                   uint32_t ulComparand)
{
    BaseType_t ret = ATOMIC_COMPARE_AND_SWAP_FAILED;
    uint32_t primask = frc_enter_critical();
    if (*pulDestination == ulComparand) {
        *pulDestination = ulExchange;
        ret = ATOMIC_COMPARE_AND_SWAP_SUCCESS;
    }
    frc_exit_critical(primask);
    return ret;
}

#ifdef __cplusplus
}
#endif

#endif /* ATOMIC_H */

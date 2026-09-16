/*
 * portmacro.h —— FreeRTOS 的「移植层」类型与宏
 *
 * 【本文件的作用】
 * 原版 FreeRTOS 里，portmacro.h 由具体芯片的 port 提供（Cortex-M33 那份会定义
 * 上下文切换、临界区、SVC/PendSV 挂钩等）。
 *
 * 我们**不实现 FreeRTOS 的 port** —— 上下文切换归 XiZi 的
 * arch/arm/cortex-m33/pendsv.S。所以这份 portmacro.h 只提供：
 *   1. 调用方需要的类型（StackType_t / BaseType_t / TickType_t ...）
 *   2. 临界区宏 —— 转成 XiZi 的 PRIMASK 操作，**语义正确但没有 FreeRTOS 的
 *      「中断优先级屏蔽」概念**（FreeRTOS 会同时提 BASEPRI，我们只动 PRIMASK）
 *   3. 几个工具宏
 *
 * 【为什么临界区这样实现是安全的】
 * FreeRTOS 的 taskENTER_CRITICAL 屏蔽到 configMAX_SYSCALL_INTERRUPT_PRIORITY，
 * 允许高优先级中断继续跑；进临界区的目的是保护内核数据结构。
 * 我们这里进临界区保护的是**兼容层自己的控制块**（见 frc_handle.c），
 * 用一个全局 PRIMASK 关闭反而是更强也更简单的保证。
 * 代价是临界区期间所有中断都被挡住 —— 对 TKL 那几处极短的临界区可以接受。
 * 若日后实测发现影响实时性，再改成 BASEPRI 方案。
 *
 * 依据：docs/09-freertos-compat-layer.md §4.4
 */

#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. 基础类型 —— 与 FreeRTOS 官方 ARM_CM33 port 保持一致
 * ========================================================================== */
#define portCHAR        char
#define portFLOAT       float
#define portDOUBLE      double
#define portLONG        long
#define portSHORT       short
#define portSTACK_TYPE  uint32_t
#define portBASE_TYPE   long

typedef portSTACK_TYPE  StackType_t;
typedef long            BaseType_t;
typedef unsigned long   UBaseType_t;

#if (configUSE_16_BIT_TICKS == 1)
typedef uint16_t        TickType_t;
#define portMAX_DELAY   (TickType_t)0xffff
#else
typedef uint32_t        TickType_t;
#define portMAX_DELAY   (TickType_t)0xffffffffUL
#define portTICK_TYPE_IS_ATOMIC 1
#endif

/* ==========================================================================
 * 2. 临界区 —— 用 XiUOS 的中断开关
 *
 * 这里**不 include XiUOS 的头**，是为了让本兼容层能独立编译（便于自检）。
 * 下面用内联汇编直接操作 PRIMASK，与 XiUOS 的
 * DisableLocalInterrupt() / EnableLocalInterrupt() 完全等价
 * （见 arch/arm/cortex-m33/interrupt.c:24-39）。
 * ========================================================================== */
static inline uint32_t frc_enter_critical(void)
{
    uint32_t primask;
    __asm__ volatile ("MRS %0, PRIMASK" : "=r" (primask));
    __asm__ volatile ("CPSID i" ::: "memory");
    __asm__ volatile ("DSB" ::: "memory");
    __asm__ volatile ("ISB" ::: "memory");
    return primask;
}

static inline void frc_exit_critical(uint32_t primask)
{
    __asm__ volatile ("MSR PRIMASK, %0" :: "r" (primask));
    __asm__ volatile ("DSB" ::: "memory");
    __asm__ volatile ("ISB" ::: "memory");
}

extern void vPortEnterCritical(void);
extern void vPortExitCritical(void);

#define portENTER_CRITICAL()            vPortEnterCritical()
#define portEXIT_CRITICAL()             vPortExitCritical()
#define portENTER_CRITICAL_FROM_ISR()   (0U)
#define portEXIT_CRITICAL_FROM_ISR(x)   do { (void)(x); } while (0)

#define portSET_INTERRUPT_MASK_FROM_ISR()       frc_enter_critical()
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(x)    frc_exit_critical(x)

/* ==========================================================================
 * 3. 工具宏
 * ========================================================================== */
#define portYIELD()                     __asm__ volatile ("DSB" ::: "memory"); \
                                        __asm__ volatile ("ISB" ::: "memory"); \
                                        (*(volatile uint32_t *)0xE000ED04UL = 0x10000000UL)

#define portYIELD_FROM_ISR(x)           do { (void)(x); portYIELD(); } while (0)
#define portEND_SWITCHING_ISR(x)        portYIELD_FROM_ISR(x)

#define portNVIC_INT_CTRL_REG           (*(volatile uint32_t *)0xE000ED04UL)
#define portNVIC_PENDSVSET_BIT          (0x10000000UL)

#define portBYTE_ALIGNMENT              8
#define portBYTE_ALIGNMENT_MASK         (portBYTE_ALIGNMENT - 1)

#define portNOP()                       __asm__ volatile ("NOP")

/* 任务切换时的栈增长方向与栈填充值（供栈使用量统计用） */
#define portSTACK_GROWTH                (-1)
#define portTICK_PERIOD_MS              ((TickType_t)1000 / configTICK_RATE_HZ)

/* 位操作 —— FreeRTOS 的 list/task 内部用不到，但调用方偶尔引用 */
#define portINLINE                      inline
#ifndef portFORCE_INLINE
#define portFORCE_INLINE                inline __attribute__((always_inline))
#endif

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */

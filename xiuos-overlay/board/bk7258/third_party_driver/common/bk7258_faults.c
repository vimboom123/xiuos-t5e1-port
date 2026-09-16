/*
* Copyright (c) 2020 AIIT XUOS Lab
* XiUOS is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*        http://license.coscl.org.cn/MulanPSL2
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
* See the Mulan PSL v2 for more details.
*/

/**
* @file bk7258_faults.c
* @brief 补齐 arch/arm/cortex-m33/ 缺失的系统异常处理函数
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

/*
 * 【为什么需要这个文件】
 *
 * arch/arm/cortex-m33/ 只导出 4 个系统异常符号（实测 grep 全目录）：
 *     NMI_Handler      interrupt.c:86
 *     BusFault_Handler interrupt.c:78
 *     UsageFault_Handler interrupt.c:70
 *     SVC_Handler      syscall_gcc.S:38
 *     PendSV_Handler_NS pendsv.S:44     <-- 注意名字带 _NS 后缀
 *
 * 而 Cortex-M33 的向量表需要 10 个。缺的这几个必须由 BSP 补，
 * 否则链接期就会 undefined reference（因为向量表是 .word 引用，不是弱符号）。
 *
 * 缺的：HardFault_Handler / MemManage_Handler / SecureFault_Handler / DebugMon_Handler
 * （SysTick_Handler 由 board.c 提供；Reset_Handler 由 startup/boot.S 提供。）
 *
 * 对比：现存两块 m33 板子靠瑞萨 FSP 生成的 vector_data.c + startup.c 顶上了这些，
 *       而 BK7258 没有 FSP，所以只能自己写。
 */

/* 复用 arch 层已有的故障转储：arm32_switch.c 里的 MemFaultHandler 会自己判断
 * 异常发生在 MSP 还是 PSP，然后交给 MemFaultHandle -> MemFaultExceptionPrint
 * 打印出完整异常现场。HardFault 正是最需要这份现场的地方。 */
extern void MemFaultHandler(void);

void __attribute__((naked)) HardFault_Handler(void)
{
    __asm__ volatile("B  MemFaultHandler");
}

void __attribute__((naked)) MemManage_Handler(void)
{
    __asm__ volatile("B  MemFaultHandler");
}

void __attribute__((naked)) SecureFault_Handler(void)
{
    __asm__ volatile("B  MemFaultHandler");
}

/* DebugMonitor 不该在正常运行时触发；真触发了就停住，便于调试器抓现场。 */
void __attribute__((naked)) DebugMon_Handler(void)
{
    __asm__ volatile("b  .");
}

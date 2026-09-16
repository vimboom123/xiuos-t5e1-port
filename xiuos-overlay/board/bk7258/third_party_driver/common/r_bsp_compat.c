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
* @file r_bsp_compat.c
* @brief 顶替 Renesas FSP 的两个中断开关函数，让 arch/ 零改动
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#include <stdint.h>
#include <bk7258_soc.h>

/*
 * 背景：上游 arch/arm/cortex-m33/interrupt.c 的 ArchEnableHwIrq / ArchDisableHwIrq
 *       直接调用 R_BSP_IrqEnable / R_BSP_IrqDisable —— 那是瑞萨 FSP 的 API，
 *       因为现存两块 m33 板子（rzg2ul-m33 / rzv2l-m33）都用 FSP。
 *
 * 做法：在这里提供同名函数，内部转调 CMSIS 等价的 NVIC 操作。
 *       原型声明放在影子头 include/bsp_api.h 里（arch_interrupt.h 会 include 它），
 *       这样 arch/arm/cortex-m33/ 一行都不用改。
 *
 * 语义：完全等价 —— FSP 的 R_BSP_IrqEnable(irq) 也就是 NVIC_EnableIRQ(irq)。
 */

void R_BSP_IrqEnable(uint32_t irq_num)
{
    NVIC_EnableIRQ((IRQn_Type)irq_num);
}

void R_BSP_IrqDisable(uint32_t irq_num)
{
    NVIC_DisableIRQ((IRQn_Type)irq_num);
}

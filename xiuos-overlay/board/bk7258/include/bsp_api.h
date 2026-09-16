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
* @file bsp_api.h
* @brief 【影子头文件】顶替 Renesas FSP 的 bsp_api.h
*
* 背景：arch/arm/cortex-m33/arch_interrupt.h 第 17 行 `#include "bsp_api.h"`，
*       唯一目的是拿到 __NVIC_PRIO_BITS（用于算 MAX_SYSCALL_INTERRUPT_PRIORITY）。
*       那个头是瑞萨 FSP 的，BK7258 上没有。
*
* 做法：在 board/bk7258/include/ 放一个同名头，靠 path_kernel.mk 里
*       `-I$(BSP_ROOT)/include` 排在 Beken 官方头之前来命中它。
*       这样 arch/arm/cortex-m33/ 一行都不用改。
*
* 取值来源见 bk7258_soc.h 顶部注释（Beken armstar.h 实测）。
*/

#ifndef __BK7258_BSP_API_SHIM_H__
#define __BK7258_BSP_API_SHIM_H__

#include <stdint.h>
#include "bk7258_soc.h"

/*
 * arch/arm/cortex-m33/interrupt.c 的 ArchEnableHwIrq/ArchDisableHwIrq 会调这两个。
 * 原型必须在这里给出（而不是等编译器隐式声明），否则参数类型对不上会静默截断。
 * 实现见 third_party_driver/common/r_bsp_compat.c。
 */
void R_BSP_IrqEnable(uint32_t irq_num);
void R_BSP_IrqDisable(uint32_t irq_num);

#endif /* __BK7258_BSP_API_SHIM_H__ */

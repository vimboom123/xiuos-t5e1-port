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
* @file hal_data.h
* @brief 【影子头文件】空实现，顶替 Renesas FSP 的 hal_data.h
*
* 背景：arch/arm/cortex-m33/arm32_switch.c 第 15 行 `#include <hal_data.h>`，
*       但**全文没有使用其中任何一个符号** —— 这是一个死 include。
*
* 做法：放一个空的同名头即可，arch/ 零改动。
*       （已逐行确认过 arm32_switch.c 的 191 行，确实没有任何符号来自该头。）
*/

#ifndef __BK7258_HAL_DATA_SHIM_H__
#define __BK7258_HAL_DATA_SHIM_H__

/* 故意留空 —— 这个头只为满足一个死 include 而存在。 */

#endif /* __BK7258_HAL_DATA_SHIM_H__ */

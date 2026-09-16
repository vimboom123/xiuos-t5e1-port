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
* @file connect_uart.h
* @brief define bk7258 uart function and struct
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#ifndef CONNECT_UART_H
#define CONNECT_UART_H

#include <device.h>
#include <bk7258_soc.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 控制台绑定到 UART1（不是 UART0）。
 * 依据：sdkconfig 里 CONFIG_UART_PRINT_PORT=1、CONFIG_SYS_PRINT_DEV_UART=y、
 *       CONFIG_UART_PRINT_BAUD_RATE=460800；向量表里 UART1_Handler 在 IRQ 15。
 *
 * 与上游 nuvoton-m2354 的差异：那块板子是 UART0 + 115200。
 */
#define KERNEL_CONSOLE_BUS_NAME        SERIAL_BUS_NAME_1
#define KERNEL_CONSOLE_DRV_NAME        SERIAL_DRV_NAME_1
#define KERNEL_CONSOLE_DEVICE_NAME     SERIAL_1_DEVICE_NAME_0

int Bk7258HwUartInit(void);

#ifdef __cplusplus
}
#endif

#endif

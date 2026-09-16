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
* @file bk7258_sys.c
* @brief BK7258 的 SystemCoreClock
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#include <bk7258_soc.h>

/*
 * AP 核主频 = 120 MHz。
 * 出处：build/bk7258/tuya_app/bk7258/sdkconfig 里的 CONFIG_CPU_FREQ_HZ=120000000
 *       （另有 CONFIG_XTAL_FREQ=26000000、CONFIG_DCO_FREQ=120000000）。
 *
 * 注意：参考 README 里一度写的 "480MHz" 是错的 —— 那是把 BK7258 的
 *       Wi-Fi/多媒体标称能力当成了 CPU 主频。实测就是 120 MHz。
 *
 * 第一版直接写死：Beken 的 bootloader 在跳转到我们之前已经把 PLL 配好了，
 * 我们不需要也不应该重配时钟（那会和 CP 核、Wi-Fi 子系统抢控制权）。
 * 等要做动态调频时，再去读 CKMN（0x448A0000）的寄存器反推。
 */
uint32_t SystemCoreClock = BK7258_CPU_FREQ_HZ;

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = BK7258_CPU_FREQ_HZ;
}

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
* @file board.h
* @brief define bk7258 board (Tuya T5-E1) init configure and start-up function
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#ifndef __BOARD_H__
#define __BOARD_H__

/*
 * ---- BK7258 AP 核内存布局（实测，非估算）----
 *
 * 出处：t5_os/build/bk7258/tuya_app/partitions/ram_regions.h
 *       + 真实链接脚本 bk7258_ap_out.ld
 *       + 从 app.elf 向量表读出的初始 _sp = 0x28063800（独立佐证）
 *
 * SRAM 总 640 KiB @0x28000000，五段精确闭合：
 *   0x28000000  AP_SPINLOCK  64 KiB   Beken 核间自旋锁
 *   0x28010000  AP_RAM      334 KiB   <-- XiZi 用这一块
 *   0x28063800  CP_RAM      244 KiB   CP 核，绝不可碰
 *   0x2809F700  PWR_MNG     256 B
 *   0x2809F800  SWAP          2 KiB
 *   0x280A0000  (结束)
 */

#define BK7258_AP_SPINLOCK_BASE   0x28000000UL
#define BK7258_AP_RAM_BASE        0x28010000UL
#define BK7258_AP_RAM_SIZE        0x53800UL          /* 334 KiB */
#define BK7258_AP_RAM_END         (BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE)
#define BK7258_CP_RAM_BASE        0x28063800UL       /* 不要越过这条线 */
#define BK7258_CP_RAM_SIZE        0x3BF00UL

/* 兼容 XiUOS 各板通用的写法 */
#define SRAM_SIZE   (BK7258_AP_RAM_SIZE / 1024)      /* 334，单位 KiB */
#define SRAM_END    (BK7258_AP_RAM_END)

extern int __bss_end;
#define HEAP_BEGIN      ((void *)&__bss_end)
#define HEAP_END        ((void *)SRAM_END)

void InitBoardHardware(void);

#endif

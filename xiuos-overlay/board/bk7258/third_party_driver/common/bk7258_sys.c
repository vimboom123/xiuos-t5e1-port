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

/*
 * UART0（P10 RX / P11 TX，板上丝印 RX0/TX0）的系统级初始化。
 *
 * 板上原厂 TuyaOS 的日志走 UART1（P0/P1），正常启动后没人管 UART0：
 * 时钟门、时钟源、引脚复用、分频都可能不是 bootrom 下载时的状态。
 * 所以这里按 Armino 的 uart_id_init_common() + uart_hal_init_uart() 逐项配齐，
 * 寄存器位全部出自 t5_os/ap/middleware/soc/bk7258_ap（sys_struct.h / gpio_ll.h /
 * gpio_reg.h / uart_struct.h），2026-09-16 逐位核对。
 *
 * 注意：Armino 改 sys 寄存器前会 sys_amp_res_acquire() 和 CP 核互斥，
 * 这里是启动早期的一次性写，暂不做核间互斥。
 */
void Bk7258Uart0SysInit(void)
{
    bk7258_uart_hw_t *uart = BK7258_UART0_BASE;
    uint32_t v;
    int n;

    /* 1. 时钟源 = XTAL 26M、不再分频：clkdiv_uart0 bit[8:9]=0，clksel_uart0 bit10=0 */
    *BK7258_SYS_CLK_DIV_MODE1 &= ~(0x7UL << 8);

    /* 2. 打开 UART0 外设时钟：uart0_cken bit2 */
    *BK7258_SYS_DEV_CLK_EN |= (1UL << 2);

    /* 3. GPIO10/11 选第二功能 0 = UART0_RXD / UART0_TXD（gpio_map.h）
     *    功能号在 gpio_config1（GPIO8~15）里每脚 4 位：GPIO10 = bit[8:11]，GPIO11 = bit[12:15] */
    *BK7258_SYS_GPIO_CONFIG1 &= ~(0xFFUL << 8);

    /*    每脚的 AON 寄存器：io_mode bit[2:3]=2（交给外设）、pull bit[4:5]=3（上拉）、
     *    bit6=1（使能第二功能）。与 gpio_map.h 里 GPIO_11 的默认配置一致。 */
    for (n = 10; n <= 11; n++) {
        v = *BK7258_AON_GPIO(n);
        v &= ~(0x1FUL << 2);
        v |= (2UL << 2) | (3UL << 4) | (1UL << 6);
        *BK7258_AON_GPIO(n) = v;
    }

    /* 4. UART0 本体：旁路时钟门 → 软复位脉冲 → 8N1 + 显式分频 + 开收发
     *    soft_reset（bit0）是**低有效**：Armino 的 uart_ll_soft_reset() 只写 1 且一直保持。
     *    写 0 = 按住复位，TX 脚恒低（USB-TTL 的 RX 灯常亮）。2026-09-16 v3/v4 就栽在这里，
     *    DSH 最初的「写 1 再写 0」也是同一个坑。所以顺序是 0x2（进复位）→ 0x3（放开）。
     *    软复位会清掉 config（含 clk_div），所以分频必须在它之后写。
     *    clk_div = 26000000 / 115200 - 1 = 224，实际 115556 bps（+0.3%）。 */
    /*    驱动层 SerialInit() 会再调一次本函数；软复位会清空 FIFO，
     *    先等早期输出发完，否则 "[XZ] 3/4" 这类行会被吞掉（v5 实测）。 */
    if (uart->global_ctrl.v & 0x1UL) {
        uint32_t spins = 2000000;
        while (!uart->fifo_status.tx_fifo_empty && --spins) {
            ;
        }
    }
    uart->global_ctrl.v = 0x2UL;
    uart->global_ctrl.v = 0x3UL;
    uart->config.v = (BK7258_UART_CLK_HZ / BK7258_CONSOLE_BAUD - 1UL) << 8  /* clk_div */
                   | (3UL << 3)                                             /* 8 bit */
                   | 0x3UL;                                                 /* tx+rx en */

    /* 5. FIFO 阈值：软复位后是 0。rx 阈值设 1 字节，shell 每收一个字就进中断；
     *    fifo_config：tx 阈值 bit[7:0]，rx 阈值 bit[15:8]，rx_stop_detect_time bit[17:16]。 */
    uart->fifo_config.v = (1UL << 8) | 0x40UL;

    /* 6. 把 UART0 中断路由给 cpu1（AP）：sys reg 0x22 cpu1_int_0_31_en，cpu1_uart_int_en = bit4。
     *    不开这一位，NVIC 使能了也收不到（v6 实测：能打字、shell 不回显）。 */
    *BK7258_SYS_CPU1_INT_EN0 |= (1UL << 4);
}

void Bk7258EarlyPutc(char c)
{
    bk7258_uart_hw_t *uart = BK7258_UART0_BASE;
    uint32_t spins = 200000;

    while (uart->fifo_status.tx_fifo_full && --spins) {
        ;
    }
    /* 整字写入：读 fifo_port 会取走 RX 字节，不能用位域的读-改-写 */
    uart->fifo_port.v = (uint8_t)c;
}

void Bk7258EarlyPuts(const char *s)
{
    while (*s) {
        if (*s == '\n') {
            Bk7258EarlyPutc('\r');
        }
        Bk7258EarlyPutc(*s++);
    }
}

/*
 * 不经过 UART 外设的存活信号：把 P11（TX0）当普通 GPIO 慢速拉低/拉高。
 * USB-TTL 的 RX 灯接在这根线上，灯闪 = AP 确实跑到了我们的代码；
 * 串口侧每个低电平段会收到 0x00（break）字节。
 * AON GPIO 寄存器：bit1 输出值，bit2 输入使能，bit3 输出使能（低有效），bit6 第二功能。
 */
static void Bk7258EarlyDelay(uint32_t n)
{
    volatile uint32_t i = n;

    while (i--) {
        ;
    }
}

void Bk7258EarlyBlink(int times)
{
    uint32_t v = *BK7258_AON_GPIO(11);

    v &= ~((1UL << 6) | (3UL << 2));    /* 关第二功能；io_mode=0 即输出使能 */
    while (times-- > 0) {
        *BK7258_AON_GPIO(11) = v & ~(1UL << 1);   /* 低：RX 灯亮 */
        Bk7258EarlyDelay(2000000UL);
        *BK7258_AON_GPIO(11) = v | (1UL << 1);    /* 高：空闲 */
        Bk7258EarlyDelay(2000000UL);
    }
}

/*
 * Reset_Handler 装完栈后第一件事就调这里（早于 .data/.bss 初始化），
 * 所以只能用栈和 flash 里的常量。串口上看到 "[XZ] reset" 就说明
 * CP 确实把 AP 拉起来了、向量表和 CRC 格式都对。
 */
void Bk7258EarlyBoot(void)
{
    Bk7258EarlyBlink(2);
    Bk7258Uart0SysInit();
    Bk7258EarlyPuts("\n[XZ] reset: XiZi AP core alive\n");
}

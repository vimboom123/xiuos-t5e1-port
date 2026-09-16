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
* @file bk7258_soc.h
* @brief Beken BK7258（涂鸦 T5-E1）AP 核的最小自包含 SoC 头
*
* 【为什么不用 CMSIS】
*   上游 arch/arm/cortex-m33/ 全用裸寄存器地址（NVIC_INT_CTRL = 0xE000ED04 之类），
*   对 CMSIS 的唯一依赖是 arch_interrupt.h 经 bsp_api.h 拿 __NVIC_PRIO_BITS。
*   与其 vendor 二十万字节的 ARM CMSIS，不如自己写这一份小而可审计的头：
*   下面每一个常量都能追溯到一次实测。
*
* 【所有取值的出处】
*   设备宏           t5_os/ap/components/cmsis/CMSIS_5/Device/Beken/armstar/armstar.h
*   CPU 主频         build/bk7258/tuya_app/bk7258/sdkconfig  CONFIG_CPU_FREQ_HZ=120000000
*   中断号           build/bk7258/tuya_app/bk7258_ap/app.elf 的 .vectors 段（实测解析）
*   UART 基址        ap/include/soc/bk7258/reg_base.h
*   UART 寄存器布局  ap/middleware/soc/bk7258_ap/soc/uart_struct.h
*   控制台端口       sdkconfig  CONFIG_UART_PRINT_PORT=1 / CONFIG_UART_PRINT_BAUD_RATE=460800
*   内存布局         partitions/ram_regions.h + bk7258_ap_out.ld + 向量表初始 _sp=0x28063800
*/

#ifndef __BK7258_SOC_H__
#define __BK7258_SOC_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ==========================================================================
 * 1. Cortex-M33 设备宏
 *    镜像 Beken armstar.h 的实测值。不要照抄通用 CMSIS 默认值 ——
 *    例如 core_cm33.h 在缺省时会把 __NVIC_PRIO_BITS 当成 3、__FPU_PRESENT 当成 0，
 *    前者恰好对（BK7258 就是 3），后者不对（BK7258 是 M33F，带 FPU）。
 * ========================================================================== */
#define __CM33_REV                0x0000U
#define __SAUREGION_PRESENT       1U
#define __MPU_PRESENT             1U
#define __VTOR_PRESENT            1U
#define __NVIC_PRIO_BITS          3U   /* Beken armstar.h 实测 */
#define __FPU_PRESENT             1U   /* BK7258 是 M33F，硬件有 FPU */
#define __DSP_PRESENT             1U
#define __ICACHE_PRESENT          1U
#define __DCACHE_PRESENT          1U

/*
 * 注意：__FPU_PRESENT=1 表示"芯片有 FPU"，不代表"我们在用 FPU"。
 * 本板 config.mk 刻意不传 -mfpu/-mfloat-abi，走软浮点，于是 __SOFTFP__ 已定义，
 * GCC 不会生成 FPU 指令。等真要开硬件浮点时，除了改 -mfpu，还必须确认
 * arch/arm/cortex-m33/pendsv.S 与 prepare_ahwstack.c 的栈帧保存了 S0-S31。
 */

/* ==========================================================================
 * 2. 时钟
 * ========================================================================== */
#define BK7258_XTAL_FREQ_HZ       26000000UL    /* CONFIG_XTAL_FREQ=26000000  */
#define BK7258_CPU_FREQ_HZ        120000000UL   /* CONFIG_CPU_FREQ_HZ=120000000 */

extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);

/* ==========================================================================
 * 3. 内存（详见 board.h）
 *    这里只放 SoC 级的、不依赖板级策略的常量。
 * ========================================================================== */
#define BK7258_FLASH_BASE         0x02000000UL
#define BK7258_SRAM_BASE          0x28000000UL
#define BK7258_SRAM_CAPACITY      0x000A0000UL   /* 640 KiB */
#define BK7258_PSRAM_BASE         0x60000000UL
#define BK7258_PSRAM_CAPACITY     0x01000000UL   /* 16 MiB */
#define BK7258_ITCM_BASE          0x00000000UL
#define BK7258_DTCM_BASE          0x20000000UL

/* 地址别名：同一块物理 SRAM，取指总线与数据总线看到两个地址。
 * __AP_APP_IRAM_BASE = 0x28010000 - 0x20000000 = 0x08010000。
 * 本项目统一用数据别名 0x28xxxxxx。 */
#define BK7258_IRAM_ALIAS_OFFSET  0x20000000UL

/* ==========================================================================
 * 4. 中断号
 *    逐项来自对真实构建产物 app.elf 的 .vectors 段解析（字 16+n = 外部中断 n）。
 *    与 armstar/bk7236.h 的 IRQn_Type 完全一致，互为佐证。
 * ========================================================================== */
typedef enum {
    /* ---- Cortex-M 系统异常（负数，与向量表字 0..15 对应）---- */
    NonMaskableInt_IRQn   = -14,
    HardFault_IRQn        = -13,
    MemoryManagement_IRQn = -12,
    BusFault_IRQn         = -11,
    UsageFault_IRQn       = -10,
    SecureFault_IRQn      =  -9,
    SVCall_IRQn           =  -5,
    DebugMonitor_IRQn     =  -4,
    PendSV_IRQn           =  -2,
    SysTick_IRQn          =  -1,

    /* ---- BK7258 外部中断（0..63 已实测确认；64 以上本项目暂未使用）---- */
    DMA0_NSEC_IRQn           =  0,
    ENCP_SEC_IRQn            =  1,
    ENCP_NSEC_IRQn           =  2,
    TIMER0_IRQn              =  3,
    UART0_IRQn               =  4,
    PWM0_IRQn                =  5,
    I2C0_IRQn                =  6,
    SPI0_IRQn                =  7,
    SARADC_IRQn              =  8,
    IRDA_IRQn                =  9,
    SDIO_IRQn                = 10,
    GDMA_IRQn                = 11,
    LA_IRQn                  = 12,
    TIMER1_IRQn              = 13,
    I2C1_IRQn                = 14,
    UART1_IRQn               = 15,   /* <-- 控制台，见 CONFIG_UART_PRINT_PORT=1 */
    UART2_IRQn               = 16,
    SPI1_IRQn                = 17,
    CAN_IRQn                 = 18,
    USB_IRQn                 = 19,
    QSPI0_IRQn               = 20,
    CKMN_IRQn                = 21,
    SBC_IRQn                 = 22,
    AUDIO_IRQn               = 23,
    I2S0_IRQn                = 24,
    JPEG_ENC_IRQn            = 25,
    JPEG_DEC_IRQn            = 26,
    DISPLAY_IRQn             = 27,
    DMA2D_IRQn               = 28,
    PHY_MBP_IRQn             = 29,
    PHY_RIU_IRQn             = 30,
    MAC_INT_TX_RX_TIMER_IRQn = 31,
    MAC_INT_TX_RX_MISC_IRQn  = 32,
    MAC_INT_RX_TRIGGER_IRQn  = 33,
    MAC_INT_TX_TRIGGER_IRQn  = 34,
    MAC_INT_PORT_TRIGGER_IRQn= 35,
    MAC_INT_GEN_IRQn         = 36,
    GPIO_NS_IRQn             = 37,
    INT_MAC_WAKEUP_IRQn      = 38,
    DM_IRQn                  = 39,
    BLE_IRQn                 = 40,
    BT_IRQn                  = 41,
    QSPI1_IRQn               = 42,
    PWM1_IRQn                = 43,
    I2S1_IRQn                = 44,
    I2S2_IRQn                = 45,
    H264_IRQn                = 46,
    SDMADC_IRQn              = 47,
    ETHERNET_IRQn            = 48,
    SCAL0_IRQn               = 49,
    OTP_IRQn                 = 50,
    DPLL_UNLOCK_IRQn         = 51,
    TOUCH_IRQn               = 52,
    USB_PLUG_IRQn            = 53,
    RTC_IRQn                 = 54,
    GPIO_IRQn                = 55,
    DMA1_SEC_IRQn            = 56,
    DMA1_NSEC_IRQn           = 57,
    YUV_BUF_IRQn             = 58,
    ROTT_IRQn                = 59,
    BK7816_IRQn              = 60,
    LIN_IRQn                 = 61,
    SCAL1_IRQn               = 62,
    MAILBOX_IRQn             = 63    /* 核间邮箱 —— AP/CP 通信走这个 */
} IRQn_Type;

/* ==========================================================================
 * 5. NVIC / SysTick —— 直接写寄存器，等价于 CMSIS 的对应函数
 *    （CMSIS 里它们本来就是 __STATIC_INLINE 的寄存器操作，这里手写一份，
 *      避免为了 4 个函数 vendor 整个 core_cm33.h。）
 * ========================================================================== */
#define BK7258_NVIC_ISER   ((volatile uint32_t *)0xE000E100UL)
#define BK7258_NVIC_ICER   ((volatile uint32_t *)0xE000E180UL)
#define BK7258_NVIC_ISPR   ((volatile uint32_t *)0xE000E200UL)
#define BK7258_NVIC_ICPR   ((volatile uint32_t *)0xE000E280UL)
#define BK7258_NVIC_IPR    ((volatile uint8_t  *)0xE000E400UL)
#define BK7258_SCB_SHPR    ((volatile uint8_t  *)0xE000ED18UL)
#define BK7258_SCB_VTOR    ((volatile uint32_t *)0xE000ED08UL)
#define BK7258_SYST_CSR    ((volatile uint32_t *)0xE000E010UL)
#define BK7258_SYST_RVR    ((volatile uint32_t *)0xE000E014UL)
#define BK7258_SYST_CVR    ((volatile uint32_t *)0xE000E018UL)

#define BK7258_SYST_CSR_ENABLE    (1UL << 0)
#define BK7258_SYST_CSR_TICKINT   (1UL << 1)
#define BK7258_SYST_CSR_CLKSOURCE (1UL << 2)   /* 1 = 处理器时钟，不走外部参考 */
#define BK7258_SYST_CSR_COUNTFLAG (1UL << 16)

#define BK7258_SYST_RVR_RELOAD_Msk 0x00FFFFFFUL

static inline void NVIC_EnableIRQ(IRQn_Type irq)
{
    if ((int32_t)irq >= 0) {
        BK7258_NVIC_ISER[((uint32_t)irq) >> 5] = 1UL << (((uint32_t)irq) & 0x1FUL);
    }
}

static inline void NVIC_DisableIRQ(IRQn_Type irq)
{
    if ((int32_t)irq >= 0) {
        BK7258_NVIC_ICER[((uint32_t)irq) >> 5] = 1UL << (((uint32_t)irq) & 0x1FUL);
    }
}

/* priority 语义与 CMSIS 一致：数值越小优先级越高，只取高 __NVIC_PRIO_BITS 位 */
static inline void NVIC_SetPriority(IRQn_Type irq, uint32_t priority)
{
    if ((int32_t)irq >= 0) {
        BK7258_NVIC_IPR[((uint32_t)irq)] =
            (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & 0xFFUL);
    } else {
        BK7258_SCB_SHPR[(((uint32_t)irq) & 0xFUL) - 4UL] =
            (uint8_t)((priority << (8U - __NVIC_PRIO_BITS)) & 0xFFUL);
    }
}

/* 成功返回 0；ticks 超出 24 位重载值时返回 1（与 CMSIS 同语义） */
static inline uint32_t SysTick_Config(uint32_t ticks)
{
    if ((ticks - 1UL) > BK7258_SYST_RVR_RELOAD_Msk) {
        return 1UL;
    }
    *BK7258_SYST_RVR = (ticks - 1UL) & BK7258_SYST_RVR_RELOAD_Msk;
    *BK7258_SYST_CVR = 0UL;
    NVIC_SetPriority(SysTick_IRQn, (1UL << __NVIC_PRIO_BITS) - 1UL);
    *BK7258_SYST_CSR = BK7258_SYST_CSR_CLKSOURCE |
                       BK7258_SYST_CSR_TICKINT |
                       BK7258_SYST_CSR_ENABLE;
    return 0UL;
}

static inline void __enable_irq(void)  { __asm__ volatile ("cpsie i" ::: "memory"); }
static inline void __disable_irq(void) { __asm__ volatile ("cpsid i" ::: "memory"); }
static inline void __DSB(void)         { __asm__ volatile ("dsb 0xF" ::: "memory"); }
static inline void __ISB(void)         { __asm__ volatile ("isb 0xF" ::: "memory"); }

/* ==========================================================================
 * 6. UART
 *    布局逐字段来自 ap/middleware/soc/bk7258_ap/soc/uart_struct.h。
 *    寄存器是 32 位宽的，但 fifo_port 的 tx 域只有低 8 位。
 * ========================================================================== */
typedef volatile struct {
    uint32_t dev_id;            /* 0x00 */
    uint32_t dev_version;       /* 0x01 */
    union {                     /* 0x02 */
        struct {
            uint32_t soft_reset      : 1;
            uint32_t clk_gate_bypass : 1;
            uint32_t reserved        : 30;
        };
        uint32_t v;
    } global_ctrl;
    uint32_t dev_status;        /* 0x03 */
    union {                     /* 0x04 */
        struct {
            uint32_t tx_enable  : 1;
            uint32_t rx_enable  : 1;
            uint32_t reserved1  : 1;
            uint32_t data_bits  : 2;   /* 0:5bit 1:6bit 2:7bit 3:8bit */
            uint32_t parity_en  : 1;
            uint32_t parity     : 1;   /* 0:Even 1:Odd */
            uint32_t stop_bits  : 1;   /* 0:1bit 1:2bit */
            uint32_t clk_div    : 16;  /* uart_clk / baud_rate */
            uint32_t reserved   : 8;
        };
        uint32_t v;
    } config;
    union {                     /* 0x05 */
        struct {
            uint32_t tx_fifo_threshold   : 8;
            uint32_t rx_fifo_threshold   : 8;
            uint32_t rx_stop_detect_time : 2;
            uint32_t reserved            : 14;
        };
        uint32_t v;
    } fifo_config;
    union {                     /* 0x06 */
        struct {
            uint32_t tx_fifo_count : 8;
            uint32_t rx_fifo_count : 8;
            uint32_t tx_fifo_full  : 1;   /* bit16 */
            uint32_t tx_fifo_empty : 1;   /* bit17 */
            uint32_t rx_fifo_full  : 1;   /* bit18 */
            uint32_t rx_fifo_empty : 1;   /* bit19 */
            uint32_t fifo_wr_ready : 1;   /* bit20 */
            uint32_t fifo_rd_ready : 1;   /* bit21 */
            uint32_t reserved      : 10;
        };
        uint32_t v;
    } fifo_status;
    union {                     /* 0x07 */
        struct {
            uint32_t tx_fifo_data_in  : 8;   /* 写这里发字节，bit0:7 */
            uint32_t rx_fifo_data_out : 8;   /* 读这里收字节，bit8:15 */
            uint32_t reserved         : 16;
        };
        uint32_t v;
    } fifo_port;
    union {                     /* 0x08 */
        struct {
            uint32_t tx_fifo_need_write : 1;
            uint32_t rx_fifo_need_read  : 1;
            uint32_t rx_fifo_overflow   : 1;
            uint32_t rx_parity_err      : 1;
            uint32_t rx_stop_bits_err   : 1;
            uint32_t tx_finish          : 1;
            uint32_t rx_finish          : 1;
            uint32_t rxd_wakeup         : 1;
            uint32_t reserved           : 24;
        };
        uint32_t v;
    } int_enable;
    union {                     /* 0x09 */
        struct {
            uint32_t tx_fifo_need_write : 1;
            uint32_t rx_fifo_need_read  : 1;   /* <-- 收数据中断 */
            uint32_t rx_fifo_overflow   : 1;
            uint32_t rx_parity_err      : 1;
            uint32_t rx_stop_bits_err   : 1;
            uint32_t tx_finish          : 1;
            uint32_t rx_finish          : 1;
            uint32_t rxd_wakeup         : 1;
            uint32_t reserved           : 24;
        };
        uint32_t v;
    } int_status;
    union {                     /* 0x0A */
        struct {
            uint32_t flow_ctrl_low_cnt  : 8;
            uint32_t flow_ctrl_high_cnt : 8;
            uint32_t flow_ctrl_en       : 1;
            uint32_t rts_polarity_sel   : 1;
            uint32_t cts_polarity_sel   : 1;
            uint32_t reserved           : 13;
        };
        uint32_t v;
    } flow_ctrl_config;
    union {                     /* 0x0B */
        struct {
            uint32_t wake_cnt             : 10;
            uint32_t txd_wait_cnt         : 10;
            uint32_t rxd_wake_en          : 1;
            uint32_t txd_wake_en          : 1;
            uint32_t rxd_neg_edge_wake_en : 1;
            uint32_t reserved             : 9;
        };
        uint32_t v;
    } wake_config;
} bk7258_uart_hw_t;

/* 出处：ap/include/soc/bk7258/reg_base.h:78-80
 * 该头里写的是 (0x44820000 + SOC_ADDR_OFFSET)，而 SOC_ADDR_OFFSET 在
 * CONFIG_SPE=y 时为 0、否则为 0x10000000。本板 sdkconfig 是 CONFIG_SPE=1，
 * 且真实链接脚本里 SOC_FLASH_DATA_BASE 展开为裸 0x02000000（未加偏移），
 * 所以这里直接用基址本身。 */
#define BK7258_UART0_BASE   ((bk7258_uart_hw_t *)0x44820000UL)
#define BK7258_UART1_BASE   ((bk7258_uart_hw_t *)0x45830000UL)
#define BK7258_UART2_BASE   ((bk7258_uart_hw_t *)0x45840000UL)

/* 控制台：sdkconfig 里 CONFIG_UART_PRINT_PORT=1、CONFIG_UART_PRINT_BAUD_RATE=460800。
 * 注意这与上游 nuvoton 模板的"UART0 + 115200"都不同。 */
#define BK7258_CONSOLE_UART      BK7258_UART1_BASE
#define BK7258_CONSOLE_IRQn      UART1_IRQn
#define BK7258_CONSOLE_BAUD      460800UL

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_SOC_H__ */

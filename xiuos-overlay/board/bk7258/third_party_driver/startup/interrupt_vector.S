/****************************************************************************//**
 * @file     interrupt_vector.S
 * @brief    BK7258 (涂鸦 T5-E1) AP 核中断向量表
 *
 * 模板来源：arch/arm/cortex-m23/interrupt_vector.S（XiUOS 上游）
 *   同 boot.S 的理由：上游 cortex-m33 目录里没有向量表文件，必须自备。
 *
 * 【外部中断槽为什么全填 IsrEntry】
 *   XiUOS 的中断派发是两级的：
 *     IsrEntry()（arch/arm/cortex-m33/interrupt.c:57）
 *       -> MRS IPSR，拿到异常号
 *       -> isrManager.done->handleIrq(ipsr)
 *       -> 查 isrManager.irq_table[] —— 由 DECLARE_HW_IRQ(irq_num, handler, arg) 填充
 *          （kernel/include/xs_isr.h:33，落进 .isrtbl.idx / .isrtbl 段）
 *   所以向量表**不需要**为每个设备硬编码处理函数，全填 IsrEntry 即可，
 *   具体是谁的中断由驱动用 DECLARE_HW_IRQ 注册。这样板级和驱动解耦。
 *   （对比：nuvoton 的向量表把 UART0_IRQHandler 硬编码在第 36 槽，同时
 *     connect_uart.c 里的 DECLARE_HW_IRQ 是注释掉的 —— 两条路它只走了前一条。）
 *
 * 【槽位数】
 *   16 个系统异常 + 96 个外部中断。96 是刻意留的余量：
 *   从真实构建产物 app.elf 的 .vectors 解析出 BK7258 的外部中断号 0..63 已确认
 *   （IRQ 0=DMA0_NSEC … 15=UART1 … 55=GPIO … 63=MAILBOX），留到 96 以防
 *   Beken 后续固件把号段往上扩。
 *
 * 【向量表位置】
 *   由 link.lds 的 `KEEP(*(.isr_vector))` 放在 flash 最前，
 *   即 0x02120000（= SOC_FLASH_DATA_BASE + CONFIG_AP_VIRTUAL_PARTITION_OFFSET，
 *   与实测的 bk7258_ap_out.ld 一致）。
 *****************************************************************************/

    .syntax unified
    .arch   armv8-m.main
    .thumb

    .global InterruptVectors
    .section .isr_vector, "a", %progbits
    .type InterruptVectors, %object
    .size InterruptVectors, . - InterruptVectors

InterruptVectors:
    /* ================= Cortex-M33 系统异常（字 0..15）================= */
    .word   _sp                     /* 0  初始栈顶，由 link.lds 定义      */
    .word   Reset_Handler           /* 1  startup/boot.S                  */
    .word   NMI_Handler             /* 2  arch/.../interrupt.c:86         */
    .word   HardFault_Handler       /* 3  BSP: common/bk7258_faults.c     */
    .word   MemManage_Handler       /* 4  BSP: common/bk7258_faults.c     */
    .word   BusFault_Handler        /* 5  arch/.../interrupt.c:78         */
    .word   UsageFault_Handler      /* 6  arch/.../interrupt.c:70         */
    .word   SecureFault_Handler     /* 7  BSP: common/bk7258_faults.c     */
    .word   0                       /* 8  保留                            */
    .word   0                       /* 9  保留                            */
    .word   0                       /* 10 保留                            */
    .word   SVC_Handler             /* 11 arch/.../syscall_gcc.S:38       */
    .word   DebugMon_Handler        /* 12 BSP: common/bk7258_faults.c     */
    .word   0                       /* 13 保留                            */
    .word   PendSV_Handler_NS       /* 14 arch/.../pendsv.S:44
                                     *    注意名字是 _NS 后缀（非安全态），
                                     *    写 PendSV_Handler 会链接失败。     */
    .word   SysTick_Handler         /* 15 BSP: board.c                    */

    /* ================= 外部中断（字 16+n = IRQ n）=================
     * 全部走 IsrEntry，由 isrManager 二级派发。
     * 右列注释是实测的 BK7258 中断号对照（来自 app.elf 的 .vectors 解析）。 */
    .word   IsrEntry    /* IRQ  0  DMA0_NSEC            */
    .word   IsrEntry    /* IRQ  1  ENCP_SEC             */
    .word   IsrEntry    /* IRQ  2  ENCP_NSEC            */
    .word   IsrEntry    /* IRQ  3  TIMER0               */
    .word   IsrEntry    /* IRQ  4  UART0                */
    .word   IsrEntry    /* IRQ  5  PWM0                 */
    .word   IsrEntry    /* IRQ  6  I2C0                 */
    .word   IsrEntry    /* IRQ  7  SPI0                 */
    .word   IsrEntry    /* IRQ  8  SARADC               */
    .word   IsrEntry    /* IRQ  9  IRDA                 */
    .word   IsrEntry    /* IRQ 10  SDIO                 */
    .word   IsrEntry    /* IRQ 11  GDMA                 */
    .word   IsrEntry    /* IRQ 12  LA                   */
    .word   IsrEntry    /* IRQ 13  TIMER1               */
    .word   IsrEntry    /* IRQ 14  I2C1                 */
    .word   IsrEntry    /* IRQ 15  UART1  <-- 控制台    */
    .word   IsrEntry    /* IRQ 16  UART2                */
    .word   IsrEntry    /* IRQ 17  SPI1                 */
    .word   IsrEntry    /* IRQ 18  CAN                  */
    .word   IsrEntry    /* IRQ 19  USB                  */
    .word   IsrEntry    /* IRQ 20  QSPI0                */
    .word   IsrEntry    /* IRQ 21  CKMN                 */
    .word   IsrEntry    /* IRQ 22  SBC                  */
    .word   IsrEntry    /* IRQ 23  AUDIO                */
    .word   IsrEntry    /* IRQ 24  I2S0                 */
    .word   IsrEntry    /* IRQ 25  JPEG_ENC             */
    .word   IsrEntry    /* IRQ 26  JPEG_DEC             */
    .word   IsrEntry    /* IRQ 27  DISPLAY              */
    .word   IsrEntry    /* IRQ 28  DMA2D                */
    .word   IsrEntry    /* IRQ 29  PHY_MBP              */
    .word   IsrEntry    /* IRQ 30  PHY_RIU              */
    .word   IsrEntry    /* IRQ 31  MAC_INT_TX_RX_TIMER  */
    .word   IsrEntry    /* IRQ 32  MAC_INT_TX_RX_MISC   */
    .word   IsrEntry    /* IRQ 33  MAC_INT_RX_TRIGGER   */
    .word   IsrEntry    /* IRQ 34  MAC_INT_TX_TRIGGER   */
    .word   IsrEntry    /* IRQ 35  MAC_INT_PORT_TRIGGER */
    .word   IsrEntry    /* IRQ 36  MAC_INT_GEN          */
    .word   IsrEntry    /* IRQ 37  GPIO_NS              */
    .word   IsrEntry    /* IRQ 38  INT_MAC_WAKEUP       */
    .word   IsrEntry    /* IRQ 39  DM                   */
    .word   IsrEntry    /* IRQ 40  BLE                  */
    .word   IsrEntry    /* IRQ 41  BT                   */
    .word   IsrEntry    /* IRQ 42  QSPI1                */
    .word   IsrEntry    /* IRQ 43  PWM1                 */
    .word   IsrEntry    /* IRQ 44  I2S1                 */
    .word   IsrEntry    /* IRQ 45  I2S2                 */
    .word   IsrEntry    /* IRQ 46  H264                 */
    .word   IsrEntry    /* IRQ 47  SDMADC               */
    .word   IsrEntry    /* IRQ 48  ETHERNET             */
    .word   IsrEntry    /* IRQ 49  SCAL0                */
    .word   IsrEntry    /* IRQ 50  OTP                  */
    .word   IsrEntry    /* IRQ 51  DPLL_UNLOCK          */
    .word   IsrEntry    /* IRQ 52  TOUCH                */
    .word   IsrEntry    /* IRQ 53  USB_PLUG             */
    .word   IsrEntry    /* IRQ 54  RTC                  */
    .word   IsrEntry    /* IRQ 55  GPIO                 */
    .word   IsrEntry    /* IRQ 56  DMA1_SEC             */
    .word   IsrEntry    /* IRQ 57  DMA1_NSEC            */
    .word   IsrEntry    /* IRQ 58  YUV_BUF              */
    .word   IsrEntry    /* IRQ 59  ROTT                 */
    .word   IsrEntry    /* IRQ 60  BK7816               */
    .word   IsrEntry    /* IRQ 61  LIN                  */
    .word   IsrEntry    /* IRQ 62  SCAL1                */
    .word   IsrEntry    /* IRQ 63  MAILBOX  <-- AP/CP 核间邮箱 */

    /* IRQ 64..95 —— 本固件未确认，留作号段扩张的余量 */
    .rept   32
    .word   IsrEntry
    .endr

    .end

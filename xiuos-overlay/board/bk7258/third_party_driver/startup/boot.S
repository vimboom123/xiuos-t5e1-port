/****************************************************************************//**
 * @file     boot.S
 * @brief    BK7258 (涂鸦 T5-E1) AP 核复位与初始化
 *
 * 模板来源：arch/arm/cortex-m23/boot.S（XiUOS 上游）
 *   为什么抄 m23 而不是 m33 —— 因为上游的 arch/arm/cortex-m33/ 目录里
 *   **根本没有 boot.S，也没有 interrupt_vector.S**（cortex-m23/ 两个都有）。
 *   现存两块 m33 板子全靠瑞萨 FSP 的 startup.c + vector_data.c 顶上，
 *   BK7258 没有 FSP，只能自备。
 *
 * 与模板的差异（每一条都是有意的）：
 *   1. 去掉 Nuvoton 专有的寄存器解锁 / 电源门控 / GPIO 时钟使能那几段 ——
 *      Beken 的 bootloader 已经从 ROM 跑过来，时钟和引脚都已就绪，
 *      这些初始化再由我们做一遍反而会和 CP 核、Wi-Fi 子系统抢控制权。
 *   2. 去掉 `bl SystemInit` —— 改为在 C 侧由 InitBoardHardware() 统一处理。
 *   3. **修正了模板里清 .bss 的越界 bug**（见下）。
 *   4. 顶层不是 entry() 而是保持调 entry() —— 与 arch 层 kernel/thread/init.c:269 对齐。
 *****************************************************************************/

    .syntax unified
    .arch   armv8-m.main
    .fpu    softvfp
    .thumb

/* 链接脚本里定义的段边界。这几个 .word 让汇编器把它们的地址放进字面量池，
 * 下面 ldr rX, =符号 才能取到。 */
.word   _sidata         /* .data 初值在 flash 里的起点 */
.word   _sdata          /* .data 在 sram 里的起点   */
.word   _edata          /* .data 在 sram 里的终点   */
.word   _sbss           /* .bss 起点                */
.word   _ebss           /* .bss 终点                */
.word   _sp             /* 系统栈顶                 */

    .section .text.Reset_Handler
    .weak Reset_Handler
    .type Reset_Handler, %function

Reset_Handler:

    /* ---- 1. 装栈指针 ----
     * 上游 m23 模板没做这一步（Nuvoton 的 ROM 会替它装好）。
     * 我们显式做，因为 BK7258 原厂向量表的初始 _sp 是 0x28063800（AP_RAM 顶端），
     * 而 XiUOS 的链接脚本把 .stack 段放在 SRAM 低端、_sp 指向其顶端。
     * 两者都在 AP_RAM 内、都合法，但必须由我们自己装成后者。 */
    ldr     r0, =_sp
    msr     msp, r0

    /* ---- 1b. 存活标记 ----
     * 早于 .data/.bss，只用栈和 flash 常量。只有一个串口（UART0）可看，
     * 这一行是判断「AP 到底有没有跑到我们代码」的唯一依据。 */
    bl      Bk7258EarlyBoot

    /* ---- 2. 把 .data 从 flash 搬到 sram ---- */
    movs    r1, #0
    b       LoopCopyDataInit

CopyDataInit:
    ldr     r3, =_sidata
    ldr     r3, [r3, r1]
    str     r3, [r0, r1]
    adds    r1, r1, #4

LoopCopyDataInit:
    ldr     r0, =_sdata
    ldr     r3, =_edata
    adds    r2, r0, r1
    cmp     r2, r3
    bcc     CopyDataInit

    /* ---- 3. 清零 .bss ----
     *
     * 注意：上游 m23 模板这里写的是
     *       movs r3, #0
     *       str  r3, [r2, #4]     <-- 写到 r2+4
     *       adds r2, r2, #4
     *   这样第一次写的是 _sbss+4，循环结束后 **_sbss 那一个字永远没被清零**。
     *   这是模板里的一个真实越界/漏清 bug（写到了 _sbss+4 起、漏掉 _sbss 本身）。
     *   这里改成标准的后自增写法，语义正确。 */
    ldr     r2, =_sbss
    movs    r3, #0
    b       LoopFillZerobss

FillZerobss:
    str     r3, [r2], #4        /* 先写 [r2]，再把 r2 加 4 */

LoopFillZerobss:
    ldr     r1, =_ebss
    cmp     r2, r1
    bcc     FillZerobss

    /* ---- 4. 进内核入口 ----
     * kernel/thread/init.c:269 的 int entry(void) 才是 XiUOS 的系统入口，
     * 它内部会依次做 SysInitIsrManager() -> InitBoardHardware() -> XiUOSStartup()。
     * 所以这里**不要**再调 SystemInit 或任何板级初始化。 */
    bl      entry

    /* entry() 理论上不返回；真返回了就停住。 */
LoopForever:
    b       LoopForever

.size Reset_Handler, .-Reset_Handler

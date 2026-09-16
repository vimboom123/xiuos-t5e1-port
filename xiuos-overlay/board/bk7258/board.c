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
* @file board.c
* @brief BK7258 (涂鸦 T5-E1) 板级初始化
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#include <board.h>
#include <xizi.h>
#include <bk7258_soc.h>
#include <connect_uart.h>

#include <FreeRTOS.h>   /* 兼容层；FreeRTOSCompatInit() 的声明在这里 */

/* ==========================================================================
 * FreeRTOS API 兼容层
 *
 * 见 docs/09-freertos-compat-layer.md。它不是 FreeRTOS 内核 —— 只提供 API 形状，
 * 实现全部转调 XiZi 原语，目的是让 Tuya 的 TKL 系统层
 * （platform/T5AI/tuyaos/tuyaos_adapter/src/system/ 那 6 个文件）一行不改就能跑。
 *
 * 为什么在这个阶段（还没接 Tuya）就要把它链进来：
 *   1. XiUOS 的 LFLAGS 带 -Wl,--gc-sections，没有引用的段会被整个回收，
 *      于是「编译过了」不代表「链接得过」。这里显式引用一下，
 *      未定义符号才会在链接期暴露，而不是等到接 Tuya 的那一天。
 *   2. 它只分配一块控制块表，不创建任务、不占栈，几乎没有运行时开销。
 * ========================================================================== */

/*
 * 调用链（已核对上游源码）：
 *   Reset_Handler (startup/boot.S)
 *     -> entry()                 kernel/thread/init.c:269
 *          -> DISABLE_INTERRUPT()
 *          -> SysInitIsrManager()      "系统中断表必须先于硬件中断初始化"
 *          -> InitBoardHardware()      <-- 就是下面这个
 *          -> XiUOSStartup()           建主任务、起调度器
 *
 * 所以 InitBoardHardware() 是**内核起来之前**跑的最后一段板级代码，
 * 它必须自己把时钟、tick、内存池、控制台全部准备好。
 */

/*
 * tick 中断服务。
 *
 * 【签名注意】这不是 ARM 标准的 void SysTick_Handler(void)，
 * 而是 XiUOS 的约定 (int irqn, void *arg)。向量表是直接跳过来的，
 * 两个参数是寄存器里的残留值，函数忽略即可。
 * arch/arm/cortex-m33/arch_interrupt.h 里定义了 NVIC_SYSTICK_PRI/MASK，
 * board/nuvoton-m2354/board.c 也是同样的写法 —— 这是 XiUOS 的既有约定。
 */
void SysTick_Handler(int irqn, void *arg)
{
    TickAndTaskTimesliceUpdate();
}

/**
 * 初始化 BK7258 板级硬件。
 */
void InitBoardHardware(void)
{
    /* 进度标记：只有 UART0 一个口可看，靠它定位卡在哪一步 */
    Bk7258EarlyPuts("[XZ] 1 InitBoardHardware\n");

    /*
     * 1. 时钟
     *    Beken 的 bootloader 在跳过来之前已经把 PLL 配好了（120 MHz），
     *    我们**不做任何时钟配置** —— 那会和 CP 核、Wi-Fi 子系统抢控制权。
     *    只把 CMSIS 那个全局量对齐一下，供 SysTick 算重载值。
     */
    SystemCoreClockUpdate();

    /*
     * 2. 引脚复用 / 电源域
     *    同样归 bootloader。BK7258 的 IO 复用寄存器在 AON 域（0x44000400 起），
     *    改错会让 Wi-Fi 射频或 PSRAM 失效，第一版一律不碰。
     */

    /*
     * 3. 系统 tick
     *    TICK_PER_SECOND 来自 .defconfig（本板 = 1000）。
     *    120000000 / 1000 = 120000，远小于 SysTick 的 24 位重载上限 16777215。
     */
    SysTick_Config(SystemCoreClock / TICK_PER_SECOND);
    Bk7258EarlyPuts("[XZ] 2 systick\n");

    /*
     * 4. 内存池
     *    HEAP_BEGIN = &__bss_end（链接脚本给），HEAP_END = AP_RAM 顶端 0x28063800。
     *    中间 244 KiB 是 CP 核的 RAM，绝不能越界 —— 见 board.h 的说明。
     */
    InitBoardMemory(HEAP_BEGIN, HEAP_END);
    Bk7258EarlyPuts("[XZ] 3 heap\n");

    /*
     * 5. 控制台串口（UART0 = 板上 RX0/TX0，115200 8N1）
     *    时钟/引脚/分频已在 Bk7258Uart0SysInit() 里显式配好。
     */
#ifdef BSP_USING_UART
    Bk7258HwUartInit();
#endif
    Bk7258EarlyPuts("[XZ] 4 uart driver\n");

    /*
     * 7. 装控制台
     *    三个名字由 connect_uart.h 从 Kconfig 映射过来，
     *    与 Bk7258HwUartInit() 里注册的 bus/driver/device 名字必须一致。
     */
    InstallConsole(KERNEL_CONSOLE_BUS_NAME, KERNEL_CONSOLE_DRV_NAME, KERNEL_CONSOLE_DEVICE_NAME);
    Bk7258EarlyPuts("[XZ] 5 console installed\n");

    /*
     * 8. FreeRTOS API 兼容层
     *    必须在任何 Tuya 代码调用 xTaskCreate / xSemaphoreTake 之前跑一次，
     *    它要初始化句柄控制块表与任务通知表。
     *
     *    刻意放在 InstallConsole() **之后**：它自己会打一行启动日志，
     *    而控制台装好之前 KPrintf 是没有出口的。上板时看到这行
     *    「frc: FreeRTOS API compat layer ready」就说明这一层起来了。
     */
    FreeRTOSCompatInit();
    Bk7258EarlyPuts("[XZ] 6 board init done\n");
}

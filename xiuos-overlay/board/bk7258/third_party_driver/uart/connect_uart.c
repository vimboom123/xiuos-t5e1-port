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
* @file connect_uart.c
* @brief BK7258 串口驱动，注册到 XiUOS 的 serial bus 框架
* @version 1.0
* @author XiUOS T5-E1 port
* @date 2026-09-16
*/

#include <board.h>
#include <connect_uart.h>

/*
 * 模板：board/nuvoton-m2354/third_party_driver/uart/connect_uart.c
 * 保留了它全部 9 个 static 函数的签名与职责，只把内部实现从 Nuvoton 的
 * UART_Open / UART_Read / UART_Write 换成 BK7258 的寄存器操作。
 *
 * 【为什么 SerialInit 不重配波特率 —— 这是有意的，不是没写完】
 *   Beken 的 bootloader 在跳到 XiZi 之前，已经把 UART1 配成 460800 8N1
 *   （sdkconfig: CONFIG_UART_PRINT_PORT=1、CONFIG_UART_PRINT_BAUD_RATE=460800；
 *    我们能从 COM 口看到 TuyaOS 日志就是这件事的直接证据）。
 *
 *   而 UART 的输入时钟（clk_div = uart_clk / baud_rate 里的 uart_clk）
 *   是 26 MHz XTAL 还是 120 MHz 分频，**尚未实测确认**。
 *   在第一版就把一个正在工作的控制台重配掉，等于拿唯一的调试通道去赌一个猜的值。
 *
 *   所以 SerialInit 只做一件事：打开 RX 中断。波特率沿用 bootloader 的配置。
 *   等阶段③跑通、需要改波特率时，再补 clk_div 写入并实测验证。
 */

#define BK7258_UART_FIFO_PORT_OFFSET   7

struct Bk7258UartHwCfg
{
    bk7258_uart_hw_t *uart_handle;
    IRQn_Type irq_type;
};

#ifdef BSP_USING_UART0
static struct SerialBus serial_bus_0;
static struct SerialDriver serial_driver_0;
static struct SerialHardwareDevice serial_device_0;
#endif

static void SerialCfgParamCheck(struct SerialCfgParam *serial_cfg_default, struct SerialCfgParam *serial_cfg_new)
{
    struct SerialDataCfg *data_cfg_default = &serial_cfg_default->data_cfg;
    struct SerialDataCfg *data_cfg_new = &serial_cfg_new->data_cfg;

    if ((data_cfg_default->serial_baud_rate != data_cfg_new->serial_baud_rate) && (data_cfg_new->serial_baud_rate)) {
        data_cfg_default->serial_baud_rate = data_cfg_new->serial_baud_rate;
    }
    if ((data_cfg_default->serial_bit_order != data_cfg_new->serial_bit_order) && (data_cfg_new->serial_bit_order)) {
        data_cfg_default->serial_bit_order = data_cfg_new->serial_bit_order;
    }
    if ((data_cfg_default->serial_buffer_size != data_cfg_new->serial_buffer_size) && (data_cfg_new->serial_buffer_size)) {
        data_cfg_default->serial_buffer_size = data_cfg_new->serial_buffer_size;
    }
    if ((data_cfg_default->serial_data_bits != data_cfg_new->serial_data_bits) && (data_cfg_new->serial_data_bits)) {
        data_cfg_default->serial_data_bits = data_cfg_new->serial_data_bits;
    }
    if ((data_cfg_default->serial_invert_mode != data_cfg_new->serial_invert_mode) && (data_cfg_new->serial_invert_mode)) {
        data_cfg_default->serial_invert_mode = data_cfg_new->serial_invert_mode;
    }
    if ((data_cfg_default->serial_parity_mode != data_cfg_new->serial_parity_mode) && (data_cfg_new->serial_parity_mode)) {
        data_cfg_default->serial_parity_mode = data_cfg_new->serial_parity_mode;
    }
    if ((data_cfg_default->serial_stop_bits != data_cfg_new->serial_stop_bits) && (data_cfg_new->serial_stop_bits)) {
        data_cfg_default->serial_stop_bits = data_cfg_new->serial_stop_bits;
    }
    if ((data_cfg_default->serial_timeout != data_cfg_new->serial_timeout) && (data_cfg_new->serial_timeout)) {
        data_cfg_default->serial_timeout = data_cfg_new->serial_timeout;
    }
}

static void UartHandler(struct SerialBus *serial_bus, struct SerialDriver *serial_drv)
{
    struct SerialHardwareDevice *serial_dev = (struct SerialHardwareDevice *)serial_bus->bus.owner_haldev;
    struct SerialCfgParam *serial_cfg = (struct SerialCfgParam *)serial_drv->private_data;
    struct Bk7258UartHwCfg *serial_hw_cfg = (struct Bk7258UartHwCfg *)serial_cfg->hw_cfg.private_data;

    uint32 status = serial_hw_cfg->uart_handle->int_status.v;

    /* 收满阈值就通知上层去取；溢出/校验错也一并报，避免卡死 */
    if (status & ((1U << 1) | (1U << 2) | (1U << 3) | (1U << 4))) {
        SerialSetIsr(serial_dev, SERIAL_EVENT_RX_IND);
    }

    /* 写 1 清中断 */
    serial_hw_cfg->uart_handle->int_status.v = status;
}

#ifdef BSP_USING_UART0
/*
 * 走 XiUOS 的二级派发：向量表里 IRQ 15 槽是 IsrEntry，它按 IPSR 查 isrManager，
 * 再转到这个函数。签名必须匹配 IsrHandlerType: void (*)(int vector, void *param)。
 */
void Bk7258Uart0Isr(int vector, void *param)
{
    x_base lock = 0;

    lock = DISABLE_INTERRUPT();
    UartHandler(&serial_bus_0, &serial_driver_0);
    ENABLE_INTERRUPT(lock);
}

DECLARE_HW_IRQ(BK7258_CONSOLE_IRQn, Bk7258Uart0Isr, NONE);
#endif

static uint32 SerialInit(struct SerialDriver *serial_drv, struct BusConfigureInfo *configure_info)
{
    NULL_PARAM_CHECK(serial_drv);

    struct SerialCfgParam *serial_cfg = (struct SerialCfgParam *)serial_drv->private_data;
    struct Bk7258UartHwCfg *serial_hw_cfg = (struct Bk7258UartHwCfg *)serial_cfg->hw_cfg.private_data;

    struct SerialHardwareDevice *serial_dev = (struct SerialHardwareDevice *)serial_drv->driver.owner_bus->owner_haldev;
    struct SerialDevParam *dev_param = (struct SerialDevParam *)serial_dev->haldev.private_data;

    if (configure_info->private_data) {
        struct SerialCfgParam *serial_cfg_new = (struct SerialCfgParam *)configure_info->private_data;
        SerialCfgParamCheck(serial_cfg, serial_cfg_new);

        if (serial_cfg_new->data_cfg.dev_recv_callback) {
            BusDevRecvCallback(&(serial_dev->haldev), serial_cfg_new->data_cfg.dev_recv_callback);
        }
    }

    dev_param->serial_timeout = serial_cfg->data_cfg.serial_timeout;

    /*
     * ======================================================================
     * 【必须自己把 UART 打开 —— 这是 2026-09-16 踩的第二个大坑】
     *
     * 最初这一版**什么都不配**，理由是"bootloader 已经配好了，别动"。
     * 那个理由是错的：烧录时是 **bootrom** 自己临时配好 UART0 收发，
     * 正常启动后**没有任何东西替我们打开 UART0 的发送器**。
     *
     * 于是症状是：烧写全部成功（bootrom 的链路是好的），
     * 但应用一个字都发不出来（tx_enable 是 0，往 FIFO 写等于写进关闭的发送器）。
     * 而且换波特率、换板子都没用 —— 因为根本不是速率或硬件的问题。
     *
     * 修法（2026-09-16 晚第二版）：只开 UART 本体还不够。原厂 TuyaOS 的日志
     * 走 UART1，UART0 的系统时钟门、时钟源、GPIO10/11 复用都没人管，
     * 软复位还会清掉 clk_div。全部交给 Bk7258Uart0SysInit() 按 Armino 源码配齐，
     * 分频显式写成 26M/115200。
     * ======================================================================
     */
    Bk7258Uart0SysInit();

    KPrintf("bk7258 uart: UART0 (P10/P11) 8N1, clk_div %u -> %u bps\n",
            (uint32)serial_hw_cfg->uart_handle->config.clk_div,
            (uint32)(BK7258_UART_CLK_HZ / (serial_hw_cfg->uart_handle->config.clk_div + 1U)));

    /* 清空并复位 FIFO 状态 */
    serial_hw_cfg->uart_handle->int_status.v = 0xFFFFFFFFU;

    /* 打开接收中断（rx_fifo_need_read, bit1） */
    serial_hw_cfg->uart_handle->int_enable.rx_fifo_need_read = 1;

    /* 使能 NVIC。arch 层的 ArchEnableHwIrq 也走同一条路。 */
    NVIC_EnableIRQ(serial_hw_cfg->irq_type);

    return EOK;
}

static uint32 SerialConfigure(struct SerialDriver *serial_drv, int serial_operation_cmd)
{
    NULL_PARAM_CHECK(serial_drv);

    struct SerialHardwareDevice *serial_dev = (struct SerialHardwareDevice *)serial_drv->driver.owner_bus->owner_haldev;
    struct SerialCfgParam *serial_cfg = (struct SerialCfgParam *)serial_drv->private_data;
    struct Bk7258UartHwCfg *serial_hw_cfg = (struct Bk7258UartHwCfg *)serial_cfg->hw_cfg.private_data;
    struct SerialDevParam *serial_dev_param = (struct SerialDevParam *)serial_dev->haldev.private_data;

    if (OPER_CLR_INT == serial_operation_cmd) {
        if (SIGN_OPER_INT_RX & serial_dev_param->serial_work_mode) {
            serial_hw_cfg->uart_handle->int_enable.rx_fifo_need_read = 0;
        }
    } else if (OPER_SET_INT == serial_operation_cmd) {
        serial_hw_cfg->uart_handle->int_enable.rx_fifo_need_read = 1;
    }

    return EOK;
}

static uint32 SerialDrvConfigure(void *drv, struct BusConfigureInfo *configure_info)
{
    NULL_PARAM_CHECK(drv);
    NULL_PARAM_CHECK(configure_info);

    x_err_t ret = EOK;
    int serial_operation_cmd;
    struct SerialDriver *serial_drv = (struct SerialDriver *)drv;

    switch (configure_info->configure_cmd)
    {
        case OPE_INT:
            ret = SerialInit(serial_drv, configure_info);
            break;
        case OPE_CFG:
            serial_operation_cmd = *(int *)configure_info->private_data;
            ret = SerialConfigure(serial_drv, serial_operation_cmd);
            break;
        default:
            break;
    }

    return ret;
}

/*
 * 【控制台输出】
 *
 * 本板（T5-E1 模组）排针只引出 UART0（P10 RX / P11 TX，丝印 RX0/TX0），
 * 它同时是 bootrom 烧录口。原厂 TuyaOS 的日志在 UART1（P0 TX / P1 RX），板上未引出。
 * 所以控制台只写 UART0；UART1 仅在其时钟已被 CP 打开时顺带镜像一份
 * （uart1_cken = sys reg 0x0C bit10），UART2 不碰 —— 往时钟关着的外设写寄存器
 * 可能直接卡死总线。
 *
 * 数据寄存器整字写入：fifo_port 的位域读-改-写会先读它，而读会取走一个 RX 字节。
 */
static int SerialPutChar(struct SerialHardwareDevice *serial_dev, char c)
{
    struct SerialCfgParam *serial_cfg = (struct SerialCfgParam *)serial_dev->private_data;
    struct Bk7258UartHwCfg *serial_hw_cfg = (struct Bk7258UartHwCfg *)serial_cfg->hw_cfg.private_data;
    uint32 spins = 200000;

    /* TX FIFO 满则等待（有上限，避免一个不出字的口把内核拖死） */
    while (serial_hw_cfg->uart_handle->fifo_status.tx_fifo_full && --spins) {
        ;
    }

    serial_hw_cfg->uart_handle->fifo_port.v = (uint32)(uint8)c;

    if ((*BK7258_SYS_DEV_CLK_EN & (1UL << 10)) &&
        serial_hw_cfg->uart_handle != BK7258_UART1_BASE &&
        !BK7258_UART1_BASE->fifo_status.tx_fifo_full) {
        BK7258_UART1_BASE->fifo_port.v = (uint32)(uint8)c;
    }

    return EOK;
}

static int SerialGetChar(struct SerialHardwareDevice *serial_dev)
{
    struct SerialCfgParam *serial_cfg = (struct SerialCfgParam *)serial_dev->private_data;
    struct Bk7258UartHwCfg *serial_hw_cfg = (struct Bk7258UartHwCfg *)serial_cfg->hw_cfg.private_data;

    /* RX FIFO 空则返回失败，与上游 nuvoton 版语义一致 */
    if (serial_hw_cfg->uart_handle->fifo_status.rx_fifo_empty) {
        return -1;
    }

    return (int)(serial_hw_cfg->uart_handle->fifo_port.rx_fifo_data_out & 0xFFU);
}

static const struct SerialDataCfg data_cfg_init =
{
    /* 115200 —— bootrom 下载握手的默认速率，正常启动后会保持。
     * 见 bk7258_soc.h 里「这块板子引出的唯一串口是 UART0」的说明。 */
    .serial_baud_rate = BAUD_RATE_115200,
    .serial_data_bits = DATA_BITS_8,
    .serial_stop_bits = STOP_BITS_1,
    .serial_parity_mode = PARITY_NONE,
    .serial_bit_order = BIT_ORDER_LSB,
    .serial_invert_mode = NRZ_NORMAL,
    .serial_buffer_size = SERIAL_RB_BUFSZ,
    .serial_timeout = WAITING_FOREVER,
};

/* 管理串口设备操作 */
static const struct SerialDrvDone drv_done =
{
    .init = SerialInit,
    .configure = SerialConfigure,
};

/* 管理串口设备 hal 操作 */
static struct SerialHwDevDone hwdev_done =
{
    .put_char = SerialPutChar,
    .get_char = SerialGetChar,
};

static int BoardSerialBusInit(struct SerialBus *serial_bus, struct SerialDriver *serial_driver, const char *bus_name, const char *drv_name)
{
    x_err_t ret = EOK;

    ret = SerialBusInit(serial_bus, bus_name);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit SerialBusInit error %d\n", ret);
        return ERROR;
    }

    ret = SerialDriverInit(serial_driver, drv_name);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit SerialDriverInit error %d\n", ret);
        return ERROR;
    }

    ret = SerialDriverAttachToBus(drv_name, bus_name);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit SerialDriverAttachToBus error %d\n", ret);
        return ERROR;
    }

    return ret;
}

static int BoardSerialDevBend(struct SerialHardwareDevice *serial_device, void *serial_param, const char *bus_name, const char *dev_name)
{
    x_err_t ret = EOK;

    ret = SerialDeviceRegister(serial_device, serial_param, dev_name);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit SerialDeviceRegister device %s error %d\n", dev_name, ret);
        return ERROR;
    }

    ret = SerialDeviceAttachToBus(dev_name, bus_name);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit SerialDeviceAttachToBus device %s error %d\n", dev_name, ret);
        return ERROR;
    }

    return ret;
}

int Bk7258HwUartInit(void)
{
    x_err_t ret = EOK;

#ifdef BSP_USING_UART0
    memset(&serial_bus_0, 0, sizeof(struct SerialBus));
    memset(&serial_driver_0, 0, sizeof(struct SerialDriver));
    memset(&serial_device_0, 0, sizeof(struct SerialHardwareDevice));

    static struct SerialCfgParam serial_cfg_0;
    memset(&serial_cfg_0, 0, sizeof(struct SerialCfgParam));

    static struct Bk7258UartHwCfg serial_hw_cfg_0;
    memset(&serial_hw_cfg_0, 0, sizeof(struct Bk7258UartHwCfg));

    static struct SerialDevParam serial_dev_param_0;
    memset(&serial_dev_param_0, 0, sizeof(struct SerialDevParam));

    serial_driver_0.drv_done = &drv_done;
    serial_driver_0.configure = SerialDrvConfigure;
    serial_device_0.hwdev_done = &hwdev_done;

    serial_cfg_0.data_cfg = data_cfg_init;

    serial_cfg_0.hw_cfg.private_data = (void *)&serial_hw_cfg_0;
    serial_hw_cfg_0.uart_handle = BK7258_CONSOLE_UART;
    serial_hw_cfg_0.irq_type = BK7258_CONSOLE_IRQn;
    serial_driver_0.private_data = (void *)&serial_cfg_0;

    serial_dev_param_0.serial_work_mode = SIGN_OPER_INT_RX;
    serial_device_0.haldev.private_data = (void *)&serial_dev_param_0;

    ret = BoardSerialBusInit(&serial_bus_0, &serial_driver_0, SERIAL_BUS_NAME_0, SERIAL_DRV_NAME_0);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit bus init failed, ret %u\n", ret);
        return ERROR;
    }

    ret = BoardSerialDevBend(&serial_device_0, (void *)&serial_cfg_0, SERIAL_BUS_NAME_0, SERIAL_0_DEVICE_NAME_0);
    if (EOK != ret) {
        KPrintf("Bk7258HwUartInit dev bind failed, ret %u\n", ret);
        return ERROR;
    }
#endif

    return ret;
}

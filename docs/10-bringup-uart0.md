# 10 — 上板：从串口 0 字节到可交互 shell

2026-09-16 晚在联泓 T5-E1 定制板上完成验收阶梯 ②「串口出 banner」与 ③「shell 可用」。
本文记录每一步的现象、原因和修法，供换板或复现时对照。

## 1. 板子与串口

- 排针只有一排：`GND | CEN | RX0 | TX0 | VBAT`。RX0/TX0 是 UART0，即模组 36/37 脚 P10/P11，
  规格书标注为「模组烧录授权用户串口」（[T5-E1 模组规格书](https://developer.tuya.com/cn/docs/iot/T5-E1-Module-Datasheet?id=Kdar6hf0kzmfi)，2026-09-16 查阅）。
- 板上原厂固件是涂鸦闭源 TuyaOS AI 固件，日志走 UART1（P0 = 15 脚 TX，P1 = 12 脚 RX），本板没有引出。
  所以原厂固件正常运行时，接在 RX0/TX0 上的 USB-TTL 收不到任何字节，这不是接线故障。
- 结论：XiZi 的控制台只能放在 UART0，和 bootrom 烧录共用一个口，115200 8N1。
- CH340 的 DTR/RTS 没有接到 CEN，每次进下载模式都要手动把 CEN 短接到 GND 再松开。

## 2. 镜像格式：32+2 CRC

BK7258 flash 的物理布局是每 32 字节数据跟 2 字节 CRC16（大端，poly 0x8005，init 0xFFFF，不反射）。
CPU 看到的地址是去掉 CRC 之后的逻辑地址：

```
CPU 地址 = 0x02000000 + 物理地址 / 34 * 32
物理 0x011000 → 0x02010000   CP
物理 0x132000 → 0x02120000   AP（link.lds 的 flash ORIGIN）
```

链接出来的裸 bin 必须先编码再按物理地址烧写，否则 CPU 从第 33 个字节起读错位。
`tools/bk_crc.py` 负责编解码，`selftest` 子命令用整片备份逐块回归（bootloader/CP/AP 约 15 万个非擦除块一致）。
bk_loader 4.0.1 自带的 `tools addcrc` 会抛 `TypeError: can't concat str to bytes`，不可用。

另一个坑：bk_loader 把擦写长度**向下**截到 4K 整数倍（日志 `length: 0x37700` → `write length: 0x37000`），
尾部会被静默丢弃。编码后的镜像要先补 0xFF 到 4K 整数倍。

```
python tools/bk_crc.py encode build/XiZi-bk7258.bin XiZi_crc.bin   # 再补齐到 4K
bk_loader download -p COM4 -b 460800 -i XiZi_crc4k.bin -s 0x132000 -e 1 -r
```

CH340 上工作波特率用 460800；默认的 2 Mbps 不稳。

## 3. 逐版排查

| 版本 | 改动 | 串口现象 |
|---|---|---|
| DSH 原版 | 裸 bin、控制台 UART0、`soft_reset` 写 1 再写 0 | 0 字节 |
| v2 | 加 CRC 编码 | 0 字节（尾部被截） |
| v3 | `Bk7258Uart0SysInit()`：UART0 时钟、时钟源、GPIO10/11 复用、分频；Reset_Handler 首行加存活标记 | 0 字节 |
| v4 | 启动时把 P11 当 GPIO 慢速拉低 6 次 | 收到 6 个 0x00（break），RX 灯间隔闪 |
| v5 | `soft_reset` 改为 0x2 → 0x3 | 出字：`[XZ]` 标记、兼容层、XiUOS banner，随后在任务切换处进异常 |
| v6 | `-DARCH_ARM_SECURE`；软复位前等 TX FIFO 空 | 启动到 `letter:/$` 和 `Hello, world!`，输入无回显 |
| v7 | UART0 中断路由给 cpu1；RX 阈值 1 字节 | 收发双向可用，`ShowTask` 正常 |

v4 的做法值得保留：只有一个串口、又不知道 CPU 有没有跑起来时，把 TX 脚当 GPIO 翻转，
不依赖 UART 外设本身，就能区分「没跑到我们的代码」和「UART 没配对」。

## 4. 四个根因

寄存器位全部取自 TuyaOpen SDK 的 `platform/T5AI/t5_os/ap/middleware/soc/bk7258_ap`
（`sys_struct.h`、`gpio_ll.h`、`gpio_reg.h`、`uart_struct.h`、`uart_ll.h`），逐位核对。

1. UART 的 `soft_reset`（global_ctrl bit0）低有效。Armino 的 `uart_ll_soft_reset()` 只写 1 并保持；
   写 0 等于按住复位，TX 脚恒为低电平，USB-TTL 的 RX 灯常亮或无输出。
2. 原厂系统不用 UART0 打印，系统级配置要由 XiZi 自己完成：

   | 寄存器 | 地址 | 操作 |
   |---|---|---|
   | clk_div_mode1 | 0x44010020 | bit[10:8] 清零：时钟源 XTAL 26M，不分频 |
   | device_clk_enable | 0x44010030 | bit2 置 1：uart0_cken |
   | gpio_config1 | 0x440100C4 | bit[15:8] 清零：GPIO10/11 选第二功能 0（UART0） |
   | AON GPIO10/11 | 0x44000428 / 0x4400042C | io_mode=2、上拉、bit6 第二功能使能 |
   | UART0 config | 0x44820010 | clk_div = 26000000/115200 − 1 = 224（约 115556 bps） |

3. AP 核运行在安全态（TuyaOpen AP sdkconfig：`CONFIG_TZ=y`、`CONFIG_SPE=1`）。
   XiUOS 的 `arch/arm/cortex-m33/prepare_ahwstack.c` 在未定义 `ARCH_ARM_SECURE` 时给新任务填
   EXC_RETURN = 0xFFFFFFBC（返回非安全态），第一次任务切换即进异常。上游没有对应的 Kconfig 符号，
   在 `board/bk7258/config.mk` 的 `DEFINES` 里定义。改后 `KTaskStackSetup` 反汇编由 `mvn #67` 变为 `mvn #2`。
4. 多核中断路由：外设中断需要在系统控制器里按核使能。UART0 对应 sys reg 0x22（0x44010088）
   `cpu1_uart_int_en` bit4。只使能 NVIC 收不到中断。`DECLARE_HW_IRQ` 本身会加 16 换算成 IPSR 编号，注册没有问题。

## 5. 当前能力与遗留

- 启动顺序：CP（原厂 TuyaOS）拉起 AP → XiZi Reset_Handler → 板级初始化 → FreeRTOS 兼容层（65 个 API）
  → vfs / workqueue / fatfs / libc → letter-shell → 应用 `Hello, world!`。
- 启动阶段个别行有错字或断行：早期裸输出与内核控制台交替写 FIFO。控制台装好后可停用 `Bk7258EarlyPuts`。
- 启动时打印的任务表里 CURRENT RUNNING TASK 名字是乱码，原因是调度器启动前还没有当前任务，属于打印时机问题。
- XiZi 还没有与 CP 建立 mailbox 通信，Wi-Fi/BT 不可用。这是验收阶梯 ④ 的内容。
- 回滚原厂 AP：用整片备份里 0x132000–0x16A000 的切片，以同样参数烧回（已实测可恢复，原厂固件重新播报配网提示）。

## 6. 工具

| 文件 | 用途 |
|---|---|
| `tools/bk_crc.py` | CRC 块编解码、校验、整片备份回归 |
| `tools/flash-and-watch.ps1` | 烧写后在同一个口上监听，保存原始字节并统计 break |
| `tools/shell_probe.py` | 逐字发送命令，验证 shell 收发 |
| `tools/autobaud_watch.py` | 轮询 460800/115200/921600，收到可读文本即锁定 |

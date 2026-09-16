# 06 — `board/bk7258/` 逐文件实现清单

目标：在 XiUOS（`Ubiquitous/XiZi_IIoT_Macro`）里新增一块 BSP，让 XiZi 内核跑在涂鸦 T5-E1（Beken BK7258）的 **AP 核**上。

本文所有数字来自对上游源码与一次**真实 TuyaOpen 构建产物**的实测，不是估算。凡未实测的，明确标为「待确认」。

---

## 0. 结论摘要

| 项 | 结果 |
|---|---|
| 上游必须修改的文件 | **2 个**：`arch/arm/Makefile`、`path_kernel.mk`，合计约 15 行 |
| `arch/arm/cortex-m33/` 需要改的地方 | **0 处**（影子头文件解耦 Renesas FSP，见 §5） |
| 新增自有文件 | board 骨架 9 个 + 影子头 3 个 + UART 驱动 1 个 + 启动文件 1 个 |
| 最大的真实缺口 | 向量表 + `Reset_Handler` —— `cortex-m33` arch 目录里**没有** `boot.S` / `interrupt_vector.S`，必须 BSP 自备 |
| AP 核可用 SRAM | **334 KiB @ `0x28010000`**（SRAM 共 640 KiB，其余归 CP 核等） |
| AP 核可用 FLASH | **3.5 MiB @ `0x02120000`** |
| 可加挂 PSRAM | 最多 16 MiB @ `0x60000000`（AP 侧现成两块共 9.6 MiB） |

---

## 1. 构建系统怎么串起来的（已逐文件核对）

七步链，每一步都对应一个必须存在的文件：

1. `make BOARD=bk7258` → 顶层 `Makefile:8` 用 `find board/ -mindepth 1 -maxdepth 1 -type d` 枚举板子。`board/bk7258/` **必须存在**，否则第 19 行 `$(error "break")`。
2. `Makefile:31` → `include board/$(BOARD)/config.mk`。**这是最早被读的文件**，决定 `CROSS_COMPILE`、`CFLAGS`、`ARCH`。
3. `Makefile:27` → `-include .config`，由 `board/bk7258/.defconfig` 在 `make menuconfig` 时拷入。
4. `Makefile:86 COMPILE_ALL` → 依次 `make -C arch`、`board`、`lib`、`fs`、`kernel`、`resources`、`tool`。
5. `board/Makefile` → `SRC_DIR := $(BOARD)`，递归进 `board/bk7258`。
6. `board/bk7258/Makefile` → `SRC_FILES := board.c`，`SRC_DIR := third_party_driver`，`include $(KERNEL_ROOT)/compiler.mk`。
7. `compiler.mk` 把每个 `.c` 编成 `build/board/bk7258/xxx.o`，并把路径**追加**到 `build/make.obj`；最后 `link.mk` 用 `$(shell cat make.obj)` 全量链接。

**关键推论**：`compiler.mk` 每次只处理「当前目录 + `SRC_DIR` 列出的下一层」，不做自动遍历。目录树必须写死在各自的 Makefile 里。

**arch 选择**：`arch/arm/Makefile` 用 **`CONFIG_BOARD_*` 宏**（不是 `MCU`）决定进哪个子目录：

```make
ifeq ($(CONFIG_BOARD_NUVOTON_M2354),y)
SRC_DIR += cortex-m23
endif
```

所以让 bk7258 用上 m33，就是在这里加一个 `ifeq`。`config.mk` 里的 `MCU = cortex-m33` 只是给人看、以及给 `path_kernel.mk` 的 risc-v 分支用。

### 1.1 与 m33 相关的两个坑（实测）

**坑一：`cortex-m33` 没有启动文件。**

| 目录 | `boot.S` | `interrupt_vector.S` |
|---|---|---|
| `arch/arm/cortex-m23/` | ✅ 有 | ✅ 有 |
| `arch/arm/cortex-m33/` | ❌ **没有** | ❌ **没有** |

现存两块 m33 板子（`rzg2ul-m33`、`rzv2l-m33`）正是靠 Renesas FSP 的 `startup.c` + `vector_data.c` 把向量表和 `Reset_Handler` 顶上的。**BK7258 没有 FSP，必须自己写**（见 §6）。

**坑二：m33 arch 层硬耦合 Renesas FSP。**

| 文件 | 耦合点 |
|---|---|
| `arch/arm/cortex-m33/interrupt.c:43,50` | `R_BSP_IrqEnable(irq_num)` / `R_BSP_IrqDisable(irq_num)` |
| `arch/arm/cortex-m33/arch_interrupt.h:17` | `#include "bsp_api.h"`（为拿 `__NVIC_PRIO_BITS`） |
| `arch/arm/cortex-m33/arm32_switch.c:15` | `#include <hal_data.h>` —— **死 include，全文未使用任何符号** |

好消息：耦合面就这么大。`arch/arm/shared/`（`arm32_switch.c`、`pendsv.S`、`prepare_ahwstack.c`）**零 FSP 耦合**，只 include XiUOS 自己的头。且 m33 板子**不编译 `shared`**（`SRC_DIR += cortex-m33`，没有 `+= shared`），用的是 m33 目录里自带的那份副本。

→ 解法见 §5，代价是 0 处 arch 修改。

---

## 2. BK7258 AP 核内存映射（实测）

来源：`t5_os/build/bk7258/tuya_app/partitions/ram_regions.h` 与真实链接脚本 `bk7258_ap_out.ld`。

### 2.1 SRAM（`CONFIG_SRAM_BASE 0x28000000`，容量 `0x000A0000` = 640 KiB）

| 区域 | 起始 | 大小 | 归属 |
|---|---|---|---|
| `AP_SPINLOCK` | `0x28000000` | `0x00010000` (64 KiB) | Beken 核间自旋锁 |
| **`AP_RAM`** | **`0x28010000`** | **`0x00053800` (334 KiB)** | **← XiZi 的 .data / .bss / stack** |
| `CP_RAM` | `0x28063800` | `0x0003BF00` (244 KiB) | CP 核，**不可碰** |
| `PWR_MNG` | `0x2809F700` | `0x00000100` (256 B) | 电源管理 |
| `SWAP` | `0x2809F800` | `0x00000800` (2 KiB) | OTA swap |

五段相加：`0x10000 + 0x53800 + 0x3BF00 + 0x100 + 0x800 = 0xA0000`，与 `CONFIG_SRAM_CAPACITY` **精确闭合**。

结论：AP 核的 SRAM 就是这连续的 334 KiB，没有第二个可用块。想更多内存只能上 PSRAM。

### 2.2 FLASH 与 PSRAM

- **FLASH**：`0x02000000 + CONFIG_AP_VIRTUAL_PARTITION_OFFSET`（实测 `0x00120000`）→ **`0x02120000`**，长度实测 `0x00380000` = **3.5 MiB**
- **PSRAM**：`0x60000000`，容量 `0x01000000` = **16 MiB**
  - AP 侧可用：`PSRAM_STACK_HEAP` `0x60640000` / `0x00100000`（1 MiB）、`PSRAM_HEAP` `0x60740000` / `0x008A0000`（8.625 MiB）
  - 其余（`SLAB_USER` / `SLAB_AUDIO` / `SLAB_ENCODE` / `SLAB_DISPLAY` / `SECTION`）归 Beken 音频与显示子系统

### 2.3 两套地址别名（写 `link.lds` 时必须选对）

```
__AP_APP_IRAM_OFFSET = 0x20000000
__AP_APP_IRAM_BASE   = 0x28010000 - 0x20000000 = 0x08010000
```

同一块物理 SRAM：**取指总线**看 `0x0801_0000`，**数据总线**看 `0x2801_0000`。

→ `board/bk7258/link.lds` **统一用数据别名 `0x28010000`**，与 Beken 自己的 `RAM` 段一致，避免和它的 `IRAM` 段重叠。

---

## 3. 逐文件清单

### 3.1 `board/bk7258/config.mk`

- **模板**：`board/nuvoton-m2354/config.mk`（11 行）
- **要点**：这是第一个被读的文件，必须先于任何 Kconfig 生效
  - `CROSS_COMPILE`：TuyaOpen 自带 `arm-none-eabi-`（`tos.py` 用的那套）；上游 nuvoton 写死的是 `/opt/gcc-arm-none-eabi-6-2017-q1-update/bin/`，**要换成 `?=` 加环境变量覆盖**
  - `CFLAGS`：`-mcpu=cortex-m33 -mthumb`（ARMv8-M Mainline）。nuvoton 那串 `-fgnu89-inline`、`-Wa,-mimplicit-it=thumb` 是 M23 时代遗留，**m33 不需要 `-mimplicit-it`**
  - `LFLAGS`：`-T $(BSP_ROOT)/link.lds`，`-Wl,--gc-sections,-Map=XiZi-bk7258.map,-cref,-u,Reset_Handler`
  - `DEFINES`：`-DHAVE_CCONFIG_H`
  - `ARCH = arm`、`MCU = cortex-m33`
- **验收**：`make BOARD=bk7258 show_info` 能打印出正确 `BSP_ROOT` 与 `CROSS_COMPILE`

### 3.2 `board/bk7258/Kconfig`

- **模板**：`board/nuvoton-m2354/Kconfig`（44 行）
- **要点**：
  - `config BOARD_BK7258` / `bool` / `select ARCH_ARM` / `default y` —— **宏名必须与 §4.1 的 `arch/arm/Makefile` 判断一致**
  - `source "$KERNEL_DIR/arch/Kconfig"`（提供 `ARCH_ARM`）
  - 自己的 `menu "bk7258 feature"` → `source "$BSP_DIR/third_party_driver/Kconfig"`
  - `config BOARD_APP_NAME` → `"/XiUOS_bk7258_app.bin"`
  - `config SERVICE_TABLE_ADDRESS` → hex，nuvoton 给 `0x20000000`；bk7258 给 `0x28010000`（AP RAM 起点）
  - `source "$KERNEL_DIR/resources/Kconfig"`（在 `menu "Hardware feature"` 里）
  - 结尾 `source "$KERNEL_DIR/Kconfig"`
- **验收**：`make BOARD=bk7258 menuconfig` 能打开，且 `nuvoton` 的选项不再出现

### 3.3 `board/bk7258/.defconfig`

- **模板**：直接拷 `board/nuvoton-m2354/.defconfig`（218 行）再改
- **必改**：第 5 行 `CONFIG_BOARD_NUVOTON_M2354=y` → `CONFIG_BOARD_BK7258=y`
- **保留**：`CONFIG_RESOURCES_SERIAL=y`、`CONFIG_RESOURCES_PIN=y`、`CONFIG_TOOL_SHELL=y`、`CONFIG_KERNEL_*`（信号量/互斥/事件/消息队列）、`CONFIG_TICK_PER_SECOND=1000`、`CONFIG_FS_VFS=y`
- **注意**：`.defconfig` 头部写着 `DO NOT EDIT`，但上游每个板子都是手改的 —— 它只是 `menuconfig` 的初始快照
- **验收**：`make BOARD=bk7258 menuconfig`（只存盘退出）后 `.config` 与 `.defconfig` 语义一致

### 3.4 `board/bk7258/Makefile`

- **模板**：`board/nuvoton-m2354/Makefile`（5 行）
- **内容**：
  ```make
  SRC_FILES := board.c
  SRC_DIR := third_party_driver
  include $(KERNEL_ROOT)/compiler.mk
  ```
- **验收**：`make -C board/bk7258` 能进 `third_party_driver`

### 3.5 `board/bk7258/board.h`

- **模板**：`board/nuvoton-m2354/board.h`（33 行）
- **要点**：定义堆边界。nuvoton 的写法是：
  ```c
  #define SRAM_SIZE  (256)
  #define SRAM_END   (0x20000000 + SRAM_SIZE * 1024)
  extern int __bss_end;
  #define HEAP_BEGIN ((void *)&__bss_end)
  #define HEAP_END   (void *)SRAM_END
  ```
- **bk7258 取值**（用 §2.1 实测值）：
  ```c
  #define BK7258_AP_RAM_BASE   0x28010000UL
  #define BK7258_AP_RAM_SIZE   (0x53800UL)          /* 334 KiB */
  #define BK7258_AP_RAM_END    (BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE)
  extern int __bss_end;
  #define HEAP_BEGIN ((void *)&__bss_end)
  #define HEAP_END   (void *)BK7258_AP_RAM_END
  ```
  **不要**把 `HEAP_END` 写成 `0x280A0000` —— 中间 244 KiB 是 CP 核的。
- 声明 `void InitBoardHardware(void);`
- **验收**：`HEAP_END - HEAP_BEGIN` ≈ 334 KiB 减掉 .data/.bss/stack

### 3.6 `board/bk7258/board.c` — 板级初始化

- **模板**：`board/nuvoton-m2354/board.c`（81 行）
- **上游骨架**（照抄结构，替换实现）：
  ```c
  void SysTick_Handler(int irqn, void *arg) { TickAndTaskTimesliceUpdate(); }

  void InitBoardHardware(void)
  {
      /* 1. 时钟 / 模块时钟 */
      /* 2. 解锁受保护寄存器 */
      /* 3. 引脚复用 */
      /* 4. SysTick_Config(SystemCoreClock / TICK_PER_SECOND) */
      /* 5. SystemCoreClockUpdate() */
      /* 6. 锁回受保护寄存器 */
      InitBoardMemory(HEAP_BEGIN, HEAP_END);          /* 7. 内存池 */
  #ifdef BSP_USING_UART
      Bk7258HwUartInit();                              /* 8. 串口 */
  #endif
      InstallConsole(KERNEL_CONSOLE_BUS_NAME,          /* 9. 控制台 */
                     KERNEL_CONSOLE_DRV_NAME,
                     KERNEL_CONSOLE_DEVICE_NAME);
  }
  ```
- **bk7258 要补的实现细节**：
  - `SysTick_Handler` 的签名是 `(int irqn, void *arg)` —— XiUOS 自己的中断派发约定，**不是** ARM 标准 `void SysTick_Handler(void)`。Beken 的向量表要按 XiUOS 的签名注册。
  - `SystemCoreClock` 是 CMSIS 全局变量，Beken SDK 里有自己的 `SystemCoreClockUpdate()`；若直接用 Beken 的，注意别拉进整个 SDK 初始化链 —— **建议自己算主频写死常量**，避免在 XiZi 起来之前跑 Beken 的 `soc_init`
  - 步骤 2/6 的 `SYS_UnlockReg/SYS_LockReg` 是 Nuvoton 专有，bk7258 无对应物，删掉
- **验收**：串口能打出 XiUOS banner + shell 提示符

### 3.7 `board/bk7258/link.lds` — 最需要动脑的一个文件

- **模板**：`board/nuvoton-m2354/link.lds`（105 行）**的段布局**，但 MEMORY 段必须按 §2 重写
- **MEMORY 段**：
  ```ld
  MEMORY
  {
      flash (rx) : ORIGIN = 0x02120000, LENGTH = 0x380000   /* 3.5 MiB */
      sram  (rw) : ORIGIN = 0x28010000, LENGTH = 0x53800    /* 334 KiB */
  }
  ```
- **必须保留的段**（XiUOS 靠这些段做组件注册，缺一个就会链接失败或功能静默丢失）：
  - `KEEP(*(.isr_vector))` —— 向量表，**必须在 flash 最前**
  - `_shell_command_start/_end` ← `KEEP(*(shellCommand))` —— 否则 shell 里一条命令都没有
  - `__isrtbl_idx_start` / `__isrtbl_start` / `__isrtbl_end` ← `KEEP(*(.isrtbl.idx))` / `KEEP(*(.isrtbl))` —— 中断服务表
  - `g_service_table_start/_end` ← `KEEP(*(.g_service_table))`
  - `.ARM.exidx` 段并设 `_sidata` —— 启动代码靠它定位 `.data` 初值
  - `_sbss` / `_ebss` / `__bss_start` / `__bss_end` —— `board.h` 的 `HEAP_BEGIN` 依赖 `__bss_end`
  - `_system_stack_size` + `.stack` 段设 `_sp`
- **注意**：nuvoton 的 `.stack` 段放在 `sram` 开头、`.data` 之前。保持这个顺序，否则 `_sp` 会和 `.data` 撞。
- **验收**：`arm-none-eabi-nm XiZi-bk7258.elf | grep -E '_shell_command_start|g_service_table_start|__bss_end'` 三个符号都有非零地址

### 3.8 `board/bk7258/third_party_driver/{Kconfig,Makefile}`

- **模板**：nuvoton 同名两文件（各 7 行）
- **Kconfig**：
  ```
  menuconfig BSP_USING_UART
      bool "Using UART device"
      default y
      select RESOURCES_SERIAL
      if BSP_USING_UART
          source "$BSP_DIR/third_party_driver/uart/Kconfig"
      endif
  ```
- **Makefile**：
  ```make
  SRC_DIR := common
  ifeq ($(CONFIG_BSP_USING_UART),y)
    SRC_DIR += uart
  endif
  include $(KERNEL_ROOT)/compiler.mk
  ```
  （若 Beken 驱动不进 `common`，第一行改成 `SRC_DIR :=` 或直接去掉）
- **验收**：`make -C board/bk7258/third_party_driver` 能进 `uart`

### 3.9 `board/bk7258/third_party_driver/uart/{connect_uart.c,connect_uart.h,Makefile,Kconfig}`

**这是每块板子唯一必须从零重写的驱动。** 上游 `connect_uart.c` 共 13652 B，结构如下（实测函数清单）：

| 函数 | 链接 | 作用 |
|---|---|---|
| `SerialCfgParamCheck(struct SerialCfgParam *, struct SerialCfgParam *)` | static | 参数校验 + 填默认 |
| `UartHandler(struct SerialBus *, struct SerialDriver *)` | static | 中断里搬运 |
| `UART0_IRQHandler(int irqn, void *arg)` | 全局 | **IRQ 入口，签名是 XiUOS 约定** |
| `SerialInit(struct SerialDriver *, struct BusConfigureInfo *)` | static | 挂 `SerialOperations` |
| `SerialConfigure(struct SerialDriver *, int serial_operation_cmd)` | static | |
| `SerialDrvConfigure(void *drv, struct BusConfigureInfo *)` | static | |
| `SerialPutChar(struct SerialHardwareDevice *, char)` | static | |
| `SerialGetChar(struct SerialHardwareDevice *)` | static | |
| `BoardSerialBusInit(struct SerialBus *, struct SerialDriver *, const char *, const char *)` | static | 注册总线 + 驱动 |
| `BoardSerialDevBend(struct SerialHardwareDevice *, void *, const char *, const char *)` | static | 绑定设备 |
| `M2354HwUartInit(void)` | **导出** | `board.c` 调用 |

- **移植动作**：保留全部 9 个 static 函数的**签名与职责**，把内部实现从 `UART_Write()` / `UART_Read()`（Nuvoton）换成 Beken 的 UART 寄存器/API；导出函数改名 `Bk7258HwUartInit()`（与 §3.6 一致）
- **uart/Kconfig**：照 nuvoton 写 `menuconfig BSP_USING_UART0` + 三个 string（bus/drv/device name）。**注意**：`.defconfig` 里 nuvoton 是 `CONFIG_BSP_USING_UART1=y` 但 Kconfig 只定义了 `UART0` —— 上游这对不上，抄的时候统一成一个
- **验收**：`InstallConsole()` 成功，shell 双向可用（能敲命令、能看回显）

### 3.10 `board/bk7258/third_party_driver/startup/` — 上游没有的东西

- **模板**：`arch/arm/cortex-m23/boot.S` + `arch/arm/cortex-m23/interrupt_vector.S`（**从 m23 抄结构，不是从 m33**，因为 m33 目录里没有）
- **必须提供的符号**：
  - `.isr_vector` 段，前两项 `_sp` / `Reset_Handler`，后接 16 个系统异常（`NMI_Handler`、`HardFault_Handler`、`MemManage_Handler`、`BusFault_Handler`、`UsageFault_Handler`、`SVC_Handler`、`DebugMon_Handler`、`PendSV_Handler`、`SysTick_Handler`）+ 外部 IRQ
  - `Reset_Handler`：设 `_sp` → 从 `_sidata` 搬 `.data` → 清 `.bss` → `bl InitBoardHardware` → `bl main`
  - `Default_Handler`：死循环兜底
- **三个必须对齐的契约**（对不上就是开机即 HardFault）：
  1. `PendSV_Handler` 由 `arch/arm/cortex-m33/pendsv.S` 提供 —— **那里已经定义了，启动文件里不要再定义一个**，只放 weak 引用或干脆不写
  2. `SysTick_Handler` 由 `board.c` 提供，签名 `(int irqn, void *arg)`，与 ARM 标准签名不同
  3. `SVC_Handler` 由 `arch/arm/cortex-m33/syscall_gcc.S` 提供
- **验收**：`arm-none-eabi-nm` 里 `Reset_Handler` 与 `PendSV_Handler` 各只有一个定义（无 `multiple definition` 链接错误）

### 3.11 `board/bk7258/include/{bsp_api.h,hal_data.h}` + `board/bk7258/third_party_driver/common/r_bsp_compat.c`

见 §5。

---

## 4. 上游改动（唯一的两处）

### 4.1 `arch/arm/Makefile` — 加 4 行

在 `CONFIG_BOARD_RZG2UL_M33` 那组后面（约第 96 行）加：

```make
ifeq ($(CONFIG_BOARD_BK7258),y)
SRC_DIR += cortex-m33
endif
```

**只加 `cortex-m33`，不加 `shared`** —— 与 rzg2ul/rzv2l 一致。m33 目录里自带 `arm32_switch.c` / `pendsv.S` / `prepare_ahwstack.c` 的副本，加了 `shared` 会重复定义。

### 4.2 `path_kernel.mk` — 加一个板子块

在 `board/nuvoton-m2354` 那组后面（约第 598 行）加：

```make
ifeq ($(BSP_ROOT),$(KERNEL_ROOT)/board/bk7258)
KERNELPATHS += \
	-I$(KERNEL_ROOT)/arch/arm/cortex-m33 \
	-I$(BSP_ROOT)/include \
	-I$(BSP_ROOT)/third_party_driver/include \
	-I$(KERNEL_ROOT)/include #
endif
```

**注意**：`path_kernel.mk` 是「每个板子手写一段 `ifeq`」的风格，没有通配机制。`-I$(BSP_ROOT)/include` 必须排在 Beken 官方头之前 —— 这是 §5 影子头文件能生效的前提。

---

## 5. 影子头文件：把 FSP 耦合降到 0

§1.1 列出的 3 个耦合点，**全部可以用 `board/bk7258/include/` 里的同名头文件顶掉**，一行都不用改 `arch/`：

| 上游 include | 影子文件 | 内容 |
|---|---|---|
| `arch_interrupt.h` → `"bsp_api.h"` | `board/bk7258/include/bsp_api.h` | `#include "cmsis_gcc.h"` / `core_cm33.h`，提供 `__NVIC_PRIO_BITS` |
| `arm32_switch.c` → `<hal_data.h>` | `board/bk7258/include/hal_data.h` | **空文件**（实测该 include 是死的，全文没用到任何符号） |
| `interrupt.c` → `R_BSP_IrqEnable/Disable` | `third_party_driver/common/r_bsp_compat.c` | 两行转调 CMSIS `NVIC_EnableIRQ` / `NVIC_DisableIRQ` |

`r_bsp_compat.c` 内容：

```c
#include "cmsis_gcc.h"          /* 或 core_cm33.h */

void R_BSP_IrqEnable(IRQn_Type irq)  { NVIC_EnableIRQ(irq); }
void R_BSP_IrqDisable(IRQn_Type irq) { NVIC_DisableIRQ(irq); }
```

- **为什么可行**：`-I$(BSP_ROOT)/include` 在 `path_kernel.mk` 里的位置早于 Beken 官方头目录，`#include "bsp_api.h"` 会先命中我们的影子文件。renesas 的 FSP 头根本不在 bk7258 的 include 路径里，不会冲突。
- **风险**：这是靠 include 顺序「顶替」，不是标准做法。若日后觉得不干净，替代方案是给 `cortex-m33` 复制一份 `cortex-m33-bk7258/`，但那要多改 `arch/arm/Makefile` 一行、且从此背着 arch 层分叉。**先按影子头做，功能跑通后再决定要不要升格。**

---

## 6. 向量表与 `Reset_Handler`（最大缺口）

上游 `cortex-m33` 目录不带启动文件，所以 BK7258 的启动链是**自备**的。三种来源可选：

| 方案 | 做法 | 取舍 |
|---|---|---|
| A. 抄 m23 的 `boot.S` | 改 `-mcpu`、补 m33 的系统异常 | 最干净，工作量约 150 行汇编 |
| B. 用 Beken 官方 `startup.c` + `vector_data.c` | 从 TuyaOpen SDK 拷过来 | 省事，但会把 SDK 的 `soc_init` 链路一起拉进来，和 XiZi 抢硬件初始化权 |
| C. 用 CMSIS `startup_<device>.S` 模板 | ARM 官方模板 | 中性，需要自己配 `__Vectors` |

**推荐 A**，理由是 B 的初始化顺序冲突会在后期变成难查的间歇性故障。

**`Reset_Handler` 必须做、且只需做四件事**：设栈 → 搬 `.data`（源 `_sidata`）→ 清 `.bss`（`_sbss`..`_ebss`）→ 调 `InitBoardHardware()` 再调 `main()`。**不要**在这里做时钟/PLL/PSRAM 初始化 —— 那些归 `board.c` 的 `InitBoardHardware()`。

---

## 7. 未决问题

1. **CP 核怎么办？** 本清单只覆盖 AP 核。Beken 的 Wi-Fi/蓝牙闭源库（`libwifi.a` 等 6 个）依赖 CP 核的 `bk_rtos`。低风险路径是 **CP 保持原样、AP 换 XiZi**，但两者之间那套 `AP_SPINLOCK` + `SWAP` + 共享内存的核间协议需要单独梳理 —— 见 `docs/05-open-questions.md`。
2. **`CONFIG_AP_VIRTUAL_PARTITION_OFFSET` 是否会变？** 实测值是 `0x00120000`，它来自分区表。若分区表随固件版本变，`link.lds` 里的 `0x02120000` 就要跟着改。**建议在 `config.mk` 里把它做成可覆盖变量**，而不是写死在 `link.lds`。
3. **Beken UART 驱动是否依赖 `bk_rtos`？** 若 `uart_ll.c` 一类文件内部调了 `rtos_*`，就要么改 CP 侧、要么在 AP 侧给最小 stub。**上手第一件事就是 `nm` 一遍要用的 `.a`**（工具已在 `tools/nm_closed_libs.py`）。
4. **PSRAM 初始化时机。** `PSRAM_HEAP` 有 8.6 MiB，但要先跑 Beken 的 PSRAM 控制器初始化。若把它并进 XiZi 的堆，必须保证初始化在 `InitBoardMemory()` 之前完成。
5. **TrustZone 要不要开。** `arch/arm/cortex-m33/trustzone.c` 存在，但 BK7258 的 AP 核出厂是否使能 TF-M / 是否处在安全态，未确认。**建议第一版不开**，`config.mk` 里不加 `-mcmse`。

---

## 8. 分阶段验收

| 阶段 | 目标 | 判据 |
|---|---|---|
| ① 骨架 | `make BOARD=bk7258` 链接通过 | 产出 `XiZi-bk7258.elf`，无 `multiple definition` |
| ② 启动 | AP 核跑到 `Reset_Handler` 之后 | 串口打出 XiUOS banner |
| ③ shell | 控制台可用 | 能敲 `help` 并看到命令列表（验证 `shellCommand` 段没丢） |
| ④ 内核 | 调度器起来 | `TickAndTaskTimesliceUpdate` 走动，能创建任务并切换 |
| ⑤ 中断 | 中断可用 | `ArchEnableHwIrq()` 接通 CMSIS，外部中断能进 `IsrEntry()` |
| ⑥ 联网 | Wi-Fi 通（**用户的完成定义**） | 涂鸦 App 能连上设备 |

阶段 ①–③ 是纯 BSP 工作，不需要碰 Beken 闭源库。**④ 之后才需要面对 `bk_rtos` 那 95 个函数**（见 `docs/02-os-abstraction.md`、`docs/04-shim-design.md`）。

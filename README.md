# XiUOS BSP：Beken BK7258（涂鸦 T5-E1）

给 **XiUOS / XiZi 内核**新增一块板级支持包：**Beken BK7258**，即涂鸦 **T5-E1** 模组
（Cortex-M33 / ARMv8-M Mainline，双核 AP+CP）。

**当前状态：已上板跑到可交互 shell。** 串口控制台、内核调度、letter-shell 双向收发均实测可用。

```
[XZ] reset: XiZi AP core alive
frc: FreeRTOS API compat layer ready, 65 APIs, no FreeRTOS kernel (XiZi underneath)
... 初始化 vfs / workqueue / fatfs / libc ...
letter:/$ initialize letter-shell system success
Hello, world!

letter:/$ ShowTask
   STAT    ID  NAME                 PRI  STACK_DEPTH  USED  ...
   ...
```

> 本文档面向 XiUOS 维护者：**上游需要改什么、怎么构建、怎么验证、哪些还没做**。
> 开发过程中的踩坑记录在 [`docs/`](docs/)，本文只保留结论。

---

## 1. 给 XiUOS 带来什么

| | |
|---|---|
| 新增板子 | `board/bk7258/`，**43 个文件** |
| 使用现有架构层 | `arch/arm/cortex-m33/` —— **零改动**（见 §4） |
| 上游侵入面 | **2 个文件、约 15 行**（见 §4） |
| 新增硬件能力 | 涂鸦 T5-E1（Wi-Fi 6 + BLE 5.2 模组，8 MiB Flash / 16 MiB PSRAM / 双核 M33） |
| 附带的通用件 | 一套 **FreeRTOS API 兼容层**（65 个 API），可给任何需要跑 FreeRTOS 生态代码的 M33 板子复用 |

**为什么值得进上游**：这是 XiUOS 在**商用无线模组**上的第一个 BSP。
现有 M33 板子（`rzg2ul-m33`、`rzv2l-m33`）是瑞萨 AMP 从核，需要主核带；
`bk7258` 是**独立跑得起来的单芯片方案**，且带真实射频。

---

## 2. 快速开始

### 2.1 环境

在 **WSL2 / Linux** 下构建（上游 Makefile 是纯 POSIX 的，Windows 原生跑不了）。

```bash
sudo apt install gcc-arm-none-eabi kconfig-frontends build-essential rsync
```

装出来是 **arm-none-eabi-gcc 10.3.1**，与 TuyaOpen 自带的 Windows 版同代。

### 2.2 获取上游

两个上游都很大，不纳入本仓库，按 [`docs/00-upstream-setup.md`](docs/00-upstream-setup.md) 获取：

```
<repo>/Ubiquitous/XiZi_IIoT_Macro/    XiUOS（KERNEL_ROOT）
<repo>/APP_Framework/                 XiUOS 的兄弟目录，**必须一起放**
```

> ⚠️ **目录层级不能塌**。XiUOS 的 `Kconfig` 与 `path_kernel.mk` 大量引用
> `$(KERNEL_ROOT)/../../APP_Framework`。只拷 `XiZi_IIoT_Macro` 会让 kconfig 静默失败，
> 症状是满屏 `NAME_NUM_MAX undeclared` —— **看起来像代码问题，其实是配置没生成**。
> 详见 [`docs/07`](docs/07-build-environment.md) 坑 1。

### 2.3 构建

```bash
# 一键：同步 → 剥 CRLF → 修上游缺陷 → 铺 overlay → kconfig → make
wsl -- bash tools/xizi-build.sh bk7258 --overlay
```

产出 `build/XiZi-bk7258.elf` 与 `build/XiZi-bk7258.bin`。

脚本做的六步、以及为什么每一步都必要，见 [`docs/07`](docs/07-build-environment.md)。
其中两步是**上游当前的真实缺陷**，任何板子都会撞到：

| 步骤 | 原因 |
|---|---|
| 剥 CRLF | Windows 上 clone 出来整棵树是 CRLF，`xsconfig.sh` 直接报 `bad interpreter: /bin/bash^M` |
| `fix_upstream.py` | `arch/arm/cortex-m{23,33}/prepare_ahwstack.c` **缺 `#include <stdint.h>`**（参考副本 `arch/arm/shared/` 那份是有的），第 390 行用 `uintptr_t` 编不过 |
| `kconfig-conf --olddefconfig` | 各板陈旧的 `.defconfig` 缺 `kernel/Kconfig` 后加的符号，上游靠交互式 menuconfig 补 |
| 切板子清 `build/` | 旧 `.d` 还指着上一块板的 `xsconfig.h` |

### 2.4 镜像编码（**BK7258 特有，容易漏**）

BK7258 的 flash 物理布局是**每 32 字节数据跟 2 字节 CRC16**。CPU 看到的地址是去掉 CRC 后的逻辑地址：

```
CPU 地址 = 0x02000000 + 物理地址 / 34 * 32
物理 0x011000 → 0x02010000   CP 核固件
物理 0x132000 → 0x02120000   AP 核固件 ← link.lds 的 flash ORIGIN
```

**链接出来的裸 bin 必须先编码再烧**，否则 CPU 从第 33 字节起读错位：

```bash
python tools/bk_crc.py encode build/XiZi-bk7258.bin XiZi_crc.bin
python -c "..."   # 或任何方式补 0xFF 到 4K 整数倍，见下
```

> ⚠️ 另一个坑：bk_loader 把擦写长度**向下**截到 4K 整数倍（日志里 `length: 0x37700` → 实际 `write length: 0x37000`），
> **尾部会被静默丢弃**。编码后必须先补 `0xFF` 到 4K 整数倍。

### 2.5 烧录

板子排针只有一排：`GND | CEN | RX0 | TX0 | VBAT`。RX0/TX0 是 **UART0**，
控制台与 bootrom 烧录**共用这一个口**。

```bash
bk_loader download -p COM4 -b 460800 -i XiZi_crc4k.bin -s 0x132000 -e 1 -r
```

| 事项 | 值 |
|---|---|
| AP 分区物理偏移 | `0x00132000`（`partitions.csv` 的 `primary_ap_app`） |
| 工作波特率 | **460800** —— 默认的 2 Mbps 在 CH340 上不稳，表现为 `Write timeout` / `erase fail` |
| 进下载模式 | **手动把 CEN 短接到 GND 再松开**。CH340 的 DTR/RTS 没接到 CEN，软复位无效 |
| 回滚 | 用整片备份 `0x132000–0x16A000` 切片同样参数烧回（已实测可恢复） |

### 2.6 应该看到什么

115200 8N1。启动顺序：

```
CP（原厂 TuyaOS）拉起 AP → XiZi Reset_Handler → 板级初始化
  → FreeRTOS API 兼容层（65 个 API）→ vfs / workqueue / fatfs / libc
  → letter-shell → 应用 Hello, world!
```

---

## 3. `board/bk7258/` 里有什么

```
board/bk7258/                              43 个文件
├── .defconfig  Kconfig  Makefile          板级配置与构建接线
├── config.mk                              工具链 / -mcpu / ARCH_ARM_SECURE / 分区常量
├── board.c  board.h                       InitBoardHardware()、堆边界
├── link.lds                               内存布局
├── include/
│   ├── bk7258_soc.h                       自包含 SoC 头（寄存器、中断号、时钟）
│   ├── bsp_api.h      ┐
│   └── hal_data.h     ┘ 影子头文件，用来零改动解耦 arch 层的 Renesas FSP 耦合
└── third_party_driver/
    ├── startup/         boot.S + interrupt_vector.S
    │                    ★ 上游 cortex-m33 目录**两个都没有**，必须 BSP 自备
    ├── common/          SystemCoreClock、R_BSP 兼容、故障处理
    ├── uart/            UART0 控制台驱动（含系统级时钟/复用/分频配置）
    └── freertos_compat/ FreeRTOS API 兼容层（8 头 + 8 源）
```

### 3.1 为什么要自备启动文件

`arch/arm/cortex-m33/` **既没有 `boot.S` 也没有 `interrupt_vector.S`**（而 `cortex-m23/` 两个都有）。
现存两块 m33 板子靠 Renesas FSP 的 `startup.c` + `vector_data.c` 顶上。
BK7258 没有 FSP，所以从 `cortex-m23/boot.S` 抄结构自备。

另外注意：`pendsv.S` 导出的名字是 **`PendSV_Handler_NS`**（带 `_NS` 后缀），
写 `PendSV_Handler` 会链接失败。

### 3.2 FreeRTOS API 兼容层

**不是 FreeRTOS 内核** —— 只提供 API 形状，实现全部转调 XiZi 原语。
目的是让依赖 FreeRTOS 的第三方代码（如涂鸦 TKL 系统层）在不改一行的前提下跑起来。

覆盖 **65 个 API**（线程 / 信号量 / 队列 / 任务通知 / 堆 / 临界区），全部已链接进镜像。
设计与逐条映射见 [`docs/09`](docs/09-freertos-compat-layer.md)。

> 若你的板子不需要跑 FreeRTOS 生态代码，把 `third_party_driver/freertos_compat/`
> 从 `third_party_driver/Makefile` 的 `SRC_DIR` 里去掉即可。

---

## 4. 需要 XiUOS 上游接受什么

**总共 2 个文件、约 15 行。** 由 `tools/apply_overlay.py` 幂等打上
（用标记块 `# >>> xiuos-t5e1-port overlay >>>` 包住，回滚只需删标记之间的内容）。

### 4.1 `arch/arm/Makefile` —— 加 3 行

```make
ifeq ($(CONFIG_BOARD_BK7258),y)
SRC_DIR += cortex-m33
endif
```

> **只加 `cortex-m33`，不加 `shared`。** m33 目录里自带 `arm32_switch.c` / `pendsv.S` /
> `prepare_ahwstack.c` 的副本，再加 `shared` 会 `multiple definition`。
> 这与 `rzg2ul-m33` / `rzv2l-m33` 的现有处理一致。

### 4.2 `path_kernel.mk` —— 加 8 行 include 块

```make
ifeq ($(BSP_ROOT),$(KERNEL_ROOT)/board/bk7258)
KERNELPATHS += \
	-I$(KERNEL_ROOT)/arch/arm/cortex-m33 \
	-I$(BSP_ROOT) \
	-I$(BSP_ROOT)/include \
	-I$(BSP_ROOT)/third_party_driver/include \
	-I$(BSP_ROOT)/third_party_driver/freertos_compat/include \
	-I$(BSP_ROOT)/third_party_driver/freertos_compat/src \
	-I$(KERNEL_ROOT)/include #
endif
```

### 4.3 `arch/arm/cortex-m33/` —— **零改动**

m33 架构层对 Renesas FSP 的耦合**精确是 3 处**，全部由 BSP 侧顶掉：

| 位置 | 耦合点 | BSP 侧的解法 |
|---|---|---|
| `interrupt.c:43,50` | `R_BSP_IrqEnable/Disable()` | `third_party_driver/common/r_bsp_compat.c` 提供同名函数，转调 CMSIS `NVIC_*` |
| `arch_interrupt.h:17` | `#include "bsp_api.h"` | `include/bsp_api.h` 影子头 |
| `arm32_switch.c:15` | `#include <hal_data.h>` | `include/hal_data.h` 影子头（**该 include 是死的**，全文未用到任何符号） |

### 4.4 建议上游单独修的三个缺陷

这三个与本 BSP 无关，是**上游自身的 bug**，任何板子都会撞到：

| 文件 | 问题 | 修法 |
|---|---|---|
| `arch/arm/cortex-m23/prepare_ahwstack.c` | 第 390 行用 `uintptr_t` 但缺 `#include <stdint.h>` | 加一行（`arch/arm/shared/` 的同名文件是有的） |
| `arch/arm/cortex-m33/prepare_ahwstack.c` | 同上 | 同上 |
| 顶层 `Makefile:87` | `@for dir in $(SRC_DIR);do $(MAKE) -C $$dir; done` —— shell 的 for 循环退出码取**最后一条命令**，中间目录编译失败仍返回 0 | 改为 `set -e` 或逐个检查 `$$?` |

> 第三条影响最大：它让「构建成功」变成假象。本项目的构建脚本现在自己扫日志里的 `error:` 行数来兜底。

---

## 5. 硬件事实（实测，非推测）

| 项 | 值 | 出处 |
|---|---|---|
| AP 核 | Cortex-M33，**120 MHz**，XTAL 26 MHz | TuyaOpen `bk7258/sdkconfig` |
| **AP 核跑在安全态** | `CONFIG_TZ=y` / `CONFIG_SPE=1`，外设走**不带** `0x10000000` 偏移的安全地址 | 同上 |
| AP 核 SRAM | **334 KiB @`0x28010000`**（整块 640 KiB） | `partitions/ram_regions.h` |
| CP 核 SRAM | 244 KiB @`0x28063800` —— **不可碰** | 同上 |
| AP 核 FLASH | **3.5 MiB @`0x02120000`** | `bk7258_ap_out.ld` |
| PSRAM | 16 MiB @`0x60000000`（AP 侧现成两块共 9.6 MiB） | 同上 |
| 控制台 | **UART0 @`0x44820000`，115200 8N1** | 板子排针丝印 `RX0/TX0` |
| 中断号 | IRQ 4 = UART0，IRQ 15 = UART1，IRQ 63 = MAILBOX | 从 `app.elf` 的 `.vectors` 段逐项解析 |
| 向量表 | 256 字 / 1024 B，SMP 下含多张 512 B 对齐表 | 同上 |
| FPU | 是 M33F（带 FPU），但**本项目走软浮点** | Beken `armstar.h` |
| `__NVIC_PRIO_BITS` | 3 | 同上 |

### 5.1 三个只有上板才能发现的坑

1. **UART 的 `soft_reset` 是低有效**。写 0 等于按住复位，TX 脚恒为低，串口一个字都没有。
   Armino 的 `uart_ll_soft_reset()` 只写 1 并保持。
2. **UART0 的系统级配置没人做**。原厂系统不用 UART0 打印，所以时钟源、`uart0_cken`、
   GPIO10/11 复用、分频全都要 XiZi 自己配。四个寄存器地址见 [`docs/10`](docs/10-bringup-uart0.md) §4。
3. **多核中断路由**。外设中断要在系统控制器里按核使能（UART0 对应 sys reg `0x22` 的
   `cpu1_uart_int_en` bit4）。只使能 NVIC 收不到中断。

---

## 6. 验证状态

诚实分栏 —— 这个 BSP 目前的成熟度。

### ✅ 已验证

| 项 | 证据 |
|---|---|
| 构建通过 | `make` rc=0，日志 0 条 error（用自写检测，不看 make 的 rc） |
| 链接完整 | 向量表 16/16 正确、兼容层 **65 个 API 全部链入**（`tools/verify-bk7258.sh` 抽查其中 33 个关键项，含全部核心路径）、0 未定义符号 |
| 内存边界 | `__bss_end` = `0x28016894`，可用堆 **307 KiB（92% of AP_RAM）**，未越界到 CP 核 |
| **上板启动** | 串口打出 banner、兼容层日志、vfs/fatfs/libc 初始化 |
| **交互 shell** | `letter:/$` 提示符，`ShowTask` 等命令正常工作，收发双向 |
| 兼容层签名 | 用**真实 TKL 源码**编译验证，9/10 通过（第 10 个是 Beken 电源管理头链，与 FreeRTOS 无关） |
| 回滚 | 原厂 AP 切片烧回后，原厂固件重新播报配网提示 |

### ❌ 未验证 / 已知问题

| 项 | 状态 |
|---|---|
| **Wi-Fi / BLE 不可用** | XiZi 还没和 CP 核建立 mailbox 通信。这是验收阶梯 ④ |
| 运行时行为 | 兼容层的 ISR 变体安全性、超时精度未实测 |
| 启动阶段输出 | 早期裸输出与内核控制台交替写 FIFO，个别行有错字或断行。控制台装好后可停用 `Bk7258EarlyPuts` |
| `ShowTask` 的当前任务名 | 乱码。调度器启动前还没有当前任务，属打印时机问题 |
| SMP | 未开（`CONFIG_ARCH_SMP` 未设）。兼容层的 `atomic.h` 用「关中断 + 读改写」，**若日后开 SMP 必须换成 LDREX/STREX** |

### 验收阶梯

| 阶段 | 目标 | 状态 |
|---|---|---|
| ① | 链接通过 | ✅ |
| ② | 串口出 banner | ✅ |
| ③ | shell 可用 | ✅ |
| ④ | 与 CP 核 mailbox 对接 | ⬜ 下一步 |
| ⑤ | 中断/驱动完备 | ⬜ |
| ⑥ | Wi-Fi 通、涂鸦 App 能连上 | ⬜ |

---

## 7. 文档

| 文件 | 内容 |
|---|---|
| [`docs/00-upstream-setup.md`](docs/00-upstream-setup.md) | 两个上游的获取方式 |
| [`docs/01-feasibility.md`](docs/01-feasibility.md) | 可行性判定与证据链 |
| [`docs/02-os-abstraction.md`](docs/02-os-abstraction.md) | Beken `bk_rtos` 抽象层剖析（23 符号 / 95 接口 / 三套后端） |
| [`docs/03-arch-board.md`](docs/03-arch-board.md) | XiUOS `arch/` 与 `board/` 现状与模板 |
| [`docs/04-shim-design.md`](docs/04-shim-design.md) | `bk_rtos/xizi/` 设计：95 函数映射表 |
| [`docs/05-open-questions.md`](docs/05-open-questions.md) | 未决项与风险 |
| [`docs/06-board-bk7258-checklist.md`](docs/06-board-bk7258-checklist.md) | 逐文件实现清单（内存映射、上游改动面、验收阶梯） |
| [`docs/07-build-environment.md`](docs/07-build-environment.md) | **构建环境五个坑**（现象 / 根因 / 修法）+ 基线验证 |
| [`docs/08-keeping-tuya-stack.md`](docs/08-keeping-tuya-stack.md) | 换掉 RTOS 后涂鸦那套还保不保得住 |
| [`docs/09-freertos-compat-layer.md`](docs/09-freertos-compat-layer.md) | **FreeRTOS API 兼容层设计**（TKL 所需的 30 个 API 逐条映射 + 实现记录；本层共提供 65 个） |
| [`docs/10-bringup-uart0.md`](docs/10-bringup-uart0.md) | **上板记录**：CRC 镜像格式、单串口烧录、四个根因、②③ 达成 |
| [`docs/11-devlog.md`](docs/11-devlog.md) | 开发时间线（本文档原「状态」一节的内容） |
| [`docs/12-tuyaopen-e2e-baseline.md`](docs/12-tuyaopen-e2e-baseline.md) | 涂鸦端到端基线：CLI 建产品、联泓板级配置、日志改到 UART0、整片烧录、App 配网实测 |

---

## 8. 仓库结构

```
xiuos-t5e1-port/
├── README.md              本文件（面向 XiUOS 维护者）
├── docs/                  设计与分析文档
├── analysis/              实测原始数据（nm 导出、扫描结果）
├── xiuos-overlay/         XiUOS 侧新增/修改文件的 overlay
│   ├── arch/cortex-m33-notes/
│   └── board/bk7258/      ← 43 个文件，这就是要进上游的东西
├── beken-overlay/         Beken 侧新增文件的 overlay（④ 阶段才需要）
├── tuyaopen-overlay/      TuyaOpen 侧：联泓板级配置 LINKH_T5E1 + 应用补丁（端到端基线用）
└── tools/                 构建、烧录、验证脚本
```

**为什么用 overlay 而不是 submodule**：XiUOS 上游 604 MB、TuyaOpen SDK 亦数百 MB，
且上游 7 个月无提交、必然 fork 自维护。本项目只保存**我们自己的代码与补丁**，
两个上游按 [`docs/00`](docs/00-upstream-setup.md) 单独获取。

### 工具

| 文件 | 用途 |
|---|---|
| `tools/xizi-build.sh` | 六步构建（同步/剥 CRLF/修上游/铺 overlay/kconfig/make），固化五个坑 |
| `tools/apply_overlay.py` | 铺 `board/bk7258/` + 打两处上游补丁，幂等 |
| `tools/fix_upstream.py` | 修上游的 `<stdint.h>` 缺失，幂等 |
| `tools/verify-bk7258.sh` | 段布局 / 向量表 / 内存边界 / 兼容层链入 / 未定义符号 |
| `tools/tkl-compat-test.sh` | 用真实 TKL 源码验证兼容层签名 |
| `tools/bk_crc.py` | BK7258 的 32+2 CRC 块编解码与整片回归 |
| `tools/flash-and-watch.ps1` | 烧写后在同一口监听，保存原始字节并统计 break |
| `tools/shell_probe.py` | 逐字发送命令，验证 shell 收发 |
| `tools/autobaud_watch.py` | 轮询 460800/115200/921600，收到可读文本即锁定 |
| `tools/tcmd.py` | 向 TuyaOpen 固件的 `tuya>` 命令行发命令并记录输出 |
| `tools/reset_pairing.py` | 连续 `sys_reboot` 触发重置配网计数 |

---

## 9. 许可与归属

- 新增代码（`board/bk7258/` 与 `tools/`）沿用 **Mulan PSL v2**，与 XiUOS 上游一致。
- 影子头文件 `bsp_api.h` / `hal_data.h` 是空实现，不含第三方代码。
- `freertos_compat/` 的**函数签名**参照 FreeRTOS（MIT）公开 API 编写，代码为本项目原创，
  未包含 FreeRTOS 内核或头文件。

---

## 10. 联系

问题请开 issue。上游 XiUOS：https://www.gitlink.org.cn/xuos/xiuos

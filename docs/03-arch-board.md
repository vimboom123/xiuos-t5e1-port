# 03 · XiUOS 侧：`arch/` 与 `board/` 现状

上游：`https://www.gitlink.org.cn/xuos/xiuos`　master `0af75ee8`（2026-02-05，**7 个月无新提交**）
本地浅克隆体积：**604.6 MB / 15,906 文件**

> 注意：GitHub 镜像 `xuos/xiuos_mirror` 的 master 为 `49b20dfa`，**落后于 gitlink，不可作准**。
> `forgeplus.trustie.net` 上原地址已 404。

---

## 顶层结构

```
xiuos/
├── APP_Framework/
└── Ubiquitous/
    ├── Nuttx_Fusion_XiUOS
    ├── RT-Thread_Fusion_XiUOS
    ├── XiZi_AIoT_Micro        ← 微内核，面向带 MMU 的 A 核，非本项目目标
    └── XiZi_IIoT_Macro        ← ★ 本项目目标（宏内核，原 XiZi_IIoT）
```

> 09-03 记录的路径 `Ubiquitous/XiZi_IIoT/` **已不存在**，现名 `XiZi_IIoT_Macro`。

## `XiZi_IIoT_Macro/` 顶层

```
arch/       架构代码          ← 要检查/微调
board/      BSP               ← 要新增 bk7258
kernel/     XiZi 内核本体      ← 不改
resources/  驱动              ← 按需补
fs/  lib/  tool/
compiler.mk  link.mk  link_libc.mk  link_lwip.mk  link_mongoose.mk
Makefile  Kconfig  path_app.mk  path_kernel.mk  script.sh  mergebin.py
```

---

## `arch/` 现状 —— **好消息**

```
arch/arm/
├── cortex-m0
├── cortex-m23
├── cortex-m3
├── cortex-m33      ★ 已存在
├── cortex-m4
├── cortex-m7
└── shared
arch/risc-v/
├── ch32v208rbt6  ch32v307vct6  ch569w  fe310  gap8
├── gd32vf103-rvstar  k210  rv32m1-vega
└── shared
```

### `arch/arm/cortex-m33/` 内容（仅 8 个文件）

| 文件 | 大小 | 作用 |
|---|---|---|
| `arm32_switch.c` | 6,393 B | 上下文切换 |
| `pendsv.S` | 7,146 B | PendSV（调度切换中断） |
| `prepare_ahwstack.c` | 11,521 B | 栈准备 |
| `interrupt.c` | 1,991 B | 中断框架 |
| `arch_interrupt.h` | 1,767 B | 中断接口 |
| `syscall_gcc.S` | 1,629 B | 系统调用 |
| **`trustzone.c`** | 2,331 B | **TrustZone 支持** |
| `Makefile` | 133 B | |

**两点重要含义：**

1. **ARMv8-M Mainline (M33) 支持不是从零** —— 比 09-05 记录中「需在现有 v8-M/M7 基础上扩展」的估计要好。
2. **`trustzone.c` 存在** —— BK7258 带 TrustZone，因此
   「无线栈放 Secure 侧 / XiZi 放 Non-secure 侧」这条备选路径**有现成基础**。

---

## `board/` 现状 —— 39 个 BSP，无 BK7258

```
aiit-arm32-board          aiit-ch32v208rbt6-board   aiit-riscv64-board
at32f437vmt7              ch32v208rbt6              ch32v307vct6
ch569w                    cortex-m0-emulator        cortex-m3-emulator
cortex-m4-emulator        cortex-m7-emulator        edu-arm32
edu-riscv64               gapuino                   gd32f415rgo6
gd32vf103-rvstar          hifive1-emulator          hifive1-rev-B
imx8mp                    imxrt1176-sbc             k210-emulator
kd233                     maix-go                   nuvoton-m2354
ok1052-c                  rt1062xb                  rv32m1-vega
rzg2ul-m33                rzv2l-m33                 stm32f103-nano
stm32f407-st-discovery    stm32f407zgt6             stm32g474
stm32h750                 stm32l476rgt6             xidatong-arm32
xidatong-riscv64          xishutong-arm32           xiwangtong-arm32
```

### 模板 ①：`board/nuvoton-m2354/` —— 板级 BSP 首选模板

Nuvoton M2354 是 **ARMv8-M Baseline (M23) 的独立 MCU**（非 AMP 从核），
与本项目形态最接近。核心仅 6 个文件：

| 文件 | 大小 | 作用 |
|---|---|---|
| `.defconfig` | 5,038 B | 板级默认配置 |
| `board.c` | 2,120 B | 板级初始化 |
| `board.h` | 977 B | 板级头 |
| `link.lds` | 2,639 B | **链接脚本（内存布局）** |
| `config.mk` | 731 B | 构建变量 |
| `Kconfig` | 965 B | 配置项 |
| `Makefile` | 93 B | |

配套目录：
- `include/` —— CMSIS 头（`cmsis_gcc.h`、`core_armv8mbl.h`、`arm_math.h` …）
- `third_party_driver/` —— 厂商驱动（`nu_clk.c`、`nu_gpio.c` …）

**`board/bk7258/` 照此建立**，其中 `link.lds` 必须按 BK7258 的真实内存布局重写
（640 KB SRAM + PSRAM + 8 MB Flash，且分 AP / CP 两侧布局）。

### 模板 ②：`board/rzg2ul-m33/amp/` —— 双核通信模板

| 目录 | 文件 |
|---|---|
| `amp/include/` | `amp.h` `channel.h` `client.h` `msg.h` `msgqueue.h` `shm.h`（共享内存） `spinlock.h` `service.h` |
| `amp/include/config/` | `addr_cfg.h` `channel_cfg.h` `core_cfg.h` `ipi_cfg.h` `msgqueue_cfg.h` `shm_cfg.h` `platform_cfg.h` |
| `amp/include/service/` | **`service_linux.h`** + **`service_xiuos.h`** |
| `amp/src/` | `channel.c`(16 KB) `client.c` `config.c` `msg.c` `msgqueue.c` … |

**`service_xiuos.h` 的存在说明上游已经在「一侧 Linux、一侧 XiZi」的 AMP 形态上跑通过。**
Beken 的 AP/CP 双核同样是 AMP 形态，可直接参照。

### 模板 ③：两块现成的 Cortex-M33 BSP（有限参考）

`rzg2ul-m33` / `rzv2l-m33` 已包含 `configuration.xml`、`link.lds`、`rzg_cfg.txt` 等。

> ⚠️ **它们都是瑞萨 AMP 从核**：主核 A55 跑 Linux、通过 RPMsg 通信，
> **启动与时钟可能依赖主核**，不能直接照抄到独立 MCU。
> 板级模板请用 `nuvoton-m2354`，双核通信部分才参照它们。

---

## 本项目在 XiUOS 侧要加/改的东西

```
xiuos-overlay/
├── board/bk7258/              ★ 新建
│   ├── .defconfig             ← 参照 nuvoton-m2354
│   ├── board.c / board.h
│   ├── link.lds               ← ⚠️ 按 BK7258 内存布局重写
│   ├── config.mk / Kconfig / Makefile
│   ├── third_party_driver/    ← Beken UART/GPIO/Timer/Flash 对接
│   └── amp/ (可选)            ← 参照 rzg2ul-m33/amp
└── arch/cortex-m33-notes/     ★ 判定记录（不改上游，先记录）
    └── README.md
```

**`kernel/` 不动。**

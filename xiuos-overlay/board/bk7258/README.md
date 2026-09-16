# board/bk7258/ —— 实现清单已产出

XiUOS 的 BK7258 板级支持包。**模板：`board/nuvoton-m2354/`。**

> **详细逐文件清单见 [`docs/06-board-bk7258-checklist.md`](../../docs/06-board-bk7258-checklist.md)**
> 本文只留索引与已实测的硬数字，避免两处内容漂移。

## 文件清单（模板来源已确认）

| 文件 | 模板 | 说明 |
|---|---|---|
| `config.mk` | `nuvoton-m2354/config.mk` | **最先被读**；定 `CROSS_COMPILE` 与 `-mcpu=cortex-m33 -mthumb` |
| `Kconfig` | `nuvoton-m2354/Kconfig` | 定义 `CONFIG_BOARD_BK7258` + `select ARCH_ARM` |
| `.defconfig` | `nuvoton-m2354/.defconfig` | 只改第 5 行的 BOARD 宏 |
| `Makefile` | `nuvoton-m2354/Makefile` | `SRC_FILES := board.c`，`SRC_DIR := third_party_driver` |
| `board.h` | `nuvoton-m2354/board.h` | `HEAP_BEGIN/END`，取值见下方内存布局 |
| `board.c` | `nuvoton-m2354/board.c` | `InitBoardHardware()` 九步；删掉 Nuvoton 的 `SYS_UnlockReg` |
| `link.lds` | `nuvoton-m2354/link.lds` 段布局 | MEMORY 段按下方布局重写 |
| `third_party_driver/{Kconfig,Makefile}` | 同名 | |
| `third_party_driver/uart/` | `nuvoton-m2354/.../uart/` | 9 个 static + 1 个导出 `Bk7258HwUartInit()` |
| `third_party_driver/startup/` | `arch/arm/cortex-m23/boot.S` | **上游 m33 目录没有启动文件**，必须自备 |
| `include/{bsp_api.h,hal_data.h}` | — | 影子头文件，用来零改动解耦 Renesas FSP |

## 实测内存布局（AP 核）

来源：`t5_os/build/bk7258/tuya_app/partitions/ram_regions.h` 与真实链接脚本 `bk7258_ap_out.ld`。

### SRAM（640 KiB @ `0x28000000`，五段精确闭合）

| 区域 | 起始 | 大小 | 归属 |
|---|---|---|---|
| `AP_SPINLOCK` | `0x28000000` | 64 KiB | Beken 核间自旋锁 |
| **`AP_RAM`** | **`0x28010000`** | **334 KiB** | **← XiZi 的 .data / .bss / stack** |
| `CP_RAM` | `0x28063800` | 244 KiB | CP 核，**不可碰** |
| `PWR_MNG` | `0x2809F700` | 256 B | 电源管理 |
| `SWAP` | `0x2809F800` | 2 KiB | OTA swap |

```c
/* board.h 取值 */
#define BK7258_AP_RAM_BASE   0x28010000UL
#define BK7258_AP_RAM_SIZE   0x53800UL              /* 334 KiB */
#define BK7258_AP_RAM_END    (BK7258_AP_RAM_BASE + BK7258_AP_RAM_SIZE)
extern int __bss_end;
#define HEAP_BEGIN ((void *)&__bss_end)
#define HEAP_END   (void *)BK7258_AP_RAM_END
```

**不要把 `HEAP_END` 写成 `0x280A0000`** —— 中间 244 KiB 是 CP 核的。

```ld
/* link.lds MEMORY 段 */
MEMORY
{
    flash (rx) : ORIGIN = 0x02120000, LENGTH = 0x380000   /* 3.5 MiB */
    sram  (rw) : ORIGIN = 0x28010000, LENGTH = 0x53800    /* 334 KiB */
}
```

### FLASH / PSRAM

- FLASH：`0x02000000` + `CONFIG_AP_VIRTUAL_PARTITION_OFFSET`（实测 `0x00120000`）→ `0x02120000`，长 `0x380000` = 3.5 MiB
- PSRAM：`0x60000000`，容量 16 MiB。AP 侧可用 `PSRAM_STACK_HEAP`（`0x60640000`/1 MiB）与 `PSRAM_HEAP`（`0x60740000`/8.625 MiB）；其余归 Beken 音频/显示

### 地址别名（易错）

`__AP_APP_IRAM_BASE = 0x28010000 - 0x20000000 = 0x08010000`。同一块物理 SRAM：取指总线看 `0x0801_0000`，数据总线看 `0x2801_0000`。
→ **统一用数据别名 `0x28010000`**。

## 两个必须知道的坑

1. **`arch/arm/cortex-m33/` 没有 `boot.S`，也没有 `interrupt_vector.S`**（`cortex-m23/` 两个都有）。向量表与 `Reset_Handler` 由 BSP 自备 —— 现存两块 m33 板子靠 Renesas FSP 的 `startup.c` + `vector_data.c` 顶上。
2. **m33 arch 层硬耦合 Renesas FSP**：`interrupt.c` 调 `R_BSP_IrqEnable/Disable`、`arch_interrupt.h` include `"bsp_api.h"`、`arm32_switch.c` include `<hal_data.h>`。**耦合面就这 3 处**，可用 `include/` 影子头文件全部顶掉，`arch/` 零改动。详见 `docs/06` §5。

## 上游必须改的两处

| 文件 | 改动 |
|---|---|
| `arch/arm/Makefile` | 加 `ifeq ($(CONFIG_BOARD_BK7258),y)` → `SRC_DIR += cortex-m33`（**不加 `shared`**） |
| `path_kernel.mk` | 加一个 `board/bk7258` 的 `ifeq` 块，include `-I$(BSP_ROOT)/include` 等 |

## 验收阶梯

① 链接通过 → ② 串口出 banner → ③ shell 可用 → ④ 调度器起来 → ⑤ 中断接通 → ⑥ Wi-Fi 通（用户定义的完成线）

①–③ 纯 BSP，不碰 Beken 闭源库；**④ 之后才需要面对 `bk_rtos` 那 95 个函数**。

# arch/cortex-m33 判定记录

上游 `arch/arm/cortex-m33/` **已存在**，共 8 个文件。逐项判定结果（依据：对上游源码的实际 grep，见「证据」列）：

| 文件 | 大小 | 作用 | BK7258 可直接用？ | 证据 |
|---|---|---|---|---|
| `arm32_switch.c` | 6,393 B | 上下文切换 | ✅ **可用** | `#include <hal_data.h>` 是**死 include** —— 全文未使用其中任何符号，影子空头即可 |
| `pendsv.S` | 7,146 B | PendSV | ✅ **可用** | 只 `#include <xsconfig.h>` |
| `prepare_ahwstack.c` | 11,521 B | 栈准备 | ✅ **可用** | 只 include XiUOS 自己的头（`board.h`/`shell.h`/`xizi.h`…） |
| `interrupt.c` | 1,991 B | 中断框架 | ⚠️ **需 2 个兼容函数** | `R_BSP_IrqEnable()` / `R_BSP_IrqDisable()`（Renesas FSP 符号） |
| `arch_interrupt.h` | 1,767 B | 中断接口 | ⚠️ **需影子头** | `#include "bsp_api.h"`，为拿 `__NVIC_PRIO_BITS` |
| `syscall_gcc.S` | 1,629 B | 系统调用 | ✅ **可用** | 无任何 `#include` |
| `trustzone.c` | 2,331 B | TrustZone | ✅ **可用** | 只 `#include <xizi.h>` |
| `Makefile` | 133 B | 6 个源文件 | — | `SRC_FILES := arm32_switch.c interrupt.c pendsv.S prepare_ahwstack.c syscall_gcc.S trustzone.c` |

## 结论：FSP 耦合面 = 3 处，可全部用影子头文件顶掉

```
arch/arm/cortex-m33/
  interrupt.c        →  R_BSP_IrqEnable / R_BSP_IrqDisable     ← BSP 侧提供兼容函数
  arch_interrupt.h   →  #include "bsp_api.h"                   ← BSP 侧影子头
  arm32_switch.c     →  #include <hal_data.h>   (死 include)    ← BSP 侧影子头（空）
```

在 `board/bk7258/include/` 放同名头 + 一个 `r_bsp_compat.c` 即可，**`arch/` 零改动**。
前提是 `path_kernel.mk` 里 `-I$(BSP_ROOT)/include` 排在 Beken 官方头之前。详见 `docs/06` §5。

> 另注：`arch/arm/shared/` 下的同名文件（`arm32_switch.c`、`pendsv.S`、`prepare_ahwstack.c`）
> **零 FSP 耦合**。但 m33 板子**不编译 `shared`**（`arch/arm/Makefile` 里只有 `SRC_DIR += cortex-m33`，
> 没有 `+= shared`），用的是 m33 目录里自带的副本，所以这份干净代码帮不上忙。

## 逐项确认（原「需要逐项确认」清单）

### ✅ 中断向量表 —— 已确认是**缺口**，不是差异

`arch/arm/cortex-m33/` **既没有 `boot.S`，也没有 `interrupt_vector.S`**：

| 目录 | `boot.S` | `interrupt_vector.S` |
|---|---|---|
| `arch/arm/cortex-m23/` | ✅ 有（2,116 B） | ✅ 有（8,913 B） |
| `arch/arm/cortex-m33/` | ❌ **没有** | ❌ **没有** |

现存两块 m33 板子（`rzg2ul-m33`、`rzv2l-m33`）靠 Renesas FSP 的 `startup.c` + `vector_data.c` 顶上。
**BK7258 必须自备** —— 模板从 `cortex-m23/boot.S` 抄，不是从 m33 抄。

**Beken 私有偏移：确实存在。** `bk7258_ap_bsp.ld` 里：

```ld
ASSERT((. == ALIGN(512)), "vector table address align fault.")
.vectors :
{
#if (CONFIG_SOC_SMP)
    __vector_core0_table = .;  KEEP(*(.vectors_core0))   . = ALIGN(512);
    __vector_core1_table = .;  KEEP(*(.vectors_core1))   . = ALIGN(512);
    __vector_core2_table = .;  KEEP(*(.vectors_core2))   . = ALIGN(512);
#else
    __vector_table = .;        KEEP(*(.vectors))         . = ALIGN(512);
#endif
} > FLASH
```

SMP 模式下是**三张 512 字节对齐的向量表**（core0/1/2），入口符号也叫 `Reset_Handler_Cpu0` 而非 `Reset_Handler`。
→ XiZi 的启动文件必须决定：跟 Beken 的 SMP 三表布局，还是只用单表。

### ✅ 时钟源 —— `arch` **确实假设 SysTick**

证据两条：
- `arch_interrupt.h` 定义了 `NVIC_SYSTICK_PRI = (MIN_INTERRUPT_PRIORITY << 24UL)` 与 `NVIC_SYSTICK_MASK`
- `board/nuvoton-m2354/board.c` 里直接 `SysTick_Config(SystemCoreClock / TICK_PER_SECOND)`，并在 `SysTick_Handler` 里调 `TickAndTaskTimesliceUpdate()`

**但有一处签名差异必须注意**：XiUOS 的 `SysTick_Handler` 是 `void SysTick_Handler(int irqn, void *arg)`，
**不是** ARM 标准的 `void SysTick_Handler(void)`。Beken 的向量表要按 XiUOS 的签名注册。

### ⏳ TrustZone —— 仍未确认

`trustzone.c` 本身可编译，但 BK7258 的 AP 核**出厂是否使能 TF-M / 当前处于安全态还是非安全态**，未实测。
建议第一版 `config.mk` **不加 `-mcmse`**，先跑通非 TrustZone 路径。

### ⏳ FPU —— 仍未确认

BK7258（Cortex-M33）是否带 FPU、`arch` 的上下文切换是否保存 FPU 寄存器（`pendsv.S` / `prepare_ahwstack.c` 里的栈帧大小是否含 S0–S31），未核对。
这会影响浮点任务切换正确性，属于**跑通 shell 之后、调度器阶段必须验证**的一项。

## 备注

上游另有两块 Cortex-M33 BSP（`board/rzg2ul-m33`、`board/rzv2l-m33`），
但**都是瑞萨 AMP 从核**（主核 A55 跑 Linux、RPMsg 通信），启动与时钟依赖主核，
**不能直接照抄到独立 MCU**。`board/nuvoton-m2354/`（独立 ARMv8-M MCU）是更合适的模板，
且它的 `config.mk` 已经证明「一块完全独立的 MCU 板子如何只用 6 个文件接进 XiUOS 构建系统」。

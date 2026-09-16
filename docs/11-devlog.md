# 11 — 开发时间线

本文是 README 原「状态」一节的归档。按时间顺序记录每个阶段做了什么、得到什么结论。
**结论性内容已提炼进 README 与 `docs/01`～`docs/10`，这里保留的是过程与当时的判断。**

时间：2026-09-16（一天内完成 ①～③）。

---

## 阶段 0：可行性实测与架构判定 → `docs/01`～`docs/05`

**做了什么**：用 `arm-none-eabi-nm --undefined-only` 扫描 6 个关键闭源静态库。

| 库 | 未定义符号总数 | 其中 RTOS 相关 |
|---|---|---|
| `libwifi.a`（5.9 MB） | 1,915 | **15** |
| `libwifi_csi.a`（6.2 MB） | 2,009 | **19** |
| `libbluetooth_controller_dual.a`（6.6 MB） | 2,352 | **6** |
| `libbluetooth_host_dm_dual.a` / `_ap.a`（7.2 MB） | 3,152 / 3,151 | **0** |
| `libfdk_aac_enc.a`（9.5 MB） | 1,048 | **0** |

并集仅 **23 个符号，全是 Beken 私有抽象**（`os_mem*`、`rtos_*`），
**零个 FreeRTOS 原生符号**。

**结论**：闭源库一行都不用改。Beken 的 `bk_rtos` 本来就是可替换的 OS 抽象层
（已有 `freertos/`、`freertos/v10/`、`non_os/` 三套后端），移植 = 新增第四套 `xizi/`。

> 这条**推翻了本库此前的判断**「垫片层 = 在 XiZi 上再养一个 FreeRTOS」。
> 那个判断建立在「闭源库直接调 FreeRTOS」的假设上，而假设是错的。

---

## 阶段 1：`board/bk7258/` 逐文件清单 → `docs/06`

**做了什么**：读完 `board/nuvoton-m2354/` 的 6 个核心文件，再把 XiUOS 构建系统整条链读完
（顶层 `Makefile`、`compiler.mk`、`path_kernel.mk`、`link.mk`、`arch/arm/Makefile`）。

**得到的结论**：

- 上游侵入面 **2 个文件约 15 行**；`arch/arm/cortex-m33/` 可做到**零改动**
- m33 架构层对 Renesas FSP 的耦合**精确是 3 处**（2 个函数调用 + 2 个 include，其中一个是死 include）
- `arch/arm/cortex-m33/` **既没有 `boot.S` 也没有 `interrupt_vector.S`**，向量表与 `Reset_Handler` 必须 BSP 自备
- AP 核 SRAM 334 KiB / CP 核 244 KiB，两者精确闭合到 640 KiB

---

## 阶段 2：构建环境与上游缺陷 → `docs/07`

搭 WSL2 环境，**先用上游现成的 `nuvoton-m2354` 跑基线**——这一步很关键：
基线编不过就是环境问题；基线编得过而新板编不过，问题就一定在新板。

撞到五个坑，全部固化进 `tools/xizi-build.sh`：

| # | 坑 | 症状 |
|---|---|---|
| 1 | **目录层级**：`APP_Framework` 必须与 `Ubiquitous` 平级 | kconfig 静默失败 → 满屏 `NAME_NUM_MAX undeclared`，**看起来像代码问题** |
| 2 | 整树 CRLF | `xsconfig.sh: /bin/bash^M: bad interpreter` |
| 3 | 上游缺 `<stdint.h>` | `uintptr_t` 编不过（`shared/` 那份是有的，属抄漏） |
| 4 | 陈旧的 `.defconfig` | 缺 `kernel/Kconfig` 后加的符号 |
| 5 | 写死的工具链路径 | `/opt/gcc-arm-none-eabi-6-2017-q1-update/bin/` |

**基线结果**：`nuvoton-m2354` 构建成功，`make` rc=0，ELF 1,505,588 B，
`Reset_Handler` / `IsrEntry` / `_shell_command_start` / `_sp` / `__bss_end` 全部就位。

---

## 阶段 3：`board/bk7258/` 骨架 → 验收阶梯 ①

43 个文件写完后，`make BOARD=bk7258` 链接通过。

```
text    data     bss     dec     hex
199780    3220   23100  226100   37334
```

核验：`.text@0x02120000`（与实测 `bk7258_ap_out.ld` 的 `.vectors` 地址一致）、
向量表 16/16 正确、`__bss_end` 未越界到 CP 核、未定义符号 0。

---

## 阶段 4：FreeRTOS API 兼容层 → `docs/09`

**决策**：TKL 那层走**路线 A（FreeRTOS API 兼容层）**，不改 Tuya 代码。
理由是 owner 要求「仍然留在涂鸦平台」——fork 掉 Tuya 的 6 个 TKL 文件会让每次
TuyaOpen 升级都变成手工合并。

**实现过程中撞到四个坑，其中两个是「构建成功」的假象**：

| 坑 | 现象 |
|---|---|
| 上游 `make` 吞掉子目录失败 | 顶层 `for dir in ...; do $(MAKE) -C $$dir; done`，循环退出码取最后一条命令 → 中间目录编译失败仍返回 0 |
| `--gc-sections` 回收整层 | 没有调用方时整层被回收，`make` 成功但一个符号都没进镜像 |
| **常量折叠让链接校验失效** | 加了 API 覆盖表后，`if (table[0] == NULL)` 被 GCC 折叠 → 对表的引用消失 → 表与它引用的几十个 API 一起被回收。**索引必须走 `volatile`** |
| `KTaskCoreCombine` 只在 `ARCH_SMP` 下存在 | 单核构建下 undefined reference |

> 第一条尤其要命：它让「阶段 3 的 rc=0」这个判定标准本身不可靠。
> 已用新判定（扫日志里的 `error:` 行数）重跑，结论不变，但过程是教训。

**核验**：33/33 兼容层 API 链入、0 未定义符号、可用堆 307 KiB。

---

## 阶段 5：用真实 TKL 源码验证兼容层 → `docs/09` §8

只编译不链接，把 `tuyaos_adapter/src/system/` 下真实的 10 个 TKL 文件用 compat include 编一遍。

**9/10 通过。** 发现三个「只看文档看不出来」的缺口：

1. **TKL 直接用 FreeRTOS 的内部实现头**：`atomic.h`（7 个原子操作）、`projdefs.h`、`mpu_wrappers.h`
2. **两个废弃别名不能省**：`portTICK_RATE_MS`（V10.4 后改名）、`xQueueHandle`
3. `tkl_sleep.c` 缺的是 **Beken 电源管理**头链，与 FreeRTOS 无关 —— 明确不追

---

## 阶段 6：上板 → 验收阶梯 ②③ → `docs/10`

这是最耗时的一段，因为**串口一直收不到任何字节**。

### 走过的弯路（值得记住）

**核心错误：我从一开始就假设「能烧录 = 能看到日志」。**

在这块板子上这两件事走的是**不同的 UART**：

```
排针丝印:  GND | CEN | RX0 | TX0 | VBAT
                            ↑
                     UART0 = 唯一的对外串口
原厂固件日志走 UART1（P0/P1），本板没引出
```

所以原厂固件正常运行时，接在 RX0/TX0 上的 USB-TTL **本来就应该收不到任何字节**——
而我把这当成了「板子没跑」的证据，在镜像、波特率、板子之间反复排查了很久。

**正确的起点应该是**：先确认能听见原厂固件，再动任何东西。

### 逐版排查

| 版本 | 改动 | 串口现象 |
|---|---|---|
| 原版 | 裸 bin、控制台 UART0、`soft_reset` 写 1 再写 0 | 0 字节 |
| v2 | 加 CRC 编码 | 0 字节（尾部被 4K 截断） |
| v3 | `Bk7258Uart0SysInit()`：UART0 时钟、时钟源、GPIO10/11 复用、分频 | 0 字节 |
| v4 | 启动时把 P11 当 GPIO 慢速拉低 6 次 | 收到 6 个 0x00（break），RX 灯间隔闪 |
| v5 | `soft_reset` 改为 0x2 → 0x3 | **出字**：`[XZ]` 标记、兼容层、banner，随后在任务切换处进异常 |
| v6 | `-DARCH_ARM_SECURE`；软复位前等 TX FIFO 空 | 启动到 `letter:/$` 和 `Hello, world!`，输入无回显 |
| v7 | UART0 中断路由给 cpu1；RX 阈值 1 字节 | **收发双向可用**，`ShowTask` 正常 |

> v4 的做法值得保留：**只有一个串口、又不知道 CPU 有没有跑起来时，把 TX 脚当 GPIO 翻转**，
> 不依赖 UART 外设本身，就能区分「没跑到我们的代码」和「UART 没配对」。

### 四个根因

1. **UART 的 `soft_reset` 低有效**。写 0 等于按住复位，TX 脚恒为低。Armino 的
   `uart_ll_soft_reset()` 只写 1 并保持。
2. **UART0 的系统级配置没人做**（时钟源、`uart0_cken`、GPIO10/11 复用、分频）。
3. **AP 核跑在安全态**。`arch/arm/cortex-m33/prepare_ahwstack.c` 在未定义 `ARCH_ARM_SECURE` 时
   给新任务填 `EXC_RETURN = 0xFFFFFFBC`（返回非安全态），第一次任务切换即进异常。
4. **多核中断路由**。外设中断要在系统控制器里按核使能，只使能 NVIC 收不到。

### 另一个大发现：32+2 CRC 镜像格式

BK7258 flash 物理上每 32 字节数据跟 2 字节 CRC16，CPU 地址 = `0x02000000 + 物理/34*32`。
裸 bin 必须先编码再烧，否则从第 33 字节起读错位。

---

## 当前遗留

见 README §6。最主要是 **Wi-Fi/BLE 还不可用**——XiZi 尚未与 CP 核建立 mailbox 通信，
那是验收阶梯 ④ 的内容。

---

## 2026-09-16 深夜 — 涂鸦端到端基线（FreeRTOS）

见 [`docs/12`](12-tuyaopen-e2e-baseline.md)，改动在 `tuyaopen-overlay/`。
CLI 复制出新产品，自建 `LINKH_T5E1` 板级配置，TAL 日志挂到 UART0，整片烧录后设备进入配网。
App 蓝牙配网链路与令牌下发已通；第一次选的热点不在 2.4G 上（`WSS_NO_AP_FOUND`），激活上云待复测。

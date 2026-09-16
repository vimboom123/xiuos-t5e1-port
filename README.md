# XiUOS → BK7258 (Tuya T5-E1) 移植

把 **XiUOS / XiZi 内核**移植到 **Beken BK7258**（= 涂鸦 T5-E1 模组）上，使其上层的
**闭源无线（Wi-Fi 6 / BLE）与音频库完全不用改**。

> 目标硬件：涂鸦 T5-E1 模组（Beken BK7258，Cortex-M33 / ARMv8-M Mainline，双核 AP+CP）
> **AP 核主频 120 MHz**（实测 `CONFIG_CPU_FREQ_HZ=120000000`，XTAL 26 MHz）。
> 早先版本这里写过 480 MHz —— 那是把芯片的 Wi-Fi/多媒体标称能力当成了 CPU 主频，**是错的**。
> 上游 XiUOS：https://www.gitlink.org.cn/xuos/xiuos （master `0af75ee8`，2026-02-05，**7 个月无新提交**）

---

## 一句话结论

**可行，而且比预想的干净得多。**

Beken 的 `bk_rtos` **本来就是一层可替换的 OS 抽象层** —— 它已经带三套后端
（`freertos/`、`freertos/v10/`、`non_os/`），移植 = **新增第四套 `xizi/`**。

闭源库不调用 FreeRTOS，只调用 Beken 自己的 23 个 `rtos_*` / `os_*` 符号，
所以 **`libwifi.a` / `libbluetooth_*.a` / `libfdk_aac_enc.a` 一行都不用改**。

---

## 架构

```
┌──────────────────────────────────────────────────────────┐
│  应用 / AI 组件（TuyaOpen src/ai_components, src/tuya_*） │
├──────────────────────────────────────────────────────────┤
│  闭源库                                                   │
│    libwifi.a  libwifi_csi.a                               │
│    libbluetooth_controller_dual.a  libbluetooth_host_dm_* │
│    libfdk_aac_enc.a                                       │
│  ↑ 只依赖 23 个 rtos_*/os_* 符号，不改                    │
├──────────────────────────────────────────────────────────┤
│  bk_rtos/xizi/          ★ 本项目新增（第四套后端）        │
│    95 个 rtos_* 函数 → 映射到 XiZi 原语                    │
├──────────────────────────────────────────────────────────┤
│  XiZi 内核（XiUOS）                                       │
│    kernel/  +  arch/arm/cortex-m33/  +  board/bk7258/     │
└──────────────────────────────────────────────────────────┘
```

---

## 两边各自要动什么

| 侧 | 路径 | 状态 |
|---|---|---|
| **XiUOS** | `arch/arm/cortex-m33/` | ✅ **上游已有**（8 个文件），**零改动**接入（影子头文件 + 2 个兼容函数顶掉 Renesas FSP 耦合） |
| **XiUOS** | `board/bk7258/` | ✅ **已完成骨架并链接通过**（24 个文件），照 `board/nuvoton-m2354/` |
| **XiUOS** | 上游侵入面 | ✅ 仅 **2 个文件约 15 行**（`arch/arm/Makefile`、`path_kernel.mk`），由 `tools/apply_overlay.py` 幂等打上 |
| **Beken** | `ap/components/bk_rtos/xizi/` | ❌ **待做**，照 `bk_rtos/non_os/`（无 OS 后端模板）。④ 阶段之后才需要 |

---

## 关键实测数据

| 测量 | 结果 | 出处 |
|---|---|---|
| 闭源库需要的 RTOS 符号 | **23 个**（全是 Beken 私有抽象） | `analysis/closed-lib-rtos-symbols.txt` |
| `bk_rtos` 公共接口规模 | **约 95 个函数** | `ap/include/os/os.h`（1397 行） |
| SDK 内 FreeRTOS API 使用 | 78 个 API / 3,568 处调用 / 239 文件 | `analysis/freertos-surface.txt` |
| FreeRTOS 形态 | **源码 vendored**，非二进制 | `os_source/freertos_smp_v2p0/`（AP）、`freertos_v10/`（CP） |
| XiUOS 的 Cortex-M33 支持 | ✅ 已有 `arch/arm/cortex-m33/` | 上游 master |
| XiUOS 的 M33 BSP | 2 块（`rzg2ul-m33`、`rzv2l-m33`），均为瑞萨 AMP 从核 | 上游 master |
| **AP 核主频** | **120 MHz**（XTAL 26 MHz） | TuyaOpen `bk7258/sdkconfig` |
| **AP 核 SRAM** | **334 KiB @`0x28010000`**（整块 640 KiB，其余归 CP 核等） | `partitions/ram_regions.h` + `bk7258_ap_out.ld` |
| **AP 核 FLASH** | **3.5 MiB @`0x02120000`** | 同上，且与 `app.elf` 的 `.isr_vector` 地址吻合 |
| **PSRAM** | 16 MiB @`0x60000000`（AP 侧现成两块共 9.6 MiB） | 同上 |
| **控制台串口** | **UART1 @`0x45830000`，460800 8N1**（不是 UART0/115200） | `sdkconfig`: `CONFIG_UART_PRINT_PORT=1`、`CONFIG_UART_PRINT_BAUD_RATE=460800` |
| **中断号映射** | 从 `app.elf` 的 `.vectors` 段逐项解析；**IRQ 15 = UART1** | `docs/06` §2.3 |
| **向量表结构** | 256 字 / 1024 B，SMP 下含多张 512 B 对齐表；初始 `_sp` = `0x28063800`（AP_RAM 顶端） | `app.elf` + `bk7258_ap_out.ld` |
| **FPU** | BK7258 确实是 **M33F（带 FPU）**；`__NVIC_PRIO_BITS=3` | Beken `armstar.h` |
| **上游侵入面** | **2 个文件、约 15 行**；`arch/arm/cortex-m33/` **零改动** | `docs/06` §4、§5 |

---

## 文档

| 文件 | 内容 |
|---|---|
| [`docs/01-feasibility.md`](docs/01-feasibility.md) | 可行性判定与证据链 |
| [`docs/02-os-abstraction.md`](docs/02-os-abstraction.md) | Beken OS 抽象层剖析（本文档是核心） |
| [`docs/03-arch-board.md`](docs/03-arch-board.md) | XiUOS `arch/` 与 `board/` 现状与模板 |
| [`docs/04-shim-design.md`](docs/04-shim-design.md) | `bk_rtos/xizi/` 设计：95 函数映射表 |
| [`docs/05-open-questions.md`](docs/05-open-questions.md) | 未决项与风险 |
| [`docs/06-board-bk7258-checklist.md`](docs/06-board-bk7258-checklist.md) | **`board/bk7258/` 逐文件实现清单**（含实测内存映射、上游改动面、验收阶梯） |
| [`docs/07-build-environment.md`](docs/07-build-environment.md) | **构建环境与上游缺陷**（WSL 搭建、五个坑的现象/根因/修法、基线验证结果） |
| [`docs/08-keeping-tuya-stack.md`](docs/08-keeping-tuya-stack.md) | **换掉 RTOS 后涂鸦那套还保不保得住**（实测移植面、智能体编排为何不受影响、两套垫片） |
| [`docs/09-freertos-compat-layer.md`](docs/09-freertos-compat-layer.md) | **FreeRTOS API 兼容层设计**（路线 A，30 API 逐条映射 + 实现记录 + 四个坑） |

---

## 目录结构

```
xiuos-t5e1-port/
├── README.md                      本文件
├── docs/                          设计与分析文档
├── analysis/                      实测原始数据（nm 导出、扫描结果）
├── xiuos-overlay/                 XiUOS 侧新增/修改文件的 overlay
│   ├── arch/cortex-m33-notes/
│   └── board/bk7258/
├── beken-overlay/                 Beken 侧新增文件的 overlay
│   └── bk_rtos/xizi/
└── tools/                         分析脚本（可复现上述测量）
```

> **为什么用 overlay 而不是 submodule**：XiUOS 上游 604 MB、TuyaOpen SDK 亦数百 MB，
> 且上游 7 个月无提交、必然 fork 自维护。本项目只保存**我们自己的代码与补丁**，
> 两个上游按 `docs/` 里的说明单独获取。

---

## 状态

- 2026-09-16　完成可行性实测与架构判定。见 `docs/01` ~ `docs/05`。
- 2026-09-16　读完 `board/nuvoton-m2354/` 6 个核心文件 + 构建系统全链路，产出 `docs/06` 逐文件实现清单。
  实测结论：**上游只需改 2 个文件约 15 行**，`arch/arm/cortex-m33/` 零改动（影子头文件解耦 FSP）。
- 2026-09-16　搭好 WSL 构建环境，产出 `docs/07`。**基线板 `nuvoton-m2354` 构建成功**（`make` rc=0）。
- 2026-09-16　**`board/bk7258/` 骨架完成，验收阶梯 ①「链接通过」达成。**

  ```
  $ wsl -- bash tools/xizi-build.sh bk7258 --overlay
  make 退出码: 0
  ELF: build/XiZi-bk7258.elf  (1,305,836 字节)
     text    data     bss     dec     hex
   199780    3220   23100  226100   37334
  ```

  已核验（`tools/verify-bk7258.sh`）：

  | 检查项 | 结果 |
  |---|---|
  | 段布局 | `.text@0x02120000`（与实测 `bk7258_ap_out.ld` 的 `.vectors` 地址一致）、`.stack@0x28010000`、`.data@0x28014000`、`.bss@0x28014c98` |
  | 向量表 16 个系统异常 | 全部正确解析；保留位为 0 |
  | `PendSV_Handler_NS` | ✅ 就是带 `_NS` 后缀的那个名字（上游 `pendsv.S` 的定义） |
  | `__bss_end` | `0x280166D4` → **308 KiB 可用堆，占 AP_RAM 92%**，未越界到 CP 核的 `0x28063800` |
  | `DECLARE_HW_IRQ` | `__irq_desc_Bk7258Uart1Isr` 已落进 `.isrtbl` 段 |
  | 未定义符号 | **零** |

- 下一步：上板。把 `XiZi-bk7258.bin` 烧进 T5-E1，验收阶梯 ②「串口出 banner」→ ③「shell 可用」。
  这两步是纯 BSP 工作，**不需要碰 Beken 的闭源库**；④ 之后才要面对 `bk_rtos` 那 95 个函数。
- 2026-09-16　**决策：TKL 那层走路线 A（FreeRTOS API 兼容层），不改 Tuya 代码。**
  见 `docs/09`。已实现并核验：

  ```
  构建日志 error 行数: 0
  向量表 16 项不匹配: 0
  可用堆: 0x28016894..0x28063800 = 307 KiB (92% of AP_RAM)
  FreeRTOS API 兼容层: 33/33 全部链入
  未定义符号: 0
  ```

  实现过程中撞到四个坑，其中两个是**「构建成功」的假象**，值得单独记住：

  | 坑 | 现象 |
  |---|---|
  | 上游 `make` 吞掉子目录失败 | 顶层用 `for dir in ...; do $(MAKE) -C $$dir; done`，循环退出码取最后一条命令 → 中间目录编译失败仍返回 0 |
  | `--gc-sections` 回收整层 | 没有调用方时整层被回收，`make` 成功但一个符号都没进镜像 |
  | **常量折叠让链接校验失效** | 加了覆盖表后，`if (table[0] == NULL)` 被 GCC 折叠 → 对表的引用消失 → 表与它引用的几十个 API 一起被回收。索引必须走 `volatile` |
  | `KTaskCoreCombine` 只在 `ARCH_SMP` 下存在 | 单核构建下 undefined reference |

  **仍待验证**：运行时行为、ISR 变体安全性、超时精度。
  **与真实 TKL 源码的编译兼容已验证通过**（见下）。

- 2026-09-16　**用真实 TKL 源码验证兼容层：9/10 编译通过。**
  只编译不链接（`tools/tkl-compat-test.sh`），把 `tuyaos_adapter/src/system/` 下的
  真实 TKL 源码用 compat include 编译一遍 —— 编译通过即证明签名与调用方一致。
  脚本会核对实际命中的头文件路径，确认用的是我们的而不是 Beken 自带的。

  这次测试发现了三个**只看文档看不出来**的缺口，都已修：

  | 缺口 | 内容 |
  |---|---|
  | TKL 直接用 FreeRTOS 的**内部实现头** | `atomic.h`（7 个原子操作）、`projdefs.h`、`mpu_wrappers.h` —— 公开 API 之外还要提供这三个 |
  | 两个废弃别名不能省 | `portTICK_RATE_MS`（V10.4 后改名）、`xQueueHandle`（现名 `QueueHandle_t`） |
  | `tkl_sleep.c` 编译不过 | 缺的是 **Beken 电源管理**头链，与 FreeRTOS 无关；它本来就不在那 6 个用 FreeRTOS 的文件里，**明确不追** |

  补 `atomic.h` 时的取舍：没有搬 FreeRTOS 那套 port 原子宏，而是按本平台直接实现 ——
  AP 核在 XiZi 下是单核，所以「关中断 + 读改写」就够且正确。
  **若日后开 SMP 必须换成 LDREX/STREX**，这条写在了 `atomic.h` 文件头里。

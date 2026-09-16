# XiUOS → BK7258 (Tuya T5-E1) 移植

把 **XiUOS / XiZi 内核**移植到 **Beken BK7258**（= 涂鸦 T5-E1 模组）上，使其上层的
**闭源无线（Wi-Fi 6 / BLE）与音频库完全不用改**。

> 目标硬件：涂鸦 T5-E1 模组（Beken BK7258，Cortex-M33 / ARMv8-M Mainline @480MHz，双核 AP+CP）
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
| **XiUOS** | `arch/arm/cortex-m33/` | ✅ **上游已有**（8 个文件），检查时钟/tick/中断向量是否需微调 |
| **XiUOS** | `board/bk7258/` | ❌ **新建**，照 `board/nuvoton-m2354/`（独立 ARMv8-M MCU 模板） |
| **Beken** | `ap/components/bk_rtos/xizi/` | ❌ **新建**，照 `bk_rtos/non_os/`（无 OS 后端模板） |

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

---

## 文档

| 文件 | 内容 |
|---|---|
| [`docs/01-feasibility.md`](docs/01-feasibility.md) | 可行性判定与证据链 |
| [`docs/02-os-abstraction.md`](docs/02-os-abstraction.md) | Beken OS 抽象层剖析（本文档是核心） |
| [`docs/03-arch-board.md`](docs/03-arch-board.md) | XiUOS `arch/` 与 `board/` 现状与模板 |
| [`docs/04-shim-design.md`](docs/04-shim-design.md) | `bk_rtos/xizi/` 设计：95 函数映射表 |
| [`docs/05-open-questions.md`](docs/05-open-questions.md) | 未决项与风险 |

---

## 目录结构

```
xiuos-t5e1-port/
├── README.md                      本文件
├── docs/                          设计与分析文档
├── analysis/                      实测原始数据（nm 导出、扫描结果）
├── xiuz-overlay/                  XiUOS 侧新增/修改文件的 overlay
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
- 下一步：读 `board/nuvoton-m2354/` 的 6 个核心文件，产出 `board/bk7258/` 逐文件实现清单。

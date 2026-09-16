# 05 · 未决项与风险

**这一节里的任何一条，都可能独立地把整体风险重新顶高。**
在它们有答案之前，不要把工期当实数。

---

## ① SMP / 双核 —— 最大技术分叉点

BK7258 是 **AP + CP 双核**，且两核各自有独立的 `bk_rtos`：

```
ap/components/bk_rtos/     → FreeRTOS SMP v2.0
cp/components/bk_rtos/     → FreeRTOS v10
```

接口里有 5 个多核函数：`rtos_smp_create_thread` / `rtos_core0_create_thread` /
`rtos_core0_create_psram_thread` / `rtos_core1_create_thread` / `rtos_core1_create_psram_thread`。

**XiZi 大概率不支持 SMP。** 三条路：

| 方案 | 内容 | 风险 | 备注 |
|---|---|---|---|
| **A** | **只在 AP 核跑 XiZi，CP 核保留 FreeRTOS + 无线** | **低** | **优先验证**。闭源无线库全在 CP 核（`cp/components/bk_libs/bk7258/libs/`），CP 核本就是无线的家 |
| B | 两核都跑 XiZi，用 TrustZone 隔离无线 | 高 | `arch/arm/cortex-m33/trustzone.c` 有基础，但跨侧调用开销是新问题 |
| C | 单核化，放弃双核并行 | 中 | 性能损失，且与 SDK 的双核假设冲突 |

### 待查
- [ ] AP/CP 之间的 mailbox / IPC 依赖哪些 RTOS 原语？
      **若只依赖「队列 + 信号量」⇒ 方案 A 基本成立**
- [ ] `os_source/` 里的 FreeRTOS-Kernel 是 SMP 分支，`rtos_pub_smp.c` 有哪些 SMP 专属逻辑？
- [ ] 上游 `board/rzg2ul-m33/amp/` 的 `service_xiuos.h` / `shm.h` / `spinlock.h` 能否直接借鉴？

---

## ② XiZi 的原语缺口

需要逐条对照（读 XiZi `kernel/` 源码后填，不得猜）：

| 需要的原语 | XiZi 是否有 | 语义差 |
|---|---|---|
| 线程（创建/删除/挂起/恢复/join） | `?` | |
| 队列（含**插队** push_front） | `?` | |
| 信号量（计数型 + 初值） | `?` | |
| 互斥量（**优先级继承**？） | `?` | |
| **递归互斥量** | `?` | |
| **事件标志 EventFlags** | `?` | **最可能缺失** |
| 软件定时器（含 oneshot、改周期） | `?` | 回调上下文？ |
| 临界区（可嵌套？返回 flags？） | `?` | |
| tick / ms 换算 | `?` | |
| 堆（内部 SRAM + **PSRAM**） | `?` | PSRAM 堆是 BK7258 的必需项 |
| 中断上下文检测 | `?` | |

---

## ③ `port.c` 的可复用度未逐项分类

`non_os/port.c`（11,440 B）+ `portmacro.h`（11,579 B）里混着两类东西：

**芯片私有（应可复用）**
- `platform_is_in_interrupt_context()`
- `platform_cpsr_content()`
- `bk_fake_clock`
- 中断向量、上下文切换汇编

**OS 相关（必须重写）**
- 调度触发
- 临界区实现
- tick 推进

- [ ] 逐函数分类，产出「复用 / 重写」两栏清单

---

## ④ `link.lds` 与内存布局

BK7258 的内存布局比 nuvoton-m2354 复杂得多：

- 640 KB SRAM（另分 ITCM / DTCM / SPINLOCK 等区）
- PSRAM（多段：`PSRAM_SLAB_USER` / `AUDIO` / `ENCODE` / `DISPLAY` / `STACK_HEAP` / `HEAP`）
- 8 MB Flash（bl2 / cp / ap 分区 + OTA 双槽）

**且 AP / CP 两侧各有自己的链接脚本。**

> 参考：已有构建产物中的分区信息 —— bl2 32,480 B（limit 65,536）、cp 968,852 B（limit 1,048,576）、
> ap 1,843,200 B（limit 3,670,016）、OTA 1,705.0K / 2,940.0K。

- [ ] 从现有 `app.map` / `sdkconfig` / `auto_partitions.csv` 反推布局，写成 `board/bk7258/link.lds`

---

## ⑤ 构建系统合并

XiUOS 用 **Makefile + Kconfig**，Beken Armino 用 **CMake + Ninja**。

两者需要合并 —— 是让 XiZi 作为 Armino 的一个 component，还是把 Beken 的库当外部依赖链进 XiUOS 的 Makefile？

- [ ] 判定合并方式（这是一个独立的架构决策，可能影响上面所有工作量估计）

---

## ⑥ 上游停更

- gitlink master `0af75ee8` 提交于 **2026-02-05，距 2026-09-16 约 7 个月无新提交**
- 累计 2462 commits / 277 forks / 70 watchers / 671 issues / 598 PRs
- **fork 后自维护是必须计入的成本，不是可选项**

---

## ⑦ 历史伤疤（不可忽略）

同一产品线在**原厂 FreeRTOS 栈上**就已踩过：

- `2026-09-02 — BK7258 晚间故障链汇总与死机根因分析`
- `2026-09-02 — BK7258 自动配网抢占内置 Wi-Fi 修复`

**在替换 OS 之后，无线抢占与死机链会以新的形态重现。**
这两个案例应作为移植后的回归测试用例保留。

---

## ⑧ 与业务目标的关系（需澄清）

需求侧给的完成定义是「**Wi-Fi 能连上、能用涂鸦 App 连上**」。

**而这恰恰是最高风险项（无线）**，且它依赖上面 ①～⑤ 全部有答案。

> **建议将完成定义分层**：
> - **里程碑 1**：XiZi 内核在 BK7258 上启动 + 串口 shell + GPIO/UART 可用（不碰无线）
> - **里程碑 2**：`bk_rtos/xizi/` 全接口通过，SDK 能在 XiZi 上跑起来
> - **里程碑 3**：Wi-Fi 连上
> - **里程碑 4**：涂鸦 App 配网成功
>
> 把「Wi-Fi 能连」当作第 1 个 DoD 会把最高风险项放在路径最前端。

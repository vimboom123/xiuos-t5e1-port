# 08 — 换掉 RTOS 之后，涂鸦那套还保不保得住？

这是整个移植最该先问清楚的问题：**烧了 XiZi 之后，设备还能以 Tuya T5-E1 接入涂鸦云吗？
智能体编排还能用吗？**

结论：**能，而且代价比预想小得多。** 下面是实测出来的依据。

---

## 1. 设备身份不在 RTOS 里

涂鸦云看到的是 **PID + UUID/AUTHKEY**，那是**固件里的凭据**，不是运行时环境。
云侧不关心、也无法感知设备上跑的是 FreeRTOS 还是 XiZi。

已确认的事实：

| 项 | 值 |
|---|---|
| 产品 PID | `rcrnzif7ftahh6i8`（小乐），`moduleCategory = "T5-E1"` |
| firmwareKey | `keycmccshr45d5mx`（固件名 `Xalpha`） |
| 凭据 | 2 组 TuyaOpen UUID/AUTHKEY 在 `LICENSE.txt` 里（TuyaOpen 用 `TUYA_OPENSDK_UUID`，不是 TuyaOS 授权） |
| 已验证 | PID / UUID / AUTHKEY 各在 `all-app.bin` 里出现 1 次；demo PID 出现 0 次 |

所以「还能以 T5-E1 接入吗」的答案是**能** —— 只要固件还能把涂鸦的协议栈跑起来、还能说 DP 协议。

---

## 2. 真正的问题：谁在跟涂鸦云说话？

答案是 **TuyaOpen 自己的三层抽象**，不是 FreeRTOS：

```
TuyaOpen 应用层（开源 src/）
  tuya_cloud_service/   authorize cloud lan netcfg netmgr protocol schema tls transport
  tuya_ai_service/
  ai_components/                        ← 实测 0 处 FreeRTOS 调用
        ↓  只调用 TAL
TAL —— Tuya Abstraction Layer（src/tal_*）
  tal_system  tal_wifi  tal_network  tal_bluetooth  tal_driver  tal_kv ...
        ↓  只调用 TKL（tal_thread.c 里 include 的是 tkl_thread.h）
TKL —— Tuya Kernel Layer
        ↓  平台实现
platform/T5AI/tuyaos/tuyaos_adapter/src/system/
  tkl_thread.c  tkl_mutex.c  tkl_semaphore.c  tkl_queue.c
  tkl_system.c  tkl_sleep.c  tkl_task_notify.c  tkl_memory.c  tkl_atomic.c  tkl_output.c
        ↓  ★ 这一层直接调 FreeRTOS
Beken SDK（bk_rtos）+ 闭源库
```

**关键结论：TAL 及以上完全不碰 FreeRTOS。**

实测：`src/` + `boards/` 全树对 FreeRTOS 的依赖一共 **70 处**，其中

| 目录 | 调用 | 性质 |
|---|---|---|
| `src/liblvgl` | 43 | LVGL 的 FreeRTOS OSAL（`lv_freertos.c` 33 处）—— UI 库，不用就不编译 |
| `src/tal_kv/FlashDB/demos` | 14 | FlashDB 的 ESP32/ESP8266 **示例** |
| `boards/ESP32` | 8 | ESP32 的 LCD 驱动 |
| `src/libcjson` | 2 | cJSON 的 **unity 测试夹具** |
| **`src/tuya_cloud_service/tls/tuya_tls.c`** | **2** | 云连接层，仅此 2 处 |
| `src/tal_bluetooth/.../tuya_ble_os_adapter.c` | 1 | 蓝牙 OS 适配，本身就是设计给你替换的 adapter |

扣掉不编译的（LVGL / demo / 测试 / ESP32），**真实移植面只剩 3 处**。

---

## 3. 要动的其实是 TKL 系统层：6 个文件、47 处、30 个 API

`platform/T5AI/tuyaos/tuyaos_adapter/src/system/` 实测：

| 文件 | 大小 | FreeRTOS 调用 | 用到的 API |
|---|---|---|---|
| `tkl_thread.c` | 6,930 B | 11 | `xTaskCreate` `xTaskCreateInPsram` `xTaskCreatePinnedToCore` `vTaskDelete` `xTaskGetCurrentTaskHandle` `tskNO_AFFINITY` |
| `tkl_mutex.c` | 4,612 B | 9 | `xSemaphoreCreateMutex` `xSemaphoreCreateRecursiveMutex` `xSemaphoreTake/Give`(+Recursive, +FromISR) `vSemaphoreDelete` |
| `tkl_task_notify.c` | 3,328 B | 8 | `xTaskNotify` `xTaskNotifyFromISR` `xTaskNotifyGive` `ulTaskNotifyTake` `vTaskNotifyGiveFromISR` `xTaskNotifyStateClear` |
| `tkl_queue.c` | 3,732 B | 7 | `xQueueCreate` `xQueueSend`(+FromISR) `xQueueReceive` `uxQueueMessagesWaiting` |
| `tkl_semaphore.c` | 3,467 B | 6 | `xSemaphoreCreateCounting` `xSemaphoreTake/Give`(+FromISR) `vSemaphoreDelete` |
| `tkl_system.c` | 9,695 B | 6 | `xTaskCreate` `vTaskDelete` `vTaskDelay` `xTaskGetTickCount` `xPortGetFreeHeapSize` `xPortGetMinimumEverFreeHeapSize` |
| **合计** | **≈32 KB** | **47** | **30 个不同 API** |

**这就是「换掉 FreeRTOS 之后 Tuya 全栈还能不能跑」的全部工作面。**

注意这一层里**还混着 `bk_rtos`**：同一目录下的驱动适配器如 `tkl_spi.c` 用 `rtos_init_semaphore`、
`tkl_touch.c` 用 `rtos_create_thread`。所以两条路都要通。

---

## 4. 智能体编排：完全不受影响

三条独立理由：

1. **`ai_components` 实测 0 处 FreeRTOS 调用** —— 这一层本来就是干净的。
2. **编排跑在涂鸦云侧，不在设备上。** 已确认的云端对象：
   - 智能体项目 `aipt_foo8u1nbn85c`（`modelVersionId 1019708012`、`llm "275"`、`voiceId "122"`、`vadTime 500`）
   - 低延迟工作流 `12033`「小乐涂鸦单次语义低延迟-0914」，节点为
     `Start → MCP xiaole_context → LLM(model "262") → MCP xiaole_commit → End`，`isStream: 1`
   设备侧只负责**说 DP 协议 + 收发音频流**，编排逻辑一行都不在固件里。
3. **TAL 层零改动**，所以 `tuya_ai_service` 也不用动。

→ **智能体编排不需要为这次移植做任何事。** 前提只是设备最后能连上云（验收阶梯 ⑥）。

---

## 5. 所以要写两套垫片，而且都不大

| 垫片 | 服务对象 | 规模 | 依据 |
|---|---|---|---|
| `bk_rtos/xizi/` | 6 个闭源库 + 用 `rtos_*` 的驱动适配器 | **~95 个函数** | 闭源库只需 23 个符号（`analysis/closed-lib-rtos-symbols.txt`），其余是 SDK 其他部分在用 |
| TKL 系统层落 XiZi | `tuyaos_adapter/src/system/` 的 6 个文件 | **30 个 API / 47 处** | 本文 §3 |

### 两条实现路线

| 路线 | 做法 | 取舍 |
|---|---|---|
| **A. FreeRTOS API 兼容层** | 在 XiZi 上提供同名 `xTaskCreate` / `xSemaphoreTake` … | **不改 Tuya 任何代码**；但要精确复刻 FreeRTOS 语义（超时、优先级、ISR 变体），细节坑多 |
| **B. 重写 TKL 系统层**（推荐） | 把那 6 个文件改成直接调 XiZi 原语 | 不假装是 FreeRTOS，语义按 TKL 头文件实现即可，**更少、更正确**；代价是 fork 了 Tuya 的 6 个文件 |

**推荐 B。** 理由：这 6 个文件本来就是**薄适配器**（TKL 接口 → OS 原语），
在 TKL 这一层实现语义比在 FreeRTOS 那一层复刻语义要简单得多。
唯一需要留意的是 `tkl_task_notify.c` —— XiZi 没有与 FreeRTOS task notification 直接对应的原语，
要用事件或信号量模拟（一个任务一个通知槽的语义）。

**但这是验收阶梯 ④（调度器）之后的事。** ② 出 banner、③ 通 shell 都用不到。

---

## 6. 一个必须提前知道的现实

**烧进 XiZi 之后，设备在跑到验收阶梯 ⑥ 之前，是连不上涂鸦 App 的。**

因为：

- XiZi 取代了 AP 核上的 FreeRTOS
- TKL 系统层还没落到 XiZi 上（§5）
- 所以 `tal_*` → `tuya_cloud_service` 这条链暂时是断的

这不是 bug，是移植的中间状态。**当前的 TuyaOpen 固件一旦被覆盖，设备的涂鸦功能就暂停，
直到移植完成。** 如果这台 T5-E1 近期还要用来演示或测试涂鸦功能，
要么先备份 flash 以便随时回滚，要么用第二块板子做移植。

备份已有：`T5E1_full_flash_20260916_1642.bin`
（8,388,608 B，SHA-256 `BCBCC7EE…22`；同一备份目录下另有 `MANIFEST.txt` 记录出处）。

---

## 7. 一句话回答

| 问题 | 答案 |
|---|---|
| 烧进去后还能以 Tuya T5-E1 接入涂鸦吗？ | **能。** 身份在 PID + UUID/AUTHKEY 里，与 RTOS 无关 |
| 要改多少 Tuya 代码？ | `tuya_cloud_service` **2 处**、`tal_bluetooth` **1 处**，加上 TKL 系统层 6 个文件 |
| 智能体编排还能弄吗？ | **能，而且不用做任何事** —— 编排在云侧，`ai_components` 零 FreeRTOS 依赖 |
| 代价是什么？ | `bk_rtos/xizi/`（~95 函数）+ TKL 系统层（30 API）。都在阶梯 ④ 之后 |
| 中间状态？ | 烧了之后、到阶梯 ⑥ 之前，设备连不上涂鸦 App。先备份 flash |

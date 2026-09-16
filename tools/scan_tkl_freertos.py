#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
量化 T5AI 的 TKL 系统层（tuyaos_adapter/src/system/）对 FreeRTOS 的依赖面。

这一层是「XiZi 替代 FreeRTOS 之后，Tuya 云连接还保不保得住」的关键：
它上面是 TAL（tal_thread/tal_mutex/...），TAL 上面是 tuya_cloud_service 与
tuya_ai_service。只要把这 ~10 个文件搞定，上面的 Tuya 全栈不用动。
"""
import os
import re
import sys
from collections import Counter

# 用法: TUYAOPEN_SDK=<SDK根> python3 scan_tkl_freertos.py   或   python3 scan_tkl_freertos.py <SDK根>
_SDK = os.environ.get("TUYAOPEN_SDK") or (sys.argv[1] if len(sys.argv) > 1 else ".")
SYS = os.path.join(_SDK, "platform", "T5AI", "tuyaos", "tuyaos_adapter", "src", "system")

# 完整 FreeRTOS API 名表（尽量全）
API_RE = re.compile(
    r"\b(xTaskCreate\w*|vTaskDelete|xTaskGetTickCount|xTaskGetCurrentTaskHandle|"
    r"vTaskDelay\w*|vTaskSuspend|vTaskResume|vTaskPrioritySet|uxTaskPriorityGet|"
    r"xTaskGetSchedulerState|vTaskStartScheduler|taskYIELD|"
    r"taskENTER_CRITICAL\w*|taskEXIT_CRITICAL\w*|pcTaskGetName|"
    r"xTaskNotify\w*|ulTaskNotifyTake\w*|vTaskNotifyGiveFromISR|"
    r"xTimerCreate|xTimerStart|xTimerStop|xTimerDelete|xTimerChangePeriod|xTimerIsTimerActive|"
    r"xSemaphoreCreate\w*|xSemaphoreTake\w*|xSemaphoreGive\w*|vSemaphoreDelete|uxSemaphoreGetCount|"
    r"xQueueCreate\w*|xQueueSend\w*|xQueueReceive\w*|xQueuePeek\w*|uxQueueMessagesWaiting|xQueueReset|"
    r"xEventGroup\w+|vEventGroupDelete|"
    r"xStreamBuffer\w+|"
    r"pvPortMalloc|vPortFree|xPortGetFreeHeapSize|xPortGetMinimumEverFreeHeapSize|xPortGetHeapStats|"
    r"vPortDefineHeapRegions|"
    r"portENTER_CRITICAL\w*|portEXIT_CRITICAL\w*|portYIELD_FROM_ISR|"
    r"portSET_INTERRUPT_MASK_FROM_ISR|portCLEAR_INTERRUPT_MASK_FROM_ISR|"
    r"portTICK_PERIOD_MS|pdMS_TO_TICKS|tskNO_AFFINITY|"
    r"vTaskPlaceOnEventList|prvAddCurrentTaskToDelayedList)\b"
)

per_file = {}
all_api = Counter()
all_api_per_file = {}

for fn in sorted(os.listdir(SYS)):
    if not fn.endswith(".c"):
        continue
    p = os.path.join(SYS, fn)
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        txt = f.read()
    c = Counter(m.group(1) for m in API_RE.finditer(txt))
    if c:
        per_file[fn] = c
        all_api.update(c)
        all_api_per_file[fn] = c

print("=== TKL 系统层（%s）===" % SYS)
print()
print("%-24s %6s  %s" % ("文件", "调用数", "用到的 FreeRTOS API"))
print("-" * 100)
total = 0
for fn, c in sorted(per_file.items(), key=lambda kv: -sum(kv[1].values())):
    n = sum(c.values())
    total += n
    apis = ", ".join("%s×%d" % (a, k) for a, k in c.most_common())
    print("%-24s %6d  %s" % (fn, n, apis))

print("-" * 100)
print("%-24s %6d" % ("合计", total))
print()
print("=== 去重后的 API 清单（这就是最小垫片要实现的面）===")
for a, n in all_api.most_common():
    print("  %5d  %s" % (n, a))
print()
print("不同 API 种类: %d" % len(all_api))

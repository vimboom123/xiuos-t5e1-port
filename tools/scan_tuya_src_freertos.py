#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分析 TuyaOpen 自身的 src/ 层对 FreeRTOS 的依赖面。
目的：判断「换掉 AP 核的 RTOS 之后，Tuya 云连接与智能体接入还保不保得住」。

与 analysis/freertos-surface.txt 的区别：那份是全局扫描（含 FreeRTOS 自身源码，
噪声极大）；这份只看 TuyaOpen 自己的应用层源码。
"""
import os
import re
import sys
from collections import Counter, defaultdict

SDK = os.environ.get("TUYAOPEN_SDK") or (sys.argv[1] if len(sys.argv) > 1 else ".")
OUT = os.environ.get("OUT", "tuya-src-freertos-surface.txt")

# FreeRTOS API 名（小写匹配）
APIS = [
    "xTaskCreate","xTaskCreateStatic","vTaskDelete","vTaskDelay","vTaskDelayUntil",
    "xTaskGetTickCount","xTaskGetCurrentTaskHandle","vTaskSuspend","vTaskResume",
    "vTaskPrioritySet","uxTaskPriorityGet","xTaskGetSchedulerState","vTaskStartScheduler",
    "taskYIELD","taskENTER_CRITICAL","taskEXIT_CRITICAL","pcTaskGetName","xTaskNotify",
    "xTaskNotifyWait","ulTaskNotifyTake","ulTaskNotifyTakeIndexed","xTaskNotifyGive",
    "xTimerCreate","xTimerStart","xTimerStop","xTimerDelete","xTimerChangePeriod",
    "xSemaphoreCreateBinary","xSemaphoreCreateMutex","xSemaphoreCreateCounting",
    "xSemaphoreCreateRecursiveMutex","xSemaphoreTake","xSemaphoreGive",
    "xSemaphoreGiveFromISR","xSemaphoreTakeFromISR","vSemaphoreDelete",
    "xQueueCreate","xQueueCreateSet","xQueueSend","xQueueSendToBack","xQueueSendToFront",
    "xQueueReceive","xQueuePeek","xQueueSendFromISR","xQueueReceiveFromISR",
    "xQueueSelectFromSet","uxQueueMessagesWaiting","xQueueReset",
    "xEventGroupCreate","xEventGroupSetBits","xEventGroupClearBits","xEventGroupWaitBits",
    "xEventGroupSetBitsFromISR","vEventGroupDelete",
    "xStreamBufferSend","xStreamBufferReceive","xStreamBufferCreate","xStreamBufferReset",
    "pvPortMalloc","vPortFree","xPortGetFreeHeapSize","xPortGetMinimumEverFreeHeapSize",
    "portENTER_CRITICAL","portEXIT_CRITICAL","portYIELD_FROM_ISR",
    "portSET_INTERRUPT_MASK_FROM_ISR","portCLEAR_INTERRUPT_MASK_FROM_ISR",
    "vPortDefineHeapRegions","xPortGetHeapStats",
    "prvAddCurrentTaskToDelayedList","vTaskPlaceOnEventList",
]

lines = []
def emit(s=""):
    lines.append(s)
    print(s)

# 只扫 TuyaOpen 自己的源码（排除 platform/ 下的 Beken SDK 与 FreeRTOS 自身）
SCAN_DIRS = [
    ("src",            os.path.join(SDK, "src")),
    ("boards",         os.path.join(SDK, "boards")),
    ("src/ai_components", None),   # 后面按需补
]

api_re = re.compile(r"\b(" + "|".join(APIS) + r")\s*\(", re.IGNORECASE)

def scan(root, label):
    per_file = Counter()
    per_api = Counter()
    nfiles = 0
    if not root or not os.path.isdir(root):
        return per_file, per_api, nfiles
    for dirpath, dirnames, filenames in os.walk(root):
        if "platform" in dirpath.split(os.sep):
            continue
        for fn in filenames:
            if not fn.endswith((".c", ".h", ".cpp", ".cc")):
                continue
            p = os.path.join(dirpath, fn)
            try:
                with open(p, "r", encoding="utf-8", errors="replace") as f:
                    txt = f.read()
            except OSError:
                continue
            n = 0
            for m in api_re.finditer(txt):
                per_api[m.group(1).lower()] += 1
                n += 1
            if n:
                per_file[os.path.relpath(p, root)] = n
                nfiles += 1
    return per_file, per_api, nfiles

emit("=== TuyaOpen 自身源码对 FreeRTOS 的依赖面 ===")
emit("（排除 platform/ 下的 Beken SDK 与 FreeRTOS 自身源码）")
emit()

roots = [
    ("src", os.path.join(SDK, "src")),
    ("boards", os.path.join(SDK, "boards")),
]
grand_total = 0
for label, root in roots:
    pf, pa, nf = scan(root, label)
    tot = sum(pf.values())
    grand_total += tot
    emit("--- %s ---" % label)
    emit("  命中文件: %d   调用次数: %d" % (nf, tot))
    if pf:
        emit("  最密集的文件 top 15:")
        for f, n in pf.most_common(15):
            emit("    %4d  %s" % (n, f))
        emit("  用到的 API top 20:")
        for a, n in pa.most_common(20):
            emit("    %4d  %s" % (n, a))
    emit()

emit("=== 合计: %d 处 ===" % grand_total)
emit()

# 重点：云服务与 AI 组件
emit("=== 重点目录逐个看 ===")
for sub in ["src/tuya_cloud_service", "src/ai_components", "src/tal_kv",
            "src/tal_bluetooth", "src/tal_wifi", "src/tal_network",
            "src/libcjson", "src/liblvgl"]:
    p = os.path.join(SDK, sub.replace("/", os.sep))
    if not os.path.isdir(p):
        emit("  %-28s (不存在)" % sub)
        continue
    pf, pa, nf = scan(p, sub)
    tot = sum(pf.values())
    emit("  %-28s 文件命中 %3d  调用 %4d" % (sub, nf, tot))
    if pf:
        for f, n in pf.most_common(6):
            emit("        %4d  %s" % (n, f))

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
print("\n[写出] " + OUT)

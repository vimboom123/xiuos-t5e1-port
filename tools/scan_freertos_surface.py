#!/usr/bin/env python3
"""
扫描 TuyaOpen SDK 中 FreeRTOS API 的使用面。

产出: analysis/freertos-surface.txt（按目录分布 / API 频次 / 热点文件）

注意: 结果会被 FreeRTOS **自身源码**主导（os_source/ 与 FreeRTOS-Kernel/ 下的
      task.h / tasks.c / queue.c 等）。要做"真实移植面"判断，
      应结合 tools/nm_closed_libs.py 的未定义符号结果，而不是本脚本的原始计数。

用法:
    set TUYAOPEN_SDK=C:\\Users\\<you>\\TuyaOpenIDE\\TuyaOpenSDK
    python tools/scan_freertos_surface.py
"""
import os
import re
import collections
import sys

SDK = os.environ.get("TUYAOPEN_SDK") or os.path.expanduser("~/TuyaOpenIDE/TuyaOpenSDK")
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "analysis", "freertos-surface.txt")

PAT = re.compile(
    r'\b(xTaskCreate|xTaskCreatePinnedToCore|xTaskDelete|xTaskDelay|vTaskDelay|vTaskDelayUntil|'
    r'xTaskGetTickCount|xTaskGetCurrentTaskHandle|xTaskGetHandle|vTaskSuspend|vTaskResume|xTaskResume|'
    r'xQueueCreate|xQueueSend|xQueueSendToBack|xQueueSendToFront|xQueueReceive|xQueuePeek|xQueueDelete|'
    r'uxQueueMessagesWaiting|xQueueSendFromISR|xQueueReceiveFromISR|xQueueCreateSet|xQueueSelectFromSet|'
    r'xSemaphoreCreateBinary|xSemaphoreCreateMutex|xSemaphoreCreateCounting|xSemaphoreTake|xSemaphoreGive|'
    r'xSemaphoreGiveFromISR|xSemaphoreTakeFromISR|xSemaphoreCreateRecursiveMutex|vSemaphoreDelete|'
    r'xEventGroupCreate|xEventGroupSetBits|xEventGroupWaitBits|xEventGroupClearBits|xEventGroupDelete|'
    r'xTimerCreate|xTimerStart|xTimerStop|xTimerChangePeriod|xTimerDelete|vTimerSetTimerID|'
    r'xTaskNotify|xTaskNotifyGive|ulTaskNotifyTake|xTaskNotifyWait|vTaskNotifyGiveFromISR|'
    r'taskENTER_CRITICAL|taskEXIT_CRITICAL|taskDISABLE_INTERRUPTS|taskENABLE_INTERRUPTS|'
    r'portENTER_CRITICAL|portEXIT_CRITICAL|'
    r'pvPortMalloc|vPortFree|vPortDefineHeapRegions|xPortGetFreeHeapSize|'
    r'xPortGetMinimumEverFreeHeapSize|xTaskGetSchedulerState|vTaskStartScheduler|vTaskDelete|'
    r'xPortGetCoreID|portYIELD_FROM_ISR|portSET_INTERRUPT_MASK_FROM_ISR|'
    r'xStreamBufferCreate|xStreamBufferSend|xStreamBufferReceive|'
    r'xRingbufferCreate|xRingbufferSend|xRingbufferReceive|'
    r'vTaskSetApplicationTaskTag|xTaskGetApplicationTaskTag|'
    r'taskYIELD|portYIELD|vTaskPrioritySet|uxTaskPriorityGet|pcTaskGetName|'
    r'xPortInIsrContext|xTaskCreateStatic|xQueueCreateStatic|ulTaskNotifyTakeIndexed|'
    r'vTaskPlaceOnEventList|prvAddCurrentTaskToDelayedList)\s*\(', re.I)

SKIP = ("\\.build", "\\build\\", "\\.git", "\\.venv", "\\.tools", "\\build_tools", "\\.tmp_build")


def main():
    hits = collections.Counter()
    files = collections.Counter()
    perdir = collections.Counter()
    scanned = 0

    for root, _dirs, fs in os.walk(SDK):
        if any(s in root for s in SKIP):
            continue
        for f in fs:
            if not f.endswith((".c", ".h", ".cpp")):
                continue
            p = os.path.join(root, f)
            try:
                t = open(p, encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            scanned += 1
            found = PAT.findall(t)
            if not found:
                continue
            files[p] += len(found)
            rel = os.path.relpath(p, SDK)
            parts = rel.split(os.sep)
            perdir["/".join(parts[:2])] += len(found)
            for a in found:
                hits[a.lower()] += 1

    lines = [
        "扫描 .c/.h/.cpp 文件数: %d" % scanned,
        "命中 FreeRTOS API 的文件数: %d" % len(files),
        "API 调用总次数: %d" % sum(hits.values()),
        "不同 API 种类: %d" % len(hits),
        "",
        "=== 按 SDK 顶层目录分布（调用次数）===",
    ]
    for d, c in perdir.most_common(25):
        lines.append("  %-60s %6d" % (d, c))
    lines += ["", "=== API 使用频次 Top 60 ==="]
    for a, c in hits.most_common(60):
        lines.append("  %-42s %6d" % (a, c))
    lines += ["", "=== 调用最密集的 25 个文件 ==="]
    for p, c in files.most_common(25):
        lines.append("  %6d  %s" % (c, os.path.relpath(p, SDK)))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("apis=%d calls=%d files=%d -> %s"
          % (len(hits), sum(hits.values()), len(files), OUT))


if __name__ == "__main__":
    main()

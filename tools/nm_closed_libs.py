#!/usr/bin/env python3
"""
扫描 TuyaOpen SDK 里闭源静态库的未定义符号，过滤出 RTOS 相关项。

产出: analysis/closed-lib-rtos-symbols.txt

依赖: TuyaOpen SDK 自带工具链
      platform/tools/gcc-arm-none-eabi-*/bin/arm-none-eabi-nm(.exe)

用法:
    set TUYAOPEN_SDK=C:\\Users\\<you>\\TuyaOpenIDE\\TuyaOpenSDK
    python tools/nm_closed_libs.py
"""
import os
import re
import subprocess
import collections
import sys

SDK = os.environ.get("TUYAOPEN_SDK") or os.path.expanduser("~/TuyaOpenIDE/TuyaOpenSDK")
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "analysis", "closed-lib-rtos-symbols.txt")

# 关键闭源库（相对 SDK 根）
LIBS = [
    r"platform\T5AI\t5_os\cp\components\bk_libs\bk7258\libs\libwifi.a",
    r"platform\T5AI\t5_os\cp\components\bk_libs\bk7258\libs\libwifi_csi.a",
    r"platform\T5AI\t5_os\cp\components\bk_libs\bk7258\libs\libbluetooth_controller_dual.a",
    r"platform\T5AI\t5_os\cp\components\bk_libs\bk7258\libs\libbluetooth_host_dm_dual.a",
    r"platform\T5AI\t5_os\ap\components\bk_libs\bk7258_ap\libs\libbluetooth_host_dm_dual_ap.a",
    r"platform\T5AI\t5_os\ap\components\bk_libs\bk7258_ap\libs\libfdk_aac_enc.a",
]

RTOS = re.compile(
    r'^(x|v|u|pc|pv|prv|ux|ul|port|task|os_|rtos_|bk_rtos|rt_)[A-Za-z_]*'
    r'(Task|Queue|Semaphore|Timer|EventGroup|Notify|StreamBuffer|Ringbuffer|Critical|'
    r'Malloc|Free|Delay|Tick|Yield|Scheduler|Heap|Mutex|Isr|ISR|Thread|Mem|Sleep|'
    r'Suspend|Resume|Priority)', re.I)

ALSO = re.compile(
    r'^(pvPortMalloc|vPortFree|taskENTER_CRITICAL|taskEXIT_CRITICAL|'
    r'portENTER_CRITICAL|portEXIT_CRITICAL|xPortGetFreeHeapSize|'
    r'xPortGetMinimumEverFreeHeapSize|vTaskStartScheduler|taskYIELD|portYIELD|'
    r'xTaskGetTickCount|vTaskDelay|os_memset|os_memcpy|os_malloc_debug|os_free_debug|'
    r'rtos_|bk_rtos_)', re.I)


def find_nm():
    base = os.path.join(SDK, "platform", "tools")
    for root, _dirs, files in os.walk(base):
        for f in files:
            if f in ("arm-none-eabi-nm", "arm-none-eabi-nm.exe"):
                return os.path.join(root, f)
    return None


def main():
    nm = find_nm()
    if not nm:
        sys.exit("找不到 arm-none-eabi-nm，请确认 TUYAOPEN_SDK 指向正确")
    print("nm:", nm)

    lines = []
    all_rtos = collections.Counter()
    for rel in LIBS:
        p = os.path.join(SDK, rel)
        if not os.path.exists(p):
            lines.append("MISSING " + rel)
            print("MISSING", rel)
            continue
        r = subprocess.run([nm, "--undefined-only", p],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        syms = [ln.split()[-1] for ln in (r.stdout or "").splitlines() if len(ln.split()) >= 2]
        rtos = sorted({s for s in syms if RTOS.match(s) or ALSO.match(s)})
        for s in rtos:
            all_rtos[s] += 1
        lines.append("%-42s 未定义符号 %6d 个，其中 RTOS 相关 %3d 个"
                     % (os.path.basename(rel), len(syms), len(rtos)))
        print(lines[-1])

    lines.append("")
    lines.append("=== 闭源库需要的 RTOS 符号（并集，共 %d 个）===" % len(all_rtos))
    for s, c in all_rtos.most_common():
        lines.append("  %-46s 被 %d 个库引用" % (s, c))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines) + "\n")
    print("\nRTOS 符号并集: %d 个 -> %s" % (len(all_rtos), OUT))


if __name__ == "__main__":
    main()

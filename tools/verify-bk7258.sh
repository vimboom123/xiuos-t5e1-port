#!/bin/bash
# 核验 bk7258 镜像：内存边界、向量表、FreeRTOS 兼容层
#
# 注意：本脚本第 0 节的存在是有原因的 —— 上游顶层 Makefile:87 写的是
#     @for dir in $(SRC_DIR);do $(MAKE) -C $$dir; done
# shell 的 for 循环退出码取**最后一条命令**，所以中间任何子目录编译失败
# 都可能被吞掉，make 仍返回 0。只看产物不看日志，会得到「构建成功」的假象。
set -u
ROOT="$HOME/xiuos/Ubiquitous/XiZi_IIoT_Macro"
ELF="$ROOT/build/XiZi-bk7258.elf"
LOG=/tmp/xizi-build.log

[ -f "$ELF" ] || { echo "找不到 $ELF"; exit 1; }

echo "### 0. 构建日志错误数（不能只看 make 的退出码）###"
if [ -f "$LOG" ]; then
    n=$(grep -cE 'error:|Error [0-9]' "$LOG")
    echo "  /tmp/xizi-build.log 中 error 行数: $n"
    if [ "$n" -eq 0 ]; then
        echo "  OK —— 无编译/链接错误"
    else
        echo "  !! 有错误，产物不可信"
        grep -E 'error:' "$LOG" | head -10 | sed 's/^/    /'
    fi
else
    echo "  (没有 $LOG)"
fi

echo
echo "### 1. 段布局 ###"
arm-none-eabi-readelf -S -W "$ELF" | grep -E 'isr_vector|\.text |\.stack|\.data|\.bss|ARM.exidx' | sed 's/^/  /'

echo
echo "### 2. 向量表 16 个系统异常 ###"
python3 - "$ELF" <<'PY'
import re, subprocess, sys
elf = sys.argv[1]
nm = subprocess.run(["arm-none-eabi-nm","-n","--defined-only",elf],
                    capture_output=True, text=True).stdout
syms = {}
for ln in nm.splitlines():
    m = re.match(r"^([0-9a-fA-F]{8})\s+(\S)\s+(.+)$", ln.strip())
    if m and m.group(2).lower() in ("t","w"):
        syms.setdefault(int(m.group(1),16), m.group(3))

od = subprocess.run(["arm-none-eabi-objdump","-s","-j",".text",
                     "--start-address=0x02120000","--stop-address=0x02120040", elf],
                    capture_output=True, text=True).stdout
blob = ""
for ln in od.splitlines():
    m = re.match(r"^\s*([0-9a-fA-F]{4,8})\s+((?:[0-9a-fA-F]{2,8}\s+){1,4})", ln)
    if m:
        blob += m.group(2).replace(" ","")
words = [int.from_bytes(bytes.fromhex(blob[i:i+8]),"little") for i in range(0,len(blob)-7,8)]

# 第 14 项是 PendSV_Handler_NS —— 带 _NS 后缀。上游 arch/arm/cortex-m33/pendsv.S:44
# 导出的就是这个带后缀的名字（不是 PendSV_Handler）。
names = ["_sp","Reset_Handler","NMI_Handler","HardFault_Handler","MemManage_Handler",
         "BusFault_Handler","UsageFault_Handler","SecureFault_Handler","(保留)","(保留)",
         "(保留)","SVC_Handler","DebugMon_Handler","(保留)","PendSV_Handler_NS","SysTick_Handler"]
bad = 0
for i,w in enumerate(words[:16]):
    want = names[i]
    got = syms.get(w & ~1, "")
    if want.startswith("("):
        ok = "OK(0)" if w == 0 else "!! 期望 0"
        if w != 0: bad += 1
    elif want == "_sp":
        ok = "-> 0x%08X" % w
    else:
        ok = "OK" if got == want else "!! 期望 %s 实际 %s" % (want, got or hex(w))
        if got != want: bad += 1
    print("  [%2d] 0x%08X  %-20s %s" % (i, w, want, ok))
print()
print("  不匹配项: %d" % bad)
PY

echo
echo "### 3. 内存边界 ###"
python3 - "$ELF" <<'PY'
import re, subprocess, sys
elf = sys.argv[1]
nm = subprocess.run(["arm-none-eabi-nm","-n","--defined-only",elf],
                    capture_output=True, text=True).stdout
want = {}
for ln in nm.splitlines():
    m = re.match(r"^([0-9a-fA-F]{8})\s+(\S)\s+(.+)$", ln.strip())
    if m:
        want[m.group(3)] = int(m.group(1),16)

AP_BASE, AP_END = 0x28010000, 0x28063800
CP_BASE = 0x28063800
FLASH_BASE = 0x02120000
for name, lo, hi in [("_sp", AP_BASE, AP_END), ("__bss_end", AP_BASE, AP_END),
                     ("Reset_Handler", FLASH_BASE, FLASH_BASE+0x380000),
                     ("IsrEntry", FLASH_BASE, FLASH_BASE+0x380000)]:
    a = want.get(name)
    if a is None:
        print("  %-16s 缺失" % name); continue
    print("  %-16s 0x%08X  %s" % (name, a, "OK" if lo <= a < hi else "!! 越界"))

b = want.get("__bss_end")
if b:
    print()
    print("  可用堆: 0x%08X..0x%08X = %d KiB (%.0f%% of AP_RAM)"
          % (b, CP_BASE, (CP_BASE-b)//1024, 100.0*(CP_BASE-b)/(AP_END-AP_BASE)))
PY

echo
echo "### 4. FreeRTOS API 兼容层是否真的链进去了 ###"
python3 - "$ELF" <<'PY'
import re, subprocess, sys
elf = sys.argv[1]
nm = subprocess.run(["arm-none-eabi-nm","--defined-only",elf],
                    capture_output=True, text=True).stdout
syms = set()
for ln in nm.splitlines():
    m = re.match(r"^[0-9a-fA-F]{8}\s+\S\s+(.+)$", ln.strip())
    if m: syms.add(m.group(1))

groups = {
    "任务":   ["xTaskCreate","xTaskCreateInPsram","xTaskCreatePinnedToCore","vTaskDelete",
               "xTaskGetCurrentTaskHandle","xTaskGetTickCount","vTaskDelay"],
    "信号量": ["xSemaphoreCreateMutex","xSemaphoreCreateRecursiveMutex",
               "xSemaphoreCreateCounting","xSemaphoreCreateBinary",
               "xSemaphoreTake","xSemaphoreGive","xSemaphoreGiveFromISR","vSemaphoreDelete"],
    "队列":   ["xQueueCreate","xQueueSend","xQueueReceive","xQueueSendFromISR",
               "uxQueueMessagesWaiting"],
    "通知":   ["xTaskNotify","xTaskNotifyGive","vTaskNotifyGiveFromISR",
               "ulTaskNotifyTake","xTaskNotifyWait","xTaskNotifyStateClear"],
    "堆":     ["pvPortMalloc","vPortFree","xPortGetFreeHeapSize",
               "xPortGetMinimumEverFreeHeapSize"],
    "内部":   ["FreeRTOSCompatInit","frc_cb_check","frc_assert_failed"],
}
total = 0; missing = 0
for g, names in groups.items():
    hit = [n for n in names if n in syms]
    total += len(names); missing += len(names) - len(hit)
    tail = "" if len(hit)==len(names) else "  缺: " + ", ".join(n for n in names if n not in syms)
    print("  %-6s %2d/%2d%s" % (g, len(hit), len(names), tail))
print()
print("  合计 %d/%d，缺失 %d" % (total-missing, total, missing))
PY

echo
echo "### 5. 未定义符号（应为空）###"
u=$(arm-none-eabi-nm -u "$ELF" | wc -l)
echo "  nm -u 行数: $u"
if [ "$u" -eq 0 ]; then
    echo "  OK"
else
    arm-none-eabi-nm -u "$ELF" | head -10 | sed 's/^/    /'
fi

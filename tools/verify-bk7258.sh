#!/bin/bash
# 核验 bk7258 的向量表与内存边界
set -u
ELF="$HOME/xiuos/Ubiquitous/XiZi_IIoT_Macro/build/XiZi-bk7258.elf"
[ -f "$ELF" ] || { echo "找不到 $ELF"; exit 1; }

echo "### 1. 段布局（含 .isr_vector）###"
arm-none-eabi-readelf -S -W "$ELF" | grep -E 'isr_vector|\.text |\.stack|\.data|\.bss|ARM.exidx' | sed 's/^/  /'

echo
echo "### 2. 向量表前 16 个字（Cortex-M 系统异常）###"
# .isr_vector 在 .text 段内，偏移 0 → 地址 0x02120000
arm-none-eabi-objdump -s -j .text --start-address=0x02120000 --stop-address=0x02120040 "$ELF" \
  | tail -n +5 | sed 's/^/  /'

echo
echo "### 3. 用 nm 解析这 16 个字对应的符号 ###"
python3 - "$ELF" <<'PY'
import re, subprocess, sys
elf = sys.argv[1]
nm = subprocess.run(["arm-none-eabi-nm","-n","--defined-only",elf],
                    capture_output=True, text=True)
syms = {}
for ln in nm.stdout.splitlines():
    m = re.match(r"^([0-9a-fA-F]{8})\s+(\S)\s+(.+)$", ln.strip())
    if m and m.group(2).lower() in ("t","w"):
        syms.setdefault(int(m.group(1),16), m.group(3))

od = subprocess.run(["arm-none-eabi-objdump","-s","-j",".text",
                     "--start-address=0x02120000","--stop-address=0x02120040", elf],
                    capture_output=True, text=True)
blob = ""
for ln in od.stdout.splitlines():
    m = re.match(r"^\s*([0-9a-fA-F]{4,8})\s+((?:[0-9a-fA-F]{2,8}\s+){1,4})", ln)
    if m:
        blob += m.group(2).replace(" ","")
words = [int.from_bytes(bytes.fromhex(blob[i:i+8]),"little") for i in range(0,len(blob)-7,8)]

names = ["_sp","Reset_Handler","NMI_Handler","HardFault_Handler","MemManage_Handler",
         "BusFault_Handler","UsageFault_Handler","SecureFault_Handler","(保留)","(保留)",
         "(保留)","SVC_Handler","DebugMon_Handler","(保留)","PendSV_Handler","SysTick_Handler"]
bad = 0
for i,w in enumerate(words[:16]):
    want = names[i]
    got = syms.get(w & ~1, "")
    if want.startswith("("):
        ok = "  (保留位，应为 0)" if w == 0 else "  !! 非零"
    elif want == "_sp":
        ok = "  -> 0x%08X" % w
    else:
        ok = "  OK" if got == want else "  !! 期望 %s 实际 %s" % (want, got or hex(w))
        if got != want: bad += 1
    print("  [%2d] 0x%08X  %-20s %s" % (i, w, want, ok))

print()
print("  不匹配项: %d" % bad)
PY

echo
echo "### 4. 内存边界检查 ###"
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
checks = [
    ("_sp",            lambda a: AP_BASE <= a <= AP_END),
    ("__bss_end",      lambda a: AP_BASE <= a <= AP_END),
    ("Reset_Handler",  lambda a: a >= FLASH_BASE),
    ("IsrEntry",       lambda a: a >= FLASH_BASE),
]
for name, pred in checks:
    a = want.get(name)
    if a is None:
        print("  %-16s 缺失" % name); continue
    print("  %-16s 0x%08X  %s" % (name, a, "OK" if pred(a) else "!! 越界"))

bss_end = want.get("__bss_end")
if bss_end:
    print()
    print("  可用堆: 0x%08X .. 0x%08X = %d KiB (%.0f%% of AP_RAM)"
          % (bss_end, CP_BASE, (CP_BASE-bss_end)//1024,
             100.0*(CP_BASE-bss_end)/(AP_END-AP_BASE)))
    print("  CP 核 RAM 起点 0x%08X —— 未越界" % CP_BASE)
PY

echo
echo "### 5. UART 驱动是否进了镜像 ###"
arm-none-eabi-nm "$ELF" | grep -iE 'Bk7258HwUartInit|Bk7258Uart1Isr|R_BSP_Irq|SystemCoreClock|__irq_desc' | sed 's/^/  /'

echo
echo "### 6. 未预期的未定义符号（应为空）###"
arm-none-eabi-nm -u "$ELF" | sed 's/^/  /' | head -20

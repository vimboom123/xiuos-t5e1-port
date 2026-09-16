#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""向 XiZi letter-shell 发几条命令，验证 UART0 收发两个方向。
用法: python shell_probe.py [COM4] [命令1 命令2 ...]
"""
import sys
import time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
cmds = sys.argv[2:] or ['', 'help', 'ShowTask', 'ShowMemory']

s = serial.Serial(port, 115200, timeout=0.3)
s.dtr = False
s.rts = False
s.reset_input_buffer()
out = []
for c in cmds:
    for ch in c + '\r':            # 逐字发，给 1 字节 FIFO 阈值的中断留时间
        s.write(ch.encode())
        time.sleep(0.02)
    t0 = time.time()
    buf = b''
    while time.time() - t0 < 2.0:
        buf += s.read(4096)
    out.append('===== > %r =====\n%s' % (c, buf.decode('utf-8', 'replace')))
s.close()
text = '\n'.join(out)
open(r'D:\ds\xizi-shell-probe.txt', 'w', encoding='utf-8').write(text)
print(text[:4000])

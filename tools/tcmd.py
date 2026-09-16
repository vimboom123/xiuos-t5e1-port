#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""向 TuyaOpen 固件的 tuya> 命令行（UART0 / COM4 / 115200）发一条命令并收集输出。
用法: python tcmd.py <秒数> <命令...>
输出同时追加到 D:\\ds\\tuya\\xiaoxiu-pairing.log
"""
import re
import sys
import time
import serial

wait = float(sys.argv[1])
cmd = ' '.join(sys.argv[2:])

s = serial.Serial('COM4', 115200, timeout=0.2)
s.dtr = False
s.rts = False
s.reset_input_buffer()
for ch in cmd + '\r\n':
    s.write(ch.encode())
    time.sleep(0.01)

t_end = time.time() + wait
buf = b''
while time.time() < t_end:
    buf += s.read(4096)
s.close()

text = re.sub(r'\x1b\[[0-9;]*m', '', buf.decode('utf-8', 'replace'))
lines = [l.rstrip() for l in text.splitlines() if l.strip()]
stamp = time.strftime('%H:%M:%S')
with open(r'D:\ds\tuya\xiaoxiu-pairing.log', 'a', encoding='utf-8') as f:
    f.write('%s >>> %s\n' % (stamp, cmd))
    for l in lines:
        f.write('%s %s\n' % (stamp, l))
for l in lines:
    if 'feed watchdog' in l or 'Free heap size' in l:
        continue
    print(l)

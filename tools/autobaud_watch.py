#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""轮询波特率监听串口：收到成段可读文本就锁定该波特率并持续打印。
用法: python autobaud_watch.py [COM4] [总秒数]
"""
import sys, time, serial

port = sys.argv[1] if len(sys.argv) > 1 else 'COM4'
total = int(sys.argv[2]) if len(sys.argv) > 2 else 240
bauds = [460800, 115200, 921600]
t_end = time.time() + total
locked = None
out = open(r'D:\ds\autobaud-%s.log' % port, 'ab')
print('轮询 %s 波特率 %s，共 %d 秒' % (port, bauds, total), flush=True)

i = 0
while time.time() < t_end:
    baud = locked or bauds[i % len(bauds)]
    i += 1
    try:
        s = serial.Serial(port, baud, timeout=0.2)
    except Exception as e:
        print('打开 %s 失败: %s（1 秒后重试）' % (port, e), flush=True)
        time.sleep(1)
        continue
    s.dtr = False
    s.rts = False
    buf = bytearray()
    t0 = time.time()
    dwell = 30 if locked else 4
    try:
        while time.time() - t0 < dwell and time.time() < t_end:
            buf += s.read(4096)
            if locked and b'\n' in buf:
                *lines, rest = bytes(buf).split(b'\n')
                for ln in lines:
                    if ln.strip():
                        print('  | ' + ln.decode('utf-8', 'replace').rstrip()[:200], flush=True)
                out.write(bytes(buf[:len(buf) - len(rest)]))
                buf = bytearray(rest)
    except Exception as e:
        print('读取出错: %s' % e, flush=True)
    finally:
        s.close()
    if locked or not buf:
        continue
    ok = sum(1 for b in buf if 32 <= b < 127 or b in (9, 10, 13)) / len(buf)
    print('@%d: %d 字节, 可读 %.0f%%' % (baud, len(buf), ok * 100), flush=True)
    out.write(bytes(buf))
    if len(buf) >= 40 and ok >= 0.8:
        locked = baud
        print('*** 锁定 %d ***' % baud, flush=True)
        for ln in bytes(buf).split(b'\n')[:15]:
            if ln.strip():
                print('  | ' + ln.decode('utf-8', 'replace').rstrip()[:200], flush=True)
print('结束（锁定波特率: %s）' % locked, flush=True)

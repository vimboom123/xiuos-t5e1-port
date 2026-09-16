#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""连续 3 次 sys_reboot（每次在开机 5 秒计数窗口内），触发 reset_netcfg 清掉配网信息。"""
import re
import time
import serial

LOG = r'D:\ds\tuya\xiaoxiu-pairing.log'
s = serial.Serial('COM4', 115200, timeout=0.1)
s.dtr = False
s.rts = False
log = open(LOG, 'a', encoding='utf-8')


def send(cmd):
    for ch in cmd + '\r\n':
        s.write(ch.encode())
        time.sleep(0.01)
    log.write('%s >>> %s\n' % (time.strftime('%H:%M:%S'), cmd))


def pump(until, timeout):
    """读串口直到出现 until（正则）或超时，返回是否命中。"""
    buf = ''
    t_end = time.time() + timeout
    while time.time() < t_end:
        d = s.read(4096)
        if not d:
            continue
        t = re.sub(r'\x1b\[[0-9;]*m', '', d.decode('utf-8', 'replace'))
        buf += t
        for ln in t.splitlines():
            if ln.strip():
                log.write('%s %s\n' % (time.strftime('%H:%M:%S'), ln.rstrip()))
        if until and re.search(until, buf):
            return True, buf
    return False, buf


for n in range(1, 4):
    send('sys_reboot')
    ok, _ = pump(r'reset count write %d' % n if n > 1 else r'reset count write', 8)
    print('第 %d 次重启 -> 计数写入%s' % (n, '成功' if ok else '未见'))
    if n < 3:
        # CLI 线程起来后再发下一条（仍在 5 秒窗口内）
        pump(r'Thread:cli Exec Start|tuya>', 2)
        time.sleep(0.3)

ok, buf = pump(r'Reset ctrl data|go activation mode|BIND_START', 25)
for key in ['Reset ctrl data', 'go activation mode', 'TUYA_EVENT_BIND_START', 'ble adv updated', 'reset count is']:
    print('%-24s %s' % (key, '有' if key in buf else '—'))
s.close()
log.close()

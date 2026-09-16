#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BK7258 / T5-E1 flash 的 CRC 块编解码。

BK7258 的 flash 物理布局是每 32 字节数据后跟 2 字节 CRC16（大端，
poly 0x8005 / init 0xFFFF / 不反射，即 CRC-16/CMS），CPU 看到的是去掉
CRC 后的逻辑地址：逻辑 = 物理 / 34 * 32。
所以 AP 分区物理 0x132000 对应 CPU 地址 0x02120000。

链接到 0x02120000 的裸 bin 必须先 encode 再按物理地址烧写，
否则 CPU 从第 33 字节起就读错位，向量表和代码全乱。

用法:
  python bk_crc.py encode in.bin out.bin
  python bk_crc.py decode in.bin out.bin
  python bk_crc.py selftest <flash_dump.bin> [phys_off] [length]
  python bk_crc.py check in.bin          # 检查一个物理镜像每块 CRC 是否正确
"""
import sys

BLOCK = 32


def _table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
        t.append(c)
    return t


_T = _table()


def crc16(data):
    c = 0xFFFF
    for b in data:
        c = ((c << 8) & 0xFFFF) ^ _T[((c >> 8) ^ b) & 0xFF]
    return c


def encode(raw, pad=b"\xff"):
    if len(raw) % BLOCK:
        raw = raw + pad * (BLOCK - len(raw) % BLOCK)
    out = bytearray()
    for i in range(0, len(raw), BLOCK):
        blk = raw[i:i + BLOCK]
        out += blk + crc16(blk).to_bytes(2, "big")
    return bytes(out)


def decode(phys, strict=True):
    if len(phys) % (BLOCK + 2):
        raise ValueError("物理镜像长度 %d 不是 34 的整数倍" % len(phys))
    out = bytearray()
    bad = []
    for i in range(0, len(phys), BLOCK + 2):
        blk = phys[i:i + BLOCK]
        if crc16(blk) != int.from_bytes(phys[i + BLOCK:i + BLOCK + 2], "big"):
            bad.append(i)
        out += blk
    if strict and bad:
        raise ValueError("%d 个块 CRC 不对，第一个在物理偏移 0x%X" % (len(bad), bad[0]))
    return bytes(out), bad


def phys_to_virt(p):
    return p // (BLOCK + 2) * BLOCK


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "encode":
        raw = open(argv[2], "rb").read()
        enc = encode(raw)
        open(argv[3], "wb").write(enc)
        print("encode: %d -> %d 字节 (%d 块)" % (len(raw), len(enc), len(enc) // 34))
    elif cmd == "decode":
        raw, _ = decode(open(argv[2], "rb").read())
        open(argv[3], "wb").write(raw)
        print("decode: -> %d 字节" % len(raw))
    elif cmd == "check":
        phys = open(argv[2], "rb").read()
        n = len(phys) // 34 * 34
        _, bad = decode(phys[:n], strict=False)
        print("check: %d 块, CRC 错 %d 块%s" % (n // 34, len(bad),
              ("，首个 @0x%X" % bad[0]) if bad else ""))
        return 1 if bad else 0
    elif cmd == "selftest":
        dump = open(argv[2], "rb").read()
        off = int(argv[3], 0) if len(argv) > 3 else 0x132000
        ln = int(argv[4], 0) if len(argv) > 4 else 0x34000
        ln -= ln % (BLOCK + 2)
        phys = dump[off:off + ln]
        raw, _ = decode(phys)
        ok = encode(raw) == phys
        print("selftest @0x%X len 0x%X: 解码 %d 字节, 重新编码逐字节一致 = %s, CPU 地址 0x%08X"
              % (off, ln, len(raw), ok, 0x02000000 + phys_to_virt(off)))
        return 0 if ok else 1
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

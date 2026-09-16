#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
fix_upstream.py —— 修 XiUOS 上游两个与本移植无关、但会挡住编译的缺陷。

用法:
    python3 fix_upstream.py <XiUOS根目录>

两处都是**上游自身的 bug**，不是我们引入的，也不改任何行为：

【缺陷 1】arch/arm/cortex-m{23,33}/prepare_ahwstack.c 缺 #include <stdint.h>
    m23 的 include 块（6 行）：
        xs_base.h / xs_ktask.h / xs_assign.h / svc_handle.h / board.h / shell.h
    m33 的 include 块（7 行）：同上 + xizi.h
    而作为参考副本的 arch/arm/shared/prepare_ahwstack.c 第 21 行**有** <stdint.h>。
    该文件在第 390 行用了 uintptr_t，于是在 GCC 10.3 下报
        error: unknown type name 'uintptr_t'
    加一行 include 即可。幂等。

【缺陷 2】不是代码问题：kernel/Kconfig 里的 NAME_NUM_MAX / MEM_ALIGN_SIZE
    这两个符号在 kernel/Kconfig:267 与 :35 有定义（默认 32 / 8），
    但各板陈旧的 .defconfig 没带上它们，于是所有 kernel/include/*.h 都炸。
    修法不是改文件，而是在构建流程里补跑一次 kconfig 一致性推导：
        kconfig-conf --olddefconfig Kconfig
    那是上游 menuconfig 目标交互式做的事，这里非交互地做一遍。
    → 该步骤在 wsl-build-xizi.sh 里，不在本脚本内。
"""

import os
import sys

STDINT_ANCHOR = "#include <shell.h>"
STDINT_LINE = "#include <stdint.h>"
MARK = "/* xiuos-t5e1-port: 补 <stdint.h>，见 tools/fix_upstream.py */"

TARGETS = [
    os.path.join("arch", "arm", "cortex-m23", "prepare_ahwstack.c"),
    os.path.join("arch", "arm", "cortex-m33", "prepare_ahwstack.c"),
]


def fix_stdint(root):
    changed = 0
    for rel in TARGETS:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            print("  [跳过] %s 不存在" % rel)
            continue

        with open(path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()

        if "#include <stdint.h>" in text:
            print("  [已修] %s" % rel)
            continue

        if STDINT_ANCHOR not in text:
            print("  [失败] %s —— 找不到锚点 %r，上游结构可能变了" % (rel, STDINT_ANCHOR))
            continue

        text = text.replace(
            STDINT_ANCHOR,
            STDINT_ANCHOR + "\n" + MARK + "\n" + STDINT_LINE,
            1,
        )
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        print("  [已修] %s —— 补了 #include <stdint.h>" % rel)
        changed += 1
    return changed


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 2

    root = os.path.abspath(os.path.expanduser(sys.argv[1]))
    if not os.path.isdir(os.path.join(root, "arch")):
        print("错误：%s 看起来不是 XiUOS 的 XiZi_IIoT_Macro 根目录" % root)
        return 3

    print("=== 修复上游缺陷（XiUOS: %s）===" % root)
    n = fix_stdint(root)
    print("  本次改动 %d 个文件" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main())

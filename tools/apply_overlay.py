#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply_overlay.py —— 把 board/bk7258 装进 XiUOS 树，并打上必要的两处上游补丁。

用法:
    python3 apply_overlay.py <XiUOS根目录> <overlay源目录>

    <XiUOS根目录>    例如 ~/xizi（即 Ubiquitous/XiZi_IIoT_Macro 的内容）
    <overlay源目录>  本仓库的 xiuos-overlay 目录

设计要点：
  * **幂等**：重复跑不会重复插入补丁，也不会覆盖已有的 board/bk7258 之外的东西。
  * **只改两处上游文件**（这是整个移植对上游的全部侵入面，见 docs/06 §4）：
      1. arch/arm/Makefile   —— 加 4 行，让 CONFIG_BOARD_BK7258 选中 cortex-m33
      2. path_kernel.mk      —— 加一个板子的 include 块
  * 用标记式插入而不是 diff/patch：因为上游文件在不同 clone 上可能是 CRLF，
    patch 会因上下文不匹配失败，而标记插入只依赖锚点行。
"""

import os
import shutil
import sys

MARK_BEGIN = "# >>> xiuos-t5e1-port overlay >>>"
MARK_END = "# <<< xiuos-t5e1-port overlay <<<"

ARCH_MAKEFILE_PATCH = """
ifeq ($(CONFIG_BOARD_BK7258),y)
SRC_DIR += cortex-m33
endif
""".strip("\n")

# 注意：只加 cortex-m33，**不加 shared**。
# cortex-m33 目录里自带 arm32_switch.c / pendsv.S / prepare_ahwstack.c 的副本，
# 再加 shared 会 multiple definition（与 rzg2ul/rzv2l 的处理一致）。
ARCH_ANCHOR = "include $(KERNEL_ROOT)/compiler.mk"

PATH_KERNEL_PATCH = """
ifeq ($(BSP_ROOT),$(KERNEL_ROOT)/board/bk7258)
KERNELPATHS += \\
\t-I$(KERNEL_ROOT)/arch/arm/cortex-m33 \\
\t-I$(BSP_ROOT) \\
\t-I$(BSP_ROOT)/include \\
\t-I$(BSP_ROOT)/third_party_driver/include \\
\t-I$(BSP_ROOT)/third_party_driver/freertos_compat/include \\
\t-I$(BSP_ROOT)/third_party_driver/freertos_compat/src \\
\t-I$(KERNEL_ROOT)/include #
endif
""".strip("\n")

# 锚点选在 nuvoton 块之后、通用段之前
PATH_KERNEL_ANCHOR = "KERNELPATHS += -I$(KERNEL_ROOT)/arch \\"


def log(msg):
    print(msg)


def read_text(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def write_text(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def patch_file(path, patch, anchor, label):
    """在 anchor 之前插入 patch，带幂等标记。"""
    if not os.path.isfile(path):
        log("  [跳过] %s 不存在（%s）" % (path, label))
        return False

    text = read_text(path)

    if "CONFIG_BOARD_BK7258" in text or "board/bk7258" in text:
        log("  [已打] %s —— 检测到 bk7258 痕迹，跳过" % label)
        return False

    if anchor not in text:
        log("  [失败] %s —— 找不到锚点行: %r" % (label, anchor))
        log("         上游可能改过结构，需要人工看一下。")
        return False

    block = "%s\n%s\n%s\n" % (MARK_BEGIN, patch, MARK_END)
    text = text.replace(anchor, block + anchor, 1)
    write_text(path, text)
    log("  [已打] %s —— 插入了 %d 行" % (label, len(patch.splitlines())))
    return True


def sync_board(src_overlay, dst_root):
    """把 overlay 里的 board/bk7258 铺到目标树。"""
    src = os.path.join(src_overlay, "board", "bk7258")
    dst = os.path.join(dst_root, "board", "bk7258")

    if not os.path.isdir(src):
        log("  [失败] overlay 源目录不存在: %s" % src)
        return False

    # 每次都整目录覆盖：board/bk7258 是**我们的**目录，不是上游的，
    # 覆盖它是安全的，也能保证源改动一定生效。
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)

    n = sum(len(files) for _, _, files in os.walk(dst))
    log("  [已铺] board/bk7258 —— %d 个文件" % n)
    return True


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2

    dst_root = os.path.abspath(os.path.expanduser(sys.argv[1]))
    src_overlay = os.path.abspath(os.path.expanduser(sys.argv[2]))

    log("=== 应用 xiuos-t5e1-port overlay ===")
    log("  目标 XiUOS 树: %s" % dst_root)
    log("  overlay 源:    %s" % src_overlay)
    log("")

    if not os.path.isdir(os.path.join(dst_root, "board")):
        log("  错误：%s 看起来不是 XiUOS 的 XiZi_IIoT_Macro 根目录" % dst_root)
        return 3

    log("--- 1. 铺 board/bk7258 ---")
    if not sync_board(src_overlay, dst_root):
        return 4

    log("")
    log("--- 2. 打上游补丁（共两处）---")
    patch_file(
        os.path.join(dst_root, "arch", "arm", "Makefile"),
        ARCH_MAKEFILE_PATCH,
        ARCH_ANCHOR,
        "arch/arm/Makefile",
    )
    patch_file(
        os.path.join(dst_root, "path_kernel.mk"),
        PATH_KERNEL_PATCH,
        PATH_KERNEL_ANCHOR,
        "path_kernel.mk",
    )

    log("")
    log("=== 完成 ===")
    log("  上游侵入面：2 个文件，各一个标记块。")
    log("  回滚：删掉两个文件里 %s .. %s 之间的内容即可。" % (MARK_BEGIN, MARK_END))
    return 0


if __name__ == "__main__":
    sys.exit(main())

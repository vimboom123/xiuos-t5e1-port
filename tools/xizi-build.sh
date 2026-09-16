#!/bin/bash
# XiUOS 构建（WSL 原生盘）
# 用法: xizi-build.sh <board> [--no-sync] [--overlay]
#
# ============================================================================
# 踩过的五个坑，全部固化在流程里：
#
#  1. 【目录层级】必须复刻上游：
#        <repo>/Ubiquitous/XiZi_IIoT_Macro    <- KERNEL_ROOT
#        <repo>/APP_Framework                 <- 与 Ubiquitous 平级
#     XiUOS 的 Kconfig 与 path_kernel.mk 大量引用 $(KERNEL_ROOT)/../../APP_Framework。
#     只拷 XiZi_IIoT_Macro 会让 kconfig 直接报
#        can't open file ".../../../APP_Framework/Kconfig"
#     而 Kconfig 解析失败 = 所有符号拿不到默认值 —— 这才是
#     NAME_NUM_MAX / MEM_ALIGN_SIZE undeclared 的真正根因。
#
#  2. 【CRLF】Windows 上 clone 的整棵树是 CRLF，扔进 WSL 会
#        ./tool/hosttools/xsconfig.sh: /bin/bash^M: bad interpreter
#     Makefile 里的 \r 也会污染每条命令。→ 步骤 2 剥 CR。
#
#  3. 【上游 bug】arch/arm/cortex-m{23,33}/prepare_ahwstack.c 缺 <stdint.h>
#     （参考副本 arch/arm/shared/ 那份是有的）。→ 步骤 3 调 fix_upstream.py。
#
#  4. 【陈旧 .defconfig】各板 .defconfig 缺 kernel/Kconfig 后加的符号。
#     上游靠交互式 menuconfig 补，这里用 kconfig-conf --olddefconfig。
#     → 步骤 4。前提是坑 1 已经修好，否则这一步会静默失败。
#
#  5. 【写死的工具链】各板 config.mk 把 CROSS_COMPILE 写成
#        /opt/gcc-arm-none-eabi-6-2017-q1-update/bin/
#     它们用的是 `export CROSS_COMPILE ?=`，而 ?= 不覆盖已有环境变量，
#     所以导出 arm-none-eabi- 就能顶掉。
# ============================================================================
set -u

REPO_SRC=/mnt/d/ds/xiuos
REPO=$HOME/xiuos
DST=$REPO/Ubiquitous/XiZi_IIoT_Macro
TOOLS=/mnt/e/xiuos-t5e1-port

BOARD=${1:?用法: $0 <board> [--no-sync] [--overlay]}
shift || true
SYNC=""
OVERLAY=""
for a in "$@"; do
    case "$a" in
        --no-sync) SYNC="--no-sync" ;;
        --overlay) OVERLAY="yes" ;;
    esac
done

echo "=== [1/6] 同步 XiUOS 到 WSL 原生盘 ==="
if [ "$SYNC" != "--no-sync" ]; then
    mkdir -p "$DST" "$REPO/APP_Framework"
    rsync -a --delete --exclude 'build/' \
        "$REPO_SRC/Ubiquitous/XiZi_IIoT_Macro/" "$DST/" \
        || { echo "  rsync XiZi_IIoT_Macro 失败"; exit 2; }
    rsync -a --delete "$REPO_SRC/APP_Framework/" "$REPO/APP_Framework/" \
        || { echo "  rsync APP_Framework 失败"; exit 2; }
else
    echo "  跳过同步（--no-sync）"
fi
echo "  KERNEL_ROOT:   $DST  ($(du -sh "$DST" 2>/dev/null | cut -f1))"
echo "  APP_Framework: $REPO/APP_Framework  ($(du -sh "$REPO/APP_Framework" 2>/dev/null | cut -f1))"

cd "$DST" || exit 2

echo
echo "=== [2/6] 剥 CRLF（幂等：只处理含 CR 的文本文件）==="
# 两棵树都要剥：APP_Framework 的 Kconfig 也带 CR，会让 kconfig 报
#   warning: ignoring unsupported character ''
# 并进一步导致 source 解析失败。
strip_cr() {
    local n=0
    while IFS= read -r f; do
        sed -i 's/\r$//' "$f"
        n=$((n+1))
    done < <(grep -rlI $'\r' . 2>/dev/null)
    echo "$n"
}
n1=$(cd "$DST" && strip_cr)
n2=$(cd "$REPO/APP_Framework" && strip_cr)
echo "  XiZi_IIoT_Macro: $n1 个文件"
echo "  APP_Framework:   $n2 个文件"

cd "$DST" || exit 2

echo
echo "=== [3/6] 修上游缺陷 ==="
if [ -f "$TOOLS/tools/fix_upstream.py" ]; then
    python3 "$TOOLS/tools/fix_upstream.py" "$DST"
else
    echo "  警告：找不到 $TOOLS/tools/fix_upstream.py，跳过"
fi

if [ "$OVERLAY" = "yes" ]; then
    echo
    echo "=== [3b] 应用 board/bk7258 overlay + 上游补丁 ==="
    python3 "$TOOLS/tools/apply_overlay.py" "$DST" "$TOOLS/xiuos-overlay" || exit 5
fi

echo
echo "=== [4/6] 准备板级配置（板子=$BOARD）==="
if [ ! -f "board/$BOARD/.defconfig" ]; then
    echo "  错误：没有 board/$BOARD/.defconfig"
    exit 3
fi
cp "board/$BOARD/.defconfig" .config

export CROSS_COMPILE="${CROSS_COMPILE:-arm-none-eabi-}"
export BSP_ROOT="$DST/board/$BOARD"
export KERNEL_ROOT="$DST"
# 顶层 Makefile 里有 `export SRC_APP_DIR := ../../APP_Framework`，
# APP_Framework/Kconfig 靠 `option env="SRC_APP_DIR"` 取它来拼
#   $APP_DIR/Framework/Kconfig
# 不导出的话 $APP_DIR 退化成 "."，kconfig 就报
#   can't open file "./Framework/Kconfig"
export SRC_APP_DIR="../../APP_Framework"
chmod +x tool/hosttools/*.sh 2>/dev/null

echo "  kconfig 一致性推导（--olddefconfig）..."
kconfig-conf --olddefconfig "board/$BOARD/Kconfig" 2>/tmp/kconf.err
krc=$?
if [ $krc -ne 0 ]; then
    echo "  !! --olddefconfig 失败 (rc=$krc)："
    grep -vE 'unsupported character' /tmp/kconf.err | sed 's/^/     /' | head -5
    echo "     .config 未被补全，后面的 undeclared 错误多半源于此。"
else
    echo "    ok，.config 现在 $(wc -l < .config) 行（.defconfig 是 $(wc -l < "board/$BOARD/.defconfig") 行）"
fi
grep -E 'NAME_NUM_MAX|MEM_ALIGN_SIZE' .config | sed 's/^/    /' || echo "    (NAME_NUM_MAX / MEM_ALIGN_SIZE 仍缺失)"

./tool/hosttools/xsconfig.sh .config || { echo "xsconfig.sh 失败"; exit 4; }
echo "  xsconfig.h: $(wc -l < "$BSP_ROOT/xsconfig.h") 行"

# 【切板子必须清 build/】
# compiler.mk 会把每个 .o 对 xsconfig.h 的依赖写进 build/**.d，
# 而 xsconfig.h 是**板级**文件（board/<board>/xsconfig.h）。
# 换板子后旧 .d 还指着上一块板的路径，make 就报
#   No rule to make target '.../board/nuvoton-m2354/xsconfig.h'
# 注意症状具有误导性：它看起来像缺文件，其实是增量构建的残留。
LAST_BOARD_FILE="$DST/.last_board"
PREV_BOARD=$(cat "$LAST_BOARD_FILE" 2>/dev/null || echo "")
if [ "$PREV_BOARD" != "$BOARD" ]; then
    echo "  板子从 '${PREV_BOARD:-无}' 变成 '$BOARD'，清掉 build/"
    rm -rf build
    echo "$BOARD" > "$LAST_BOARD_FILE"
fi

echo
echo "=== [5/6] 构建 BOARD=$BOARD ==="
make BOARD="$BOARD" > /tmp/xizi-build.log 2>&1
rc=$?
echo "  make 退出码: $rc  （完整日志 /tmp/xizi-build.log，$(wc -l < /tmp/xizi-build.log) 行）"

echo
echo "=== [6/6] 结果 ==="
if [ $rc -ne 0 ]; then
    echo "  ---- 唯一错误清单（去重计数）----"
    grep -oE "error: [^;]*" /tmp/xizi-build.log | sort | uniq -c | sort -rn | head -20
    echo "  ---- undefined reference ----"
    grep -oE "undefined reference to \`[^']*'" /tmp/xizi-build.log | sort | uniq -c | sort -rn | head -20
    echo "  ---- 日志尾部 ----"
    tail -15 /tmp/xizi-build.log
else
    ELF="build/XiZi-$BOARD.elf"
    if [ -f "$ELF" ]; then
        echo "  ELF: $ELF  ($(stat -c%s "$ELF") 字节)"
        arm-none-eabi-size "$ELF"
        echo "  ---- 关键符号 ----"
        arm-none-eabi-nm "$ELF" | grep -E ' (Reset_Handler|PendSV_Handler_NS|SVC_Handler|SysTick_Handler|IsrEntry|_shell_command_start|g_service_table_start|__bss_end|_sp)$' | sed 's/^/    /'
        echo "  ---- 段布局 ----"
        arm-none-eabi-readelf -S -W "$ELF" | grep -E '\.isr_vector|\.text |\.stack|\.data|\.bss' | sed 's/^/    /'
    else
        echo "  make 成功但未产出 $ELF；build/ 内容："
        ls build/ 2>/dev/null | head -20
    fi
fi
echo "=== DONE rc=$rc ==="

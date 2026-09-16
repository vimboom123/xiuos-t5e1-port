#!/bin/bash
# 用**真实的 TKL 系统层源码**验证 FreeRTOS API 兼容层的签名兼容性。
#
# 只编译、不链接 —— 编译通过就足以证明「我声明的 30 个 API 与真实调用方的
# 用法一致」。链接需要整个 Beken SDK + TuyaOpen 的构建打通，那是下一步的事。
#
# 【关键】我的 compat include 必须排在 Beken 前面，因为：
#   - FreeRTOS.h / task.h / semphr.h / queue.h  Beken 自带一份（FreeRTOS-Kernel/include/freertos/）
#   - FreeRTOSConfig.h  Beken 有一份（bk_rtos/freertos/）
#   - portmacro.h       Beken 有一份（bk_rtos/non_os/）
# 这四份都必须被我的覆盖，否则测的就不是我的兼容层了。

set -u

SDK=/mnt/c/Users/leevi/TuyaOpenIDE/TuyaOpenSDK
AP=$SDK/platform/T5AI/t5_os/ap
TKL=$SDK/platform/T5AI/tuyaos/tuyaos_adapter
TKL_SRC=$TKL/src/system
COMPAT=/mnt/e/xiuos-t5e1-port/xiuos-overlay/board/bk7258/third_party_driver/freertos_compat

OUT=/tmp/tkl-compat
rm -rf "$OUT"; mkdir -p "$OUT"

# ---- include 路径 ----
# 顺序即优先级：compat 在最前
INC=""
INC="$INC -I$COMPAT/include"

# TKL 自己的头
INC="$INC -I$TKL/include/system"
INC="$INC -I$TKL/include/atomic"
INC="$INC -I$TKL/include/flash"
INC="$INC -I$TKL/include/gpio"
INC="$INC -I$TKL/include/ipc"
INC="$INC -I$TKL/include/wakeup"
INC="$INC -I$TKL/include/wifi"
INC="$INC -I$TKL/include/utilities/include"
INC="$INC -I$TKL/include"

# Beken SDK 的 include 根（照 armino 的布局）
INC="$INC -I$AP/include/common"
INC="$INC -I$AP/include/os"
INC="$INC -I$AP/include/driver"
INC="$INC -I$AP/include/driver/hal"
INC="$INC -I$AP/include/modules"
INC="$INC -I$AP/include/components"
INC="$INC -I$AP/include"
INC="$INC -I$AP/components/bk_libs/bk7258_ap/config"
INC="$INC -I$AP/components/bk_rtos/include"
INC="$INC -I$AP/middleware/driver"
INC="$INC -I$AP/middleware/driver/include"
INC="$INC -I$AP/middleware/driver/include/bk_private"
INC="$INC -I$AP/middleware/driver/pmu"
INC="$INC -I$AP/middleware/driver/uart"
INC="$INC -I$AP/middleware/driver/reset_reason"
INC="$INC -I$AP/middleware/soc/bk7258_ap"
INC="$INC -I$AP/middleware/soc/bk7258_ap/soc"
INC="$INC -I$AP/middleware/soc/bk7258_ap/hal"
INC="$INC -I$AP/middleware/arch/cm33"
INC="$INC -I$AP/middleware/arch/cm33/include"

# TuyaOpen 自己的 src/*/include —— 它的目录约定是每个模块一个 include 目录
# （tuya_iot_config.h 就在 src/common/include/ 下）。
# 与其一个个补，不如全加进来 —— 这也正是真实构建里 CMake 干的事。
for d in "$SDK"/src/*/include; do
    [ -d "$d" ] && INC="$INC -I$d"
done
for d in "$SDK"/src/*/include/*/; do
    [ -d "$d" ] && INC="$INC -I$d"
done
[ -d "$SDK/include" ] && INC="$INC -I$SDK/include"

# tuya_kconfig.h 是 TuyaOpen 构建时用 kconfiglib **生成**的（类似 XiUOS 的
# xsconfig.h），不在 SDK 源码里，而在工程的 .build/include 下。
# 用我们那个真实工程（xiaole）已经生成好的那一份，省得再跑一遍 kconfig。
PROJ=/mnt/c/Users/leevi/TuyaOpenIDE/projects/xiaole
if [ -d "$PROJ/source/embedded/.build/include" ]; then
    INC="$INC -I$PROJ/source/embedded/.build/include"
    echo "  (用工程已生成的 tuya_kconfig.h)"
else
    echo "  !! 找不到工程生成的 tuya_kconfig.h，编译会失败"
fi

CFLAGS="-mcpu=cortex-m33 -mthumb -mfloat-abi=soft -ffunction-sections -fdata-sections \
        -O0 -g -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
        -Dgcc -DCONFIG_SPE=1 -DCONFIG_SOC_BK7258=1 -DCONFIG_ARCH_CM33=1 \
        $INC"

TARGETS="tkl_thread.c tkl_mutex.c tkl_semaphore.c tkl_queue.c tkl_system.c tkl_task_notify.c tkl_sleep.c tkl_memory.c tkl_atomic.c tkl_output.c"

echo "=== 用真实 TKL 源码验证 FreeRTOS API 兼容层 ==="
echo "  compat: $COMPAT/include"
echo "  TKL   : $TKL_SRC"
echo

pass=0; fail=0
for f in $TARGETS; do
    src="$TKL_SRC/$f"
    if [ ! -f "$src" ]; then
        echo "  [--] $f 不存在"
        continue
    fi
    log="$OUT/$f.log"
    if arm-none-eabi-gcc $CFLAGS -c "$src" -o "$OUT/$f.o" > "$log" 2>&1; then
        echo "  [OK] $f"
        pass=$((pass+1))
    else
        n=$(grep -c 'error:' "$log")
        echo "  [!!] $f   ($n 个 error)"
        fail=$((fail+1))
    fi
done

echo
echo "=== 结果: $pass 通过 / $fail 失败 ==="

if [ $fail -gt 0 ]; then
    echo
    echo "=== 错误摘要（去重）==="
    cat "$OUT"/*.log 2>/dev/null | grep -oE "error: [^;]*" | sort | uniq -c | sort -rn | head -25
    echo
    echo "=== 首个失败文件的详细错误 ==="
    for f in $TARGETS; do
        log="$OUT/$f.log"
        [ -s "$log" ] && { echo "--- $f ---"; grep -E 'error:' "$log" | head -12; break; }
    done
else
    echo
    echo "=== 兼容层提供的头文件确实被用上了吗 ==="
    # 看 .d 依赖文件里 FreeRTOS.h / task.h 来自哪里
    for f in $TARGETS; do
        obj="$OUT/$f.o"
        [ -f "$obj" ] || continue
        arm-none-eabi-gcc $CFLAGS -MM "$TKL_SRC/$f" 2>/dev/null | tr ' ' '\n' \
            | grep -E 'FreeRTOS\.h|task\.h|semphr\.h|queue\.h|portmacro\.h|FreeRTOSConfig\.h' \
            | sort -u | sed 's/^/    /'
        break
    done
fi

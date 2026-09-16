# FreeRTOS API 兼容层
#
# 这不是 FreeRTOS 内核 —— 只提供 API 形状，实现在 frc_*.c 里，全部转调 XiZi 原语。
# 详见 docs/09-freertos-compat-layer.md
#
# 默认参与构建（而不是靠 Kconfig 开关）：这样每次 make 都会编译一遍，
# 编译错误当场暴露；同时也验证它不与 XiUOS 自身的头文件撞名。

SRC_FILES := src/frc_misc.c \
             src/frc_handle.c \
             src/frc_heap.c \
             src/frc_task.c \
             src/frc_sem.c \
             src/frc_queue.c \
             src/frc_notify.c

include $(KERNEL_ROOT)/compiler.mk

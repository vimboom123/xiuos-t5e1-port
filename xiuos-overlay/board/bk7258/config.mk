#
# board/bk7258/config.mk —— BK7258 (涂鸦 T5-E1) 板级构建配置
#
# 注意：这是**整个构建里最早被读的文件**（顶层 Makefile:31 在 Kconfig 之前就 include 它），
#       所以交叉工具链、-mcpu、链接脚本都必须在这里定下来。
#

# 交叉工具链。上游 nuvoton 写死了 /opt/gcc-arm-none-eabi-... 的绝对路径，
# 这里改成 ?= 以便用环境变量覆盖（WSL 里是 apt 装的 arm-none-eabi-gcc）。
export CROSS_COMPILE ?= arm-none-eabi-

# ---- CPU ----
# Cortex-M33 / ARMv8-M Mainline。
# 刻意不指定 -mfpu/-mfloat-abi，走软浮点：既避开「BK7258 是否带 FPU、
# arch 的上下文切换是否保存 S0-S31」这个未决问题，也让第一版更容易跑通。
export CPU_FLAGS := -mcpu=cortex-m33 -mthumb

export CFLAGS   := $(CPU_FLAGS) -ffunction-sections -fdata-sections -Dgcc \
                   -O0 -gdwarf-2 -g -fgnu89-inline
export AFLAGS   := -c $(CPU_FLAGS) -ffunction-sections -fdata-sections \
                   -x assembler-with-cpp -gdwarf-2
export CXXFLAGS := $(CPU_FLAGS) -ffunction-sections -fdata-sections -Dgcc \
                   -O0 -gdwarf-2 -g
export LFLAGS   := $(CPU_FLAGS) -ffunction-sections -fdata-sections \
                   -Wl,--gc-sections,-Map=XiZi-bk7258.map,-cref,-u,Reset_Handler \
                   -T $(BSP_ROOT)/link.lds

# ARCH_ARM_SECURE：BK7258 的 AP 核跑在安全态（TuyaOpen/原厂 sdkconfig 都是 CONFIG_SPE=1，
# 外设走不带 0x10000000 偏移的安全地址，向量表里有 SecureFault）。
# 不定义时 arch/arm/cortex-m33/prepare_ahwstack.c 给新任务填 EXC_RETURN=0xFFFFFFBC（返回非安全态），
# 第一次任务切换就进异常 —— 2026-09-16 v5 实测停在 XiUOSStartup 之后的故障现场。
# 上游没有对应的 Kconfig 符号，只能在这里定义。
export DEFINES := -DHAVE_CCONFIG_H -DARCH_ARM_SECURE

export ARCH = arm
export MCU  = cortex-m33

#
# ---- BK7258 AP 核分区常量（实测值，见 docs/06-board-bk7258-checklist.md §2）----
#
# 这些值来自 t5_os/build/bk7258/tuya_app/partitions/ram_regions.h 与真实链接脚本
# bk7258_ap_out.ld。做成可覆盖变量而不是写死在 link.lds 里，是因为
# CONFIG_AP_VIRTUAL_PARTITION_OFFSET 会随分区表变化。
#
export BK7258_FLASH_ORIGIN ?= 0x02120000
export BK7258_FLASH_LENGTH ?= 0x380000
export BK7258_SRAM_ORIGIN  ?= 0x28010000
export BK7258_SRAM_LENGTH  ?= 0x53800

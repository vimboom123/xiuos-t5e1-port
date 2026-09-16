SRC_DIR := common startup freertos_compat

ifeq ($(CONFIG_BSP_USING_UART),y)
  SRC_DIR += uart
endif

include $(KERNEL_ROOT)/compiler.mk

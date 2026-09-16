/*
 * mpu_wrappers.h —— FreeRTOS 的 MPU 包装层
 *
 * tkl_thread.c:17 直接 include 了它。
 *
 * 原版这个头的内容是：不开 MPU（configUSE_MPU_WRAPPERS_V1 == 0）时
 * 基本是空的，只提供几个宏；开了 MPU 才把 xTaskCreate 之类的调用
 * 重定向到 MPU 版本。
 *
 * 本平台不开 FreeRTOS 的 MPU（内存保护归 XiZi / Cortex-M33 自己的 MPU，
 * 见 bk7258_soc.h 里的 __MPU_PRESENT=1），所以这里给一个空实现即可 ——
 * 关键是不能让调用方因为找不到头而编译失败。
 */

#ifndef MPU_WRAPPERS_H
#define MPU_WRAPPERS_H

#include "FreeRTOS.h"

/* 不开 MPU 包装：这几个宏在原版里也是空定义 */
#ifndef portUSING_MPU_WRAPPERS
#define portUSING_MPU_WRAPPERS 0
#endif

#define portPRIVILEGE_BIT       0x00

#endif /* MPU_WRAPPERS_H */

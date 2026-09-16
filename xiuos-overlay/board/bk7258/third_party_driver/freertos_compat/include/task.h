/*
 * task.h —— 任务相关 API（FreeRTOS 形状，XiZi 实现）
 *
 * 覆盖 platform/T5AI/tuyaos/tuyaos_adapter/src/system/tkl_thread.c 与
 * tkl_system.c 实际用到的全部函数。签名的权威来源是 TuyaOpen 自带的那份
 * FreeRTOS-Kernel/include/freertos/task.h（SMP v2.0）以及
 * include/additions/freertos_tasks_c_additions.h（Beken 私有扩展）。
 */

#ifndef INC_TASK_H
#define INC_TASK_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*TaskFunction_t)(void *);
typedef void *TaskHandle_t;

/* ==========================================================================
 * 创建 / 删除
 * ========================================================================== */

/* 标准创建。usStackDepth 的单位是**字**（4 字节），与 FreeRTOS 一致。
 * 实现里会乘 sizeof(StackType_t) 再交给 KTaskCreate（它要字节数）。 */
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void *const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *const pxCreatedTask);

/* Beken 私有扩展 1：指定栈分配在 PSRAM。
 * XiZi 第一版不区分内存域，行为等同 xTaskCreate。 */
BaseType_t xTaskCreateInPsram(TaskFunction_t pxTaskCode,
                              const char *const pcName,
                              const configSTACK_DEPTH_TYPE usStackDepth,
                              void *const pvParameters,
                              UBaseType_t uxPriority,
                              TaskHandle_t *const pxCreatedTask,
                              const BaseType_t xCoreID);

/* Beken 私有扩展 2：绑定到指定核。
 * 实现走 KTaskCreate + KTaskCoreCombine(xCoreID)；xCoreID == tskNO_AFFINITY
 * 时跳过 combine。 */
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *const pcName,
                                   const uint32_t usStackDepth,
                                   void *const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *const pxCreatedTask,
                                   const BaseType_t xCoreID);

/* 静态创建：XiZi 没有对应物，第一版直接转向 xTaskCreate 并忽略静态缓冲。
 * TKL 未使用；保留是为了让调用方编译得过。 */
BaseType_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                             const char *const pcName,
                             const uint32_t ulStackDepth,
                             void *const pvParameters,
                             UBaseType_t uxPriority,
                             StackType_t *const puxStackBuffer,
                             void *const pxTaskBuffer,
                             TaskHandle_t *const pxCreatedTask);

void vTaskDelete(TaskHandle_t xTaskToDelete);
void vTaskStartScheduler(void);

/* ==========================================================================
 * 查询
 * ========================================================================== */
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskGetSchedulerState(void);
UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask);
eTaskState eTaskGetState(TaskHandle_t xTask);
char *pcTaskGetName(TaskHandle_t xTaskToQuery);
TaskHandle_t xTaskGetIdleTaskHandle(void);
TaskHandle_t xTaskGetHandle(const char *pcNameToQuery);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);

/* ==========================================================================
 * 控制
 * ========================================================================== */
void vTaskDelay(const TickType_t xTicksToDelay);
void vTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement);
void vTaskSuspend(TaskHandle_t xTaskToSuspend);
void vTaskResume(TaskHandle_t xTaskToResume);
BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume);
void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);
void taskYIELD(void);

/* ==========================================================================
 * 任务通知（XiZi 无对应原语，见 frc_notify.c 的模拟方案）
 * ========================================================================== */
BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction);
BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify);
void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken);
BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify,
                              uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken);
uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait);
BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait);
BaseType_t xTaskNotifyStateClear(TaskHandle_t xTask);

#ifdef __cplusplus
}
#endif

#endif /* INC_TASK_H */

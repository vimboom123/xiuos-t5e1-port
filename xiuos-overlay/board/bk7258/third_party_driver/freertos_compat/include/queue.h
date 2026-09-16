/*
 * queue.h —— 队列 API（FreeRTOS 形状，XiZi 的 KMsgQueue* 实现）
 *
 * 【注意参数顺序】
 *   FreeRTOS:  xQueueCreate(uxQueueLength, uxItemSize)   —— 先长度后元素
 *   XiZi:      KCreateMsgQueue(msg_size, max_msgs)       —— 先元素后长度
 * 实现在 frc_queue.c 里已按正确顺序转过去。
 */

#ifndef INC_QUEUE_H
#define INC_QUEUE_H

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *QueueHandle_t;
typedef void *QueueSetHandle_t;
typedef void *QueueSetMemberHandle_t;

/* 队列已满 / 空 的返回标记（xQueueReceive 等用不到，保留给调用方） */
#define queueQUEUE_IS_MUTEX     NULL

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);
QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength,
                                 UBaseType_t uxItemSize,
                                 uint8_t *pucQueueStorageBuffer,
                                 void *pxQueueBuffer);

BaseType_t xQueueSend(QueueHandle_t xQueue,
                      const void *pvItemToQueue,
                      TickType_t xTicksToWait);

BaseType_t xQueueSendToBack(QueueHandle_t xQueue,
                            const void *pvItemToQueue,
                            TickType_t xTicksToWait);

BaseType_t xQueueSendToFront(QueueHandle_t xQueue,
                             const void *pvItemToQueue,
                             TickType_t xTicksToWait);

BaseType_t xQueueReceive(QueueHandle_t xQueue,
                         void *pvBuffer,
                         TickType_t xTicksToWait);

BaseType_t xQueuePeek(QueueHandle_t xQueue,
                      void *pvBuffer,
                      TickType_t xTicksToWait);

BaseType_t xQueueSendFromISR(QueueHandle_t xQueue,
                             const void *pvItemToQueue,
                             BaseType_t *pxHigherPriorityTaskWoken);

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue,
                                void *pvBuffer,
                                BaseType_t *pxHigherPriorityTaskWoken);

BaseType_t xQueueReset(QueueHandle_t xQueue);
void vQueueDelete(QueueHandle_t xQueue);

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif

#endif /* INC_QUEUE_H */

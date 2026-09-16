/*
 * frc_queue.c —— 队列 API 转 XiZi
 *
 * 【参数顺序相反，别搞错】
 *   FreeRTOS:  xQueueCreate(uxQueueLength,  uxItemSize)   —— 先长度后元素
 *   XiZi:      KCreateMsgQueue(msg_size,    max_msgs)     —— 先元素后长度
 *
 * 【占用数自己记】
 * XiZi 的 struct MsgQueue 不暴露元素个数，所以 uxQueueMessagesWaiting 用
 * 控制块里的 count。这是完备的，因为**所有收发路径都经过本层** ——
 * TKL 不会绕过兼容层直接调 KMsgQueueSend。
 */

#include <string.h>

#include "frc_internal.h"
#include "FreeRTOS.h"
#include "queue.h"

/* 【包含顺序很重要】xizi.h 必须**最先**包含 —— 它按正确顺序拉进内核头。
 * XiUOS 上游有些头单独包含会失败：例如 kernel/include/xs_msg.h:34 的
 * `struct IdNode id;` 依赖 xs_id.h，而 xs_msg.h 自己并不 include 它。
 * 先包含 xizi.h 就绕开了这个上游的包含顺序陷阱。（下面的 xs_*.h 仍保留，
 * 因为有 include guard，重复包含是空操作，但读代码时能看清依赖了谁。） */
#include <xizi.h>

#include <xs_base.h>
#include <xs_msg.h>
#include <xs_ktick.h>

/* 复用 frc_sem.c 的换算逻辑，这里再写一遍是为了让本文件能独立编译 */
static int32 frc_ticks_to_ms(TickType_t ticks)
{
    if (ticks == portMAX_DELAY) {
        return WAITING_FOREVER;
    }
    if (ticks == 0) {
        return 0;
    }
    return (int32)CalculateTimeMsFromTick((x_ticks_t)ticks);
}

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    struct frc_cb *cb;

    if (uxQueueLength == 0 || uxItemSize == 0) {
        return NULL;
    }

    cb = frc_cb_alloc(FRC_KIND_QUEUE);
    if (cb == NULL) {
        return NULL;
    }

    /* 注意参数顺序：XiZi 是 (元素大小, 最大个数) */
    cb->id = KCreateMsgQueue((x_size_t)uxItemSize, (x_size_t)uxQueueLength);
    if (cb->id < 0) {
        frc_log("frc: KCreateMsgQueue(len=%u,item=%u) failed\n",
                (unsigned)uxQueueLength, (unsigned)uxItemSize);
        frc_cb_free(cb);
        return NULL;
    }

    cb->max       = (uint32)uxQueueLength;
    cb->count     = 0;
    cb->item_size = (uint32)uxItemSize;
    return (QueueHandle_t)cb;
}

QueueHandle_t xQueueCreateStatic(UBaseType_t uxQueueLength,
                                 UBaseType_t uxItemSize,
                                 uint8_t *pucQueueStorageBuffer,
                                 void *pxQueueBuffer)
{
    (void)pucQueueStorageBuffer;
    (void)pxQueueBuffer;
    return xQueueCreate(uxQueueLength, uxItemSize);
}

/* 内部共用的发送。urgent 为真走 XiZi 的「插队」发送。 */
static BaseType_t frc_queue_send(void *handle, const void *item, TickType_t ticks, int urgent)
{
    struct frc_cb *cb = frc_cb_check(handle, FRC_KIND_QUEUE);
    uint32_t primask;
    x_err_t ret;

    if (cb == NULL || item == NULL) {
        return pdFALSE;
    }

    primask = frc_critical_enter();
    if (cb->count >= cb->max) {
        frc_critical_exit(primask);
        return pdFALSE;     /* 队满，与 FreeRTOS 语义一致（非阻塞超时下） */
    }
    frc_critical_exit(primask);

    if (urgent) {
        ret = KMsgQueueUrgentSend(cb->id, item, (x_size_t)cb->item_size);
    } else {
        ret = KMsgQueueSendwait(cb->id, item, (x_size_t)cb->item_size,
                                frc_ticks_to_ms(ticks));
    }

    if (ret == EOK) {
        primask = frc_critical_enter();
        cb->count++;
        frc_critical_exit(primask);
        return pdTRUE;
    }
    return pdFALSE;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    return frc_queue_send(xQueue, pvItemToQueue, xTicksToWait, 0);
}

BaseType_t xQueueSendToBack(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    return frc_queue_send(xQueue, pvItemToQueue, xTicksToWait, 0);
}

BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue, TickType_t xTicksToWait)
{
    /* XiZi 的 UrgentSend 是**非阻塞**的，所以这里忽略 xTicksToWait —— 语义上
     * 略强于 FreeRTOS（不会等），但插队发送本来就不该等。 */
    (void)xTicksToWait;
    return frc_queue_send(xQueue, pvItemToQueue, 0, 1);
}

BaseType_t xQueueSendFromISR(QueueHandle_t xQueue,
                             const void *pvItemToQueue,
                             BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    /* ISR 里不能阻塞，用非阻塞版 */
    return frc_queue_send(xQueue, pvItemToQueue, 0, 0);
}

static BaseType_t frc_queue_receive(void *handle, void *buf, TickType_t ticks)
{
    struct frc_cb *cb = frc_cb_check(handle, FRC_KIND_QUEUE);
    uint32_t primask;
    x_err_t ret;

    if (cb == NULL || buf == NULL) {
        return pdFALSE;
    }

    ret = KMsgQueueRecv(cb->id, buf, (x_size_t)cb->item_size, frc_ticks_to_ms(ticks));
    if (ret == EOK) {
        primask = frc_critical_enter();
        if (cb->count > 0) {
            cb->count--;
        }
        frc_critical_exit(primask);
        return pdTRUE;
    }
    return pdFALSE;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    return frc_queue_receive(xQueue, pvBuffer, xTicksToWait);
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *pvBuffer, TickType_t xTicksToWait)
{
    /* XiZi 没有偷看接口。第一版退化为「收到再放回去」——对 TKL 未使用的路径，
     * 这样至少不是静默错值。 */
    struct frc_cb *cb = frc_cb_check(xQueue, FRC_KIND_QUEUE);
    uint8_t tmp[64];
    void *buf = pvBuffer;

    if (cb == NULL || pvBuffer == NULL) {
        return pdFALSE;
    }
    if (cb->item_size > sizeof(tmp)) {
        frc_log("frc: xQueuePeek item too large (%u)\n", (unsigned)cb->item_size);
        return pdFALSE;
    }

    (void)buf;
    if (frc_queue_receive(xQueue, tmp, xTicksToWait) != pdTRUE) {
        return pdFALSE;
    }
    memcpy(pvBuffer, tmp, cb->item_size);
    (void)frc_queue_send(xQueue, tmp, 0, 1);   /* 放回队首，保持顺序 */
    return pdTRUE;
}

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue,
                                void *pvBuffer,
                                BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return frc_queue_receive(xQueue, pvBuffer, 0);
}

BaseType_t xQueueReset(QueueHandle_t xQueue)
{
    struct frc_cb *cb = frc_cb_check(xQueue, FRC_KIND_QUEUE);
    if (cb == NULL) {
        return pdFALSE;
    }
    (void)KMsgQueueReinit(cb->id);
    cb->count = 0;
    return pdTRUE;
}

void vQueueDelete(QueueHandle_t xQueue)
{
    struct frc_cb *cb = frc_cb_check(xQueue, FRC_KIND_QUEUE);
    if (cb == NULL) {
        return;
    }
    (void)KDeleteMsgQueue(cb->id);
    frc_cb_free(cb);
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
    struct frc_cb *cb = frc_cb_check(xQueue, FRC_KIND_QUEUE);
    if (cb == NULL) {
        return 0;
    }
    return (UBaseType_t)cb->count;
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
    struct frc_cb *cb = frc_cb_check(xQueue, FRC_KIND_QUEUE);
    if (cb == NULL || cb->count > cb->max) {
        return 0;
    }
    return (UBaseType_t)(cb->max - cb->count);
}

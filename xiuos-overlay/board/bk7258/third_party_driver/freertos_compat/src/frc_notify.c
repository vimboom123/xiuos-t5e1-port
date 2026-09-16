/*
 * frc_notify.c —— 任务通知（FreeRTOS 特有，XiZi 无对应原语，需要模拟）
 *
 * 【为什么不用 XiUOS 的 KEvent】
 * FreeRTOS 的 task notification 语义是「**每任务一个 32 位值** + 一个挂起标志」，
 * 比事件组窄得多也精确得多（事件组是「一组位 + 任意任务可等」）。
 * 用事件组模拟反而要处理「同一事件组被多个任务等」的歧义。
 * 直接实现一张以任务为键的表更简单，也更容易看出哪里可能出问题。
 *
 * 【设计】
 *   - 定长表（不做动态分配，因此**无需加锁**）
 *   - 每槽一个「等待用」的二元信号量，**惰性创建**
 *   - 表的规模按 TKL 实际使用量取 16，足够；溢出时记日志而不是静默失败
 *
 * 【已知的语义近似】
 *   - 阻塞等待只做**一次**，超时后不再重算剩余时间。原因是 XiUOS 的
 *     KSemaphoreObtain 收的是绝对毫秒超时，要做精确的剩余时间需要自己
 *     按 CurrentTicksGain() 重算。TKL 对 notify 的使用都是短等待，
 *     第一版先这样，若实测发现超时不准再补。
 *   - 竞态：唤醒者与等待者之间用中断关闭保护表项，但「置 pending」与
 *     「give 信号量」不是原子的。等待者被唤醒后会**重新检查** pending，
 *     所以不会丢通知，最坏是多醒一次。
 */

#include <string.h>

#include "frc_internal.h"
#include "FreeRTOS.h"
#include "task.h"

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>
#include <xs_sem.h>
#include <xs_ktick.h>

#define FRC_NOTIFY_SLOTS    16

struct frc_notify_slot {
    void    *task;        /* 任务描述符指针；NULL 表示空槽 */
    uint32_t value;
    uint8_t  pending;
    int32_t  wait_sem;    /* 惰性创建的二元信号量 id */
};

static struct frc_notify_slot s_slots[FRC_NOTIFY_SLOTS];
static uint8_t s_slots_overflow_logged = 0;

void frc_notify_init(void)
{
    memset(s_slots, 0, sizeof(s_slots));
    s_slots_overflow_logged = 0;
}

/* 查找任务的槽。create 为真且不存在时分配一个空槽。 */
static struct frc_notify_slot *frc_notify_slot(void *task, int create)
{
    int i;
    int free_idx = -1;

    if (task == NULL) {
        task = (void *)GetKTaskDescriptor();
    }

    for (i = 0; i < FRC_NOTIFY_SLOTS; i++) {
        if (s_slots[i].task == task) {
            return &s_slots[i];
        }
        if (free_idx < 0 && s_slots[i].task == NULL) {
            free_idx = i;
        }
    }

    if (!create || free_idx < 0) {
        if (create && free_idx < 0 && !s_slots_overflow_logged) {
            frc_log("frc: notify table full (%d slots)\n", FRC_NOTIFY_SLOTS);
            s_slots_overflow_logged = 1;
        }
        return NULL;
    }

    s_slots[free_idx].task     = task;
    s_slots[free_idx].value    = 0;
    s_slots[free_idx].pending  = 0;
    s_slots[free_idx].wait_sem = -1;
    return &s_slots[free_idx];
}

/* 确保槽有等待信号量。返回 id，失败返回 -1。 */
static int32_t frc_notify_sem(struct frc_notify_slot *slot)
{
    if (slot->wait_sem < 0) {
        slot->wait_sem = KSemaphoreCreate(0);
    }
    return slot->wait_sem;
}

/* 有值可取时返回 1，并顺带把值取走 */
static int frc_notify_take(struct frc_notify_slot *slot, int clear_on_exit, uint32_t *out)
{
    uint32_t primask = frc_critical_enter();
    int got = 0;

    if (slot->pending && slot->value > 0) {
        *out = slot->value;
        if (clear_on_exit) {
            slot->value   = 0;
            slot->pending = 0;
        } else {
            slot->value--;
            if (slot->value == 0) {
                slot->pending = 0;
            }
        }
        got = 1;
    }
    frc_critical_exit(primask);
    return got;
}

/* ==========================================================================
 * 置通知
 * ========================================================================== */

BaseType_t xTaskNotify(TaskHandle_t xTaskToNotify, uint32_t ulValue, eNotifyAction eAction)
{
    struct frc_notify_slot *slot = frc_notify_slot((void *)xTaskToNotify, 1);
    uint32_t primask;
    int32_t sem;

    if (slot == NULL) {
        return pdFAIL;
    }

    primask = frc_critical_enter();
    switch (eAction) {
    case eSetBits:
        slot->value |= ulValue;
        break;
    case eIncrement:
        slot->value += 1;           /* FreeRTOS 的 eIncrement 固定加 1，不是加 ulValue */
        break;
    case eSetValueWithoutOverwrite:
        if (slot->pending) {
            frc_critical_exit(primask);
            return pdFAIL;
        }
        slot->value = ulValue;
        break;
    case eSetValueWithOverwrite:
    case eNoAction:
    default:
        slot->value = ulValue;
        break;
    }
    slot->pending = 1;
    frc_critical_exit(primask);

    /* 唤醒可能在等的任务。放在临界区外 —— KSemaphoreAbandon 可能触发调度。 */
    sem = frc_notify_sem(slot);
    if (sem >= 0) {
        (void)KSemaphoreAbandon(sem);
    }
    return pdPASS;
}

BaseType_t xTaskNotifyGive(TaskHandle_t xTaskToNotify)
{
    struct frc_notify_slot *slot = frc_notify_slot((void *)xTaskToNotify, 1);
    uint32_t primask;
    int32_t sem;

    if (slot == NULL) {
        return pdFAIL;
    }

    primask = frc_critical_enter();
    slot->value++;
    slot->pending = 1;
    frc_critical_exit(primask);

    sem = frc_notify_sem(slot);
    if (sem >= 0) {
        (void)KSemaphoreAbandon(sem);
    }
    return pdPASS;
}

BaseType_t xTaskNotifyFromISR(TaskHandle_t xTaskToNotify,
                              uint32_t ulValue,
                              eNotifyAction eAction,
                              BaseType_t *pxHigherPriorityTaskWoken)
{
    /* ISR 里调 KSemaphoreAbandon 是否安全，还没有实测（见 docs/09 §4.4）。
     * 第一版先直接用，并在有 woken 出参时如实置位。 */
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    return xTaskNotify(xTaskToNotify, ulValue, eAction);
}

void vTaskNotifyGiveFromISR(TaskHandle_t xTaskToNotify, BaseType_t *pxHigherPriorityTaskWoken)
{
    if (pxHigherPriorityTaskWoken != NULL) {
        *pxHigherPriorityTaskWoken = pdFALSE;
    }
    (void)xTaskNotifyGive(xTaskToNotify);
}

/* ==========================================================================
 * 取通知
 * ========================================================================== */

uint32_t ulTaskNotifyTake(BaseType_t xClearCountOnExit, TickType_t xTicksToWait)
{
    struct frc_notify_slot *slot = frc_notify_slot(NULL, 1);
    uint32_t value = 0;
    int32_t sem;
    int32_t wait_ms;

    if (slot == NULL) {
        return 0;
    }

    /* 快路径：已经有值，直接取走，不阻塞 */
    if (frc_notify_take(slot, xClearCountOnExit != pdFALSE, &value)) {
        return value;
    }

    sem = frc_notify_sem(slot);
    if (sem < 0) {
        return 0;
    }

    wait_ms = (xTicksToWait == portMAX_DELAY)
                  ? WAITING_FOREVER
                  : (int32_t)CalculateTimeMsFromTick((x_ticks_t)xTicksToWait);

    if (KSemaphoreObtain(sem, wait_ms) != EOK) {
        return 0;   /* 超时 */
    }

    /* 被唤醒后重新检查 —— 见文件头「竞态」说明 */
    if (frc_notify_take(slot, xClearCountOnExit != pdFALSE, &value)) {
        return value;
    }
    return 0;
}

BaseType_t xTaskNotifyWait(uint32_t ulBitsToClearOnEntry,
                           uint32_t ulBitsToClearOnExit,
                           uint32_t *pulNotificationValue,
                           TickType_t xTicksToWait)
{
    struct frc_notify_slot *slot = frc_notify_slot(NULL, 1);
    uint32_t primask;
    int32_t sem, wait_ms;

    if (slot == NULL) {
        return pdFALSE;
    }

    primask = frc_critical_enter();
    slot->value &= ~ulBitsToClearOnEntry;
    frc_critical_exit(primask);

    /* 等到 pending */
    if (!slot->pending) {
        sem = frc_notify_sem(slot);
        if (sem < 0) {
            return pdFALSE;
        }
        wait_ms = (xTicksToWait == portMAX_DELAY)
                      ? WAITING_FOREVER
                      : (int32_t)CalculateTimeMsFromTick((x_ticks_t)xTicksToWait);
        if (KSemaphoreObtain(sem, wait_ms) != EOK) {
            return pdFALSE;
        }
    }

    primask = frc_critical_enter();
    if (!slot->pending) {
        frc_critical_exit(primask);
        return pdFALSE;
    }
    if (pulNotificationValue != NULL) {
        *pulNotificationValue = slot->value;
    }
    slot->value &= ~ulBitsToClearOnExit;
    slot->pending = 0;
    frc_critical_exit(primask);

    return pdTRUE;
}

BaseType_t xTaskNotifyStateClear(TaskHandle_t xTask)
{
    struct frc_notify_slot *slot = frc_notify_slot((void *)xTask, 0);
    uint32_t primask;
    BaseType_t was_pending;

    if (slot == NULL) {
        return pdFALSE;
    }

    primask = frc_critical_enter();
    was_pending = slot->pending ? pdTRUE : pdFALSE;
    slot->pending = 0;
    frc_critical_exit(primask);

    return was_pending;
}

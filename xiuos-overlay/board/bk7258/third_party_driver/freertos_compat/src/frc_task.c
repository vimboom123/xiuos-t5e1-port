/*
 * frc_task.c —— 任务 API 转 XiZi
 *
 * 【句柄设计】FreeRTOS 的 TaskHandle_t 在本层**直接用 `struct TaskDescriptor *`**，
 * 不为任务分配控制块。理由：
 *   1. xTaskGetCurrentTaskHandle() 就是 GetKTaskDescriptor()，一行搞定，
 *      而且对**内核自己创建的任务**（idle、main、shell）同样成立 ——
 *      若用控制块方案，这些任务没有控制块，拿不到句柄
 *   2. xTaskNotify 的通知表以描述符指针为键，也不需要额外一层
 * 描述符里有 `id.id`（见 tool/shell/letter-shell/cmd.c:125 的用法），
 * 所以从句柄能还原出 XiZi 的 int32 id。
 *
 * 【两个单位换算】见 frc_internal.h 末尾的说明：
 *   - 栈深度：FreeRTOS 字 → XiUOS 字节，×4
 *   - 超时：FreeRTOS tick → XiUOS 毫秒
 */

#include <string.h>

#include "frc_internal.h"
#include "FreeRTOS.h"
#include "task.h"

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>
#include <xs_ktask.h>
#include <xs_ktick.h>

/* 从 XiZi 的 int32 id 还原出 FreeRTOS 句柄 */
static TaskHandle_t frc_handle_from_id(int32 id)
{
    if (id < 0) {
        return NULL;
    }
    return (TaskHandle_t)GetTaskWithIdnodeInfo(id);
}

/* 从 FreeRTOS 句柄取出 XiZi 的 int32 id */
static int32 frc_id_from_handle(TaskHandle_t h)
{
    struct TaskDescriptor *desc = (struct TaskDescriptor *)h;
    if (desc == NULL) {
        return -1;
    }
    return (int32)desc->id.id;
}

/* 统一的创建路径。beken_extra 为真时是 Beken 的 xTaskCreateInPsram（语义等同）。 */
static BaseType_t frc_task_create_common(TaskFunction_t pxTaskCode,
                                         const char *const pcName,
                                         const uint32_t usStackDepth,
                                         void *const pvParameters,
                                         UBaseType_t uxPriority,
                                         TaskHandle_t *const pxCreatedTask,
                                         const BaseType_t xCoreID,
                                         int pin_to_core)
{
    int32 id;

    if (pxTaskCode == NULL) {
        return pdFAIL;
    }

    /* 优先级方向：XiUOS 与 FreeRTOS **一致**（数字大 = 优先级高）。
     * 证据：kernel/thread/bitmap.c 的 PrioCaculate 返回最高置位，
     *      配合 assign.c:70 的 `if (highest_prio < running.cur_prio) 不切换`。
     * 所以这里 1:1 映射，不做反转。 */
    if (uxPriority >= (UBaseType_t)configMAX_PRIORITIES) {
        uxPriority = (UBaseType_t)configMAX_PRIORITIES - 1;
    }

    /* 栈深度换算：FreeRTOS 的 usStackDepth 是**字**，XiUOS 要**字节**。 */
    id = KTaskCreate(pcName ? pcName : "frc",
                     pxTaskCode,
                     pvParameters,
                     FRC_STACK_BYTES(usStackDepth),
                     (uint8)uxPriority);
    if (id < 0) {
        frc_log("frc: KTaskCreate(%s) failed id=%d\n", pcName ? pcName : "?", (int)id);
        return pdFAIL;
    }

    /* 绑核（仅 xTaskCreatePinnedToCore 且非 tskNO_AFFINITY）。
     *
     * 【必须用 ARCH_SMP 包住】KTaskCoreCombine 只在多核构建下存在 ——
     * xs_ktask.h:188 无条件声明了它，但实现体在 #ifdef ARCH_SMP 里
     * （见 ktask.c / smp_assign.c）。本板 .defconfig 是
     * `# CONFIG_ARCH_SMP is not set`，所以直接调会 undefined reference。
     * 单核构建下绑核本来就是无意义的，直接忽略。 */
#ifdef ARCH_SMP
    if (pin_to_core && xCoreID != tskNO_AFFINITY) {
        (void)KTaskCoreCombine(id, (uint8)xCoreID);
    }
#else
    (void)pin_to_core;
    (void)xCoreID;
#endif

    /* XiUOS 的创建与启动是**两步**；FreeRTOS 的 xTaskCreate 一步到位。 */
    if (StartupKTask(id) != EOK) {
        frc_log("frc: StartupKTask(%s) failed\n", pcName ? pcName : "?");
        (void)KTaskDelete(id);
        return pdFAIL;
    }

    if (pxCreatedTask != NULL) {
        *pxCreatedTask = frc_handle_from_id(id);
    }
    return pdPASS;
}

/* ==========================================================================
 * 创建 / 删除
 * ========================================================================== */

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode,
                       const char *const pcName,
                       const configSTACK_DEPTH_TYPE usStackDepth,
                       void *const pvParameters,
                       UBaseType_t uxPriority,
                       TaskHandle_t *const pxCreatedTask)
{
    return frc_task_create_common(pxTaskCode, pcName, (uint32_t)usStackDepth,
                                  pvParameters, uxPriority, pxCreatedTask,
                                  tskNO_AFFINITY, 0);
}

BaseType_t xTaskCreateInPsram(TaskFunction_t pxTaskCode,
                              const char *const pcName,
                              const configSTACK_DEPTH_TYPE usStackDepth,
                              void *const pvParameters,
                              UBaseType_t uxPriority,
                              TaskHandle_t *const pxCreatedTask,
                              const BaseType_t xCoreID)
{
    /* Beken 扩展：栈放 PSRAM。第一版 XiZi 不区分内存域，行为等同 xTaskCreate。 */
    return frc_task_create_common(pxTaskCode, pcName, (uint32_t)usStackDepth,
                                  pvParameters, uxPriority, pxCreatedTask,
                                  xCoreID, 1);
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t pxTaskCode,
                                   const char *const pcName,
                                   const uint32_t usStackDepth,
                                   void *const pvParameters,
                                   UBaseType_t uxPriority,
                                   TaskHandle_t *const pxCreatedTask,
                                   const BaseType_t xCoreID)
{
    return frc_task_create_common(pxTaskCode, pcName, usStackDepth,
                                  pvParameters, uxPriority, pxCreatedTask,
                                  xCoreID, 1);
}

BaseType_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                             const char *const pcName,
                             const uint32_t ulStackDepth,
                             void *const pvParameters,
                             UBaseType_t uxPriority,
                             StackType_t *const puxStackBuffer,
                             void *const pxTaskBuffer,
                             TaskHandle_t *const pxCreatedTask)
{
    /* XiZi 没有静态任务创建。转向动态创建，忽略调用方给的缓冲。
     * TKL 未使用此函数，保留只为编译得过。 */
    (void)puxStackBuffer;
    (void)pxTaskBuffer;
    return frc_task_create_common(pxTaskCode, pcName, ulStackDepth,
                                  pvParameters, uxPriority, pxCreatedTask,
                                  tskNO_AFFINITY, 0);
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
    if (xTaskToDelete == NULL) {
        /* FreeRTOS 语义：传 NULL 表示删自己 */
        KTaskQuit();
        return;
    }
    (void)KTaskDelete(frc_id_from_handle(xTaskToDelete));
}

void vTaskStartScheduler(void)
{
    /* 空操作：XiUOS 的调度器由 kernel/thread/init.c 的 XiUOSStartup() 启动，
     * 那发生在 Tuya 代码跑起来之前。能走到这里说明调度器已在运行。 */
}

/* ==========================================================================
 * 查询
 * ========================================================================== */

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)GetKTaskDescriptor();
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)CurrentTicksGain();
}

BaseType_t xTaskGetSchedulerState(void)
{
    return taskSCHEDULER_RUNNING;
}

UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask)
{
    struct TaskDescriptor *desc = (struct TaskDescriptor *)xTask;
    if (desc == NULL) {
        desc = GetKTaskDescriptor();
    }
    if (desc == NULL) {
        return 0;
    }
    return (UBaseType_t)desc->task_dync_sched_member.cur_prio;
}

/* 注意：FreeRTOS 里枚举类型叫 eTaskState，函数叫 eTaskGetState —— 名字不同，
 * 写错会得到 "unknown type name" + "conflicting types" 一对错误。 */
eTaskState eTaskGetState(TaskHandle_t xTask)
{
    struct TaskDescriptor *desc = (struct TaskDescriptor *)xTask;
    uint8 stat;

    if (desc == NULL) {
        return eInvalid;
    }

    /* 注意字段位置：stat 在 task_dync_sched_member 里，**不在** task_base_info 里。
     * 见 kernel/include/xs_ktask.h:68（TaskDyncSchedMember.stat）
     * 与 :57-64（TaskBaseInfo 没有 stat）。 */
    stat = (uint8)(desc->task_dync_sched_member.stat & KTASK_STAT_MASK);

    switch (stat) {
    case KTASK_CLOSE:   return eDeleted;
    case KTASK_SUSPEND: return eSuspended;
    case KTASK_RUNNING: return eRunning;
    case KTASK_READY:   return eReady;
    case KTASK_INIT:    return eReady;
    default:            return eReady;
    }
}

char *pcTaskGetName(TaskHandle_t xTaskToQuery)
{
    struct TaskDescriptor *desc = (struct TaskDescriptor *)xTaskToQuery;
    if (desc == NULL) {
        desc = GetKTaskDescriptor();
    }
    if (desc == NULL) {
        return NULL;
    }
    return (char *)desc->task_base_info.name;
}

TaskHandle_t xTaskGetIdleTaskHandle(void)
{
    return (TaskHandle_t)GetIdleKTaskDescripter();
}

TaskHandle_t xTaskGetHandle(const char *pcNameToQuery)
{
    if (pcNameToQuery == NULL) {
        return NULL;
    }
    return (TaskHandle_t)KTaskSearch((char *)pcNameToQuery);
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask)
{
    /* XiUOS 的栈使用量统计在 kernel/thread/assignstat.c 里，接口形态不同。
     * 第一版不实现真实水位，返回 0 表示「未知」。 */
    (void)xTask;
    return 0;
}

/* ==========================================================================
 * 控制
 * ========================================================================== */

void vTaskDelay(const TickType_t xTicksToDelay)
{
    /* DelayKTask 收的是 tick，无需换算 */
    (void)DelayKTask((x_ticks_t)xTicksToDelay);
}

void vTaskDelayUntil(TickType_t *const pxPreviousWakeTime, const TickType_t xTimeIncrement)
{
    TickType_t now;
    if (pxPreviousWakeTime == NULL) {
        return;
    }
    /* FreeRTOS 语义：等到 *pxPreviousWakeTime + xTimeIncrement；
     * 若已经过了，立刻返回并推进基准。 */
    now = xTaskGetTickCount();
    if ((TickType_t)(*pxPreviousWakeTime + xTimeIncrement) > now) {
        (void)DelayKTask((x_ticks_t)(*pxPreviousWakeTime + xTimeIncrement - now));
    }
    *pxPreviousWakeTime += xTimeIncrement;
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend)
{
    if (xTaskToSuspend == NULL) {
        xTaskToSuspend = xTaskGetCurrentTaskHandle();
    }
    (void)SuspendKTask(frc_id_from_handle(xTaskToSuspend));
}

void vTaskResume(TaskHandle_t xTaskToResume)
{
    (void)KTaskWakeup(frc_id_from_handle(xTaskToResume));
}

BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume)
{
    (void)KTaskWakeup(frc_id_from_handle(xTaskToResume));
    return pdTRUE;
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
    /* 注意：XiUOS 上游在 xs_ktask.h:186 明确写着
     *     // KTaskPrioSet is bugged, dont use this
     * 所以这里不调它，只记录一条日志。TKL 的 tkl_thread.c 不在运行中改优先级
     * （创建时一次定好），所以这条路径在实践中不会被走到。
     * 若日后真需要，得先修上游的 KTaskPrioSet。 */
    (void)xTask;
    (void)uxNewPriority;
    frc_log("frc: vTaskPrioritySet ignored (upstream KTaskPrioSet is marked bugged)\n");
}

void taskYIELD(void)
{
    (void)YieldOsAssign();
}

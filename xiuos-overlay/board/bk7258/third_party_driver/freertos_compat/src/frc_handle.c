/*
 * frc_handle.c —— 兼容层控制块的分配与校验
 *
 * 这是整个兼容层安全性的支点：TKL 那边如果传了野句柄，
 * 我们必须在**碰到 XiZi 内核之前**拦住，否则会拿着一个乱数当 id 去操作内核对象。
 */

#include "frc_internal.h"

/* 【包含顺序】xizi.h 必须最先 —— 见 frc_queue.c 里的说明 */
#include <xizi.h>

#include <xs_base.h>
#include <xs_memory.h>

/* 统计量，便于上板后用 shell 看有没有泄漏 */
static uint32_t s_cb_alive = 0;
static uint32_t s_cb_total = 0;

void frc_handle_init(void)
{
    s_cb_alive = 0;
    s_cb_total = 0;
}

struct frc_cb *frc_cb_alloc(uint16_t kind)
{
    /* 用 x_malloc 而不是 pvPortMalloc —— 后者就是本层转调 x_malloc，
     * 在这里用会形成递归。 */
    struct frc_cb *cb = (struct frc_cb *)x_malloc(sizeof(struct frc_cb));
    if (cb == NULL) {
        frc_log("frc: control block alloc failed (kind=%u)\n", (unsigned)kind);
        return NULL;
    }

    cb->magic     = FRC_MAGIC;
    cb->kind      = kind;
    cb->flags     = 0;
    cb->id        = -1;
    cb->max       = 0;
    cb->count     = 0;
    cb->item_size = 0;
    cb->aux       = NULL;

    s_cb_alive++;
    s_cb_total++;
    return cb;
}

void frc_cb_free(struct frc_cb *cb)
{
    if (cb == NULL) {
        return;
    }
    /* 先毁掉 magic：若还有人在用这个句柄，下次 frc_cb_check 会当场抓住，
     * 而不是静默地操作一块已释放的内存。 */
    cb->magic = FRC_MAGIC_DEAD;
    cb->kind  = FRC_KIND_NONE;
    cb->id    = -1;

    if (s_cb_alive > 0) {
        s_cb_alive--;
    }
    x_free(cb);
}

struct frc_cb *frc_cb_check(void *handle, uint16_t kind)
{
    struct frc_cb *cb = (struct frc_cb *)handle;

    if (cb == NULL) {
        return NULL;
    }

    if (cb->magic == FRC_MAGIC_DEAD) {
        frc_log("frc: use-after-free handle %p\n", handle);
        return NULL;
    }

    if (cb->magic != FRC_MAGIC) {
        frc_log("frc: invalid handle %p (magic=0x%08X)\n", handle, (unsigned)cb->magic);
        return NULL;
    }

    if (kind != FRC_KIND_NONE && cb->kind != kind) {
        frc_log("frc: handle %p kind mismatch (want %u got %u)\n",
                handle, (unsigned)kind, (unsigned)cb->kind);
        return NULL;
    }

    return cb;
}

uint32_t frc_cb_alive_count(void)
{
    return s_cb_alive;
}

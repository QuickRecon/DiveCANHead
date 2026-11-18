/**
 * @file storage_work_queue.c
 * @brief Share the existing factory worker's stack with periodic NVS writes.
 *
 * Flash/NVS can take deep call paths and block for erase. Keep it off the
 * cooperative system workqueue, below every zbus subscriber consumer, so it
 * cannot starve their message queues. Priority 9 remains below consumers 6–8
 * and above the battery monitor at 10. No second storage stack is allocated.
 */
#include "storage_work_queue.h"

#define STORAGE_WORK_STACK_SIZE 2048
#define STORAGE_WORK_PRIORITY 9

K_THREAD_STACK_DEFINE(storage_work_stack, STORAGE_WORK_STACK_SIZE);

struct k_work_q *storage_work_queue_get(void)
{
    static struct k_work_q storage_work_q;
    return &storage_work_q;
}

static bool *get_started_flag(void)
{
    static bool started = false;
    return &started;
}

void storage_work_queue_init(void)
{
    bool *started = get_started_flag();
    if (!*started) {
        const struct k_work_queue_config cfg = {
            .name = "storage_wq",
            .no_yield = false,
            .essential = false,
            .work_timeout_ms = 0U,
        };
        k_work_queue_init(storage_work_queue_get());
        k_work_queue_start(storage_work_queue_get(), storage_work_stack,
                           K_THREAD_STACK_SIZEOF(storage_work_stack),
                           STORAGE_WORK_PRIORITY, &cfg);
        *started = true;
    }
}

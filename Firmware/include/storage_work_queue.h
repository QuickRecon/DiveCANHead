/** @file Shared preemptible queue for synchronous storage operations. */
#ifndef STORAGE_WORK_QUEUE_H
#define STORAGE_WORK_QUEUE_H

#include <zephyr/kernel.h>

/** Initialise once from thread context, before scheduling storage work. */
void storage_work_queue_init(void);

/** Get the existing queue. Safe from ISR after initialisation. */
struct k_work_q *storage_work_queue_get(void);

#endif

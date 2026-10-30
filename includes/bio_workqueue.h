/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#ifndef _BIO_WORKQUEUE_
#define _BIO_WORKQUEUE_

#include <linux/spinlock.h>

struct bio_write_workqueue {
    struct workqueue_struct *bio_ordered_wq;
    enum {
        READY,
        CLEANING_UP,
    } status;
    spinlock_t lock;
};

#endif


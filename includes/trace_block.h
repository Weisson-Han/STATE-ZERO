/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#ifndef _TRACE_BLOCK_
#define _TRACE_BLOCK_

#include <linux/blkdev.h>


struct traced_block_device {
    struct block_device *bdev;
    size_t bdev_sector_size;
    struct block_device *incre_dev;
    struct file *increment_fp;
    loff_t increment_fp_linear_offset;

    void (*orig_submit_bio)(struct bio *bio);

    struct block_device_operations orig_bd_ops;
    struct block_device_operations *orig_bd_ops_ptr;
    bool orig_bd_has_submit_bio;
};

#endif


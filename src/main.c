// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
/*
 * STATE-ZERO Kernel Module
 * Linux 6.6 (2026-09-16)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/kallsyms.h>

#include <linux/workqueue.h>

#include "trace_block.h"
#include "bio_redirection.h"
#include "bio_workqueue.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Weisson");
MODULE_DESCRIPTION("A system level snapshottor");
MODULE_VERSION("0.1");


#define DEV_NAME_MAX_LEN 64
static char traced_dev_name[DEV_NAME_MAX_LEN] = "default_state_zero";
static char incre_dev_name[DEV_NAME_MAX_LEN] = "default_state_zero_increment_target";

module_param_string(dev_name, traced_dev_name, DEV_NAME_MAX_LEN, 0644);
module_param_string(incre_dev_name, incre_dev_name, DEV_NAME_MAX_LEN, 0644);
MODULE_PARM_DESC(dev_name, "The name of the base block device (max 63 chars)");
MODULE_PARM_DESC(incre_dev_name, "The name of the increment block device (max 63 chars)");

extern struct traced_block_device tbdev;
extern struct bio_write_workqueue bio_workqueue;


static int __init main_init(void)
{
    if (*traced_dev_name != '/' || *incre_dev_name != '/') {
        pr_err("[STATE-ZERO] invalid parameters dev_name provided!\n");
        pr_err("[STATE-ZERO] usage: insmod <module name> dev_name=<path to dev needed tracing> incre_dev_name=<path to local file to record increment>\n");
        pr_err("[STATE-ZERO] eg: insmod dev_name=/dev/vda1 incre_dev_name=/increment-storage \n");
        return -EINVAL;
    }

    struct block_device_operations *hooker_bd_ops = kzalloc(sizeof(struct block_device_operations), GFP_KERNEL);
    if (!hooker_bd_ops) {
        pr_err("[STATE-ZERO] out of memory");
        return -ENOMEM;
    }

    tbdev.bdev = blkdev_get_by_path(traced_dev_name, BLK_OPEN_READ, NULL, NULL); // hack hack hack
    if (IS_ERR(tbdev.bdev)) {
        int err = PTR_ERR(tbdev.bdev);
        pr_err("[STATE-ZERO] failed to open backing device '%s', error code: %d\n", traced_dev_name, err);
	kfree(hooker_bd_ops);
        return err;
    }
    tbdev.bdev_sector_size = 512;

    /* prepare workqueue */
    bio_workqueue.bio_ordered_wq = alloc_ordered_workqueue("bio_write_wq", 0);
    if (!bio_workqueue.bio_ordered_wq) {
        pr_err("[STATE-ZERO] failed to initialize workqueue\n");
        kfree(hooker_bd_ops);
        return -ENOMEM;
    }
    spin_lock_init(&bio_workqueue.lock);
    bio_workqueue.status = READY;

    /* create increment storage */
    pr_info("[STATE-ZERO] creating increment storage at %s", incre_dev_name);
    tbdev.increment_fp = filp_open(incre_dev_name, O_RDWR | O_CREAT, 0644);
    if (IS_ERR(tbdev.increment_fp)) {
        int err = PTR_ERR(tbdev.increment_fp);
        pr_err("[STATE-ZERO] failed to open increment storage device %s, err: %ld\n", incre_dev_name, err);
	kfree(hooker_bd_ops);
	return err;
    }

    pr_info("[STATE-ZERO] tracking device %s - path %s: %d-%d\n", tbdev.bdev->bd_disk->disk_name, traced_dev_name, tbdev.bdev->bd_start_sect, tbdev.bdev->bd_nr_sectors + tbdev.bdev->bd_start_sect);

    memcpy(hooker_bd_ops, tbdev.bdev->bd_disk->fops, sizeof(struct block_device_operations));
    tbdev.orig_bd_ops_ptr = tbdev.bdev->bd_disk->fops;
    tbdev.orig_bd_ops = *tbdev.orig_bd_ops_ptr;

    /* install the hooker */
    hooker_bd_ops->submit_bio = hooked_submit_bio;
    tbdev.orig_submit_bio = tbdev.orig_bd_ops.submit_bio ? tbdev.orig_bd_ops.submit_bio : kallsyms_lookup_name("blk_mq_submit_bio");
    WRITE_ONCE(tbdev.bdev->bd_disk->fops, hooker_bd_ops);
    tbdev.orig_bd_has_submit_bio = tbdev.bdev->bd_has_submit_bio;
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev.bdev->bd_has_submit_bio, true);

    pr_info("[STATE-ZERO] tracing bio interface - origal: 0x%lx -> new: 0x%lx\n", tbdev.orig_bd_ops.submit_bio, tbdev.bdev->bd_disk->fops->submit_bio);

    return 0;
}


static void __exit main_exit(void) {
    pr_info("[STATE-ZERO] restoring bio interface - hooked: 0x%lx -> orig: 0x%lx\n", tbdev.bdev->bd_disk->fops->submit_bio, tbdev.orig_bd_ops.submit_bio);

    struct block_device_operations *hooker_bd_ops = tbdev.bdev->bd_disk->fops;

    WRITE_ONCE(tbdev.bdev->bd_has_submit_bio, tbdev.orig_bd_has_submit_bio);
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev.bdev->bd_disk->fops, tbdev.orig_bd_ops_ptr);

    pr_info("[STATE-ZERO] stopping tracking device %s\n", traced_dev_name);
    blkdev_put(tbdev.bdev, NULL); // hack hack hack as above

    /* clean up the workqueue */
    spin_lock(&bio_workqueue.lock);
    bio_workqueue.status = CLEANING_UP;
    spin_unlock(&bio_workqueue.lock);
    destroy_workqueue(bio_workqueue.bio_ordered_wq);

    /* clean up hook */
    kfree(hooker_bd_ops);

    /* make sure bio redirection written on disk */
    int ret = 0;
    if ((ret = vfs_fsync(tbdev.increment_fp, 0)) < 0) {
        pr_err("[STATE-ZERO] data sync failed! err code: %d\n", ret);
    }

    filp_close(tbdev.increment_fp, NULL);
    tbdev.increment_fp = NULL;
}


module_init(main_init);
module_exit(main_exit);


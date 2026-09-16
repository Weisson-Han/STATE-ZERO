// SPDX-License-Identifier: BSD-2-Clause
/*
 * STATE-ZERO Kernel Module
 * Linux 6.6 (2026-09-16)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/pagemap.h>
#include <linux/blkdev.h>
#include <linux/kallsyms.h>

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Weisson");
MODULE_DESCRIPTION("A system level snapshottor");
MODULE_VERSION("0.1");


#define DEV_NAME_MAX_LEN 64
static char traced_dev_name[DEV_NAME_MAX_LEN] = "default_state_zero";

module_param_string(dev_name, traced_dev_name, DEV_NAME_MAX_LEN, 0644);
MODULE_PARM_DESC(dev_name, "The name of the STATE-ZERO block device (max 63 chars)");


struct traced_block_device {
    struct block_device *bdev;
    void (*orig_submit_bio)(struct bio *bio);

    struct block_device_operations *orig_bd_ops;
    bool orig_bd_has_submit_bio;
};

struct traced_block_device *tbdev = NULL;


void hooked_submit_bio(struct bio *bio) {
    pr_info("[STATE-ZERO] bio hooked!\n");
    tbdev->orig_submit_bio(bio);
}


static int __init main_init(void)
{
    if (*traced_dev_name != '/') {
        pr_err("[STATE-ZERO] Invalid parameters dev_name provided!\n");
        pr_err("[STATE-ZERO] Usage: insmod <module name> dev_name=<path to dev needed tracing, eg: /dev/vda1>\n");
        return -EINVAL;
    }

    tbdev = kzalloc(sizeof(struct traced_block_device), GFP_KERNEL);
    if (!tbdev) {
        pr_err("[STATE-ZERO] out of memory");
        return -ENOMEM;
    }

    struct block_device_operations *hooker_bd_ops = kzalloc(sizeof(struct block_device_operations), GFP_KERNEL);
    if (!hooker_bd_ops) {
        pr_err("[STATE-ZERO] out of memory");
        kfree(tbdev);
        return -ENOMEM;
    }

    tbdev->bdev = blkdev_get_by_path(traced_dev_name, BLK_OPEN_READ, NULL, NULL); // hack hack hack
    if (IS_ERR(tbdev->bdev)) {
        int err = PTR_ERR(tbdev->bdev);
        pr_err("Failed to open backing device '%s', error code: %d\n", traced_dev_name, err);
	kfree(hooker_bd_ops);
	kfree(tbdev);
        return err;
    }

    pr_info("[STATE-ZERO] Tracking device %s - path %s: %d-%d\n", tbdev->bdev->bd_disk->disk_name, traced_dev_name, tbdev->bdev->bd_start_sect, tbdev->bdev->bd_nr_sectors + tbdev->bdev->bd_start_sect);

    memcpy(hooker_bd_ops, tbdev->bdev->bd_disk->fops, sizeof(struct block_device_operations));
    tbdev->orig_bd_ops = tbdev->bdev->bd_disk->fops;

    /* install the hooker */
    hooker_bd_ops->submit_bio = hooked_submit_bio;
    tbdev->orig_submit_bio = tbdev->orig_bd_ops->submit_bio ? tbdev->orig_bd_ops->submit_bio : kallsyms_lookup_name("blk_mq_submit_bio");
    WRITE_ONCE(tbdev->bdev->bd_disk->fops, hooker_bd_ops);
    tbdev->orig_bd_has_submit_bio = tbdev->bdev->bd_has_submit_bio;
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev->bdev->bd_has_submit_bio, true);

    pr_info("[STATE-ZERO] Tracing bio interface - origal: 0x%lx -> new: 0x%lx\n", tbdev->orig_bd_ops->submit_bio, tbdev->bdev->bd_disk->fops->submit_bio);

    return 0;
}


static void __exit main_exit(void) {
    pr_info("[STATE-ZERO] Restoring bio interface - hooked: 0x%lx -> orig: 0x%lx\n", tbdev->bdev->bd_disk->fops->submit_bio, tbdev->orig_bd_ops->submit_bio);

    struct block_device_operations *hooker_bd_ops = tbdev->bdev->bd_disk->fops;

    WRITE_ONCE(tbdev->bdev->bd_has_submit_bio, tbdev->orig_bd_has_submit_bio);
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev->bdev->bd_disk->fops, tbdev->orig_bd_ops);

    pr_info("[STATE-ZERO] Stopping tracking device %s\n", traced_dev_name);
    blkdev_put(tbdev->bdev, NULL); // hack hack hack as above

    kfree(hooker_bd_ops);
    kfree(tbdev);
}


module_init(main_init);
module_exit(main_exit);

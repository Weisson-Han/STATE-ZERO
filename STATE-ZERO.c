// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
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
static char incre_dev_name[DEV_NAME_MAX_LEN] = "default_state_zero_increment_target";

module_param_string(dev_name, traced_dev_name, DEV_NAME_MAX_LEN, 0644);
module_param_string(incre_dev_name, incre_dev_name, DEV_NAME_MAX_LEN, 0644);
MODULE_PARM_DESC(dev_name, "The name of the base block device (max 63 chars)");
MODULE_PARM_DESC(incre_dev_name, "The name of the increment block device (max 63 chars)");


struct bio_record {
    size_t block_size;

    unsigned long pfn;
    unsigned long page_offset;
    size_t buffer_len;

    unsigned char bio_buffer[];
} __packed;


struct traced_block_device {
    struct block_device *bdev;
    size_t bdev_sector_size;
    struct block_device *incre_dev;
    struct file *increment_fp;
    loff_t increment_fp_linear_offset;
    spinlock_t increment_fp_lock;

    void (*orig_submit_bio)(struct bio *bio);

    struct block_device_operations *orig_bd_ops;
    bool orig_bd_has_submit_bio;
};

struct traced_block_device *tbdev = NULL;


void hooked_submit_bio(struct bio *bio) {
    if (bio->bi_bdev != tbdev->bdev) {
        tbdev->orig_submit_bio(bio);
        return ;
    }

    switch (bio_op(bio)) {
    case REQ_OP_WRITE: {
         struct bio_vec bv;
         struct bvec_iter iter;
         bio_for_each_segment(bv, bio, iter) {
             pr_debug("[STATE-ZERO] seg idx=%u pfn=%lu off=%u len=%u will be written into %llu\n",
                     iter.bi_idx, page_to_pfn(bv.bv_page), bv.bv_offset, bv.bv_len, (unsigned long long)tbdev->increment_fp_linear_offset);

             struct bio_record *rec = kmalloc(bv.bv_len + sizeof(struct bio_record), GFP_ATOMIC);
             if (rec == NULL) {
                 pr_err("[STATE-ZERO] fail to alloc rec\n");
                 break;
             }

             rec->block_size = bv.bv_len + sizeof(struct bio_record);
             rec->pfn = page_to_pfn(bv.bv_page);
             rec->page_offset = bv.bv_offset;
             rec->buffer_len = bv.bv_len;

             void *addr = kmap_local_page(bv.bv_page);
             memcpy(rec->bio_buffer, (unsigned char *)addr + rec->page_offset, rec->buffer_len);
             kunmap_local(addr);

             spin_lock(&tbdev->increment_fp_lock);
             size_t written = 0;
             ssize_t ret = 0;
             while (written < rec->block_size) {
                 loff_t offset = tbdev->increment_fp_linear_offset + written;
                 /* make it asynchronous later maybe */
                 ret = kernel_write(tbdev->increment_fp, (unsigned char *)rec + written, rec->block_size - written, &offset);
                 if (ret <= 0)
                     break;
                 written += ret;
             }
             if (ret <= 0 || written != rec->block_size) {
                 pr_err_ratelimited("[STATE-ZERO] fail to dump bio(w): ret=%zd written=%zu rec_size=%zu pfn=%lu off=%u len=%u\n",
                     ret, written, rec->block_size, page_to_pfn(bv.bv_page), bv.bv_offset, bv.bv_len);

             }
             tbdev->increment_fp_linear_offset += rec->block_size;
             spin_unlock(&tbdev->increment_fp_lock);
             kfree(rec);
         }
         break;
    }

    case REQ_OP_READ: {
        /* what a faith leap */
        break;
    }

    default:
        /* bio requests that does not traced */
        break;
    }

    /* bio requests to be directed */
    tbdev->orig_submit_bio(bio); /* just a test, why so serious? */
}


static int __init main_init(void)
{
    if (*traced_dev_name != '/' || *incre_dev_name != '/') {
        pr_err("[STATE-ZERO] invalid parameters dev_name provided!\n");
        pr_err("[STATE-ZERO] usage: insmod <module name> dev_name=<path to dev needed tracing> incre_dev_name=<path to local file to record increment>\n");
        pr_err("[STATE-ZERO] eg: insmod dev_name=/dev/vda1 incre_dev_name=/increment-storage \n");
        return -EINVAL;
    }

    tbdev = kzalloc(sizeof(struct traced_block_device), GFP_KERNEL);
    if (!tbdev) {
        pr_err("[STATE-ZERO] out of memory");
        return -ENOMEM;
    }
    spin_lock_init(&tbdev->increment_fp_lock);

    struct block_device_operations *hooker_bd_ops = kzalloc(sizeof(struct block_device_operations), GFP_KERNEL);
    if (!hooker_bd_ops) {
        pr_err("[STATE-ZERO] out of memory");
        kfree(tbdev);
        return -ENOMEM;
    }

    tbdev->bdev = blkdev_get_by_path(traced_dev_name, BLK_OPEN_READ, NULL, NULL); // hack hack hack
    if (IS_ERR(tbdev->bdev)) {
        int err = PTR_ERR(tbdev->bdev);
        pr_err("[STATE-ZERO] failed to open backing device '%s', error code: %d\n", traced_dev_name, err);
	kfree(hooker_bd_ops);
	kfree(tbdev);
        return err;
    }
    tbdev->bdev_sector_size = 512;

    /* create increment storage */
    pr_info("[STATE-ZERO] creating increment storage at %s", incre_dev_name);
    tbdev->increment_fp = filp_open(incre_dev_name, O_RDWR | O_CREAT, 0644);
    if (IS_ERR(tbdev->increment_fp)) {
        int err = PTR_ERR(tbdev->increment_fp);
        pr_err("[STATE-ZERO] failed to open increment storage device %s, err: %ld\n", incre_dev_name, err);
	kfree(hooker_bd_ops);
	kfree(tbdev);
	return err;
    }

    pr_info("[STATE-ZERO] tracking device %s - path %s: %d-%d\n", tbdev->bdev->bd_disk->disk_name, traced_dev_name, tbdev->bdev->bd_start_sect, tbdev->bdev->bd_nr_sectors + tbdev->bdev->bd_start_sect);

    memcpy(hooker_bd_ops, tbdev->bdev->bd_disk->fops, sizeof(struct block_device_operations));
    tbdev->orig_bd_ops = tbdev->bdev->bd_disk->fops;

    /* install the hooker */
    hooker_bd_ops->submit_bio = hooked_submit_bio;
    tbdev->orig_submit_bio = tbdev->orig_bd_ops->submit_bio ? tbdev->orig_bd_ops->submit_bio : kallsyms_lookup_name("blk_mq_submit_bio");
    WRITE_ONCE(tbdev->bdev->bd_disk->fops, hooker_bd_ops);
    tbdev->orig_bd_has_submit_bio = tbdev->bdev->bd_has_submit_bio;
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev->bdev->bd_has_submit_bio, true);

    pr_info("[STATE-ZERO] tracing bio interface - origal: 0x%lx -> new: 0x%lx\n", tbdev->orig_bd_ops->submit_bio, tbdev->bdev->bd_disk->fops->submit_bio);

    return 0;
}


static void __exit main_exit(void) {
    pr_info("[STATE-ZERO] restoring bio interface - hooked: 0x%lx -> orig: 0x%lx\n", tbdev->bdev->bd_disk->fops->submit_bio, tbdev->orig_bd_ops->submit_bio);

    int ret = 0;
    if ((ret = vfs_fsync(tbdev->increment_fp, 0)) < 0) {
        pr_err("[STATE-ZERO] data sync failed! err code: %d\n", ret);
    }
    filp_close(tbdev->increment_fp, NULL);
    tbdev->increment_fp = NULL;

    struct block_device_operations *hooker_bd_ops = tbdev->bdev->bd_disk->fops;

    WRITE_ONCE(tbdev->bdev->bd_has_submit_bio, tbdev->orig_bd_has_submit_bio);
    smp_wmb(); /* no reading order ensurance, wish you a good luck */
    WRITE_ONCE(tbdev->bdev->bd_disk->fops, tbdev->orig_bd_ops);

    pr_info("[STATE-ZERO] stopping tracking device %s\n", traced_dev_name);
    blkdev_put(tbdev->bdev, NULL); // hack hack hack as above

    kfree(hooker_bd_ops);
    kfree(tbdev);
}


module_init(main_init);
module_exit(main_exit);

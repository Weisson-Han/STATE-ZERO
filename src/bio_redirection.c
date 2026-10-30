// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only

#include "trace_block.h"
#include "bio_redirection.h"
#include "bio_workqueue.h"


struct bio_record {
    size_t block_size;

    unsigned long pfn;
    unsigned long page_offset;
    size_t buffer_len;

    unsigned char bio_buffer[];
} __packed;


struct bio_write_ctx {
    struct work_struct work;
    struct bio_record rec;
};


extern struct traced_block_device tbdev;
extern struct bio_write_workqueue bio_workqueue;


static void handle_bio_write_redirection(struct work_struct *work) {
    struct bio_write_ctx *ctx = container_of(work, struct bio_write_ctx, work);

    size_t written = 0;
    ssize_t ret = 0;
    while (written < ctx->rec.block_size) {
        loff_t offset = tbdev.increment_fp_linear_offset + written;

        ret = kernel_write(tbdev.increment_fp, (unsigned char *)&ctx->rec + written, ctx->rec.block_size - written, &offset);
        if (ret <= 0)
            break;
        written += ret;
    }
    if (ret <= 0 || written != ctx->rec.block_size) {
        pr_err_ratelimited("[STATE-ZERO] fail to dump bio(w): ret=%zd written=%zu rec_size=%zu pfn=%lu off=%u len=%u\n",
            ret, written, ctx->rec.block_size, ctx->rec.pfn, ctx->rec.page_offset, ctx->rec.buffer_len);

    }
    tbdev.increment_fp_linear_offset += ctx->rec.block_size;

    kfree(ctx);
}


void hooked_submit_bio(struct bio *bio) {
    if (bio->bi_bdev != tbdev.bdev) {
        tbdev.orig_submit_bio(bio);
        return ;
    }

    switch (bio_op(bio)) {
    case REQ_OP_WRITE: {
         struct bio_vec bv;
         struct bvec_iter iter;
         bio_for_each_segment(bv, bio, iter) {
             pr_debug("[STATE-ZERO] seg idx=%u pfn=%lu off=%u len=%u will be written into %llu\n",
                     iter.bi_idx, page_to_pfn(bv.bv_page), bv.bv_offset, bv.bv_len, (unsigned long long)tbdev.increment_fp_linear_offset);

             struct bio_write_ctx *ctx = kmalloc(bv.bv_len + sizeof(struct bio_write_ctx), GFP_ATOMIC);;
             if (ctx == NULL) {
                 pr_err("[STATE-ZERO] fail to alloc rec\n");
                 break;
             }

             ctx->rec.block_size = bv.bv_len + sizeof(struct bio_record);
             ctx->rec.pfn = page_to_pfn(bv.bv_page);
             ctx->rec.page_offset = bv.bv_offset;
             ctx->rec.buffer_len = bv.bv_len;

             void *addr = kmap_local_page(bv.bv_page);
             memcpy(ctx->rec.bio_buffer, (unsigned char *)addr + ctx->rec.page_offset, ctx->rec.buffer_len);
             kunmap_local(addr);

             INIT_WORK(&ctx->work, handle_bio_write_redirection);

             spin_lock(&bio_workqueue.lock);
             if (bio_workqueue.status != READY) {
                 spin_unlock(&bio_workqueue.lock);
                 pr_err("[STATE-ZERO] working queue is being cleaned up, a bio request dropped\n");
                 kfree(ctx);
                 break;
             }

             if (!queue_work(bio_workqueue.bio_ordered_wq, &ctx->work)) {
                 pr_err("[STATE-ZERO] failed to insert bio request into working queue\n");
                 kfree(ctx);
             }
             spin_unlock(&bio_workqueue.lock);
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
    tbdev.orig_submit_bio(bio); /* just a test, why so serious? */
}


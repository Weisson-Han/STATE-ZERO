/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#ifndef _BIO_REDIRECTION_
#define _BIO_REDIRECTION_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/bio.h>


extern void hooked_submit_bio(struct bio *bio);

#endif


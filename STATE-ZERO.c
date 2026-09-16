// SPDX-License-Identifier: GPL-2.0
/*
 * STATE-ZERO Kernel Module
 * Linux 6.x (2026-09-16)
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Weisson");
MODULE_DESCRIPTION("A system level snapshottor");
MODULE_VERSION("0.1");

static int __init main_init(void)
{
    pr_info("Hello, Linux Kernel!\n");
    return 0;
}

static void __exit main_exit(void)
{
    pr_info("Goodbye, Linux Kernel!\n");
}

module_init(main_init);
module_exit(main_exit);


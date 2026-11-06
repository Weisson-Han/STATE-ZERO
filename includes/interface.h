/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */

#ifndef _INTERFACE_H_
#define _INTERFACE_H_

struct sysfs_interface_operations {
    int (*get_sysfs_status)(void);
    int (*enable)(void);
    int (*disable)(void);
    int (*init)(void);
    int (*cleanup)(void);
    spinlock_t *lock;
};

extern struct sysfs_interface_operations global_sysfs_ops;

#endif


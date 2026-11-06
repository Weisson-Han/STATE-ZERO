// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
#include <linux/kobject.h>

#include "interface.h"


static ssize_t data_show(struct kobject * , struct kobj_attribute * , char * );
static ssize_t data_store(struct kobject * , struct kobj_attribute * , const char *, size_t );
int get_sysfs_status(void);
int init_state0_interface(void);
int cleanup_state0_interface(void);


struct sysfs_interface_operations global_sysfs_ops = {
    .get_sysfs_status = get_sysfs_status,
    .enable = NULL,
    .disable = NULL,
    .lock = NULL,
    .init = init_state0_interface,
    .cleanup = cleanup_state0_interface,
};


static struct kobj_attribute state0_attr = {
    .attr = {
        .name = "is_enabled",
        .mode = 0644,
    },
    .show = data_show,
    .store = data_store,
};
static struct kobject *state0_kobj = NULL;
static char *status_string_pool[2] = { "disabled", "enabled", };
static int status = 0;


int get_sysfs_status(void) {
    int _status = -1;

    if(global_sysfs_ops.lock) {
        spin_lock(global_sysfs_ops.lock);
    }
    _status = status;
    if (global_sysfs_ops.lock) {
        spin_unlock(global_sysfs_ops.lock);
    }

    return _status;
}


static ssize_t data_show(struct kobject *kobj,
                         struct kobj_attribute *attr,
                         char *buf)
{
    int output = global_sysfs_ops.get_sysfs_status();
    return sysfs_emit(buf, "%s\n", status_string_pool[output]);
}


static ssize_t data_store(struct kobject *kobj,
                          struct kobj_attribute *attr,
                          const char *buf,
                          size_t count)
{
    int input = 0;
    kstrtoint(buf, 10, &input);
    input = input != 0;


    if (global_sysfs_ops.lock) {
        spin_lock(global_sysfs_ops.lock);
    }
    if (input == status) {
        if (global_sysfs_ops.lock) {
            spin_unlock(global_sysfs_ops.lock);
        }
        return count;
    }

    switch (input) {
        case 0: {
            if (global_sysfs_ops.disable) {
                global_sysfs_ops.disable();
            }
        }
        break;

        case 1: {
            if (global_sysfs_ops.enable) {
                global_sysfs_ops.enable();
            }
        }
        break;
    }
    status = input;
    if (global_sysfs_ops.lock) {
        spin_unlock(global_sysfs_ops.lock);
    }

    return count;
}


int init_state0_interface(void) {
    int retval = 0;

    state0_kobj = kobject_create_and_add("state0", kernel_kobj);
    if (!state0_kobj)
        return -ENOMEM;

    /* Create the files associated with this kobject */
    retval = sysfs_create_file(state0_kobj, &state0_attr.attr);
    if (retval)
        kobject_put(state0_kobj);

    return retval;
}


int cleanup_state0_interface(void) {
    kobject_put(state0_kobj);

    return 0;
}


// SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only
#include <linux/kobject.h>


static ssize_t data_show(struct kobject * , struct kobj_attribute * , char * );
static ssize_t data_store(struct kobject * , struct kobj_attribute * , const char *, size_t );


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


static ssize_t data_show(struct kobject *kobj,
                         struct kobj_attribute *attr,
                         char *buf)
{
    int output = READ_ONCE(status);
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
    WRITE_ONCE(status, input);

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


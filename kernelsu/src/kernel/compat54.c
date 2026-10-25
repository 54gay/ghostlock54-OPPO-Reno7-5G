/* 5.4 compat stubs: features that depend on >=5.7 SELinux policy internals
 * (policydb/sidtab duplication) are not supported on this kernel. */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include "klog.h"

void ksu_selinux_hide_init(void) { }
void ksu_selinux_hide_exit(void) { }
void ksu_selinux_hide_drop_backup_if_unused(void) { }
void ksu_selinux_hide_handle_second_stage(void) { }
void ksu_selinux_hide_handle_post_fs_data(void) { }

long ksu_copy_from_user_nofault(void *dst, const void __user *src, size_t n)
{
    (void)dst; (void)src; (void)n;
    return -EFAULT;
}

long ksu_copy_to_user_nofault(void __user *dst, const void *src, size_t n)
{
    (void)dst; (void)src; (void)n;
    return -EFAULT;
}

struct seccomp_filter;
void ksu_seccomp_clear_cache(struct seccomp_filter *filter, int nr)
{
    (void)filter; (void)nr;
}

void ksu_seccomp_allow_cache(struct seccomp_filter *filter, int nr)
{
    (void)filter; (void)nr;
}

int ksu_observer_init(void) { return 0; }
void ksu_observer_exit(void) { }

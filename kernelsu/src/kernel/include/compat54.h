#ifndef _KSU_COMPAT54_H
#define _KSU_COMPAT54_H
#include <linux/version.h>
#include <linux/types.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
#define KSU_54 1
#include "ksu54_syms.h"
struct mm_struct;
extern struct mm_struct *ksu_init_mm;

long ksu_strncpy_from_user_nofault(char *dst, const void __user *unsafe_addr, long count);
#define strncpy_from_user_nofault ksu_strncpy_from_user_nofault

extern long probe_kernel_write(void *dst, const void *src, size_t size);
extern long probe_kernel_read(void *dst, const void *src, size_t size);
#define copy_to_kernel_nofault probe_kernel_write

#define TWA_RESUME true

long ksu_copy_from_user_nofault(void *dst, const void __user *src, size_t n);
long ksu_copy_to_user_nofault(void __user *dst, const void *src, size_t n);
#define copy_from_user_nofault ksu_copy_from_user_nofault
#define copy_to_user_nofault ksu_copy_to_user_nofault
#endif

#ifdef KSU_54
extern syscall_fn_t *ksu54_orig_table;
#define ksu_syscall_table_orig ksu54_orig_table
extern int ksu54_level;
extern void ksu54_level_banner(void);
#define ksu_tasklist_lock_ptr ksu54_tasklist_lock
#else
#define ksu_tasklist_lock_ptr (&tasklist_lock)
#endif
#endif

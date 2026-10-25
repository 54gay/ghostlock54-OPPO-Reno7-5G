#ifndef _KSU54_SYMS_H
#define _KSU54_SYMS_H
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/fs_struct.h>
#include <linux/proc_ns.h>
#include <linux/pid.h>
#include <linux/task_work.h>
#include <linux/user_namespace.h>
#include <asm/fixmap.h>
#include <asm/syscall.h>

#ifdef KSU_54
/* address of kallsyms_lookup_name, supplied by loader via module param */
extern unsigned long ksu54_kln;
unsigned long ksu54_lookup_sym(const char *name);

/* generic indirect call via inline asm (bypasses compiler CFI checks) */
static inline unsigned long ksu54_blr(void *fn, unsigned long a0, unsigned long a1,
                                      unsigned long a2, unsigned long a3, unsigned long a4)
{
    register unsigned long x0 asm("x0") = a0;
    register unsigned long x1 asm("x1") = a1;
    register unsigned long x2 asm("x2") = a2;
    register unsigned long x3 asm("x3") = a3;
    register unsigned long x4 asm("x4") = a4;
    register unsigned long x9 asm("x9") = (unsigned long)fn;

    asm volatile("mov x8, x9\n\tblr x8"
                 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x9)
                 :
                 : "x5", "x6", "x7", "x8", "x10", "x11", "x12", "x13",
                   "x14", "x15", "x16", "x17", "lr", "cc", "memory");
    return x0;
}

/* typed wrappers */
long ksu54_ksys_unshare(unsigned long flags);
int ksu54_path_mount(const char *dev_name, struct path *path, const char *type_page,
                     unsigned long flags, void *data_page);
int ksu54_ns_get_path(struct path *path, struct task_struct *task,
                      const struct proc_ns_operations *ns_ops);
void ksu54_set_fs_pwd(struct fs_struct *fs, const struct path *path);
long ksu54_arm64_sys_setns(const struct pt_regs *regs);
void ksu54_flush_dcache_area(const void *addr, unsigned long len);
void ksu54_flush_icache_range(unsigned long start, unsigned long end);
void ksu54_set_fixmap(unsigned int idx, unsigned long phys, unsigned long prot);
static inline void *ksu54_set_fixmap_offset(unsigned int idx, unsigned long phys)
{
    unsigned long fix = fix_to_virt(idx) + (phys & ~PAGE_MASK);
    ksu54_set_fixmap(idx, phys, (unsigned long)pgprot_val(PAGE_KERNEL));
    return (void *)fix;
}
static inline void ksu54_clear_fixmap(unsigned int idx)
{
    ksu54_set_fixmap(idx, 0, 0);
}
int ksu54_task_work_add(struct task_struct *task, struct callback_head *work, bool notify);
struct user_struct *ksu54_alloc_uid(kuid_t uid);
void ksu54_free_uid(struct user_struct *u);
void ksu54_change_pid(struct task_struct *task, enum pid_type type, struct pid *new_pid);
int ksu54_kallsyms_lookup_size_offset(unsigned long addr, unsigned long *size, unsigned long *offset);
void ksu54_put_seccomp_filter(struct task_struct *tsk);

extern rwlock_t *ksu54_tasklist_lock;
extern void *ksu54_tp_sys_enter;
extern void *ksu54_mntns_operations;
extern void *ksu54_selinux_state;
extern void *ksu54_selinux_blob_sizes;

int ksu54_resolve_all(void);

/* Patch the vendor anti-root hook so privilege escalation is not killed. */
void ksu54_patch_vendor_root_check(void);
#endif
#endif

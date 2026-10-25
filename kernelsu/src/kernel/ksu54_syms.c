/* 5.4 runtime symbol resolution: this vendor kernel does not export the
 * symbols below, so they are resolved through kallsyms_lookup_name (address
 * supplied by the loader via the `kln` module parameter) and called through an
 * inline-asm trampoline to avoid cross-compiler CFI type-id checks. */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/sched/task.h>
#include <linux/seccomp.h>
#include <linux/user_namespace.h>
#include <linux/mount.h>
#include <linux/lsm_hooks.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>

#include "klog.h" // IWYU pragma: keep
#include "ksu54_syms.h"
#include "hook/patch_memory.h"

unsigned long ksu54_kln __read_mostly;
module_param_named(kln, ksu54_kln, ulong, 0);
MODULE_PARM_DESC(kln, "address of kallsyms_lookup_name from /proc/kallsyms");

/* Module-local copy of the kernel's SELinux LSM blob sizes. Inline helpers in
 * security/selinux/include/objsec.h reference this symbol; a local definition
 * keeps them working without needing an exported kernel symbol. The real
 * values are copied from the kernel at init time. */
struct lsm_blob_sizes selinux_blob_sizes;

rwlock_t *ksu54_tasklist_lock __read_mostly;
void *ksu54_tp_sys_enter __read_mostly;
void *ksu54_mntns_operations __read_mostly;
void *ksu54_selinux_state __read_mostly;
void *ksu54_selinux_blob_sizes __read_mostly;

unsigned long ksu54_lookup_sym(const char *name)
{
    if (!ksu54_kln)
        return 0;
    return ksu54_blr((void *)ksu54_kln, (unsigned long)name, 0, 0, 0, 0);
}

long ksu54_ksys_unshare(unsigned long flags)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("ksys_unshare");
    if (!fn)
        return -ENOSYS;
    return (long)ksu54_blr((void *)fn, flags, 0, 0, 0, 0);
}

int ksu54_path_mount(const char *dev_name, struct path *path, const char *type_page,
                     unsigned long flags, void *data_page)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("do_mount");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)dev_name, (unsigned long)path,
                          (unsigned long)type_page, flags, (unsigned long)data_page);
}

int ksu54_ns_get_path(struct path *path, struct task_struct *task,
                      const struct proc_ns_operations *ns_ops)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("ns_get_path");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)path, (unsigned long)task,
                          (unsigned long)ns_ops, 0, 0);
}

void ksu54_set_fs_pwd(struct fs_struct *fs, const struct path *path)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("set_fs_pwd");
    if (!fn)
        return;
    ksu54_blr((void *)fn, (unsigned long)fs, (unsigned long)path, 0, 0, 0);
}

long ksu54_arm64_sys_setns(const struct pt_regs *regs)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("__arm64_sys_setns");
    if (!fn)
        return -ENOSYS;
    return (long)ksu54_blr((void *)fn, (unsigned long)regs, 0, 0, 0, 0);
}

void ksu54_flush_dcache_area(const void *addr, unsigned long len)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("__flush_dcache_area");
    if (!fn)
        return;
    ksu54_blr((void *)fn, (unsigned long)addr, len, 0, 0, 0);
}

void ksu54_flush_icache_range(unsigned long start, unsigned long end)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("__flush_icache_range");
    if (!fn)
        return;
    ksu54_blr((void *)fn, start, end, 0, 0, 0);
}

void ksu54_set_fixmap(unsigned int idx, unsigned long phys, unsigned long prot)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("__set_fixmap");
    if (!fn)
        return;
    ksu54_blr((void *)fn, idx, phys, prot, 0, 0);
}

int ksu54_task_work_add(struct task_struct *task, struct callback_head *work, bool notify)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("task_work_add");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)task, (unsigned long)work,
                          (unsigned long)notify, 0, 0);
}

struct user_struct *ksu54_alloc_uid(kuid_t uid)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("alloc_uid");
    if (!fn)
        return NULL;
    return (struct user_struct *)ksu54_blr((void *)fn, (unsigned long)uid.val, 0, 0, 0, 0);
}

void ksu54_free_uid(struct user_struct *u)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("free_uid");
    if (!fn)
        return;
    ksu54_blr((void *)fn, (unsigned long)u, 0, 0, 0, 0);
}

void ksu54_change_pid(struct task_struct *task, enum pid_type type, struct pid *new_pid)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("change_pid");
    if (!fn)
        return;
    ksu54_blr((void *)fn, (unsigned long)task, (unsigned long)type,
              (unsigned long)new_pid, 0, 0);
}

int ksu54_kallsyms_lookup_size_offset(unsigned long addr, unsigned long *size, unsigned long *offset)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("kallsyms_lookup_size_offset");
    if (!fn)
        return 0;
    return (int)ksu54_blr((void *)fn, addr, (unsigned long)size, (unsigned long)offset, 0, 0);
}

void ksu54_put_seccomp_filter(struct task_struct *tsk)
{
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("put_seccomp_filter");
    if (!fn)
        return;
    ksu54_blr((void *)fn, (unsigned long)tsk, 0, 0, 0, 0);
}

int ksu54_resolve_all(void)
{
    if (!ksu54_kln) {
        pr_err("ksu54: kln param not provided, cannot resolve symbols\n");
        return -ENOENT;
    }
    ksu54_tasklist_lock = (rwlock_t *)ksu54_lookup_sym("tasklist_lock");
    ksu54_tp_sys_enter = (void *)ksu54_lookup_sym("__tracepoint_sys_enter");
    ksu54_mntns_operations = (void *)ksu54_lookup_sym("mntns_operations");
    ksu54_selinux_state = (void *)ksu54_lookup_sym("selinux_state");
    ksu54_selinux_blob_sizes = (void *)ksu54_lookup_sym("selinux_blob_sizes");
    if (ksu54_selinux_blob_sizes)
        memcpy(&selinux_blob_sizes, ksu54_selinux_blob_sizes, sizeof(selinux_blob_sizes));
    pr_info("ksu54: tasklist=%px tp=%px mntns=%px selinux_state=%px blob=%px\n",
            ksu54_tasklist_lock, ksu54_tp_sys_enter, ksu54_mntns_operations,
            ksu54_selinux_state, ksu54_selinux_blob_sizes);
    return (ksu54_tasklist_lock && ksu54_tp_sys_enter) ? 0 : -ENOENT;
}

/* ColorOS ships a hidden kernel anti-root check ("oplus_root_check") that
 * SIGKILLs any process whose creds escalate from an app uid to uid 0.  There
 * are two pieces:
 *   - oplus_root_check_succ(): formats and printk's the payload and reports
 *     it to userspace via kevent;
 *   - oplus_invoke_syscall(): the vendor replacement for invoke_syscall(),
 *     which calls the checker and then does do_group_exit(9) ("Kill the
 *     process of escalation...").
 * Patch the checker entry to a plain "ret" and NOP the do_group_exit() call
 * so the escalation we perform on behalf of the manager survives.  The
 * symbols may be absent on kernels without this vendor feature, so treat a
 * miss as non-fatal. */
void ksu54_patch_vendor_root_check(void)
{
    unsigned long addr, invoke, site;
    u32 ret_insn = 0xd65f03c0; /* ret */
    u32 nop = 0xd503201f;
    u32 insn[4];
    int rc;

    addr = ksu54_lookup_sym("oplus_root_check_succ");
    if (!addr) {
        pr_info("ksu54: oplus_root_check_succ not found, skip patch\n");
    } else {
        memcpy(insn, (void *)addr, sizeof(insn));
        pr_info("ksu54: oplus_root_check_succ @0x%lx: %08x %08x %08x %08x\n",
                addr, insn[0], insn[1], insn[2], insn[3]);
        rc = ksu_patch_text((void *)addr, &ret_insn, sizeof(ret_insn),
                            KSU_PATCH_TEXT_FLUSH_ICACHE | KSU_PATCH_TEXT_FLUSH_DCACHE);
        pr_info("ksu54: patch oplus_root_check_succ rc=%d now=%08x\n",
                rc, *(u32 *)addr);
    }

    invoke = ksu54_lookup_sym("oplus_invoke_syscall");
    if (!invoke) {
        pr_info("ksu54: oplus_invoke_syscall not found, skip patch\n");
        return;
    }
    /* Find "mov w0, #9; bl do_group_exit" inside the first 0x400 bytes. */
    site = 0;
    for (rc = 0; rc < 0x400; rc += 4) {
        u32 a = *(u32 *)(invoke + rc);
        u32 b = *(u32 *)(invoke + rc + 4);
        if (a == 0x52800120 && (b & 0xFC000000) == 0x94000000) {
            site = invoke + rc;
            break;
        }
    }
    if (!site) {
        pr_warn("ksu54: oplus_invoke_syscall kill site not found\n");
        return;
    }
    pr_info("ksu54: oplus_invoke_syscall @0x%lx: kill site @0x%lx: %08x %08x\n",
            invoke, site, *(u32 *)site, *(u32 *)(site + 4));
    {
        u32 two_nops[2] = { nop, nop };
        rc = ksu_patch_text((void *)site, two_nops, sizeof(two_nops),
                            KSU_PATCH_TEXT_FLUSH_ICACHE | KSU_PATCH_TEXT_FLUSH_DCACHE);
        pr_info("ksu54: nop oplus kill site rc=%d now=%08x %08x\n",
                rc, *(u32 *)site, *(u32 *)(site + 4));
    }
}

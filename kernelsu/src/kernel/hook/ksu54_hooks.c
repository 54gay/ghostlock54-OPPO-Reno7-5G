/* 5.4 direct syscall-table hooking.
 *
 * The tracepoint + ni-slot dispatcher design in KSU Next targets 5.10+; on this
 * 5.4 vendor kernel the safest approach is the classic one: patch the table
 * entry of each hooked syscall with a wrapper and keep the original handler to
 * call through. The wrappers take the syscall handler prototype directly, so no
 * orig-number trampoline (x8) or tracepoint redirection is needed. */
#include <linux/ptrace.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <asm/syscall.h>

#include "klog.h" // IWYU pragma: keep
#include "hook/syscall_hook.h"
#include "arch.h"
#include "hook/syscall_event_bridge.h"
#include "hook/ksu54_hooks.h"

extern syscall_fn_t *ksu_syscall_table;
extern void ksu54_disable_seccomp_for_current(void);
extern bool ksu_su_compat_enabled;

/* Snapshot of the original table: internal code ("call the original handler")
 * must not go through the patched table, or it would recurse into our wrappers. */
syscall_fn_t *ksu54_orig_table;

static long ksu54_hook_execve_common(const struct pt_regs *regs, bool at)
{
    return at ? ksu_hook_execveat(__NR_execveat, regs) : ksu_hook_execve(__NR_execve, regs);
}

/* The KSU manager bootstraps its userspace ("magica") by exec'ing the
 * libksud.so shipped inside its own APK.  That child inherits the app's
 * seccomp filter, and this OPPO ROM's app seccomp policy blocks the reboot(2)
 * magic syscall KSU needs to obtain the driver fd (SIGSYS).  Detect that exec
 * and give the child root with seccomp disabled, exactly like the su path. */
static bool ksu54_is_manager_ksud(const struct pt_regs *regs)
{
    const char __user *fn = (const char __user *)PT_REGS_PARM1(regs);
    char path[256];
    long ret;

    if (!fn)
        return false;
    ret = strncpy_from_user(path, fn, sizeof(path));
    if (ret <= 0)
        return false;
    path[sizeof(path) - 1] = '\0';
    return strstr(path, "libksud.so") != NULL;
}


static long ksu54_hook_execve(const struct pt_regs *regs)
{
    if (unlikely(ksu54_is_manager_ksud(regs))) {
        pr_info("ksu54: manager ksud bootstrap, seccomp off (uid=%d)\n", current_uid().val);
        ksu54_disable_seccomp_for_current();
        return ksu54_hook_execve_common(regs, false);
    }
    return ksu54_hook_execve_common(regs, false);
}

static long ksu54_hook_execveat(const struct pt_regs *regs)
{
    return ksu54_hook_execve_common(regs, true);
}

static long ksu54_hook_newfstatat(const struct pt_regs *regs)
{
    return ksu_hook_newfstatat(__NR_newfstatat, regs);
}

static long ksu54_hook_faccessat(const struct pt_regs *regs)
{
    return ksu_hook_faccessat(__NR_faccessat, regs);
}

static long ksu54_hook_setresuid(const struct pt_regs *regs)
{
    return ksu_hook_setresuid(__NR_setresuid, regs);
}

void ksu54_install_table_hooks(void)
{
    if (!ksu_syscall_table) {
        pr_err("ksu54: sys_call_table missing, hooks not installed\n");
        return;
    }

    ksu54_orig_table = kmemdup(ksu_syscall_table, sizeof(syscall_fn_t) * __NR_syscalls, GFP_KERNEL);
    if (!ksu54_orig_table) {
        pr_err("ksu54: cannot snapshot sys_call_table, hooks not installed\n");
        return;
    }

    ksu_syscall_table_hook(__NR_execve, ksu54_hook_execve, NULL);
    ksu_syscall_table_hook(__NR_execveat, ksu54_hook_execveat, NULL);
    ksu_syscall_table_hook(__NR_newfstatat, ksu54_hook_newfstatat, NULL);
    ksu_syscall_table_hook(__NR_faccessat, ksu54_hook_faccessat, NULL);
    ksu_syscall_table_hook(__NR_setresuid, ksu54_hook_setresuid, NULL);

    pr_info("ksu54: direct syscall-table hooks installed\n");
}

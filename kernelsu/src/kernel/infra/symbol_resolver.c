#include <linux/kallsyms.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "infra/symbol_resolver.h"
#include "klog.h"

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
/* This vendor kernel exports neither kallsyms_lookup_name nor
 * kallsyms_on_each_symbol. register_kprobe() is EXPORT_SYMBOL_GPL and its
 * internal symbol resolution handles non-exported symbols, so it is used as
 * a bootstrap to obtain the address of kallsyms_lookup_name(). */
static unsigned long (*ksu_kallsyms_lookup_name_fn)(const char *name) __read_mostly;
static long (*ksu_strncpy_from_unsafe_user_fn)(char *, const void __user *, long) __read_mostly;
struct mm_struct *ksu_init_mm __read_mostly;

static void *ksu_kprobe_addr(const char *name)
{
    struct kprobe kp;
    int ret;

    memset(&kp, 0, sizeof(kp));
    kp.symbol_name = name;
    ret = register_kprobe(&kp);
    if (ret) {
        pr_warn("ksu: kprobe bootstrap for %s failed: %d\n", name, ret);
        return NULL;
    }
    unregister_kprobe(&kp);
    return (void *)kp.addr;
}

unsigned long find_kernel_symbol_exact(const char *symbol_name)
{
    if (!symbol_name || !symbol_name[0])
        return 0;
#ifdef KSU_54
    return ksu54_lookup_sym(symbol_name);
#else
    if (ksu_kallsyms_lookup_name_fn)
        return ksu_kallsyms_lookup_name_fn(symbol_name);
    return (unsigned long)ksu_kprobe_addr(symbol_name);
#endif
}

void *ksu_resolve_symbol_for_functable_hook(const char *symbol_name)
{
    char cfi_name[KSYM_NAME_LEN];
    unsigned long addr;

    if (!symbol_name || !symbol_name[0])
        return NULL;

    snprintf(cfi_name, sizeof(cfi_name), "%s.cfi_jt", symbol_name);
    addr = find_kernel_symbol_exact(cfi_name);
    if (addr)
        return (void *)addr;

    addr = find_kernel_symbol_exact(symbol_name);
    return (void *)addr;
}

void ksu_init_symbol_resolver(void)
{
    void *p;

#ifdef KSU_54
    if (ksu54_resolve_all()) {
        pr_err("ksu: FATAL: 5.4 symbol resolution failed (missing kln param?)\n");
        return;
    }
    ksu_init_mm = (struct mm_struct *)ksu54_lookup_sym("init_mm");
    ksu_strncpy_from_unsafe_user_fn = (void *)ksu54_lookup_sym("strncpy_from_unsafe_user");
    pr_info("ksu: resolver ready init_mm=%px\n", ksu_init_mm);
    return;
#else
    p = ksu_kprobe_addr("kallsyms_lookup_name");
    if (!p) {
        pr_err("ksu: FATAL: cannot resolve kallsyms_lookup_name\n");
        return;
    }
    ksu_kallsyms_lookup_name_fn = p;
    ksu_init_mm = (struct mm_struct *)ksu_kallsyms_lookup_name_fn("init_mm");
    ksu_strncpy_from_unsafe_user_fn =
        (void *)ksu_kallsyms_lookup_name_fn("strncpy_from_unsafe_user");
    pr_info("ksu: resolver ready init_mm=%px\n", ksu_init_mm);
#endif
}

long ksu_strncpy_from_user_nofault(char *dst, const void __user *uaddr, long count)
{
#ifdef KSU_54
    static unsigned long fn;
    if (!fn)
        fn = ksu54_lookup_sym("strncpy_from_unsafe_user");
    if (!fn)
        return -EFAULT;
    /* strncpy_from_unsafe_user(char *dst, const void __user *unsafe_addr, long count) */
    return (long)ksu54_blr((void *)fn, (unsigned long)dst, (unsigned long)uaddr, count, 0, 0);
#else
    if (ksu_strncpy_from_unsafe_user_fn)
        return ksu_strncpy_from_unsafe_user_fn(dst, uaddr, count);
#endif
    if (!dst || count <= 0)
        return -EINVAL;
    dst[0] = 0;
    return 0;
}
#else
#endif

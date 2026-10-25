/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 5.4 compat: trampolines into non-exported SELinux policy internals.
 * Every call goes through ksu54_blr() (inline asm), which is required both
 * because the symbols are not exported to modules and because a plain C
 * indirect call through a function pointer would trip KernelCFI.
 */
#include <linux/kernel.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "include/ksu54_syms.h"
#include "selinux/ksu54_secops.h"

#ifdef KSU_54

static unsigned long secop_sym(const char *name)
{
    static unsigned long cache[16];
    static const char *names[16];
    int i;

    for (i = 0; i < 16; i++) {
        if (names[i] == name)
            return cache[i];
        if (!names[i])
            break;
    }
    if (i == 16) {
        pr_err("ksu54: secop table full\n");
        return 0;
    }
    names[i] = name;
    cache[i] = ksu54_lookup_sym(name);
    if (!cache[i])
        pr_err("ksu54: selinux symbol %s not found\n", name);
    return cache[i];
}

#define SECOP_FN(symname)                                                                                              \
    ({                                                                                                                 \
        static unsigned long fn;                                                                                       \
        if (!fn)                                                                                                       \
            fn = secop_sym(symname);                                                                                   \
        fn;                                                                                                            \
    })

int ksu54_hashtab_insert(void *h, void *key, void *datum)
{
    unsigned long fn = SECOP_FN("hashtab_insert");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)h, (unsigned long)key, (unsigned long)datum, 0, 0);
}

void *ksu54_hashtab_search(void *h, const void *key)
{
    unsigned long fn = SECOP_FN("hashtab_search");
    if (!fn)
        return NULL;
    return (void *)ksu54_blr((void *)fn, (unsigned long)h, (unsigned long)key, 0, 0, 0);
}

int ksu54_avtab_alloc(struct avtab *h, unsigned long nr)
{
    unsigned long fn = SECOP_FN("avtab_alloc");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)h, nr, 0, 0, 0);
}

void ksu54_avtab_destroy(struct avtab *h)
{
    unsigned long fn = SECOP_FN("avtab_destroy");
    if (fn)
        ksu54_blr((void *)fn, (unsigned long)h, 0, 0, 0, 0);
}

struct avtab_node *ksu54_avtab_insert_nonunique(struct avtab *h, struct avtab_key *key,
                                                struct avtab_datum *datum)
{
    unsigned long fn = SECOP_FN("avtab_insert_nonunique");
    if (!fn)
        return NULL;
    return (void *)ksu54_blr((void *)fn, (unsigned long)h, (unsigned long)key,
                             (unsigned long)datum, 0, 0);
}

struct avtab_node *ksu54_avtab_search_node(struct avtab *h, struct avtab_key *key)
{
    unsigned long fn = SECOP_FN("avtab_search_node");
    if (!fn)
        return NULL;
    return (void *)ksu54_blr((void *)fn, (unsigned long)h, (unsigned long)key, 0, 0, 0);
}

struct avtab_node *ksu54_avtab_search_node_next(struct avtab_node *node, int specified)
{
    unsigned long fn = SECOP_FN("avtab_search_node_next");
    if (!fn)
        return NULL;
    return (void *)ksu54_blr((void *)fn, (unsigned long)node, (unsigned long)(long)specified, 0, 0, 0);
}

int ksu54_ebitmap_set_bit(struct ebitmap *e, unsigned long bit, int value)
{
    unsigned long fn = SECOP_FN("ebitmap_set_bit");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)e, bit, (unsigned long)(long)value, 0, 0);
}

int ksu54_ebitmap_get_bit(struct ebitmap *e, unsigned long bit)
{
    unsigned long fn = SECOP_FN("ebitmap_get_bit");
    if (!fn)
        return 0;
    return (int)ksu54_blr((void *)fn, (unsigned long)e, bit, 0, 0, 0);
}

int ksu54_security_read_policy(struct selinux_state *state, void **data, size_t *len)
{
    unsigned long fn = SECOP_FN("security_read_policy");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)state, (unsigned long)data,
                          (unsigned long)len, 0, 0);
}

int ksu54_security_load_policy(struct selinux_state *state, void *data, size_t len)
{
    unsigned long fn = SECOP_FN("security_load_policy");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)state, (unsigned long)data, len, 0, 0);
}

int ksu54_policydb_read(struct policydb *p, struct policy_file *fp)
{
    unsigned long fn = SECOP_FN("policydb_read");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)p, (unsigned long)fp, 0, 0, 0);
}

int ksu54_policydb_write(struct policydb *p, struct policy_file *fp)
{
    unsigned long fn = SECOP_FN("policydb_write");
    if (!fn)
        return -ENOSYS;
    return (int)ksu54_blr((void *)fn, (unsigned long)p, (unsigned long)fp, 0, 0, 0);
}

#endif /* KSU_54 */

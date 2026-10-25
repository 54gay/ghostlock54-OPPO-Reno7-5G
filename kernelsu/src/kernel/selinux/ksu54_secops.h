/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * 5.4 compat: SELinux policy internals are not exported to modules, so the
 * few policy-building helpers used by sepolicy.c / rules54.c are resolved
 * through kallsyms_lookup_name() and invoked through the inline-asm blr
 * trampoline (which also bypasses the compiler's CFI checks).
 *
 * The shim macros are only included from sepolicy.c and rules54.c, after
 * their kernel header includes, so they never leak into other translation
 * units or shadow the header declarations.
 */
#ifndef __KSU54_SECOPS_H
#define __KSU54_SECOPS_H

#ifdef KSU_54

#include <linux/types.h>
#include "include/ksu54_syms.h"

struct avtab;
struct avtab_key;
struct avtab_datum;
struct avtab_node;
struct ebitmap;
struct policydb;
struct policy_file;
struct selinux_state;

int ksu54_hashtab_insert(void *h, void *key, void *datum);
void *ksu54_hashtab_search(void *h, const void *key);
int ksu54_avtab_alloc(struct avtab *h, unsigned long nr);
void ksu54_avtab_destroy(struct avtab *h);
struct avtab_node *ksu54_avtab_insert_nonunique(struct avtab *h, struct avtab_key *key,
                                                struct avtab_datum *datum);
struct avtab_node *ksu54_avtab_search_node(struct avtab *h, struct avtab_key *key);
struct avtab_node *ksu54_avtab_search_node_next(struct avtab_node *node, int specified);
int ksu54_ebitmap_set_bit(struct ebitmap *e, unsigned long bit, int value);
int ksu54_ebitmap_get_bit(struct ebitmap *e, unsigned long bit);

/* whole-policy helpers (5.4 native load path) */
int ksu54_security_read_policy(struct selinux_state *state, void **data, size_t *len);
int ksu54_security_load_policy(struct selinux_state *state, void *data, size_t len);
int ksu54_policydb_read(struct policydb *p, struct policy_file *fp);
int ksu54_policydb_write(struct policydb *p, struct policy_file *fp);

/* Rule-builder shims used inside sepolicy.c */
#define hashtab_insert(h, k, d) ksu54_hashtab_insert((h), (k), (d))
#define hashtab_search(h, k) ksu54_hashtab_search((h), (k))
#define avtab_alloc(h, nr) ksu54_avtab_alloc((h), (nr))
#define avtab_destroy(h) ksu54_avtab_destroy((h))
#define avtab_insert_nonunique(h, k, d) ksu54_avtab_insert_nonunique((h), (k), (d))
#define avtab_search_node(h, k) ksu54_avtab_search_node((h), (k))
#define avtab_search_node_next(n, s) ksu54_avtab_search_node_next((n), (s))
#define ebitmap_set_bit(e, b, v) ksu54_ebitmap_set_bit((e), (b), (v))
#define ebitmap_get_bit(e, b) ksu54_ebitmap_get_bit((e), (b))

#endif /* KSU_54 */
#endif /* __KSU54_SECOPS_H */

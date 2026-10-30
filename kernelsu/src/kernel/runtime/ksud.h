#ifndef __KSU_H_KSUD
#define __KSU_H_KSUD

#include <asm/syscall.h>

#ifdef KSU_54
/* Runtime-resolved: the manager APK's lib/arm64/libksud.so (falls back to
 * /data/adb/ksud when not found). */
const char *ksu54_get_ksud_path(void);
int ksu54_set_ksud_path_from_apk(const char *apk_path);
#define KSUD_PATH (ksu54_get_ksud_path())
#else
#define KSUD_PATH "/data/adb/ksud"
#endif

void ksu_ksud_init();
void ksu_ksud_exit();

void ksu_execve_hook_ksud(const struct pt_regs *regs);
void ksu_execveat_hook_ksud(const struct pt_regs *regs);
void ksu_stop_input_hook_runtime(void);

#endif

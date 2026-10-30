/* 5.4: use the manager APK's bundled libksud.so as the su backend instead of a
 * copy in /data/adb/ksud.  The manager's native library directory is derived
 * from the manager base.apk path found by the throne tracker at init:
 *   /data/app/~~xxx==/me.weishu.kernelsu-yyy==/base.apk
 *   -> .../lib/arm64/libksud.so
 * If the apk-provided lib is not found, fall back to /data/adb/ksud.
 */
#ifdef KSU_54

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/printk.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/types.h>

#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud.h"

static char ksu54_ksud_path[256] __read_mostly;

const char *ksu54_get_ksud_path(void)
{
    if (!ksu54_ksud_path[0])
        return "/data/adb/ksud";
    return ksu54_ksud_path;
}

int ksu54_set_ksud_path_from_apk(const char *apk_path)
{
    char dir[192];
    char cand[256];
    struct path p;
    const char *slash;

    if (!apk_path || !apk_path[0])
        return -EINVAL;

    slash = strrchr(apk_path, '/');
    if (!slash || slash == apk_path)
        return -EINVAL;
    if ((size_t)(slash - apk_path) >= sizeof(dir))
        return -ENAMETOOLONG;

    memcpy(dir, apk_path, slash - apk_path);
    dir[slash - apk_path] = '\0';

    snprintf(cand, sizeof(cand), "%s/lib/arm64/libksud.so", dir);
    if (kern_path(cand, 0, &p) < 0) {
        pr_warn("ksu54: %s not found, keep %s\n", cand, ksu54_get_ksud_path());
        return -ENOENT;
    }
    path_put(&p);

    strscpy(ksu54_ksud_path, cand, sizeof(ksu54_ksud_path));
    pr_info("ksu54: ksud path -> %s\n", ksu54_ksud_path);
    return 0;
}

#endif /* KSU_54 */

/* Debug level switch for the 5.4 bring-up. 0 = inert, 1 = dispatcher+tracepoint,
 * 2 = + read/fstat/input hooks (full). */
#include <linux/module.h>
#include <linux/kernel.h>
#include "klog.h" // IWYU pragma: keep

int ksu54_level __read_mostly = 2;
module_param_named(level, ksu54_level, int, 0);
MODULE_PARM_DESC(level, "5.4 bring-up level: 0=inert, 1=hooks, 2=full");

void ksu54_level_banner(void)
{
    pr_info("ksu54: bring-up level=%d\n", ksu54_level);
}

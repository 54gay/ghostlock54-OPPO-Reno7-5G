/* stub for OPPO private header (source not released publicly).
 * structs below live at the tail of task_struct; only their size affects
 * sizeof(struct task_struct), not offsets of standard fields. */
#ifndef _SCHED_ASSIST_STATUS_STUB_H
#define _SCHED_ASSIST_STATUS_STUB_H
struct task_info {
	u64 _reserved[8];
};
#endif

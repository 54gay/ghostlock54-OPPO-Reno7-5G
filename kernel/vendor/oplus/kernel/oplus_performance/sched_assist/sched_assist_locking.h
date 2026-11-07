/* stub for OPPO private header (source not released publicly). */
#ifndef _SCHED_ASSIST_LOCKING_STUB_H
#define _SCHED_ASSIST_LOCKING_STUB_H

struct task_struct;

static inline void record_locking_info(struct task_struct *tsk, unsigned long stamp)
{
	(void)tsk;
	(void)stamp;
}

#endif

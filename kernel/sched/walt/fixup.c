// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <trace/hooks/cpufreq.h>
#include <trace/hooks/sched.h>
#include <trace/hooks/topology.h>

#include <linux/delay.h>
#include "walt.h"
#include "trace.h"

unsigned int cpuinfo_max_freq_cached;

char sched_lib_name[LIB_PATH_LENGTH];
char sched_lib_task[LIB_PATH_LENGTH];
unsigned int sched_lib_mask_force;

static bool is_sched_lib_based_app(pid_t pid)
{
	const char *name = NULL;
	char *libname, *lib_list;
	struct vm_area_struct *vma;
	char path_buf[LIB_PATH_LENGTH];
	char *tmp_lib_name;
	bool found = false;
	struct task_struct *p;
	struct mm_struct *mm;

	if (strnlen(sched_lib_name, LIB_PATH_LENGTH) == 0)
		return false;

	tmp_lib_name = kmalloc(LIB_PATH_LENGTH, GFP_KERNEL);
	if (!tmp_lib_name)
		return false;

	rcu_read_lock();
	p = pid ? get_pid_task(find_vpid(pid), PIDTYPE_PID) : get_task_struct(current);
	rcu_read_unlock();
	if (!p) {
		kfree(tmp_lib_name);
		return false;
	}

	mm = get_task_mm(p);
	if (mm) {
		MA_STATE(mas, &mm->mm_mt, 0, 0);
		down_read(&mm->mmap_lock);

		mas_for_each(&mas, vma, ULONG_MAX) {
			if (vma->vm_file && vma->vm_flags & VM_EXEC) {
				name = d_path(&vma->vm_file->f_path,
						path_buf, LIB_PATH_LENGTH);
				if (IS_ERR(name))
					goto release_sem;

				strscpy(tmp_lib_name, sched_lib_name, LIB_PATH_LENGTH);
				lib_list = tmp_lib_name;
				while ((libname = strsep(&lib_list, ","))) {
					libname = skip_spaces(libname);
					if (strnstr(name, libname,
						strnlen(name, LIB_PATH_LENGTH))) {
						found = true;
						goto release_sem;
					}
				}
			}
		}

release_sem:
		up_read(&mm->mmap_lock);
		mmput(mm);

	}
	put_task_struct(p);
	kfree(tmp_lib_name);
	return found;
}

bool is_sched_lib_task(void)
{
	if (strnlen(sched_lib_task, LIB_PATH_LENGTH) == 0)
		return false;

	if (strnstr(current->comm, sched_lib_task, strnlen(current->comm, LIB_PATH_LENGTH)))
		return true;

	return false;
}

static void android_rvh_show_max_freq(void *unused, struct cpufreq_policy *policy,
				     unsigned int *max_freq)
{
	if (!cpuinfo_max_freq_cached)
		return;

	if (!(BIT(policy->cpu) & sched_lib_mask_force))
		return;

	if (is_sched_lib_based_app(current->pid) || is_sched_lib_task())
		*max_freq = cpuinfo_max_freq_cached << 1;
}

static void android_rvh_cpu_capacity_show(void *unused,
		unsigned long *capacity, int cpu)
{
	if (!soc_sched_lib_name_capacity)
		return;

	if ((is_sched_lib_based_app(current->pid) || is_sched_lib_task()) &&
			cpu < soc_sched_lib_name_capacity)
		*capacity = 100;
}

/* frequent yielder tracking */
u8 contiguous_yielding_windows;
unsigned int total_yield_cnt;
unsigned int total_sleep_cnt;

void account_yields(u64 wallclock)
{
	struct walt_sched_cluster *cluster = cpu_cluster(task_cpu(current));
	struct smart_freq_cluster_info *smart_freq_info = cluster->smart_freq_info;
	static u64 yield_counting_window_ts;
	u64 delta = wallclock - yield_counting_window_ts;
	unsigned int threshold_cnt = MAX_YIELD_CNT_GLOBAL_THR_DEFAULT;

	if (smart_freq_info->cluster_active_reason & (BIT(PIPELINE_60FPS_OR_LESSER_SMART_FREQ) |
						      BIT(PIPELINE_90FPS_SMART_FREQ) |
						      BIT(PIPELINE_120FPS_OR_GREATER_SMART_FREQ)))
		threshold_cnt = MAX_YIELD_CNT_GLOBAL_THR_PIPELINE;

	/* window boundary crossed */
	if (delta > YIELD_WINDOW_SIZE_NSEC) {
		unsigned int target_threshold_wake = threshold_cnt;
		unsigned int target_threshold_sleep = MAX_YIELD_SLEEP_CNT_GLOBAL_THR;

		/*
		 * if update_window_start comes more than
		 * YIELD_GRACE_PERIOD_NSEC after the YIELD_WINDOW_SIZE_NSEC then
		 * extrapolate the threasholds based on  delta time.
		 */

		if (unlikely(delta > YIELD_WINDOW_SIZE_NSEC + YIELD_GRACE_PERIOD_NSEC)) {
			target_threshold_wake =
				div64_u64(delta * threshold_cnt,
					  YIELD_WINDOW_SIZE_NSEC);
			target_threshold_sleep =
				div64_u64(delta * MAX_YIELD_SLEEP_CNT_GLOBAL_THR,
					  YIELD_WINDOW_SIZE_NSEC);
		}

		if ((total_yield_cnt >= target_threshold_wake) ||
		    (total_sleep_cnt >= target_threshold_sleep / 2)) {
			if (contiguous_yielding_windows < MIN_CONTIGUOUS_YIELDING_WINDOW)
				contiguous_yielding_windows++;
		} else {
			contiguous_yielding_windows = 0;
		}

		trace_sched_yielder(wallclock, yield_counting_window_ts,
				    contiguous_yielding_windows,
				    total_yield_cnt, target_threshold_wake,
				    total_sleep_cnt, target_threshold_sleep,
				    smart_freq_info->cluster_active_reason);

		yield_counting_window_ts = wallclock;
		total_yield_cnt = 0;
		total_sleep_cnt = 0;
	}
}

u8 contiguous_yielding_windows;
DEFINE_PER_CPU(unsigned int, walt_yield_to_sleep);
static void walt_do_sched_yield_before(void *unused, long *skip)
{
	struct walt_task_struct *wts = (struct walt_task_struct *)current->android_vendor_data1;
	struct walt_sched_cluster *cluster;
	struct smart_freq_cluster_info *smart_freq_info;
	bool in_legacy_uncap;

	if (unlikely(walt_disabled))
		return;

	if (!walt_fair_task(current))
		return;

	cluster = cpu_cluster(task_cpu(current));
	smart_freq_info = cluster->smart_freq_info;

	if ((wts->yield_state & YIELD_CNT_MASK) >= MAX_YIELD_CNT_PER_TASK_THR) {
		total_yield_cnt++;
		if (contiguous_yielding_windows >= MIN_CONTIGUOUS_YIELDING_WINDOW) {
			/*
			 * if we are under any legacy frequency uncap(i.e some
			 * load condition, ignore injecting sleep for the
			 * yielding task.
			 */
			in_legacy_uncap =
				!!(smart_freq_info->cluster_active_reason &
								~BIT(NO_REASON_SMART_FREQ));
			if (!in_legacy_uncap) {
				wts->yield_state |= YIELD_INDUCED_SLEEP;
				total_sleep_cnt++;
				*skip = true;
				per_cpu(walt_yield_to_sleep, raw_smp_processor_id())++;
				usleep_range_state(YIELD_SLEEP_TIME_USEC, YIELD_SLEEP_TIME_USEC,
							TASK_INTERRUPTIBLE);
			}
		}
	} else {
		wts->yield_state++;
	}
}

void walt_fixup_init(void)
{
	register_trace_android_rvh_show_max_freq(android_rvh_show_max_freq, NULL);
	register_trace_android_rvh_cpu_capacity_show(android_rvh_cpu_capacity_show, NULL);
	register_trace_android_rvh_before_do_sched_yield(walt_do_sched_yield_before, NULL);
}

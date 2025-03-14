// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <linux/cpufreq.h>
#include <linux/kmemleak.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/hashtable.h>
#include <linux/jhash.h>
#include <trace/hooks/cpuidle.h>
#include <trace/hooks/cpufreq.h>

#include "walt.h"
#include "trace.h"
#include "sysctl_walt_stats.h"

/*******************************cpufreq time stats on per cpu**********************************/
struct cpu_freq_time {
	struct hlist_node hash_node;
	unsigned int freq;
	ktime_t total, begin;
};

/* calculate min/max frequency */
struct cal_freq {
	u64	min_freq_ns;
	u64	max_freq_ns;
	u64	min_max_freq_update_time;
};

static bool register_hook_enable;
static int enable_stat_freq;
static char statistic_dump[PAGE_SIZE*2];

#define HASH_ORDER 5
#define HASH_SEED 3532811

/*
 * The index is frequency and the value is the pointer to cpu_freq_time in hash table
 */

struct cpufreq_time_info {
	DECLARE_HASHTABLE(cpu_freq_time_table, HASH_ORDER);
	unsigned int cur_cpufreq;
	spinlock_t freq_time_lock;
};

static DEFINE_PER_CPU(struct cpufreq_time_info, cpufreq_time_info);

#define CPUFREQ_TABLE_PTR(cpu) \
	(per_cpu(cpufreq_time_info, cpu).cpu_freq_time_table)
#define CUR_CPUFREQ(cpu) (per_cpu(cpufreq_time_info, cpu).cur_cpufreq)
#define CPUFREQ_LOCK(cpu) \
	((spinlock_t *)&per_cpu(cpufreq_time_info, cpu).freq_time_lock)

static inline u32 cpufreq_hash(unsigned int val)
{
	return jhash(&val, sizeof(val), HASH_SEED);
}

static int cpufreq_store_init(unsigned int cpu, unsigned int freq)
{
	struct cpu_freq_time *freq_time =
		kmalloc(sizeof(*freq_time), GFP_ATOMIC);

	if (!freq_time) {
		pr_err("cpufreq: %s:%d memory allocation failed\n", __func__,
		       __LINE__);
		return -ENOMEM;
	}

	freq_time->total = 0;
	freq_time->begin = 0;
	freq_time->freq = freq;
	hash_add(CPUFREQ_TABLE_PTR(cpu), &freq_time->hash_node,
		 cpufreq_hash(freq));

	return 0;
}

static struct cpu_freq_time *cpufreq_load(unsigned int cpu, unsigned int freq)
{
	struct cpu_freq_time *freq_time = NULL;

	hash_for_each_possible(CPUFREQ_TABLE_PTR(cpu), freq_time, hash_node,
			       cpufreq_hash(freq)) {
		if (freq_time->freq == freq)
			break;
	}

	return freq_time;
}

static void cpufreq_update(unsigned int cpu, unsigned int freq)
{
	struct cpu_freq_time *freq_time = NULL;

	freq_time = cpufreq_load(cpu, freq);
	if (!freq_time) {
		pr_err("cpufreq: %s:%d freq %u not found on the table\n",
		       __func__, __LINE__, freq);
		return;
	}

	freq_time->total = 0;
	freq_time->begin = ktime_get();
}

static void cpufreq_dump_table(unsigned int cpu, int *pos)
{
	struct cpu_freq_time *freq_time;
	int bkt;

	hash_for_each(CPUFREQ_TABLE_PTR(cpu), bkt, freq_time, hash_node) {
		if (freq_time == NULL || !hash_hashed(&freq_time->hash_node))
			continue;

		*pos += snprintf(statistic_dump + *pos,
				 sizeof(statistic_dump) - *pos, "%uKHz %llu\n",
				 freq_time->freq, freq_time->total);
	}
}

static void cpufreq_timing_idle_enter(void *unused, int *state,
					struct cpuidle_device *dev)
{
	unsigned long flags;
	unsigned int cpu = dev->cpu;
	unsigned int cur_freq = cpufreq_quick_get(cpu);
	struct cpu_freq_time *freq_time;

	spin_lock_irqsave(CPUFREQ_LOCK(cpu), flags);

	freq_time = cpufreq_load(cpu, cur_freq);
	if (!freq_time) {
		spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
		return;
	}

	freq_time->total += ktime_get() - freq_time->begin;

	spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
}

static void cpufreq_timing_idle_exit(void *unused, int state,
					struct cpuidle_device *dev)
{
	unsigned long flags;
	unsigned int cpu = dev->cpu;
	unsigned int cur_freq = cpufreq_quick_get(cpu);
	struct cpu_freq_time *freq_time;

	spin_lock_irqsave(CPUFREQ_LOCK(cpu), flags);

	freq_time = cpufreq_load(cpu, cur_freq);
	if (!freq_time) {
		spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
		return;
	}

	freq_time->begin = ktime_get();
	spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
}

static void cpufreq_timing_transition(void *unused, struct cpufreq_policy *policy)
{
	if (!register_hook_enable)
		return;

	int cpu;
	unsigned long flags;

	for_each_cpu(cpu, policy->related_cpus) {
		struct cpu_freq_time *freq_time_last, *freq_time_new;
		unsigned int new_freq = cpufreq_quick_get(cpu);

		freq_time_last = cpufreq_load(cpu, CUR_CPUFREQ(cpu));
		freq_time_new = cpufreq_load(cpu, new_freq);

		spin_lock_irqsave(CPUFREQ_LOCK(cpu), flags);

		rcu_read_lock();

		struct cpuidle_state *idle = idle_get_state(cpu_rq(cpu));

		rcu_read_unlock();

		if (!idle)
			freq_time_last->total +=
				ktime_get() - freq_time_last->begin;

		freq_time_new->begin = ktime_get();

		CUR_CPUFREQ(cpu) = new_freq;

		spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
	}
}

static void cpufreq_time_hook_register(void)
{
	if (!register_hook_enable) {
		register_trace_android_vh_cpu_idle_enter(cpufreq_timing_idle_enter,
								NULL);
		register_trace_android_vh_cpu_idle_exit(cpufreq_timing_idle_exit,
								NULL);
		/*It's a restricted hook, no unregister*/
		register_trace_android_rvh_cpufreq_transition(cpufreq_timing_transition,
								NULL);
		register_hook_enable = true;
	}
}

static void cpufreq_time_hook_unregister(void)
{
	if (register_hook_enable) {
		unregister_trace_android_vh_cpu_idle_enter(
			cpufreq_timing_idle_enter, NULL);
		unregister_trace_android_vh_cpu_idle_exit(
			cpufreq_timing_idle_exit, NULL);
		register_hook_enable = false;
	}
}

static int cpufreq_time_table_init(struct cpufreq_policy *policy, int cpu)
{
	int ret;
	unsigned int freq;
	struct cpufreq_frequency_table *freq_table;

	cpufreq_for_each_valid_entry(freq_table, policy->freq_table) {
		ret = cpufreq_store_init(cpu, freq_table->frequency);
		if (ret)
			return ret;
	}

	freq = cpufreq_quick_get(cpu);
	cpufreq_update(cpu, freq);

	CUR_CPUFREQ(cpu) = freq;

	return 0;
}

static int walt_stats_init(void)
{
	int cpu, ret;
	struct cpufreq_policy *policy;

	for_each_possible_cpu(cpu) {
		policy = cpufreq_cpu_get_raw(cpu);
		if (!policy)
			goto err_init;

		ret = cpufreq_time_table_init(policy, cpu);
		if (ret)
			goto err_init;

		spin_lock_init(CPUFREQ_LOCK(cpu));
	}

	cpufreq_time_hook_register();

	return 0;

err_init:
	return -EINVAL;
}

static void cpufreq_free_table(unsigned int cpu)
{
	struct cpu_freq_time *freq_time = NULL;
	int bkt;

	hash_for_each(CPUFREQ_TABLE_PTR(cpu), bkt, freq_time, hash_node) {
		hash_del(&freq_time->hash_node);
		kfree(freq_time);
	}
}

static void walt_stats_exit(void)
{
	int cpu;

	cpufreq_time_hook_unregister();

	for_each_possible_cpu(cpu)
		cpufreq_free_table(cpu);
}

static int cpufreq_time_statistic_dump_handler(const struct ctl_table *table,
					       int write, void *buffer,
					       size_t *lenp, loff_t *ppos)
{
	int cpu, pos = 0;
	unsigned long flags;
	struct ctl_table tmp = {
		.data	= &enable_stat_freq,
		.maxlen	= sizeof(int),
		.mode	= table->mode,
		.extra1	= SYSCTL_ZERO,
		.extra2	= SYSCTL_ONE,
	};

	if (write) {
		char *value = buffer;

		if ('1' == value[0] && !enable_stat_freq) {
			if (walt_stats_init())
				return -EINVAL;
		} else if ('0' == value[0] && enable_stat_freq)
			walt_stats_exit();

		return proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	}

	if (!enable_stat_freq)
		return proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	tmp.data = &statistic_dump;
	tmp.maxlen = sizeof(statistic_dump);
	tmp.mode = table->mode;

	for_each_possible_cpu(cpu) {
		struct cpu_freq_time *freq_time = NULL;
		unsigned long cpufreq;

		rcu_read_lock();

		struct cpuidle_state *idle = idle_get_state(cpu_rq(cpu));

		rcu_read_unlock();

		cpufreq = cpufreq_quick_get(cpu);
		freq_time = cpufreq_load(cpu, cpufreq);

		spin_lock_irqsave(CPUFREQ_LOCK(cpu), flags);

		if (!idle) {
			freq_time->total += ktime_get() - freq_time->begin;
			freq_time->begin = ktime_get();
		}

		pos += snprintf(statistic_dump + pos,
				sizeof(statistic_dump) - pos, "cpu%d\n", cpu);
		cpufreq_dump_table(cpu, &pos);
		spin_unlock_irqrestore(CPUFREQ_LOCK(cpu), flags);
	}

	return proc_dostring(&tmp, write, buffer, lenp, ppos);
}

/*******************************add frequency reason counters***********************************/
static int enable_freq_reason;
/* frequency reason counters */
#define CPUFREQ_REASONS		22
unsigned int cpufreq_reasons_counters[CPUFREQ_REASONS][MAX_CLUSTERS];

void assign_reasons_counter(struct waltgov_policy *wg_policy)
{
	struct cpufreq_policy *policy = wg_policy->policy;
	struct waltgov_cpu *wg_driv_cpu = &per_cpu(waltgov_cpu, wg_policy->driving_cpu);
	struct walt_sched_cluster *cluster = cpu_cluster(policy->cpu);
	int id = cluster->id;
	unsigned int reasons = wg_driv_cpu->reasons;

	if (!reasons) {
		cpufreq_reasons_counters[CPUFREQ_REASON_LOAD][id]++;
		return;
	}

	for (int i = 0; i < CPUFREQ_REASONS; i++) {
		if (BIT(i) & reasons)
			cpufreq_reasons_counters[i][id]++;
	}
}

static int cpufreq_reasons_dump_handler(const struct ctl_table *table,
					int write, void *buffer, size_t *lenp,
					loff_t *ppos)
{
	int len = 0;
	int i = 0;
	char counter_buffer[1024];
	struct ctl_table tmp = {
		.data	= &enable_freq_reason,
		.maxlen	= sizeof(int),
		.mode	= table->mode,
		.extra1	= SYSCTL_ZERO,
		.extra2	= SYSCTL_ONE,
	};

	if (write || !enable_freq_reason)
		return proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	tmp.data = &counter_buffer;
	tmp.maxlen = sizeof(counter_buffer);
	tmp.mode = table->mode;

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\n\nFreq_Reason ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "  Cluster%d", i);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nLOAD ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[0][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nBTR ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[1][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nPL ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[2][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nEARLY_DET ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[3][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nRTG_BOOST ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[4][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nHISPEED ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[5][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nNWD ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[6][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nFREQ_AGR ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[7][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nKSOFTIRQD ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[8][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nTT_LOAD ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[9][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nSUH ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[10][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nADAPTIVE_LOW ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[11][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nADAPTIVE_HIGH ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[12][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nSMART_FREQ ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[13][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nHIGH_PERF_CAP ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[14][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nPARTIAL_HALT_CAP ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[15][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nTRAILBLAZER_STATE ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[16][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nTRAILBLAZER_CPU ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[17][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nADAPTIVE_LVL_1 ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[18][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nIPC_SMART_FREQ ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[19][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nUCLAMP ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[20][i]);

	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\nPIPELINE_BUSY ");
	for (i = 0; i < num_sched_clusters; i++)
		len += snprintf(counter_buffer + len,
				sizeof(counter_buffer) - len, "%u ",
				cpufreq_reasons_counters[21][i]);
	len += snprintf(counter_buffer + len, sizeof(counter_buffer) - len,
			"\n");

	return proc_dostring(&tmp, write, buffer, lenp, ppos);
}

/*******************************add debug counters on load balance***************************/
static int enable_load_balance;

atomic64_t walt_active_balance_migration_counter[WALT_NR_CPUS];
atomic64_t walt_idle_balance_migration_counter[WALT_NR_CPUS];

static int debug_counter_dump_handler(const struct ctl_table *table, int write,
				      void *buffer, size_t *lenp, loff_t *ppos)
{
	int len = 0;
	int i = 0;
	char debug_buffer[1024];
	struct ctl_table tmp = {
		.data	= &enable_load_balance,
		.maxlen	= sizeof(int),
		.mode	= table->mode,
		.extra1	= SYSCTL_ZERO,
		.extra2	= SYSCTL_ONE,
	};

	if (write || !enable_load_balance)
		return proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	tmp.data = &debug_buffer;
	tmp.maxlen = sizeof(debug_buffer);
	tmp.mode = table->mode;

	len += snprintf(debug_buffer + len, sizeof(debug_buffer) - len,
			"walt_active_balance_migration_counter ");

	for (i = 0; i < WALT_NR_CPUS; i++)
		len += snprintf(debug_buffer + len, sizeof(debug_buffer) - len, "%lu ",
				(unsigned long)atomic64_read(
					&walt_active_balance_migration_counter[i]));

	len += snprintf(debug_buffer + len, sizeof(debug_buffer) - len,
			"\nwalt_idle_balance_migration_counter ");

	for (i = 0; i < WALT_NR_CPUS; i++)
		len += snprintf(debug_buffer + len, sizeof(debug_buffer) - len, "%lu ",
				(unsigned long)atomic64_read(
					&walt_idle_balance_migration_counter[i]));

	return proc_dostring(&tmp, write, buffer, lenp, ppos);

}

/*******************************calculate min/max frequency ***********************************/
static int enable_cal_freq;
struct cal_freq freq_ns_stats;
static char calfreq_buffer[PAGE_SIZE];

void update_min_max_freq_ns(struct waltgov_policy *wg_policy, u64 time)
{
	struct cpufreq_policy *policy = wg_policy->policy;
	u64 delta_ns;

	if (policy->cur == policy->min) {
		if (!freq_ns_stats.min_max_freq_update_time)
			delta_ns = time - wg_policy->last_freq_update_time;
		else
			delta_ns = time - freq_ns_stats.min_max_freq_update_time;

		freq_ns_stats.min_freq_ns += delta_ns;
		freq_ns_stats.min_max_freq_update_time = time;
	} else if (policy->cur == policy->max) {
		if (!freq_ns_stats.min_max_freq_update_time)
			delta_ns = time - wg_policy->last_freq_update_time;
		else
			delta_ns = time - freq_ns_stats.min_max_freq_update_time;

		freq_ns_stats.max_freq_ns += delta_ns;
		freq_ns_stats.min_max_freq_update_time = time;
	}
}

void rollover_freq_ns_stats(u64 value)
{
	freq_ns_stats.min_max_freq_update_time = 0;
}

static int cal_freq_dump_handler(const struct ctl_table *table, int write,
				 void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table tmp = {
		.data = &enable_cal_freq,
		.maxlen = sizeof(int),
		.mode = table->mode,
		.extra1	= SYSCTL_ZERO,
		.extra2	= SYSCTL_ONE,
	};

	if (write || !enable_cal_freq)
		return proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	tmp.data = &calfreq_buffer;
	tmp.maxlen = sizeof(calfreq_buffer);
	tmp.mode = table->mode;

	scnprintf(calfreq_buffer, PAGE_SIZE,
		  "min_freq_ns: %llu\nmax_freq_ns: %llu\n",
		  freq_ns_stats.min_freq_ns, freq_ns_stats.max_freq_ns);

	return proc_dostring(&tmp, write, buffer, lenp, ppos);
}

static struct ctl_table walt_stats_table[] = {
	{
		.procname	= "freq_time",
		.data		= &enable_stat_freq,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cpufreq_time_statistic_dump_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "freq_reason",
		.data		= &enable_freq_reason,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cpufreq_reasons_dump_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "lb_debug_counters",
		.data		= &enable_load_balance,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= debug_counter_dump_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "cal_min_max_freq",
		.data		= &enable_cal_freq,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= cal_freq_dump_handler,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

void walt_stats_sysctl_init(void)
{
	struct ctl_table_header *hdr;

	hdr = register_sysctl("walt_stats", walt_stats_table);

	kmemleak_not_leak(hdr);
}

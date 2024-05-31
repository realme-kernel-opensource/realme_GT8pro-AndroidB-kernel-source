// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "walt.h"
#include "trace.h"

static bool smart_freq_init_done;
static inline bool has_internal_freq_limit_changed(struct walt_sched_cluster *cluster)
{
	unsigned int internal_freq;
	int i;

	internal_freq = cluster->walt_internal_freq_limit;
	cluster->walt_internal_freq_limit = cluster->max_freq;

	if (likely(!waltgov_disabled)) {
		for (i = 0; i < MAX_FREQ_CAP; i++)
			cluster->walt_internal_freq_limit = min(freq_cap[i][cluster->id],
					     cluster->walt_internal_freq_limit);
	}

	return cluster->walt_internal_freq_limit != internal_freq;
}

static void update_smart_freq_capacities_one_cluster(struct walt_sched_cluster *cluster)
{
	int cpu;

	if (!smart_freq_init_done)
		return;

	if (has_internal_freq_limit_changed(cluster)) {
		for_each_cpu(cpu, &cluster->cpus)
			update_cpu_capacity_helper(cpu);
	}
}

void update_smart_freq_capacities(void)
{
	struct walt_sched_cluster *cluster;

	if (!smart_freq_init_done)
		return;

	for_each_sched_cluster(cluster)
		update_smart_freq_capacities_one_cluster(cluster);
}

/*
 *  Update the active smart freq reason for the cluster.
 */
static void smart_freq_update_one_cluster(struct walt_sched_cluster *cluster,
			uint32_t current_reasons, u64 wallclock, int nr_big, u32 wakeup_ctr_sum)
{
	uint32_t current_reason, cluster_active_reason;
	struct smart_freq_cluster_info *smart_freq_info = cluster->smart_freq_info;
	unsigned long max_cap =
		smart_freq_info->legacy_reason_config[NO_REASON_SMART_FREQ].freq_allowed;
	int max_reason, i;
	unsigned long old_freq_cap = freq_cap[SMART_FREQ][cluster->id];

	for (i = 0; i < LEGACY_SMART_FREQ; i++) {
		current_reason = current_reasons & BIT(i);
		cluster_active_reason = smart_freq_info->cluster_active_reason & BIT(i);

		if (current_reason) {
			smart_freq_info->legacy_reason_status[i].deactivate_ns = 0;
			smart_freq_info->cluster_active_reason |= BIT(i);
		} else if (cluster_active_reason) {
			if (!smart_freq_info->legacy_reason_status[i].deactivate_ns)
				smart_freq_info->legacy_reason_status[i].deactivate_ns = wallclock;
		}

		if (cluster_active_reason) {
			/*
			 * For reasons with deactivation hysteresis, check here if we have
			 * crossed the hysteresis time and then deactivate the reason.
			 * We are relying on scheduler tick path to call this function
			 * thus deactivation of reason is only at tick
			 * boundary.
			 */
			if (smart_freq_info->legacy_reason_status[i].deactivate_ns) {
				u64 delta = wallclock -
					smart_freq_info->legacy_reason_status[i].deactivate_ns;
				if (delta >= smart_freq_info->legacy_reason_config[i].hyst_ns) {
					smart_freq_info->legacy_reason_status[i].deactivate_ns = 0;
					smart_freq_info->cluster_active_reason &= ~BIT(i);
					continue;
				}
			}
			if (max_cap < smart_freq_info->legacy_reason_config[i].freq_allowed) {
				max_cap = smart_freq_info->legacy_reason_config[i].freq_allowed;
				max_reason = i;
			}
		}
	}

	trace_sched_freq_uncap(cluster->id, nr_big, wakeup_ctr_sum, current_reasons,
				smart_freq_info->cluster_active_reason, max_cap, max_reason);

	if (old_freq_cap == max_cap)
		return;

	freq_cap[SMART_FREQ][cluster->id] = max_cap;

	update_smart_freq_capacities_one_cluster(cluster);

	walt_irq_work_queue(&walt_cpufreq_irq_work);
}

#define UNCAP_THRES		300000000
#define UTIL_THRESHOLD		90
static bool thres_based_uncap(u64 window_start, struct walt_sched_cluster *cluster)
{
	int cpu;
	bool cluster_high_load = false, sustained_load = false;
	unsigned long freq_capacity, tgt_cap;
	unsigned long tgt_freq =
		cluster->smart_freq_info->legacy_reason_config[NO_REASON_SMART_FREQ].freq_allowed;
	struct walt_rq *wrq;

	freq_capacity = arch_scale_cpu_capacity(cpumask_first(&cluster->cpus));
	tgt_cap = mult_frac(freq_capacity, tgt_freq, cluster->max_possible_freq);

	for_each_cpu(cpu, &cluster->cpus) {
		wrq = &per_cpu(walt_rq, cpu);
		if (wrq->util >= mult_frac(tgt_cap, UTIL_THRESHOLD, 100)) {
			cluster_high_load = true;
			if (!cluster->found_ts)
				cluster->found_ts = window_start;
			else if ((window_start - cluster->found_ts) >= UNCAP_THRES)
				sustained_load = true;

			break;
		}
	}
	if (!cluster_high_load)
		cluster->found_ts = 0;

	return sustained_load;
}

#define BIG_TASKCNT_LIMIT	6
#define WAKEUP_CNT		100
/*
 * reason is a two part bitmap
 * 15 - 0 : reason type
 * 31 - 16: changed state of reason
 * this will help to pass multiple reasons at once and avoid multiple calls.
 */
/*
 * This will be called from irq work path only
 */
void smart_freq_update_reason_common(u64 wallclock, int nr_big, u32 wakeup_ctr_sum)
{
	struct walt_sched_cluster *cluster;
	bool current_state;
	uint32_t cluster_reasons;
	int i;
	int cluster_active_reason;
	uint32_t cluster_participation_mask;
	bool sustained_load = false;

	if (!smart_freq_init_done)
		return;

	for_each_sched_cluster(cluster)
		sustained_load |= thres_based_uncap(wallclock, cluster);

	for_each_sched_cluster(cluster) {
		cluster_reasons = 0;
		i = cluster->id;
		cluster_participation_mask =
			cluster->smart_freq_info->smart_freq_participation_mask;
		/*
		 *  NO_REASON
		 */
		if (cluster_participation_mask & BIT(NO_REASON_SMART_FREQ))
			cluster_reasons |= BIT(NO_REASON_SMART_FREQ);

		/*
		 * BOOST
		 */
		if (cluster_participation_mask & BIT(BOOST_SMART_FREQ)) {
			current_state = is_storage_boost() || is_full_throttle_boost();
			if (current_state)
				cluster_reasons |= BIT(BOOST_SMART_FREQ);
		}

		/*
		 * TRAILBLAZER
		 */
		if (cluster_participation_mask & BIT(TRAILBLAZER_SMART_FREQ)) {
			current_state = trailblazer_state;
			if (current_state)
				cluster_reasons |= BIT(TRAILBLAZER_SMART_FREQ);
		}

		/*
		 * SBT
		 */
		if (cluster_participation_mask & BIT(SBT_SMART_FREQ)) {
			current_state = prev_is_sbt;
			if (current_state)
				cluster_reasons |= BIT(SBT_SMART_FREQ);
		}

		/*
		 * BIG_TASKCNT
		 */
		if (cluster_participation_mask & BIT(BIG_TASKCNT_SMART_FREQ)) {
			current_state = (nr_big >= BIG_TASKCNT_LIMIT) &&
						(wakeup_ctr_sum < WAKEUP_CNT);
			if (current_state)
				cluster_reasons |= BIT(BIG_TASKCNT_SMART_FREQ);
		}

		/*
		 * SUSTAINED_HIGH_UTIL
		 */
		if (cluster_participation_mask & BIT(SUSTAINED_HIGH_UTIL_SMART_FREQ)) {
			current_state = sustained_load;
			if (current_state)
				cluster_reasons |= BIT(SUSTAINED_HIGH_UTIL_SMART_FREQ);
		}

		/*
		 * PIPELINE
		 */
		if (cluster_participation_mask & BIT(PIPELINE_SMART_FREQ)) {
			current_state = pipeline_nr || sysctl_sched_heavy_nr ||
						sysctl_sched_pipeline_util_thres;
			if (current_state)
				cluster_reasons |= BIT(PIPELINE_SMART_FREQ);
		}

		/*
		 * THERMAL_ROTATION
		 */
		if (cluster_participation_mask & BIT(THERMAL_ROTATION_SMART_FREQ)) {
			current_state = (oscillate_cpu != -1);
			if (current_state)
				cluster_reasons |= BIT(THERMAL_ROTATION_SMART_FREQ);
		}

		cluster_active_reason = cluster->smart_freq_info->cluster_active_reason;
		/* update the reasons for all the clusters */
		if (cluster_reasons || cluster_active_reason)
			smart_freq_update_one_cluster(cluster, cluster_reasons, wallclock,
						      nr_big, wakeup_ctr_sum);
	}
}

/* Common config for 4 cluster system */
struct smart_freq_cluster_info default_freq_config[MAX_CLUSTERS];

void smart_freq_init(const char *soc)
{
	struct walt_sched_cluster *cluster;
	int i = 0, j;

	for_each_sched_cluster(cluster) {
		cluster->smart_freq_info = &default_freq_config[i];
		cluster->smart_freq_info->smart_freq_participation_mask = BIT(NO_REASON_SMART_FREQ);
		cluster->smart_freq_info->cluster_active_reason = 0;
		freq_cap[SMART_FREQ][cluster->id] = FREQ_QOS_MAX_DEFAULT_VALUE;

		memset(cluster->smart_freq_info->legacy_reason_status, 0,
		       sizeof(struct smart_freq_legacy_reason_status) *
		       LEGACY_SMART_FREQ);
		memset(cluster->smart_freq_info->legacy_reason_config, 0,
		       sizeof(struct smart_freq_legacy_reason_config) *
		       LEGACY_SMART_FREQ);

		for (j = 0; j < LEGACY_SMART_FREQ; j++) {
			cluster->smart_freq_info->legacy_reason_config[j].freq_allowed =
				FREQ_QOS_MAX_DEFAULT_VALUE;
		}

		i++;
	}

	smart_freq_init_done = true;
	update_smart_freq_capacities();
}


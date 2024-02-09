// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include "walt.h"
#include "trace.h"
#include <soc/qcom/socinfo.h>

unsigned long __read_mostly soc_flags;
unsigned int trailblazer_floor_freq[MAX_CLUSTERS];

void walt_config(void)
{
	int i, j;
	const char *name = socinfo_get_id_string();

	sysctl_sched_group_upmigrate_pct = 100;
	sysctl_sched_group_downmigrate_pct = 95;
	sysctl_sched_task_unfilter_period = 100000000;
	sysctl_sched_window_stats_policy = WINDOW_STATS_MAX_RECENT_AVG;
	sysctl_sched_ravg_window_nr_ticks = (HZ / NR_WINDOWS_PER_SEC);
	sched_load_granule = DEFAULT_SCHED_RAVG_WINDOW / NUM_LOAD_INDICES;
	sysctl_sched_coloc_busy_hyst_enable_cpus = 112;
	sysctl_sched_util_busy_hyst_enable_cpus = 255;
	sysctl_sched_coloc_busy_hyst_max_ms = 5000;
	sched_ravg_window = DEFAULT_SCHED_RAVG_WINDOW;
	sysctl_input_boost_ms = 40;
	sysctl_sched_min_task_util_for_boost = 51;
	sysctl_sched_min_task_util_for_uclamp = 51;
	sysctl_sched_min_task_util_for_colocation = 35;
	sysctl_sched_many_wakeup_threshold = WALT_MANY_WAKEUP_DEFAULT;
	sysctl_walt_rtg_cfs_boost_prio = 99; /* disabled by default */
	sysctl_sched_sync_hint_enable = 1;
	sysctl_panic_on_walt_bug = walt_debug_initial_values();
	sysctl_sched_skip_sp_newly_idle_lb = 1;
	sysctl_sched_hyst_min_coloc_ns = 80000000;
	sysctl_sched_idle_enough = SCHED_IDLE_ENOUGH_DEFAULT;
	sysctl_sched_cluster_util_thres_pct = SCHED_CLUSTER_UTIL_THRES_PCT_DEFAULT;
	sysctl_em_inflate_pct = 100;
	sysctl_em_inflate_thres = 1024;
	sysctl_max_freq_partial_halt = FREQ_QOS_MAX_DEFAULT_VALUE;

	for (i = 0; i < MAX_MARGIN_LEVELS; i++) {
		sysctl_sched_capacity_margin_up_pct[i] = 95; /* ~5% margin */
		sysctl_sched_capacity_margin_dn_pct[i] = 85; /* ~15% margin */
		sysctl_sched_early_up[i] = 1077;
		sysctl_sched_early_down[i] = 1204;
	}

	for (i = 0; i < WALT_NR_CPUS; i++) {
		sysctl_sched_coloc_busy_hyst_cpu[i] = 39000000;
		sysctl_sched_coloc_busy_hyst_cpu_busy_pct[i] = 10;
		sysctl_sched_util_busy_hyst_cpu[i] = 5000000;
		sysctl_sched_util_busy_hyst_cpu_util[i] = 15;
		sysctl_input_boost_freq[i] = 0;
	}

	for (i = 0; i < MAX_CLUSTERS; i++) {
		sysctl_fmax_cap[i] = FREQ_QOS_MAX_DEFAULT_VALUE;
		high_perf_cluster_freq_cap[i] = FREQ_QOS_MAX_DEFAULT_VALUE;
		sysctl_sched_idle_enough_clust[i] = SCHED_IDLE_ENOUGH_DEFAULT;
		sysctl_sched_cluster_util_thres_pct_clust[i] = SCHED_CLUSTER_UTIL_THRES_PCT_DEFAULT;
		trailblazer_floor_freq[i] = 0;
	}

	for (i = 0; i < MAX_FREQ_CAP; i++) {
		for (j = 0; j < MAX_CLUSTERS; j++)
			fmax_cap[i][j] = FREQ_QOS_MAX_DEFAULT_VALUE;
	}

	soc_feat_set(SOC_ENABLE_CONSERVATIVE_BOOST_TOPAPP);
	soc_feat_set(SOC_ENABLE_CONSERVATIVE_BOOST_FG);
	soc_feat_set(SOC_ENABLE_UCLAMP_BOOSTED);
	soc_feat_set(SOC_ENABLE_PER_TASK_BOOST_ON_MID);

	/* return if socinfo is not available */
	if (!name)
		return;

	soc_feat_set(SOC_AVAILABLE);
	if (!strcmp(name, "SUN")) {
		sysctl_sched_suppress_region2		= 1;
		soc_feat_unset(SOC_ENABLE_CONSERVATIVE_BOOST_TOPAPP);
		soc_feat_unset(SOC_ENABLE_CONSERVATIVE_BOOST_FG);
		soc_feat_unset(SOC_ENABLE_UCLAMP_BOOSTED);
		soc_feat_unset(SOC_ENABLE_PER_TASK_BOOST_ON_MID);
		trailblazer_floor_freq[0] = 1000000;
	} else if (!strcmp(name, "PINEAPPLE")) {
		soc_feat_set(SOC_ENABLE_SILVER_RT_SPREAD);
		soc_feat_set(SOC_ENABLE_ASYM_SIBLINGS);
		soc_feat_set(SOC_ENABLE_BOOST_TO_NEXT_CLUSTER);
	}
}

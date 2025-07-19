// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include "walt.h"
#include "trace.h"
#include <soc/qcom/socinfo.h>

unsigned long __read_mostly soc_flags;
unsigned int trailblazer_floor_freq[MAX_CLUSTERS];
cpumask_t asym_cap_sibling_cpus;
cpumask_t pipeline_sync_cpus;
cpumask_t storage_boost_cpus;
int oscillate_period_ns;
int soc_sched_lib_name_capacity;
#define PIPELINE_BUSY_THRESH_8MS_WINDOW 7
#define PIPELINE_BUSY_THRESH_12MS_WINDOW 11
#define PIPELINE_BUSY_THRESH_16MS_WINDOW 15
unsigned int gold_cluster_id, prime_cluster_id;
unsigned int soc_cluster_freq_table_size[MAX_CLUSTERS];
unsigned int soc_cluster_freq_table[MAX_CLUSTERS][MAX_FREQ_TABLE_ENTRIES];

void walt_config(void)
{
	int i, j, cpu;
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
	asym_cap_sibling_cpus = CPU_MASK_NONE;
	pipeline_sync_cpus = CPU_MASK_NONE;
	storage_boost_cpus = CPU_MASK_NONE;
	for_each_possible_cpu(cpu) {
		for (i = 0; i < LEGACY_SMART_FREQ; i++) {
			if (i)
				smart_freq_legacy_reason_hyst_ms[i][cpu] = 4;
			else
				smart_freq_legacy_reason_hyst_ms[i][cpu] = 0;
		}
	}

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
		sysctl_freq_cap[i] = FREQ_QOS_MAX_DEFAULT_VALUE;
		high_perf_cluster_freq_cap[i] = FREQ_QOS_MAX_DEFAULT_VALUE;
		sysctl_sched_idle_enough_clust[i] = SCHED_IDLE_ENOUGH_DEFAULT;
		sysctl_sched_cluster_util_thres_pct_clust[i] = SCHED_CLUSTER_UTIL_THRES_PCT_DEFAULT;
		trailblazer_floor_freq[i] = 0;
		for (j = 0; j < MAX_CLUSTERS; j++) {
			load_sync_util_thres[i][j] = 0;
			load_sync_low_pct[i][j] = 0;
			load_sync_high_pct[i][j] = 0;
		}
	}

	for (i = 0; i < MAX_FREQ_CAP; i++) {
		for (j = 0; j < MAX_CLUSTERS; j++)
			freq_cap[i][j] = FREQ_QOS_MAX_DEFAULT_VALUE;
	}

	sysctl_sched_lrpb_active_ms[0] = PIPELINE_BUSY_THRESH_8MS_WINDOW;
	sysctl_sched_lrpb_active_ms[1] = PIPELINE_BUSY_THRESH_12MS_WINDOW;
	sysctl_sched_lrpb_active_ms[2] = PIPELINE_BUSY_THRESH_16MS_WINDOW;
	soc_feat_set(SOC_ENABLE_CONSERVATIVE_BOOST_TOPAPP_BIT);
	soc_feat_set(SOC_ENABLE_CONSERVATIVE_BOOST_FG_BIT);
	soc_feat_set(SOC_ENABLE_UCLAMP_BOOSTED_BIT);
	soc_feat_set(SOC_ENABLE_PER_TASK_BOOST_ON_MID_BIT);
	soc_feat_set(SOC_ENABLE_COLOCATION_PLACEMENT_BOOST_BIT);
	soc_feat_set(SOC_ENABLE_PIPELINE_SWAPPING_BIT);
	soc_feat_set(SOC_ENABLE_THERMAL_HALT_LOW_FREQ_BIT);

	pipeline_swap_util_th = 0;
	prime_cluster_id = num_sched_clusters - 1;
	gold_cluster_id = num_sched_clusters > 2 ? 1 : 0;

	/* Initialize smart freq configurations */
	smart_freq_init(name);
	/* return if socinfo is not available */
	if (!name)
		return;

	if (!strcmp(name, "SUN") || !strcmp(name, "SUNP") || !strcmp(name, "CANOE")
			|| !strcmp(name, "ALOR_INTERPOSER") || !strcmp(name, "ALOR")) {
		sysctl_sched_suppress_region2		= 1;
		soc_feat_unset(SOC_ENABLE_CONSERVATIVE_BOOST_TOPAPP_BIT);
		soc_feat_unset(SOC_ENABLE_CONSERVATIVE_BOOST_FG_BIT);
		soc_feat_unset(SOC_ENABLE_UCLAMP_BOOSTED_BIT);
		soc_feat_unset(SOC_ENABLE_PER_TASK_BOOST_ON_MID_BIT);
		trailblazer_floor_freq[0] = 1000000;
		sysctl_walt_features |= WALT_FEAT_TRAILBLAZER_BIT;
		sysctl_walt_features |= WALT_FEAT_SYNC_FREQ_CAP_BIT;
		soc_feat_unset(SOC_ENABLE_COLOCATION_PLACEMENT_BOOST_BIT);
		soc_feat_set(SOC_ENABLE_FT_BOOST_TO_ALL);
		oscillate_period_ns = 8000000;
		/*G + P*/
		cpumask_copy(&pipeline_sync_cpus, cpu_possible_mask);
		cpumask_copy(&storage_boost_cpus, cpu_possible_mask);
		soc_sched_lib_name_capacity = 2;
		soc_feat_unset(SOC_ENABLE_PIPELINE_SWAPPING_BIT);

		sysctl_cluster01_load_sync[0]	= 350;
		sysctl_cluster01_load_sync[1]	= 100;
		sysctl_cluster01_load_sync[2]	= 100;
		sysctl_cluster10_load_sync[0]	= 512;
		sysctl_cluster10_load_sync[1]	= 90;
		sysctl_cluster10_load_sync[2]	= 90;
		load_sync_util_thres[0][1]	= sysctl_cluster01_load_sync[0];
		load_sync_low_pct[0][1]		= sysctl_cluster01_load_sync[1];
		load_sync_high_pct[0][1]	= sysctl_cluster01_load_sync[2];
		load_sync_util_thres[1][0]	= sysctl_cluster10_load_sync[0];
		load_sync_low_pct[1][0]		= sysctl_cluster10_load_sync[1];
		load_sync_high_pct[1][0]	= sysctl_cluster10_load_sync[2];

		sysctl_cluster01_load_sync_60fps[0]	= 400;
		sysctl_cluster01_load_sync_60fps[1]	= 60;
		sysctl_cluster01_load_sync_60fps[2]	= 100;
		sysctl_cluster10_load_sync_60fps[0]	= 500;
		sysctl_cluster10_load_sync_60fps[1]	= 70;
		sysctl_cluster10_load_sync_60fps[2]	= 90;
		load_sync_util_thres_60fps[0][1]	= sysctl_cluster01_load_sync_60fps[0];
		load_sync_low_pct_60fps[0][1]		= sysctl_cluster01_load_sync_60fps[1];
		load_sync_high_pct_60fps[0][1]		= sysctl_cluster01_load_sync_60fps[2];
		load_sync_util_thres_60fps[1][0]	= sysctl_cluster10_load_sync_60fps[0];
		load_sync_low_pct_60fps[1][0]		= sysctl_cluster10_load_sync_60fps[1];
		load_sync_high_pct_60fps[1][0]		= sysctl_cluster10_load_sync_60fps[2];

		/* CPU0 needs an 9mS bias for all legacy smart freq reasons */
		for (i = 1; i < LEGACY_SMART_FREQ; i++)
			smart_freq_legacy_reason_hyst_ms[i][0] = 9;
		for_each_cpu(cpu, &cpu_array[0][num_sched_clusters - 1]) {
			for (i = 1; i < LEGACY_SMART_FREQ; i++)
				smart_freq_legacy_reason_hyst_ms[i][cpu] = 2;
		}
		for_each_possible_cpu(cpu) {
			smart_freq_legacy_reason_hyst_ms[PIPELINE_60FPS_OR_LESSER_SMART_FREQ][cpu] =
				1;
		}
		soc_feat_unset(SOC_ENABLE_THERMAL_HALT_LOW_FREQ_BIT);
	} else if (!strcmp(name, "PINEAPPLE")) {
		soc_feat_set(SOC_ENABLE_SILVER_RT_SPREAD_BIT);
		soc_feat_set(SOC_ENABLE_BOOST_TO_NEXT_CLUSTER_BIT);

		/* T + G */
		cpumask_or(&asym_cap_sibling_cpus,
			&asym_cap_sibling_cpus, &cpu_array[0][1]);
		cpumask_or(&asym_cap_sibling_cpus,
			&asym_cap_sibling_cpus, &cpu_array[0][2]);

		/* T + G + P */
		cpumask_andnot(&storage_boost_cpus, cpu_possible_mask, &cpu_array[0][0]);

		/*
		 * Treat Golds and Primes as candidates for load sync under pipeline usecase.
		 * However, it is possible that a single CPU is not present. As prime is the
		 * only cluster with only one CPU, guard this setting by ensuring 4 clusters
		 * are present.
		 */
		if (num_sched_clusters == 4) {
			cpumask_or(&pipeline_sync_cpus,
				&pipeline_sync_cpus, &cpu_array[0][2]);
			cpumask_or(&pipeline_sync_cpus,
				&pipeline_sync_cpus, &cpu_array[0][3]);
		}

	} else if (!strcmp(name, "TUNA")) {
		soc_feat_set(SOC_ENABLE_SILVER_RT_SPREAD_BIT);
		soc_feat_set(SOC_ENABLE_BOOST_TO_NEXT_CLUSTER_BIT);
		soc_feat_set(SOC_ENABLE_SINGLE_THREAD_PIPELINE_PINNING);
		soc_sched_lib_name_capacity = 2;
		/*
		 * Treat Golds and Primes as candidates for load sync under pipeline usecase.
		 * However, it is possible that a single CPU is not present. As prime is the
		 * only cluster with only one CPU, guard this setting by ensuring 4 clusters
		 * are present.
		 */
		if (num_sched_clusters == 4) {
			cpumask_or(&pipeline_sync_cpus,
				&pipeline_sync_cpus, &cpu_array[0][2]);
			cpumask_or(&pipeline_sync_cpus,
				&pipeline_sync_cpus, &cpu_array[0][3]);
		}
		pipeline_swap_util_th = 100;

		/*
		 * Trailblazer settings
		 */
		trailblazer_floor_freq[0] = 1000000;
		trailblazer_floor_freq[1] = 1000000;
		trailblazer_floor_freq[2] = 1000000;
		sysctl_walt_features |= WALT_FEAT_TRAILBLAZER_BIT;
		sysctl_walt_features |= WALT_FEAT_SYNC_FREQ_CAP_BIT;

		/*
		 * Do not put the whole cluster at Fmin during thermal halt condition.
		 */
		soc_feat_unset(SOC_ENABLE_THERMAL_HALT_LOW_FREQ_BIT);

		sysctl_sched_suppress_region2 = 1;
	} else if (!strcmp(name, "KERA")) {
		soc_sched_lib_name_capacity = 3;
		/*
		 * Trailblazer settings
		 */
		trailblazer_floor_freq[0] = 1000000;
		trailblazer_floor_freq[1] = 1000000;
		sysctl_walt_features |= WALT_FEAT_TRAILBLAZER_BIT;
		pipeline_swap_util_th = 100;
		sysctl_walt_features |= WALT_FEAT_SYNC_FREQ_CAP_BIT;

		/*
		 * Do not put the whole cluster at Fmin during thermal halt condition.
		 */
		soc_feat_unset(SOC_ENABLE_THERMAL_HALT_LOW_FREQ_BIT);

	}
}

void early_walt_config(void)
{
	const char *name = socinfo_get_id_string();

	memset(soc_cluster_freq_table_size, 0, sizeof(soc_cluster_freq_table_size));
	memset(soc_cluster_freq_table, 0, sizeof(soc_cluster_freq_table));
	if (!strcmp(name, "SUN") || !strcmp(name, "SUNP")) {
		soc_cluster_freq_table_size[0] = 16;
		soc_cluster_freq_table_size[1] = 16;

		soc_cluster_freq_table[0][0] = 683;
		soc_cluster_freq_table[0][1] = 731;
		soc_cluster_freq_table[0][2] = 782;
		soc_cluster_freq_table[0][3] = 792;
		soc_cluster_freq_table[0][4] = 813;
		soc_cluster_freq_table[0][5] = 856;
		soc_cluster_freq_table[0][6] = 884;
		soc_cluster_freq_table[0][7] = 958;
		soc_cluster_freq_table[0][8] = 1004;
		soc_cluster_freq_table[0][9] = 1087;
		soc_cluster_freq_table[0][10] = 1153;
		soc_cluster_freq_table[0][11] = 1300;
		soc_cluster_freq_table[0][12] = 1462;
		soc_cluster_freq_table[0][13] = 1629;
		soc_cluster_freq_table[0][14] = 1894;
		soc_cluster_freq_table[0][15] = 2183;

		soc_cluster_freq_table[1][0] = 1655;
		soc_cluster_freq_table[1][1] = 1749;
		soc_cluster_freq_table[1][2] = 1775;
		soc_cluster_freq_table[1][3] = 1951;
		soc_cluster_freq_table[1][4] = 2104;
		soc_cluster_freq_table[1][5] = 2268;
		soc_cluster_freq_table[1][6] = 2425;
		soc_cluster_freq_table[1][7] = 2530;
		soc_cluster_freq_table[1][8] = 2652;
		soc_cluster_freq_table[1][9] = 2903;
		soc_cluster_freq_table[1][10] = 3225;
		soc_cluster_freq_table[1][11] = 3592;
		soc_cluster_freq_table[1][12] = 4384;
		soc_cluster_freq_table[1][13] = 5087;
		soc_cluster_freq_table[1][14] = 5390;
		soc_cluster_freq_table[1][15] = 5516;
	} else if (!strcmp(name, "CANOE")) {
		soc_cluster_freq_table_size[0] = 32;
		soc_cluster_freq_table_size[1] = 32;

		soc_cluster_freq_table[0][0] = 752;
		soc_cluster_freq_table[0][1] = 792;
		soc_cluster_freq_table[0][2] = 834;
		soc_cluster_freq_table[0][3] = 877;
		soc_cluster_freq_table[0][4] = 924;
		soc_cluster_freq_table[0][5] = 972;
		soc_cluster_freq_table[0][6] = 1023;
		soc_cluster_freq_table[0][7] = 1053;
		soc_cluster_freq_table[0][8] = 1065;
		soc_cluster_freq_table[0][9] = 1068;
		soc_cluster_freq_table[0][10] = 1078;
		soc_cluster_freq_table[0][11] = 1089;
		soc_cluster_freq_table[0][12] = 1110;
		soc_cluster_freq_table[0][13] = 1137;
		soc_cluster_freq_table[0][14] = 1180;
		soc_cluster_freq_table[0][15] = 1227;
		soc_cluster_freq_table[0][16] = 1303;
		soc_cluster_freq_table[0][17] = 1352;
		soc_cluster_freq_table[0][18] = 1439;
		soc_cluster_freq_table[0][19] = 1582;
		soc_cluster_freq_table[0][20] = 1797;
		soc_cluster_freq_table[0][21] = 1889;
		soc_cluster_freq_table[0][22] = 2003;
		soc_cluster_freq_table[0][23] = 2202;
		soc_cluster_freq_table[0][24] = 2338;
		soc_cluster_freq_table[0][25] = 2499;
		soc_cluster_freq_table[0][26] = 2659;
		soc_cluster_freq_table[0][27] = 2832;
		soc_cluster_freq_table[0][28] = 3019;
		soc_cluster_freq_table[0][29] = 3238;
		soc_cluster_freq_table[0][30] = 3458;
		soc_cluster_freq_table[0][31] = 3714;

		soc_cluster_freq_table[1][0] = 1441;
		soc_cluster_freq_table[1][1] = 1549;
		soc_cluster_freq_table[1][2] = 1666;
		soc_cluster_freq_table[1][3] = 1791;
		soc_cluster_freq_table[1][4] = 1926;
		soc_cluster_freq_table[1][5] = 1942;
		soc_cluster_freq_table[1][6] = 2111;
		soc_cluster_freq_table[1][7] = 2189;
		soc_cluster_freq_table[1][8] = 2310;
		soc_cluster_freq_table[1][9] = 2449;
		soc_cluster_freq_table[1][10] = 2606;
		soc_cluster_freq_table[1][11] = 2757;
		soc_cluster_freq_table[1][12] = 2950;
		soc_cluster_freq_table[1][13] = 3098;
		soc_cluster_freq_table[1][14] = 3253;
		soc_cluster_freq_table[1][15] = 3383;
		soc_cluster_freq_table[1][16] = 3518;
		soc_cluster_freq_table[1][17] = 3659;
		soc_cluster_freq_table[1][18] = 3992;
		soc_cluster_freq_table[1][19] = 4180;
		soc_cluster_freq_table[1][20] = 4402;
		soc_cluster_freq_table[1][21] = 4659;
		soc_cluster_freq_table[1][22] = 4777;
		soc_cluster_freq_table[1][23] = 5121;
		soc_cluster_freq_table[1][24] = 5283;
		soc_cluster_freq_table[1][25] = 5529;
		soc_cluster_freq_table[1][26] = 5645;
		soc_cluster_freq_table[1][27] = 5799;
		soc_cluster_freq_table[1][28] = 6108;
		soc_cluster_freq_table[1][29] = 6463;
		soc_cluster_freq_table[1][30] = 6968;
		soc_cluster_freq_table[1][31] = 7516;
	}

}

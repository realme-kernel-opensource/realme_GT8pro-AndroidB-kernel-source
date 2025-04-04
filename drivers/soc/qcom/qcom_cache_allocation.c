// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#define pr_fmt(fmt) "%s: " fmt, __func__

#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include <linux/jiffies.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/units.h>
#include <soc/qcom/mpam_msc.h>

#define CREATE_TRACE_POINTS
#include "trace-cache_allocation.h"

#define SAMPLING_MS 10
#define boost_dection(ptr, idx) ({ \
	(ptr->cpu_freq_curr[idx] - ptr->cpu_freq_prev[idx]) \
		> ptr->config[idx].cpu_boost_thresh ? 1 : 0; })

struct cpu_gpu_config {
	int cluster_id;
	u32 cpu_boost_thresh;
	u32 cpu_restore_thresh;
	u32 cpu_instant_thresh[2];
	u32 gpu_instant_thresh;
};

struct cache_allocation {
	struct devfreq *df;
	struct device_node *gpu_np;
	struct cpu_gpu_config *config;
	u32 *cpu_freq_prev;
	u32 *cpu_freq_curr;
	u32 gpu_freq_prev;
	u32 gpu_freq_curr;
	bool enable_monitor;
	bool running_flag;
	int cluster_num;
	int cpu_input;
	int gpu_input;
	int sampling_time_ms;
	struct delayed_work work;
	struct mutex lock;
};

struct msc_query apps_query = {
	.qcom_msc_id = {0, 0, 2},
	.part_id = 0,
	.client_id = 0,
};

struct msc_query gpu_query = {
	.qcom_msc_id = {0, 0, 2},
	.part_id = 0,
	.client_id = 1,
};

static void cpu_gpu_freq_update(struct cache_allocation *pdev)
{
	struct devfreq *devfreq = pdev->df;
	struct devfreq_dev_status status;
	int i;

	for (i = 0; i < pdev->cluster_num; i++) {
		pdev->cpu_freq_prev[i] = pdev->cpu_freq_curr[i];
		pdev->cpu_freq_curr[i] = cpufreq_quick_get(pdev->config[i].cluster_id);
	}

	pdev->gpu_freq_prev = pdev->gpu_freq_curr;

	mutex_lock(&devfreq->lock);
	status = devfreq->last_status;
	mutex_unlock(&devfreq->lock);

	pdev->gpu_freq_curr = DIV_ROUND_UP(status.current_frequency, HZ_PER_MHZ);

	for (i = 0; i < pdev->cluster_num; i++)
		trace_cache_alloc_cpu_update(i, pdev->cpu_freq_prev[i],
							pdev->cpu_freq_curr[i]);
	trace_cache_alloc_gpu_update(pdev->gpu_freq_prev, pdev->gpu_freq_curr);
}

static int cache_allocation_configure(struct cache_allocation *pdev)
{
	int ret = 0, cpu_gear_val;

	ret = msc_system_get_partition(SLC, &apps_query, &cpu_gear_val);
	if (ret < 0) {
		pr_err("fail to get cpu cache partition, ret=%d\n", ret);
		return ret;
	}

	ret = msc_system_set_partition(SLC, &apps_query, &pdev->cpu_input);
	if (ret < 0) {
		pr_err("fail to set cpu cache partition, ret=%d\n", ret);
		return ret;
	}

	ret = msc_system_set_partition(SLC, &gpu_query, &pdev->gpu_input);
	if (ret < 0) {
		msc_system_set_partition(SLC, &apps_query, &cpu_gear_val);
		pr_err("fail to set gpu cache partition, ret=%d\n", ret);
		return ret;
	}

	trace_cache_alloc_config_update(pdev->cpu_input, pdev->gpu_input);
	return ret;
}

static void cache_prefer_cpu_algo(struct cache_allocation *pdev)
{
	static u32 prefer_cpu_status;
	int ret = 0;

	if (prefer_cpu_status != 1 &&
			(boost_dection(pdev, 0) || boost_dection(pdev, 1))) {
		prefer_cpu_status = 1;
	} else if (prefer_cpu_status != 2 &&
			((pdev->cpu_freq_curr[0] >= pdev->cpu_freq_prev[0]) ||
			(pdev->cpu_freq_curr[1] >= pdev->cpu_freq_prev[1]))) {
		prefer_cpu_status = 2;
		pdev->cpu_input = 0;
		pdev->gpu_input = 3;
		ret = cache_allocation_configure(pdev);
		if (ret < 0) {
			BUG_ON(1);
			return;
		}
	} else if (prefer_cpu_status != 3 &&
			((pdev->cpu_freq_curr[0] <=
					pdev->config[0].cpu_restore_thresh) &&
			(pdev->cpu_freq_curr[1] <=
					pdev->config[1].cpu_restore_thresh))) {
		prefer_cpu_status = 3;
		pdev->cpu_input = 1;
		pdev->gpu_input = 1;
		ret = cache_allocation_configure(pdev);
		if (ret < 0) {
			BUG_ON(1);
			return;
		}
	} else if (prefer_cpu_status != 4 &&
			pdev->cpu_freq_curr[0] < pdev->cpu_freq_prev[0] &&
			pdev->cpu_freq_curr[1] < pdev->cpu_freq_prev[1]) {
		prefer_cpu_status = 4;
	}
}

static void cache_prefer_gpu_algo(struct cache_allocation *pdev)
{
	static u32 prefer_gpu_status;
	int ret = 0;

	if (prefer_gpu_status != 1 &&
	   (pdev->cpu_freq_curr[0] < pdev->config[0].cpu_instant_thresh[0] ||
	   pdev->cpu_freq_curr[1] < pdev->config[1].cpu_instant_thresh[0])) {
		prefer_gpu_status = 1;
		pdev->cpu_input = 3;
		pdev->gpu_input = 0;
		ret = cache_allocation_configure(pdev);
	} else if (prefer_gpu_status != 2 &&
			((pdev->cpu_freq_curr[0] >=
					pdev->config[0].cpu_instant_thresh[0] &&
			pdev->cpu_freq_curr[0] <
					pdev->config[0].cpu_instant_thresh[1]) ||
			(pdev->cpu_freq_curr[0] >=
					pdev->config[1].cpu_instant_thresh[0] &&
			pdev->cpu_freq_curr[1] <
					pdev->config[1].cpu_instant_thresh[1]))) {
		prefer_gpu_status = 2;
		pdev->cpu_input = 1;
		pdev->gpu_input = 1;
		ret = cache_allocation_configure(pdev);
	} else if (prefer_gpu_status != 3 &&
			(pdev->cpu_freq_curr[0] >=
					pdev->config[0].cpu_instant_thresh[1] ||
			pdev->cpu_freq_curr[1] >=
					pdev->config[1].cpu_instant_thresh[1])){
		prefer_gpu_status = 3;
		pdev->cpu_input = 0;
		pdev->gpu_input = 1;
		ret = cache_allocation_configure(pdev);
	}

	if (ret < 0) {
		BUG_ON(1);
		return;
	}
}

static void cache_allocation_select(struct cache_allocation *pdev)
{
	mutex_lock(&pdev->lock);
	if (pdev->gpu_freq_curr < pdev->config[0].gpu_instant_thresh)
		cache_prefer_cpu_algo(pdev);

	if (pdev->gpu_freq_curr > pdev->config[1].gpu_instant_thresh)
		cache_prefer_gpu_algo(pdev);
	mutex_unlock(&pdev->lock);
}

static ssize_t prefer_cpu_thresh_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	int i, cnt = 0;

	for (i = 0; i < pd->cluster_num; i++) {
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster%d_cpu_boost_thresh: %d\n", i,
					pd->config[i].cpu_boost_thresh);
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster%d_cpu_restore_thresh: %d\n", i,
					pd->config[i].cpu_restore_thresh);
	}

	return cnt;
}

static ssize_t prefer_cpu_thresh_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	int var[4] = {0};
	int ret, i = 0;
	char *kbuf, *s, *str;

	kbuf = kstrdup(buf, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	s = kbuf;
	while (((str = strsep(&s, " ")) != NULL) && i < 4) {
		ret = kstrtoint(str, 10, &var[i]);
		if (ret < 0) {
			pr_err("Invalid value :%d\n", ret);
			goto out;
		}
		i++;
	}

	mutex_lock(&pd->lock);
	for (i = 0; i < pd->cluster_num; i++) {
		pd->config[i].cpu_boost_thresh = var[i * 2];
		pd->config[i].cpu_restore_thresh = var[i * 2 + 1];
	}
	mutex_unlock(&pd->lock);

out:
	kfree(kbuf);
	return count;
}
static DEVICE_ATTR_RW(prefer_cpu_thresh);

static ssize_t prefer_gpu_thresh_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	int i, cnt = 0;

	for (i = 0; i < pd->cluster_num; i++) {
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster%d_cpu_instant_low_freq: %d\n", i,
					pd->config[i].cpu_instant_thresh[0]);
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"cluster%d_cpu_instant_high_freq: %d\n", i,
					pd->config[i].cpu_instant_thresh[1]);
		cnt += scnprintf(buf + cnt, PAGE_SIZE - cnt,
				"gpu_instant_%s_freq: %d\n", (i == 0 ? "low" : "high"),
					pd->config[i].gpu_instant_thresh);
	}

	return cnt;
}

static ssize_t prefer_gpu_thresh_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	int var[6] = {0};
	int ret, i = 0;
	char *kbuf, *s, *str;

	kbuf = kstrdup(buf, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	s = kbuf;
	while (((str = strsep(&s, " ")) != NULL) && i < 6) {
		ret = kstrtoint(str, 10, &var[i]);
		if (ret < 0) {
			pr_err("Invalid value :%d\n", ret);
			goto out;
		}
		i++;
	}

	mutex_lock(&pd->lock);
	for (i = 0; i < pd->cluster_num; i++) {
		pd->config[i].cpu_instant_thresh[0] = var[i * 3];
		pd->config[i].cpu_instant_thresh[1] = var[i * 3 + 1];
		pd->config[i].gpu_instant_thresh = var[i * 3 + 2];
	}
	mutex_unlock(&pd->lock);

out:
	kfree(kbuf);
	return count;
}
static DEVICE_ATTR_RW(prefer_gpu_thresh);

static ssize_t enable_monitor_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->enable_monitor);
}

static ssize_t enable_monitor_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	mutex_lock(&pd->lock);
	pd->enable_monitor = val;
	pd->running_flag = val;
	mutex_unlock(&pd->lock);

	if (pd->enable_monitor)
		schedule_delayed_work(&pd->work, msecs_to_jiffies(pd->sampling_time_ms));
	else
		cancel_delayed_work_sync(&pd->work);

	return count;
}
static DEVICE_ATTR_RW(enable_monitor);

static ssize_t sampling_time_ms_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->sampling_time_ms);
}

static ssize_t sampling_time_ms_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct cache_allocation *pd = dev_get_drvdata(dev);
	int val;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	if (val < 0) {
		pr_err("input error\n");
		return -EINVAL;
	}

	if (pd->enable_monitor && pd->running_flag) {
		mutex_lock(&pd->lock);
		pd->running_flag = false;
		mutex_unlock(&pd->lock);
		cancel_delayed_work_sync(&pd->work);
	}

	mutex_lock(&pd->lock);
	pd->sampling_time_ms = val;
	mutex_unlock(&pd->lock);

	if (pd->enable_monitor && !pd->running_flag) {
		mutex_lock(&pd->lock);
		pd->running_flag = true;
		mutex_unlock(&pd->lock);
		schedule_delayed_work(&pd->work, msecs_to_jiffies(pd->sampling_time_ms));
	}

	return count;
}
static DEVICE_ATTR_RW(sampling_time_ms);

static void cache_allocation_monitor_work(struct work_struct *work)
{
	struct cache_allocation *pdev = container_of(work, struct cache_allocation,
						work.work);

	cpu_gpu_freq_update(pdev);
	cache_allocation_select(pdev);
	mutex_lock(&pdev->lock);
	pdev->running_flag = true;
	mutex_unlock(&pdev->lock);
	schedule_delayed_work(&pdev->work, msecs_to_jiffies(pdev->sampling_time_ms));
}

static int cache_allocation_probe(struct platform_device *pdev)
{
	struct cache_allocation *pd;
	struct cpu_gpu_config *config;
	int ret = 0;

	config = (struct cpu_gpu_config *)of_device_get_match_data(&pdev->dev);
	if (!config)
		return -ENODEV;

	pd = devm_kzalloc(&pdev->dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	ret = of_property_read_u32(pdev->dev.of_node,
				"qcom,cluster-num", &pd->cluster_num);
	if (ret)
		return -EINVAL;

	pd->config = devm_kcalloc(&pdev->dev, pd->cluster_num,
					sizeof(*pd->config), GFP_KERNEL);
	if (!pd->config)
		return -ENOMEM;

	pd->cpu_freq_prev = devm_kcalloc(&pdev->dev, pd->cluster_num,
						sizeof(u32), GFP_KERNEL);
	if (!pd->cpu_freq_prev)
		return -ENOMEM;

	pd->cpu_freq_curr = devm_kcalloc(&pdev->dev, pd->cluster_num,
						sizeof(u32), GFP_KERNEL);
	if (!pd->cpu_freq_curr)
		return -ENOMEM;

	pd->gpu_np = of_parse_phandle(pdev->dev.of_node, "qcom,devfreq", 0);
	if (IS_ERR(pd->gpu_np))
		return PTR_ERR(pd->gpu_np);

	pd->df = devfreq_get_devfreq_by_node(pd->gpu_np);
	if (IS_ERR(pd->df)) {
		of_node_put(pd->gpu_np);
		return PTR_ERR(pd->df);
	}

	memcpy(pd->config, config, pd->cluster_num * sizeof(*config));
	pd->enable_monitor = false;
	pd->running_flag =  false;
	pd->sampling_time_ms = SAMPLING_MS;

	ret = device_create_file(&pdev->dev, &dev_attr_enable_monitor);
	if (ret) {
		dev_err(&pdev->dev, "failed: create cache alloc enable monitor entry\n");
		goto fail_create_enable_monitor;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_sampling_time_ms);
	if (ret) {
		dev_err(&pdev->dev, "failed: create cache alloc sampling time entry\n");
		goto fail_create_sampling_time_ms;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_prefer_cpu_thresh);
	if (ret) {
		dev_err(&pdev->dev, "failed: create cache alloc prefer cpu thresh entry\n");
		goto fail_create_prefer_cpu_thresh;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_prefer_gpu_thresh);
	if (ret) {
		dev_err(&pdev->dev, "failed: create cache alloc prefer gpu thresh entry\n");
		goto fail_create_prefer_gpu_thresh;
	}

	mutex_init(&pd->lock);
	INIT_DELAYED_WORK(&pd->work, cache_allocation_monitor_work);
	platform_set_drvdata(pdev, pd);

	return 0;

fail_create_prefer_gpu_thresh:
	device_remove_file(&pdev->dev, &dev_attr_prefer_cpu_thresh);
fail_create_prefer_cpu_thresh:
	device_remove_file(&pdev->dev, &dev_attr_sampling_time_ms);
fail_create_sampling_time_ms:
	device_remove_file(&pdev->dev, &dev_attr_enable_monitor);
fail_create_enable_monitor:
	of_node_put(pd->gpu_np);
	return ret;
}

static void cache_allocation_remove(struct platform_device *pdev)
{
	struct cache_allocation *pd = platform_get_drvdata(pdev);

	cancel_delayed_work_sync(&pd->work);
	device_remove_file(&pdev->dev, &dev_attr_enable_monitor);
	device_remove_file(&pdev->dev, &dev_attr_sampling_time_ms);
	device_remove_file(&pdev->dev, &dev_attr_prefer_cpu_thresh);
	device_remove_file(&pdev->dev, &dev_attr_prefer_gpu_thresh);
}

static struct cpu_gpu_config canoe_config[] = {
	[0] = {
		.cluster_id = 0,
		.cpu_boost_thresh = 1500000,
		.cpu_restore_thresh = 1000000,
		.cpu_instant_thresh = { 115200, 1996800 },
		.gpu_instant_thresh = 443,
	},
	[1] = {
		.cluster_id = 6,
		.cpu_boost_thresh = 1500000,
		.cpu_restore_thresh = 1400000,
		.cpu_instant_thresh = { 1689600, 3072000 },
		.gpu_instant_thresh = 607,
	},
};

static const struct of_device_id cache_allocation_table[] = {
	{ .compatible = "qcom,cache-allocation-canoe", .data = &canoe_config },
	{ },
};
MODULE_DEVICE_TABLE(of, cache_allocation_table);

static struct platform_driver cache_allocation_driver = {
	.probe = cache_allocation_probe,
	.remove = cache_allocation_remove,
	.driver = {
		.name = "cache allocation",
		.of_match_table = cache_allocation_table,
	},
};
module_platform_driver(cache_allocation_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) cache allocation driver");

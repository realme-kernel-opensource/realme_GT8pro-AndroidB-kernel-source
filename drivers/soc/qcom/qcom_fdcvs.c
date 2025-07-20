// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/soc/qcom/qcom_aoss.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#define MAX_QMP_MSG_SIZE	0x64
#define DEFAULT_PROFILE_INDEX 0
#define DEFAULT_AGGRESSIVE_LEVEL 100
#define MAX_PROFILE_INDEX 9
#define MIN_AGGRESSIVE_LEVEL 50
#define MAX_AGGRESSIVE_LEVEL 150
#define DEFAULT_INTERVAL 500

struct fdcvs_platform_data {
	struct mutex lock;
	struct qmp *qmp;
	bool profile_enable;
	bool pre_profile_enable;
	bool start_tuning;
	u32 aggressive_level;
	u32 profile_index;
	u32 pre_profile_index;
	u32 profile_interval;
	ktime_t last_enable_time;
	ktime_t last_disable_time;
};

static ssize_t profile_index_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->profile_index);
}

static ssize_t profile_index_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if (val > MAX_PROFILE_INDEX) {
		pr_err("profile index out of range\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);
	pd->profile_index = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(profile_index);

static ssize_t aggressive_level_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->aggressive_level);
}

static ssize_t aggressive_level_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	if ((val < MIN_AGGRESSIVE_LEVEL) || (val > MAX_AGGRESSIVE_LEVEL)) {
		pr_err("aggressive level out of range\n");
		return -EINVAL;
	}

	mutex_lock(&pd->lock);
	pd->aggressive_level = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(aggressive_level);

static ssize_t profile_enable_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->profile_enable);
}

static ssize_t profile_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);
	bool val;

	if (kstrtobool(buf, &val))
		return -EINVAL;

	mutex_lock(&pd->lock);
	pd->profile_enable = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(profile_enable);

static ssize_t profile_interval_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->profile_interval);
}

static ssize_t profile_interval_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);
	u32 val;

	if (kstrtou32(buf, 0, &val))
		return -EINVAL;

	mutex_lock(&pd->lock);
	pd->profile_interval = val;
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(profile_interval);

static ssize_t start_tuning_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", pd->start_tuning);
}

static ssize_t start_tuning_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct fdcvs_platform_data *pd = dev_get_drvdata(dev);
	char qmp_buf[MAX_QMP_MSG_SIZE] = {};
	bool val;
	int ret = 0;
	ktime_t now = ktime_get();

	if (kstrtobool(buf, &val))
		return -EINVAL;

	mutex_lock(&pd->lock);
	pd->start_tuning = val;
	if (pd->start_tuning) {
		if (pd->pre_profile_enable && pd->profile_enable) {
			if (ktime_us_delta(now, pd->last_enable_time) <
				(pd->profile_interval * USEC_PER_MSEC)) {
				pr_err("Retry after profile interval passed\n");
				mutex_unlock(&pd->lock);
				return -EBUSY;
			}
		}

		if ((!pd->pre_profile_enable) && pd->profile_enable) {
			if (ktime_us_delta(now, pd->last_disable_time) <
				(pd->profile_interval * USEC_PER_MSEC)) {
				pr_err("Retry after profile interval passed\n");
				mutex_unlock(&pd->lock);
				return -EBUSY;
			}
		}

		if ((pd->profile_index == pd->pre_profile_index) &&
			(pd->pre_profile_enable == pd->profile_enable)) {
			pr_err("Error the same profile with same config as last time\n");
			mutex_unlock(&pd->lock);
			return -EBUSY;
		}

		scnprintf(qmp_buf, sizeof(qmp_buf),
			"{class: fdcvs, profile_index: %d, aggressive_level: %d, profile_enable: %d}",
				pd->profile_index, pd->aggressive_level, pd->profile_enable);
		ret = qmp_send(pd->qmp, qmp_buf, sizeof(qmp_buf));
		if (ret)
			pr_err("Error sending qmp message: %d\n", ret);
		else {
			pd->pre_profile_enable = pd->profile_enable;
			pd->pre_profile_index = pd->profile_index;
			if (!pd->profile_enable)
				pd->last_disable_time = ktime_get();
			else
				pd->last_enable_time = ktime_get();
		}
	}
	mutex_unlock(&pd->lock);

	return count;
}
static DEVICE_ATTR_RW(start_tuning);

static int fdcvs_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fdcvs_platform_data *pd;
	int ret;

	pd = devm_kzalloc(dev, sizeof(*pd), GFP_KERNEL);
	if (!pd)
		return -ENOMEM;

	ret = device_create_file(&pdev->dev, &dev_attr_profile_index);
	if (ret) {
		dev_err(&pdev->dev, "failed: create profile index entry\n");
		return ret;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_aggressive_level);
	if (ret) {
		dev_err(&pdev->dev, "failed: create aggressive level entry\n");
		goto fail_create_aggressive_level;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_profile_enable);
	if (ret) {
		dev_err(&pdev->dev, "failed: create profile enable entry\n");
		goto fail_create_profile_enable;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_profile_interval);
	if (ret) {
		dev_err(&pdev->dev, "failed: create profile interval entry\n");
		goto fail_create_profile_interval;
	}

	ret = device_create_file(&pdev->dev, &dev_attr_start_tuning);
	if (ret) {
		dev_err(&pdev->dev, "failed: create start tuning entry\n");
		goto fail_create_start_tuning;
	}

	pd->qmp = qmp_get(&pdev->dev);
	if (IS_ERR(pd->qmp)) {
		ret = PTR_ERR(pd->qmp);
		goto fail_get_qmp;
	}

	mutex_init(&pd->lock);
	pd->profile_index = DEFAULT_PROFILE_INDEX;
	pd->aggressive_level = DEFAULT_AGGRESSIVE_LEVEL;
	pd->profile_enable = false;
	pd->pre_profile_enable = false;
	pd->pre_profile_index = 0;
	pd->start_tuning = true;
	pd->profile_interval = DEFAULT_INTERVAL;
	pd->last_enable_time = 0;
	pd->last_disable_time = 0;

	platform_set_drvdata(pdev, pd);

	return 0;

fail_get_qmp:
	device_remove_file(&pdev->dev, &dev_attr_start_tuning);
fail_create_start_tuning:
	device_remove_file(&pdev->dev, &dev_attr_profile_interval);
fail_create_profile_interval:
	device_remove_file(&pdev->dev, &dev_attr_profile_enable);
fail_create_profile_enable:
	device_remove_file(&pdev->dev, &dev_attr_aggressive_level);
fail_create_aggressive_level:
	device_remove_file(&pdev->dev, &dev_attr_profile_index);
	return ret;
}

static void fdcvs_remove(struct platform_device *pdev)
{
	struct fdcvs_platform_data *pd = platform_get_drvdata(pdev);

	device_remove_file(&pdev->dev, &dev_attr_profile_index);
	device_remove_file(&pdev->dev, &dev_attr_aggressive_level);
	device_remove_file(&pdev->dev, &dev_attr_profile_enable);
	device_remove_file(&pdev->dev, &dev_attr_profile_interval);
	device_remove_file(&pdev->dev, &dev_attr_start_tuning);
	qmp_put(pd->qmp);
}

static const struct of_device_id fdcvs_table[] = {
	{ .compatible = "qcom,fdcvs" },
	{ }
};

static struct platform_driver fdcvs_driver = {
	.probe = fdcvs_probe,
	.remove = fdcvs_remove,
	.driver = {
		.name = "fdcvs",
		.of_match_table = fdcvs_table,
	},
};
module_platform_driver(fdcvs_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qualcomm Technologies, Inc. (QTI) FDCVS driver");

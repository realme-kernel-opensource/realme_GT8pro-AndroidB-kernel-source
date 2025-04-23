// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/gunyah/gh_errno.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/gunyah/gh_common.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <soc/qcom/secure_buffer.h>
#include <linux/sizes.h>
#include <linux/align.h>
#include "gh_rm_drv_private.h"
#include "mem-prot.h"

/* heap mem parcel flags */
#define RM_HEAP_MEM_ADDED	((uint8_t)0x1)
#define RM_HEAP_MEM_ASSIGNED	((uint8_t)0x2)
#define RM_HEAP_MEM_ALLOCED	((uint8_t)0x4)

/* heap labels */
#define HYP_ROOT_HEAP_LABEL	0x0
#define RM_HEAP_LABEL		0x1

bool rm_heap_manager_enabled;
bool hyp_heap_manage_supported;

static DEFINE_MUTEX(rm_heap_lock);

/* for every 1GB of dmabuf, it would need 5MB of Root Heap memory */
#define ROOT_HEAP_PER_GB	(5 * SZ_1M)

/* for every 1GB of dmabuf, it would need 3MB of RM Heap memory */
#define RM_HEAP_PER_GB	(3 * SZ_1M)

#define HEAP_MIN_RESOLUTION	(SZ_1M)

#define RATIO_ROOT_HEAP	(ROOT_HEAP_PER_GB / SZ_1M)
#define RATIO_RM_HEAP	(RM_HEAP_PER_GB / SZ_1M)
#define TOTAL_RATIO		(RATIO_ROOT_HEAP + RATIO_RM_HEAP)

struct rm_heap_mem_parcel {
	gh_memparcel_handle_t mp_handle;
	size_t size;
	uint8_t flags;
	phys_addr_t phys_addr;
	struct list_head list;
};

struct rm_heap_manager {
	int rm_vmid;

	gh_heap_handle_t heap_handle[GH_HYP_HEAP_OBJ_MAX];
	u32 num_parcels[GH_HYP_HEAP_OBJ_MAX];
	unsigned long total_heap_added[GH_HYP_HEAP_OBJ_MAX];
	struct list_head list_mp_handles[GH_HYP_HEAP_OBJ_MAX];
};

static struct rm_heap_manager *rm_heap_manager;

/* get the total hypervisor heap memory added by hlos */
uint64_t gh_rm_get_total_heap_added(enum gh_hyp_heap_label label)
{
	return rm_heap_manager->total_heap_added[label];
}
EXPORT_SYMBOL_GPL(gh_rm_get_total_heap_added);

static uint64_t __get_dmabuf_heap_size(size_t size, enum gh_hyp_heap_label label)
{
	uint64_t num_gb, remaining_bytes, heap_size;
	uint64_t heap_per_gb;

	if (label == GH_HYP_HEAP_ROOT)
		heap_per_gb = ROOT_HEAP_PER_GB;
	else
		heap_per_gb = RM_HEAP_PER_GB;

	num_gb = size / SZ_1G;
	heap_size = num_gb * heap_per_gb;

	remaining_bytes = size % SZ_1G;
	if (remaining_bytes >= SZ_512M)
		heap_size += heap_per_gb;

	return heap_size;
}

/*
 * for every 1GB of dmabuf, we would need -
 *     5MB of Root Heap memory + 3MB of RM Heap memory
 */
uint64_t gh_get_dmabuf_hyp_heap_size(size_t size)
{
	return __get_dmabuf_heap_size(size, GH_HYP_HEAP_ROOT) +
		   __get_dmabuf_heap_size(size, GH_HYP_HEAP_RM);
}
EXPORT_SYMBOL_GPL(gh_get_dmabuf_hyp_heap_size);

static inline char *get_heap_object_name(enum gh_hyp_heap_label label)
{
	if (label == GH_HYP_HEAP_ROOT)
		return "Root heap";
	else if (label == GH_HYP_HEAP_RM)
		return "RM heap";
	else
		return "";
}

uint64_t gh_get_rm_heap_free_hyp_object(enum gh_hyp_heap_label label)
{
	struct gh_mem_heap_query_resp_stats_payload *heap_info;
	int ret;
	size_t resp_size;
	uint64_t total_size, allocated_size;
	uint64_t rm_heap_free = 0;

	ret = gh_rm_heap_query(rm_heap_manager->heap_handle[label],
			GH_RM_HEAP_QUERY_TYPE_MEM, (void *)&heap_info, &resp_size);
	if (ret < 0) {
		pr_err("%s: Failed to get heap QUERY for %s ret: %d\n",
				__func__, get_heap_object_name(label), ret);
		goto out;
	}

	if (resp_size != sizeof(*heap_info)) {
		pr_err("%s: response size of heap query not expected value. expected: %zu received: %lu\n",
				__func__, resp_size, sizeof(*heap_info));
	}

	total_size = heap_info->total_size;
	allocated_size = heap_info->allocated_size;

	rm_heap_free = total_size - allocated_size;

	if (resp_size)
		kfree(heap_info);

out:
	return rm_heap_free;
}
EXPORT_SYMBOL_GPL(gh_get_rm_heap_free_hyp_object);

uint64_t gh_get_hyp_heap_free(void)
{
	return gh_get_rm_heap_free_hyp_object(GH_HYP_HEAP_ROOT) +
			gh_get_rm_heap_free_hyp_object(GH_HYP_HEAP_RM);
}
EXPORT_SYMBOL_GPL(gh_get_hyp_heap_free);

/* size in bytes */
size_t gh_rm_heap_remove_hyp_object(size_t size, enum gh_hyp_heap_label label)
{
	int ret = -EINVAL;
	gh_memparcel_handle_t mp_handle;
	struct rm_heap_mem_parcel *heap_parcel;
	size_t initial_size = size;
	u8 fail = 0;

	if (!rm_heap_manager_enabled)
		return 0;

	mutex_lock(&rm_heap_lock);
	list_for_each_entry(heap_parcel, &rm_heap_manager->list_mp_handles[label], list) {

		if (heap_parcel->size > size)
			continue;

		mp_handle = heap_parcel->mp_handle;

		if (heap_parcel->flags & RM_HEAP_MEM_ADDED) {
			ret = gh_rm_remove_heap_memory(
					rm_heap_manager->heap_handle[label], mp_handle);
			if (ret < 0) {
				/*
				 * this parcel of memory in hyp heap could still be in use.
				 * retry with removing other mem_parcels in the list.
				 */
				fail++;
				continue;
			}
			heap_parcel->flags &= ~RM_HEAP_MEM_ADDED;
		}

		if (heap_parcel->flags & RM_HEAP_MEM_ASSIGNED) {
			ret = ghd_rm_mem_reclaim(mp_handle, 0);
			if (ret) {
				pr_err("%s: ghd_rm_mem_reclaim failed for %s error: %d\n",
						__func__, get_heap_object_name(label), ret);
				goto out;
			}
			heap_parcel->flags &= ~RM_HEAP_MEM_ASSIGNED;
		}

		ret = mem_prot_pool_free(heap_parcel->phys_addr, heap_parcel->size,
				CFG_PHYS_DDR_PROTECTION_CMD_ENABLE_AND_INIT_DARE);
		if (ret)
			pr_err("%s: mem_prot_pool_free failed for %s: %d\n",
					__func__, get_heap_object_name(label), ret);

		list_del(&heap_parcel->list);
		rm_heap_manager->num_parcels[label]--;
		rm_heap_manager->total_heap_added[label] -= heap_parcel->size;
		size -= heap_parcel->size;
		kfree(heap_parcel);

		if (!size)
			break;
	}

	pr_debug("%s: %s total_heap_added 0x%lx num_parcels %d\n",
			__func__, get_heap_object_name(label),
			rm_heap_manager->total_heap_added[label],
			rm_heap_manager->num_parcels[label]);

out:
	mutex_unlock(&rm_heap_lock);
	if (size)
		pr_err("%s: removed only heap memory 0x%zx bytes of requested 0x%zx bytes from %s. fail count: %d\n",
				__func__, initial_size - size,
				initial_size, get_heap_object_name(label), fail);

	return size;
}
EXPORT_SYMBOL_GPL(gh_rm_heap_remove_hyp_object);

/* size in bytes */
int gh_rm_heap_add_hyp_object(size_t size, enum gh_hyp_heap_label label)
{
	int ret, ret2 = 0;
	u8 perm = PERM_READ | PERM_WRITE | PERM_EXEC;
	u16 dst_vmid =  rm_heap_manager->rm_vmid;
	struct rm_heap_mem_parcel *heap_parcel;
	phys_addr_t prot_phys;
	struct gh_acl_desc *acl_desc;
	struct gh_sgl_desc *sgl_desc;
	gh_memparcel_handle_t mp_handle;

	if (!rm_heap_manager_enabled)
		return 0;

	if (!IS_ALIGNED(size, HEAP_MIN_RESOLUTION)) {
		pr_err("%s: size 0x%zx should be aligned to 1MB\n", __func__, size);
		return -EINVAL;
	}

	heap_parcel = kzalloc(sizeof(*heap_parcel), GFP_KERNEL);
	if (!heap_parcel)
		return -ENOMEM;

	INIT_LIST_HEAD(&heap_parcel->list);

	ret = mem_prot_pool_alloc(size, &prot_phys,
			CFG_PHYS_DDR_PROTECTION_CMD_ENABLE_AND_INIT_DARE);
	if (ret) {
		pr_err("%s: mem_prot_pool_alloc failed for %s: %d\n",
				__func__, get_heap_object_name(label), ret);
		goto err_mem_prot;
	}

	acl_desc = kzalloc(offsetof(struct gh_acl_desc, acl_entries[1]),
				GFP_KERNEL);
	if (!acl_desc) {
		ret = -ENOMEM;
		goto err_acl_desc;
	}

	acl_desc->n_acl_entries = 1;
	acl_desc->acl_entries[0].vmid = dst_vmid;
	acl_desc->acl_entries[0].perms = perm;

	sgl_desc = kzalloc(offsetof(struct gh_sgl_desc, sgl_entries[1]),
				GFP_KERNEL);
	if (!sgl_desc) {
		ret = -ENOMEM;
		goto err_sgl_desc;
	}

	sgl_desc->n_sgl_entries = 1;
	sgl_desc->sgl_entries[0].ipa_base = prot_phys;
	sgl_desc->sgl_entries[0].size = size;

	ret = ghd_rm_mem_lend(GH_RM_MEM_TYPE_NORMAL, 0, 0,
			acl_desc, sgl_desc, NULL, &mp_handle);
	if (ret) {
		pr_err("%s: ghd_rm_mem_lend failed for %s: %d\n",
				__func__, get_heap_object_name(label), ret);
		goto err_assign_mem;
	}

	heap_parcel->flags |= RM_HEAP_MEM_ASSIGNED;

	ret = gh_rm_add_heap_memory(rm_heap_manager->heap_handle[label], mp_handle);
	if (ret < 0) {
		pr_err("%s: Failed to add 0x%lx bytes memory to %s\n",
				__func__, size, get_heap_object_name(label));
		goto err_add_heap;
	}

	heap_parcel->flags |= RM_HEAP_MEM_ADDED;
	heap_parcel->mp_handle = mp_handle;
	heap_parcel->size = size;
	heap_parcel->phys_addr = prot_phys;

	mutex_lock(&rm_heap_lock);
	list_add(&heap_parcel->list, &rm_heap_manager->list_mp_handles[label]);
	rm_heap_manager->num_parcels[label]++;
	rm_heap_manager->total_heap_added[label] += size;
	mutex_unlock(&rm_heap_lock);

	kfree(acl_desc);
	kfree(sgl_desc);
	pr_debug("%s: %s total_heap_added 0x%lx num_parcels %d\n",
			__func__, get_heap_object_name(label),
			rm_heap_manager->total_heap_added[label],
			rm_heap_manager->num_parcels[label]);

	return 0;

err_add_heap:
	ret2 = ghd_rm_mem_reclaim(mp_handle, 0);
	if (ret2)
		pr_err("%s: ghd_rm_mem_reclaim failed in error path\n", __func__);
err_assign_mem:
	kfree(sgl_desc);
err_sgl_desc:
	kfree(acl_desc);
err_acl_desc:
	/* free mem_prot_pool only after mp_handle is unassigned */
	if (!ret2 && mem_prot_pool_free(prot_phys, size,
			CFG_PHYS_DDR_PROTECTION_CMD_ENABLE_AND_INIT_DARE))
		pr_err("%s: mem_prot_pool_free failed in error path\n", __func__);

err_mem_prot:
	kfree(heap_parcel);

	return ret;
}
EXPORT_SYMBOL_GPL(gh_rm_heap_add_hyp_object);

static void get_hyp_root_rm_heap_ratio(size_t size, size_t *root_heap, size_t *rm_heap)
{
	*root_heap = (size * RATIO_ROOT_HEAP) / TOTAL_RATIO;
	*rm_heap = (size * RATIO_RM_HEAP) / TOTAL_RATIO;
}

/* size in bytes */
size_t gh_rm_heap_remove_dmabuf(size_t size)
{
	size_t root_heap, rm_heap, remaining;

	get_hyp_root_rm_heap_ratio(size, &root_heap, &rm_heap);

	remaining = gh_rm_heap_remove_hyp_object(root_heap, GH_HYP_HEAP_ROOT);
	remaining += gh_rm_heap_remove_hyp_object(rm_heap, GH_HYP_HEAP_RM);

	return remaining;
}
EXPORT_SYMBOL_GPL(gh_rm_heap_remove_dmabuf);

/* size in bytes */
int gh_rm_heap_add_dmabuf(size_t size)
{
	size_t root_heap, rm_heap;

	get_hyp_root_rm_heap_ratio(size, &root_heap, &rm_heap);

	if (gh_rm_heap_add_hyp_object(root_heap, GH_HYP_HEAP_ROOT)) {
		pr_err("%s: failed to add root heap memory\n", __func__);
		return -EINVAL;
	}

	if (gh_rm_heap_add_hyp_object(rm_heap, GH_HYP_HEAP_RM)) {
		pr_err("%s: failed to add RM heap memory\n", __func__);
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(gh_rm_heap_add_dmabuf);

static int gh_rm_get_heap_resource_info(void)
{
	struct gh_vm_get_hyp_res_resp_entry *res_entries = NULL;
	u32 n_res, i;

	res_entries = gh_rm_vm_get_hyp_res(rm_heap_manager->rm_vmid, &n_res);
	if (IS_ERR_OR_NULL(res_entries)) {
		pr_err("%s: Get hyp resources failed.\n", __func__);
		return -EINVAL;
	}

	/* get the heap object resource info */
	for (i = 0; i < n_res; i++)
		if (res_entries[i].res_type == GH_RM_RES_TYPE_RM_HEAP_OBJECT) {
			switch (res_entries[i].resource_label) {
			case HYP_ROOT_HEAP_LABEL:
				rm_heap_manager->heap_handle[GH_HYP_HEAP_ROOT] =
						res_entries[i].resource_handle;
			break;
			case RM_HEAP_LABEL:
				rm_heap_manager->heap_handle[GH_HYP_HEAP_RM] =
						res_entries[i].resource_handle;
			}

			/*
			 * with resource type GH_RM_RES_TYPE_RM_HEAP_OBJECT present,
			 * we have the hypervisor support to manage its heap.
			 */
			if (!hyp_heap_manage_supported)
				hyp_heap_manage_supported = true;
		}

	kfree(res_entries);
	return 0;
}

static bool check_mem_prot_enabled(void)
{
	phys_addr_t prot_phys;

	if (mem_prot_pool_alloc(HEAP_MIN_RESOLUTION, &prot_phys,
			CFG_PHYS_DDR_PROTECTION_CMD_ENABLE_AND_INIT_DARE))
		return false;

	/* mem_prot is enabled */
	if (mem_prot_pool_free(prot_phys, HEAP_MIN_RESOLUTION,
			CFG_PHYS_DDR_PROTECTION_CMD_ENABLE_AND_INIT_DARE))
		pr_err("%s: mem_prot_pool_free failed\n", __func__);

	return true;
}

static int __init gh_rm_heap_manager_init(void)
{
	int ret, i;

	rm_heap_manager = kzalloc(sizeof(*rm_heap_manager), GFP_KERNEL);
	if (!rm_heap_manager)
		return -ENOMEM;

	rm_heap_manager->rm_vmid = QCOM_SCM_VMID_GH_RM;

	for (i = 0; i < GH_HYP_HEAP_OBJ_MAX; i++)
		INIT_LIST_HEAD(&rm_heap_manager->list_mp_handles[i]);

	ret = gh_rm_get_heap_resource_info();
	if (ret < 0) {
		pr_err("%s: failed to get hypervisor heap resources %d\n", __func__, ret);
		goto out_err;
	}

	if (!hyp_heap_manage_supported) {
		pr_err("%s: hyp heap management not supported by Hypervisor\n", __func__);
		ret = -EOPNOTSUPP;
		goto out_err;
	}

	if (!check_mem_prot_enabled()) {
		pr_err("%s: Memory protection feature is not supported\n", __func__);
		ret = -EOPNOTSUPP;
		goto out_err;
	}

	pr_info("free heap memory %s: 0x%llx %s: 0x%llx\n",
			get_heap_object_name(GH_HYP_HEAP_ROOT),
			gh_get_rm_heap_free_hyp_object(GH_HYP_HEAP_ROOT),
			get_heap_object_name(GH_HYP_HEAP_RM),
			gh_get_rm_heap_free_hyp_object(GH_HYP_HEAP_RM));

	rm_heap_manager_enabled = true;
	return ret;

out_err:
	kfree(rm_heap_manager);
	return ret;
}
module_init(gh_rm_heap_manager_init);

MODULE_DESCRIPTION("Qualcomm Technologies, Inc. Gunyah RM Heap Manager Driver");
MODULE_LICENSE("GPL");

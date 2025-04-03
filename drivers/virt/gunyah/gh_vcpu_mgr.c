// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"gh_vcpu_mgr: " fmt

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <linux/gunyah/gh_errno.h>
#include <linux/gunyah/gh_rm_drv.h>
#include <linux/gunyah/gh_vm.h>
#include <trace/hooks/gunyah.h>
#include "gh_vcpu_mgr.h"

struct gh_proxy_vcpu {
	struct gh_proxy_vm *vm;
	gh_capid_t cap_id;
	gh_label_t idx;
	bool wdog_frozen;
};

struct gh_proxy_vm {
	gh_vmid_t id;
	int vcpu_count;
	struct xarray vcpus;
	bool is_vcpu_info_populated;
	bool is_active;
	gh_capid_t wdog_cap_id;
};

static struct gh_proxy_vm *gh_vms;
static bool init_done;
static DEFINE_MUTEX(gh_vm_mutex);

static inline bool is_vm_supports_proxy(gh_vmid_t gh_vmid)
{
	gh_vmid_t vmid;

	if ((!ghd_rm_get_vmid(GH_TRUSTED_VM, &vmid) && vmid == gh_vmid) ||
			(!ghd_rm_get_vmid(GH_OEM_VM, &vmid) && vmid == gh_vmid))
		return true;

	return false;
}

static inline struct gh_proxy_vm *gh_get_vm(gh_vmid_t vmid)
{
	int ret;
	enum gh_vm_names vm_name;
	struct gh_proxy_vm *vm = NULL;

	ret = gh_rm_get_vm_name(vmid, &vm_name);
	if (ret) {
		pr_err("Failed to get VM name for VMID%d ret=%d\n", vmid, ret);
		return vm;
	}

	vm = &gh_vms[vm_name];

	return vm;
}

static inline void gh_reset_vm(struct gh_proxy_vm *vm)
{
	vm->id = GH_VMID_INVAL;
	vm->vcpu_count = 0;
	vm->is_vcpu_info_populated = false;
	vm->is_active = false;
}

static void gh_init_vms(void)
{
	struct gh_proxy_vm *vm;
	int i;

	for (i = 0; i < GH_VM_MAX; i++) {
		vm = &gh_vms[i];
		gh_reset_vm(vm);
		xa_init(&vm->vcpus);
	}
}

static int gh_wdog_manage(gh_vmid_t vmid, gh_capid_t cap_id, bool populate)
{
	struct gh_proxy_vm *vm;
	int ret = 0;

	if (!init_done) {
		pr_err("%s: vcpu_mgr not initiatized\n", __func__);
		return -ENXIO;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip populating VCPU affinity info for VM=%d\n", vmid);
		return -EINVAL;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (!vm) {
		ret = -ENODEV;
		goto unlock;
	}

	if (populate)
		vm->wdog_cap_id = cap_id;
	else
		vm->wdog_cap_id = GH_CAPID_INVAL;

unlock:
	mutex_unlock(&gh_vm_mutex);
	return ret;
}

/*
 * Called when vm_status is STATUS_READY, multiple times before status
 * moves to STATUS_RUNNING
 */
static int gh_populate_vm_vcpu_info(gh_vmid_t vmid, gh_label_t cpu_idx,
					gh_capid_t cap_id, int virq_num)
{
	struct gh_proxy_vm *vm;
	struct gh_proxy_vcpu *vcpu;
	int ret = 0;

	if (!init_done) {
		pr_err("%s: vcpu_mgr not initiatized\n", __func__);
		ret = -ENXIO;
		goto out;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip populating VCPU affinity info for VM=%d\n", vmid);
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && !vm->is_vcpu_info_populated) {
		vcpu = kzalloc(sizeof(*vcpu), GFP_KERNEL);
		if (!vcpu) {
			ret = -ENOMEM;
			goto unlock;
		}
		vcpu->cap_id = cap_id;
		vcpu->idx = cpu_idx;
		vm->id = vmid;
		vcpu->vm = vm;
		ret = xa_err(xa_store(&vm->vcpus, cpu_idx, vcpu, GFP_KERNEL));
		if (ret) {
			kfree(vcpu);
			goto unlock;
		}
		vm->vcpu_count++;
	}
unlock:
	mutex_unlock(&gh_vm_mutex);
out:
	return ret;
}

static int gh_unpopulate_vm_vcpu_info(gh_vmid_t vmid, gh_label_t cpu_idx,
					gh_capid_t cap_id, int *irq)
{
	struct gh_proxy_vm *vm;
	struct gh_proxy_vcpu *vcpu;

	if (!init_done) {
		pr_err("%s: vcpu_mgr not initiatized\n", __func__);
		return -ENXIO;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Skip unpopulating VCPU affinity info for VM=%d\n", vmid);
		goto out;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (vm && vm->is_vcpu_info_populated) {
		vcpu = xa_load(&vm->vcpus, cpu_idx);
		kfree(vcpu);
		xa_erase(&vm->vcpus, cpu_idx);
		vm->vcpu_count--;
	}
	mutex_unlock(&gh_vm_mutex);

out:
	return 0;
}

static void gh_populate_all_res_info(gh_vmid_t vmid, bool res_populated)
{
	struct gh_proxy_vm *vm;

	if (!init_done) {
		pr_err("%s: vcpu_mgr not initiatized\n", __func__);
		return;
	}

	if (!is_vm_supports_proxy(vmid)) {
		pr_info("Proxy Scheduling isn't supported for VM=%d\n", vmid);
		return;
	}

	mutex_lock(&gh_vm_mutex);
	vm = gh_get_vm(vmid);
	if (!vm)
		goto unlock;

	if (res_populated && !vm->is_vcpu_info_populated) {
		vm->is_vcpu_info_populated = true;
		vm->is_active = true;
	} else if (!res_populated && vm->is_vcpu_info_populated) {
		gh_reset_vm(vm);
	}
unlock:
	mutex_unlock(&gh_vm_mutex);
}

static void android_rvh_gh_before_vcpu_run(void *unused, u16 vmid, u32 vcpu_id)
{
	struct gh_proxy_vcpu *vcpu;
	struct gh_proxy_vm *vm;

	if (vmid > QCOM_SCM_MAX_MANAGED_VMID)
		return;

	vm = gh_get_vm(vmid);
	if (!vm || !vm->is_active)
		return;

	vcpu = xa_load(&vm->vcpus, vcpu_id);
	if (!vcpu)
		return;

	/* Call into Gunyah to run vcpu. */
	preempt_disable();
	if (vcpu->wdog_frozen) {
		gh_hcall_wdog_manage(vm->wdog_cap_id, WATCHDOG_MANAGE_OP_UNFREEZE);
		vcpu->wdog_frozen = false;
	}
}

static void android_rvh_gh_after_vcpu_run(void *unused, u16 vmid, u32 vcpu_id, int hcall_ret,
			const struct gunyah_hypercall_vcpu_run_resp *resp)
{
	struct gh_proxy_vcpu *vcpu;
	struct gh_proxy_vm *vm;

	if (vmid > QCOM_SCM_MAX_MANAGED_VMID)
		return;

	vm = gh_get_vm(vmid);
	if (!vm || !vm->is_active)
		return;

	vcpu = xa_load(&vm->vcpus, vcpu_id);
	if (!vcpu)
		return;

	if (hcall_ret == GH_ERROR_OK && resp->state == GUNYAH_VCPU_STATE_READY) {
		if (need_resched()) {
			gh_hcall_wdog_manage(vm->wdog_cap_id,
					WATCHDOG_MANAGE_OP_FREEZE);
			vcpu->wdog_frozen = true;
		}
	}
	preempt_enable();
	if (signal_pending(current)) {
		if (!vcpu->wdog_frozen) {
			gh_hcall_wdog_manage(vm->wdog_cap_id,
					WATCHDOG_MANAGE_OP_FREEZE);
			vcpu->wdog_frozen = true;
		}
	}
}

static int gh_vcpu_mgr_reg_rm_cbs(void)
{
	int ret = -EINVAL;

	ret = gh_rm_set_wdog_manage_cb(&gh_wdog_manage);
	if (ret) {
		pr_err("fail to set the WDOG resource callback\n");
		return ret;
	}

	ret = gh_rm_set_vcpu_affinity_cb(&gh_populate_vm_vcpu_info);
	if (ret) {
		pr_err("fail to set the VM VCPU populate callback\n");
		return ret;
	}

	ret = gh_rm_reset_vcpu_affinity_cb(&gh_unpopulate_vm_vcpu_info);
	if (ret) {
		pr_err("fail to set the VM VCPU unpopulate callback\n");
		return ret;
	}

	ret = gh_rm_all_res_populated_cb(&gh_populate_all_res_info);
	if (ret) {
		pr_err("fail to set the all res populate callback\n");
		return ret;
	}

	return 0;
}

static void gh_register_hooks(void)
{
	register_trace_android_rvh_gh_before_vcpu_run(android_rvh_gh_before_vcpu_run, NULL);
	register_trace_android_rvh_gh_after_vcpu_run(android_rvh_gh_after_vcpu_run, NULL);
}

int gh_vcpu_mgr_init(void)
{
	int ret;

	gh_vms = kcalloc(GH_VM_MAX, sizeof(struct gh_proxy_vm), GFP_KERNEL);
	if (!gh_vms) {
		ret = -ENOMEM;
		goto err;
	}

	ret = gh_vcpu_mgr_reg_rm_cbs();
	if (ret)
		goto free_gh_vms;

	gh_init_vms();
	gh_register_hooks();
	init_done = true;
	return 0;

free_gh_vms:
	kfree(gh_vms);
err:
	return ret;
}
module_init(gh_vcpu_mgr_init);

void gh_vcpu_mgr_exit(void)
{
	struct gh_proxy_vm *vm;
	int i;

	for (i = 0; i < GH_VM_MAX; i++) {
		vm = &gh_vms[i];
		xa_destroy(&vm->vcpus);
	}

	kfree(gh_vms);
	init_done = false;
}
module_exit(gh_vcpu_mgr_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Gunyah VCPU management Driver");

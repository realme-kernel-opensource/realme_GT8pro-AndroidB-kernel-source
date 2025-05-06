/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __GH_VCPU_MGR_H
#define __GH_VCPU_MGR_H

#include <linux/err.h>
#include <linux/types.h>

#include <linux/gunyah/hcall_common.h>
#include <linux/gunyah/gh_common.h>
#include <asm/gunyah/hcall.h>

#define WATCHDOG_MANAGE_OP_FREEZE		0
#define WATCHDOG_MANAGE_OP_FREEZE_AND_RESET	1
#define WATCHDOG_MANAGE_OP_UNFREEZE		2

#define GH_VCPU_WFI			1
#define GH_VCPU_CPU_SUSPEND		2
#define GH_VCPU_SYSTEM_SUSPEND		3

static inline int gh_hcall_wdog_manage(gh_capid_t wdog_capid, u16 operation)
{
	int ret;
	struct gh_hcall_resp _resp = {0};

	ret = _gh_hcall(0x6063, (struct gh_hcall_args){ wdog_capid, operation }, &_resp);

	return ret;
}

/*
 * proxy scheduler APIs called by gunyah driver
 */
#if IS_ENABLED(CONFIG_GH_VCPU_MGR)
int gh_vcpu_mgr_init(void);
void gh_vcpu_mgr_exit(void);
#else /* !CONFIG_GH_VCPU_MGR */
static inline int gh_vcpu_mgr_init(void)
{
	return -ENODEV;
}
static inline void gh_vcpu_mgr_exit(void) { }
#endif /* CONFIG_GH_VCPU_MGR */
#endif

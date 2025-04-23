/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __GH_RM_HEAP_MANAGER_H
#define __GH_RM_HEAP_MANAGER_H

#if IS_ENABLED(CONFIG_GH_RM_HEAP_MANAGER)
uint64_t gh_rm_get_total_heap_added(enum gh_hyp_heap_label label);
uint64_t gh_get_dmabuf_hyp_heap_size(size_t size);
uint64_t gh_get_rm_heap_free_hyp_object(enum gh_hyp_heap_label label);
int gh_rm_heap_add_hyp_object(size_t size, enum gh_hyp_heap_label label);
size_t gh_rm_heap_remove_hyp_object(size_t size, enum gh_hyp_heap_label label);
uint64_t gh_get_hyp_heap_free(void);
int gh_rm_heap_add_dmabuf(size_t size);
size_t gh_rm_heap_remove_dmabuf(size_t size);
#else
static inline uint64_t gh_rm_get_total_heap_added(enum gh_hyp_heap_label label)
{
	return 0;
}

static inline uint64_t gh_get_dmabuf_hyp_heap_size(size_t size)
{
	return 0;
}

static inline uint64_t gh_get_rm_heap_free_hyp_object(enum gh_hyp_heap_label label)
{
	return 0;
}

static inline int gh_rm_heap_add_hyp_object(size_t size, enum gh_hyp_heap_label label)
{
	return 0;
}

static inline size_t gh_rm_heap_remove_hyp_object(size_t size, enum gh_hyp_heap_label label)
{
	return 0;
}

static inline uint64_t gh_get_hyp_heap_free(void)
{
	return 0;
}

static inline int gh_rm_heap_add_dmabuf(size_t size)
{
	return 0;
}

static inline size_t gh_rm_heap_remove_dmabuf(size_t size)
{
	return 0;
}
#endif //CONFIG_GH_RM_HEAP_MANAGER
#endif

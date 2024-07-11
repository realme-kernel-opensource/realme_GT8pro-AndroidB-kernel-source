// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/delay.h>

#include "qpace-constants.h"
#include "qpace-reg-accessors.h"
#include "qpace.h"

/*
 * =============================================================================
 * Urgent init code
 * =============================================================================
 */

/* Urgent command registers */
enum urg_reg_cxts {
	URG_COMP_CNTXT,
	URG_DECOMP_CNTXT,
	URG_COPY_CNTXT
};

static inline void program_urg_comp_context(int urg_reg_num)
{
	u32 urg_cmd_settings = 0;

	/* Set the limit of the compression output size */
	urg_cmd_settings = FIELD_PREP(URG_CMD_0_CFG_CNTXT_SIZE_SIZE, PAGE_SIZE - 1);
	QPACE_WRITE_URG_CMD_CTX_REG(QPACE_URG_CMD_0_CFG_CNTXT_SIZE_n_OFFSET,
				    urg_reg_num, URG_COMP_CNTXT,
				    urg_cmd_settings);

	urg_cmd_settings = FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_OPER, COMP);

	/* Indicate that the compression imput size is an order 0 page */
	urg_cmd_settings |= FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_PAGE_CNT, 0);

	/* Program part of the SMMU input and output SIDs */
	urg_cmd_settings |= FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_SCTX,
				       DEFAULT_SMMU_CONTEXT);
	urg_cmd_settings |= FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_DCTX,
				       DEFAULT_SMMU_CONTEXT);

	QPACE_WRITE_URG_CMD_CTX_REG(QPACE_URG_CMD_0_CFG_CNTXT_MISC_n_OFFSET,
				    urg_reg_num, URG_COMP_CNTXT,
				    urg_cmd_settings);
}

static inline void program_urg_decomp_context(int urg_reg_num)
{
	u32 urg_cmd_settings = 0;

	/* The input size will be programmed by a requestor */
	urg_cmd_settings = FIELD_PREP(URG_CMD_0_CFG_CNTXT_SIZE_SIZE, 0);
	QPACE_WRITE_URG_CMD_CTX_REG(QPACE_URG_CMD_0_CFG_CNTXT_SIZE_n_OFFSET,
				    urg_reg_num, URG_DECOMP_CNTXT,
				    urg_cmd_settings);

	urg_cmd_settings = FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_OPER, DECOMP);

	/* Program part of the SMMU input and output SIDs */
	urg_cmd_settings |= FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_SCTX,
				       DEFAULT_SMMU_CONTEXT);
	urg_cmd_settings |= FIELD_PREP(URG_CMD_0_CFG_CNTXT_MISC_DCTX,
				       DEFAULT_SMMU_CONTEXT);

	QPACE_WRITE_URG_CMD_CTX_REG(QPACE_URG_CMD_0_CFG_CNTXT_MISC_n_OFFSET,
				    urg_reg_num, URG_DECOMP_CNTXT,
				    urg_cmd_settings);
}

static void program_urg_command_contexts(void)
{
	int urg_reg_num;

	/* Configure the needed contexts for each of the urgent command registers */
	for (urg_reg_num = 0; urg_reg_num < NUM_TRS_ERS_URG_CMD_REGS; urg_reg_num++) {
		program_urg_comp_context(urg_reg_num);
		program_urg_decomp_context(urg_reg_num);
	}
}

/*
 * =============================================================================
 * Urgent operation code
 * =============================================================================
 */

static inline u32 qpace_urgent_command_trigger(phys_addr_t input_addr,
					       phys_addr_t output_addr,
					       int urg_reg_num,
					       enum urg_reg_cxts command)
{
	u32 urg_addr_field, stat_reg, tmp_addr_bits;

	urg_addr_field = FIELD_PREP(URG_CMD_0_TD_DST_ADDR_L__CMD_CFG_CNTXT,
			 command);

	tmp_addr_bits = FIELD_GET(GENMASK(31, 8), output_addr);
	urg_addr_field |= FIELD_PREP(URG_CMD_0_TD_DST_ADDR_L__DST_ADDR_L,
				     tmp_addr_bits);
	QPACE_WRITE_URG_CMD_REG(urg_reg_num,
				QPACE_URG_CMD_0_TD_DST_ADDR_L_CFG_CNTXT_OFFSET,
				urg_addr_field);

	urg_addr_field = FIELD_GET(GENMASK(63, 32), output_addr);
	QPACE_WRITE_URG_CMD_REG(urg_reg_num,
				QPACE_URG_CMD_0_TD_DST_ADDR_H_OFFSET,
				urg_addr_field);

	urg_addr_field = FIELD_GET(GENMASK(31, 0), input_addr);
	QPACE_WRITE_URG_CMD_REG(urg_reg_num,
				QPACE_URG_CMD_0_TD_SRC_ADDR_L_OFFSET,
				urg_addr_field);

	/* This triggers the operation */
	urg_addr_field = FIELD_GET(GENMASK(63, 32), input_addr);
	QPACE_WRITE_URG_CMD_REG(urg_reg_num,
				QPACE_URG_CMD_0_TD_SRC_ADDR_H_OFFSET,
				urg_addr_field);

	/* Wait for operation to finish */
	do {
		stat_reg = QPACE_READ_URG_CMD_REG(urg_reg_num,
						  QPACE_URG_CMD_0_ED_STAT_OFFSET);
	} while (FIELD_GET(URG_CMD_0_ED_STAT_COMP_CODE, stat_reg) == OP_URG_ONGOING);

	return stat_reg;
}

/*
 * qpace_urgent_compress() - compress a page
 * @input_addr: Input address of the page to be compressed
 * @output_addr: Address to output the compressed page to
 *
 * Compress a page of PAGE_SIZE bytes and output it to @output_addr.
 *
 * Returns the size of the compressed page in bytes if successful and returns
 * -EINVAL if compression failed.
 */
int qpace_urgent_compress(phys_addr_t input_addr, phys_addr_t output_addr)
{
	/* We have 8 cores and 8 urgent command registers */
	int urg_reg_num;
	u32 stat_reg, stat_reg_val;

	urg_reg_num = get_cpu();
	stat_reg = qpace_urgent_command_trigger(input_addr, output_addr, urg_reg_num,
						URG_COMP_CNTXT);
	put_cpu();

	stat_reg_val = FIELD_GET(URG_CMD_0_ED_STAT_COMP_CODE, stat_reg);
	if (stat_reg_val != OP_OK) {
		pr_err("%s: register %d failed with %u\n",
		       __func__, urg_reg_num, stat_reg_val);
		return -EINVAL;
	}

	return FIELD_GET(URG_CMD_0_ED_STAT_SIZE, stat_reg);
}
EXPORT_SYMBOL_GPL(qpace_urgent_compress);

/*
 * qpace_urgent_decompress() - decompress a page that was compressed by QPaCE
 * @input_addr: Input address of the compressed page
 * @output_addr: Address to output the decompressed page to
 * @input_size: size of the compressed input-page
 *
 * Decompressed a compressed page previously compressed by QPaCE. The output is
 * expected to be PAGE_SIZE bytes.
 *
 * Returns the size of the decompressed page in bytes if successful (though the
 * caller should know its page sized), and returns -EINVAL if decompression
 * failed.
 */
int qpace_urgent_decompress(phys_addr_t input_addr,
			    phys_addr_t output_addr,
			    int input_size)
{
	/* We have 8 cores and 8 urgent command registers */
	int urg_reg_num;
	u32 stat_reg, stat_reg_val;

	urg_reg_num = get_cpu();

	QPACE_WRITE_URG_CMD_CTX_REG(QPACE_URG_CMD_0_CFG_CNTXT_SIZE_n_OFFSET,
				    urg_reg_num, URG_DECOMP_CNTXT,
				    input_size);

	stat_reg = qpace_urgent_command_trigger(input_addr, output_addr, urg_reg_num,
						URG_DECOMP_CNTXT);
	put_cpu();

	stat_reg_val = FIELD_GET(URG_CMD_0_ED_STAT_COMP_CODE, stat_reg);
	if (stat_reg_val != OP_OK) {
		pr_err("%s: register %d failed with %u\n",
		       __func__, urg_reg_num, stat_reg_val);
		return -EINVAL;
	}

	return FIELD_GET(URG_CMD_0_ED_STAT_SIZE, stat_reg);
}
EXPORT_SYMBOL_GPL(qpace_urgent_decompress);

/*
 * =============================================================================
 * Ring init code
 * =============================================================================
 */

struct transfer_ring {
	struct qpace_transfer_descriptor *ring_buffer_start;

	struct qpace_transfer_descriptor *hw_read_ptr;
	struct qpace_transfer_descriptor *hw_write_ptr;
};

struct event_ring {
	struct completion ring_has_events;
	struct qpace_event_descriptor *ring_buffer_start;

	struct qpace_event_descriptor *hw_read_ptr;
	struct qpace_event_descriptor *hw_write_ptr;
};

static struct transfer_ring *tr_rings;
static struct event_ring *ev_rings;


static inline void deinit_transfer_ring(int tr_num)
{
	__free_pages(virt_to_page(tr_rings[tr_num].ring_buffer_start), RING_SIZE_ORDER);
}

static inline int init_transfer_ring(int tr_num)
{
	struct page *page_ptr;
	u32 tr_ring_reg_val;
	phys_addr_t ring_start_phys, ring_end_phys;

	BUILD_BUG_ON(sizeof(struct qpace_transfer_descriptor) != DESCRIPTOR_SIZE);
	BUILD_BUG_ON(sizeof(struct transfer_ring) > 64);

	/* Allocate TR buffer */
	page_ptr = alloc_pages(GFP_KERNEL | __GFP_ZERO,
			       RING_SIZE_ORDER);
	if (!page_ptr)
		return -ENOMEM;

	tr_rings[tr_num].ring_buffer_start = page_to_virt(page_ptr);

	/*
	 * Set block-event-interrupt on all TDs by default. We will set bei = 0
	 * only one TD per batch, in order to have a single interrupt for a
	 * batch.
	 */
	for (int td_id = 0; td_id < DESCRIPTORS_PER_RING; td_id++)
		tr_rings[tr_num].ring_buffer_start[td_id].bei = 1;

	/* Configure the ring registers */
	tr_ring_reg_val = FIELD_PREP(DMA_TR_MGR_0_CFG_CACHEALLOC, LLC_NO_CACHE_OP);
	tr_ring_reg_val |= FIELD_PREP(DMA_TR_MGR_0_CFG_CTX,
				  DEFAULT_SMMU_CONTEXT);
	/*
	 * Leave COMP_SIZE_OVER_LIMIT_FORCE_EVT as is and let interrupt
	 * generation settings be done on a per-TD basis.
	 */
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_CFG_OFFSET,
			   tr_ring_reg_val);

	/* Initialize HW and SW ring pointers */
	tr_rings[tr_num].hw_write_ptr = tr_rings[tr_num].ring_buffer_start;
	tr_rings[tr_num].hw_read_ptr = tr_rings[tr_num].ring_buffer_start +
				       DESCRIPTORS_PER_RING - 1;

	ring_start_phys = virt_to_phys(tr_rings[tr_num].hw_write_ptr);
	ring_end_phys = virt_to_phys(tr_rings[tr_num].hw_read_ptr + 1);

	tr_ring_reg_val = FIELD_GET(GENMASK(63, 32), ring_start_phys);
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_START_H_OFFSET,
			   tr_ring_reg_val);
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_START_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), ring_start_phys));

	tr_ring_reg_val = FIELD_GET(GENMASK(63, 32), ring_end_phys);
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_END_H_OFFSET,
			   tr_ring_reg_val);
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_END_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), ring_end_phys));

	return 0;
}

static void deinit_transfer_rings(int num_rings_to_deinit)
{
	for (int tr_num = 0; tr_num < num_rings_to_deinit; tr_num++)
		deinit_transfer_ring(tr_num);

	kfree(tr_rings);
}

static int init_transfer_rings(void)
{
	int ret;

	tr_rings = kmalloc_array(NUM_RINGS, sizeof(struct transfer_ring),
				 GFP_KERNEL | __GFP_ZERO);

	for (int tr_num = 0; tr_num < NUM_RINGS; tr_num++) {
		ret = init_transfer_ring(tr_num);
		if (ret) {
			deinit_transfer_rings(tr_num);
			return ret;
		}
	}

	return 0;
}

static inline void deinit_event_ring(int er_num)
{
	__free_pages(virt_to_page(ev_rings[er_num].ring_buffer_start), RING_SIZE_ORDER);
}

static inline int init_event_ring(int er_num)
{
	struct page *page_ptr;
	u32 ev_ring_reg_val;
	phys_addr_t ring_start_phys, ring_end_phys;

	BUILD_BUG_ON(sizeof(struct qpace_event_descriptor) != DESCRIPTOR_SIZE);
	BUILD_BUG_ON(sizeof(struct event_ring) > 64);

	init_completion(&ev_rings[er_num].ring_has_events);

	/* Allocate EV buffer */
	page_ptr = alloc_pages(GFP_KERNEL | __GFP_ZERO,
			       RING_SIZE_ORDER);
	if (!page_ptr)
		return -ENOMEM;

	ev_rings[er_num].ring_buffer_start = page_to_virt(page_ptr);

	/* Configure the ring registers */
	ev_ring_reg_val = FIELD_PREP(DMA_ER_MGR_0_CFG_CACHEALLOC, LLC_ALLOC);
	ev_ring_reg_val |= FIELD_PREP(DMA_ER_MGR_0_CFG_CTX,
				  DEFAULT_SMMU_CONTEXT);
	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_CFG_OFFSET,
			   ev_ring_reg_val);

	/* Initialize HW and SW ring pointers */
	ev_rings[er_num].hw_write_ptr = ev_rings[er_num].ring_buffer_start;
	ev_rings[er_num].hw_read_ptr = ev_rings[er_num].ring_buffer_start +
				       DESCRIPTORS_PER_RING - 1;

	ring_start_phys = virt_to_phys(ev_rings[er_num].hw_write_ptr);
	ring_end_phys = virt_to_phys(ev_rings[er_num].hw_read_ptr + 1);

	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_START_H_OFFSET,
			   FIELD_GET(GENMASK(63, 32), ring_start_phys));
	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_START_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), ring_start_phys));

	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_END_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), ring_end_phys));
	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_END_H_OFFSET,
			   FIELD_GET(GENMASK(63, 32), ring_end_phys));

	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_RD_PTR_H_OFFSET,
			   FIELD_GET(GENMASK(63, 32), ring_end_phys));
	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_RD_PTR_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), ring_end_phys) - DESCRIPTOR_SIZE);

	return 0;
}

static void deinit_event_rings(int num_rings_to_deinit)
{
	for (int er_num = 0; er_num < num_rings_to_deinit; er_num++)
		deinit_event_ring(er_num);

	kfree(ev_rings);
}

static int init_event_rings(void)
{
	int ret;

	ev_rings = kmalloc_array(NUM_RINGS, sizeof(struct event_ring),
				 GFP_KERNEL | __GFP_ZERO);

	for (int er_num = 0; er_num < NUM_RINGS; er_num++) {
		ret = init_event_ring(er_num);
		if (ret) {
			deinit_event_rings(er_num);
			return ret;
		}
	}

	/* Enable interrupt generation for all in-use rings */
	QPACE_WRITE_GEN_CMD_REG(QPACE_COMMON_EE_DMA_ER_COMPL_INTR_EN_OFFSET,
				(1 << NUM_RINGS) - 1);

	return 0;
}

/*
 * =============================================================================
 * Ring operation code
 * =============================================================================
 */

static inline void transfer_ring_increment(struct transfer_ring *ring)
{
	if (ring->hw_write_ptr == ring->ring_buffer_start + DESCRIPTORS_PER_RING - 1)
		ring->hw_write_ptr = ring->ring_buffer_start;
	else
		ring->hw_write_ptr++;
}

/*
 * qpace_queue_copy() - queue a copy operation
 * @tr_num: The transfer ring to queue the request to
 * @src_addr: Input address of the object to copy
 * @dst_addr: Address to copy the object to
 * @copy_size: Number of bytes to copy
 *
 * Queue a copy request to a chosen transfer ring
 */
void qpace_queue_copy(int tr_num, phys_addr_t src_addr, phys_addr_t dst_addr, size_t copy_size)
{
	struct transfer_ring *ring = &tr_rings[tr_num];
	struct qpace_transfer_descriptor *td = ring->hw_write_ptr;

	td->src_addr = src_addr;
	td->dst_addr = dst_addr;

	td->size = copy_size;
	td->operation = COPY;
	/* Page count not needed for copy */

	/* Use default SIDs */

	/* We pair each transfer ring with a unique event ring */
	td->event_ring_id = tr_num;

	transfer_ring_increment(ring);
}
EXPORT_SYMBOL_GPL(qpace_queue_copy);

/*
 * qpace_queue_compress() - queue a compression operation
 * @tr_num: The transfer ring to queue the request to
 * @src_addr: Input address of the object to copy
 * @dst_addr: Address to copy the object to
 *
 * Queue a compression request to a chosen transfer ring
 */
void qpace_queue_compress(int tr_num, phys_addr_t src_addr, phys_addr_t dst_addr)
{
	struct transfer_ring *ring = &tr_rings[tr_num];
	struct qpace_transfer_descriptor *td = ring->hw_write_ptr;

	td->src_addr = src_addr;
	td->dst_addr = dst_addr;

	td->size = (PAGE_SIZE) - 1;
	td->operation = COMP;
	/* Leave page count as order 0 */

	/* Use default SIDs */

	/* We pair each transfer ring with a single event ring */
	td->event_ring_id = tr_num;

	transfer_ring_increment(ring);
}
EXPORT_SYMBOL_GPL(qpace_queue_compress);

static inline void qpace_free_tr_entries(int tr_num)
{
	struct transfer_ring *ring = &tr_rings[tr_num];
	struct qpace_transfer_descriptor *last_processed_td;

	if (ring->hw_write_ptr == ring->ring_buffer_start)
		last_processed_td = ring->ring_buffer_start + DESCRIPTORS_PER_RING - 1;
	else
		last_processed_td = ring->hw_write_ptr - 1;

	/* Reset BEI and update hw_read_ptr */
	ring->hw_read_ptr = last_processed_td;
	last_processed_td->bei = 1;
}

static inline bool transfer_ring_is_empty(struct transfer_ring *ring)
{
	if (ring->hw_write_ptr == ring->ring_buffer_start)
		return (ring->hw_read_ptr == ring->ring_buffer_start +
			DESCRIPTORS_PER_RING - 1) ? true : false;
	else
		return (ring->hw_read_ptr + 1 == ring->hw_write_ptr) ? true : false;
}

/*
 * qpace_trigger_tr() - Have QPaCE process outstanding TD in the transfer ring
 * @tr_num: Transfer ring to have qpace take submissions from for processing
 *
 * Have QPaCE process outstanding transfer descriptors / TDs in the transfer ring,
 * i.e. QPaCE will process the requests submitted from the qpace_queue_* operations.
 * This boils down to updating a HW register that points to the end of the valid
 * in the ring buffer.
 *
 * Return: true if the ring had items to submit / submission was done, false if
 * the ring had no items to submit.
 */
bool qpace_trigger_tr(int tr_num)
{
	struct transfer_ring *ring = &tr_rings[tr_num];
	struct qpace_transfer_descriptor *last_processed_td;

	phys_addr_t write_ptr_addr;

	/* bail if ring is empty */
	if (transfer_ring_is_empty(ring))
		return false;

	if (ring->hw_write_ptr == ring->ring_buffer_start)
		last_processed_td = ring->ring_buffer_start + DESCRIPTORS_PER_RING - 1;
	else
		last_processed_td = ring->hw_write_ptr - 1;

	last_processed_td->bei = 0;

	write_ptr_addr = virt_to_phys(ring->hw_write_ptr);

	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_WR_PTR_H_OFFSET,
			   FIELD_GET(GENMASK(63, 32), write_ptr_addr));

	/* Below write triggers processing */
	QPACE_WRITE_TR_REG(tr_num, QPACE_DMA_TR_MGR_0_WR_PTR_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), write_ptr_addr));

	return true;
}
EXPORT_SYMBOL_GPL(qpace_trigger_tr);

/*
 * qpace_wait_for_tr_consumption() - Wait for event completion interrupt for @tr_num
 * @tr_num: Transfer ring to have qpace take submissions from for processing
 * @no_sleep: If true, poll on the relevant completion event, go to sleep otherwise.
 *
 * Wait for there to be a completion event for the event ring corresponding to the
 * transfer ring @tr_num. Can be sleeping or non-sleeping based on @no_sleep.
 */
void qpace_wait_for_tr_consumption(int tr_num, bool no_sleep)
{
	if (no_sleep)
		while (!try_wait_for_completion(&ev_rings[tr_num].ring_has_events))
			udelay(100);
	else
		wait_for_completion(&ev_rings[tr_num].ring_has_events);

	qpace_free_tr_entries(tr_num);
}
EXPORT_SYMBOL_GPL(qpace_wait_for_tr_consumption);

static inline void qpace_free_er_entries(int er_num)
{
	struct event_ring *ring = &ev_rings[er_num];
	phys_addr_t last_processed_ed_phys_addr;

	/* Update RD_PTR to last-processed ED */
	if (ring->hw_write_ptr == ring->ring_buffer_start)
		ring->hw_read_ptr = ring->ring_buffer_start + DESCRIPTORS_PER_RING - 1;
	else
		ring->hw_read_ptr = ring->hw_write_ptr - 1;

	last_processed_ed_phys_addr = virt_to_phys(ring->hw_read_ptr);

	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_RD_PTR_H_OFFSET,
			   FIELD_GET(GENMASK(63, 32), last_processed_ed_phys_addr));
	QPACE_WRITE_ER_REG(er_num, QPACE_DMA_ER_MGR_0_RD_PTR_L_OFFSET,
			   FIELD_GET(GENMASK(31, 0), last_processed_ed_phys_addr));

	/* re-enable interrupts for this event ring */
	QPACE_WRITE_GEN_CMD_REG(QPACE_COMMON_EE_DMA_ER_COMPL_STAT_CLR_OFFSET,
				1 << er_num);
}

static inline void consume_ed(struct qpace_event_descriptor *ed, int ed_index,
			      process_ed_fn success_handler,
			      process_ed_fn fail_handler)
{
	if (ed->completion_code == OP_OK)
		success_handler(ed, ed_index);
	else
		fail_handler(ed, ed_index);
}

static inline void event_ring_increment(struct event_ring *ring)
{
	if (ring->hw_read_ptr == ring->ring_buffer_start + DESCRIPTORS_PER_RING - 1)
		ring->hw_read_ptr = ring->ring_buffer_start;
	else
		ring->hw_read_ptr++;
}

void qpace_consume_er(int er_num,
		      process_ed_fn success_handler,
		      process_ed_fn fail_handler)
{
	struct event_ring *ring = &ev_rings[er_num];
	u32 er_ring_reg_val;
	phys_addr_t hw_write_ptr_phys_addr;
	struct qpace_event_descriptor *ed;
	int ed_offset;

	/* Get new value of HW write ptr to determine the number of produced EDs */
	hw_write_ptr_phys_addr = QPACE_READ_ER_REG(er_num, QPACE_DMA_ER_MGR_0_WR_PTR_L_OFFSET);
	er_ring_reg_val = QPACE_READ_ER_REG(er_num, QPACE_DMA_ER_MGR_0_WR_PTR_H_OFFSET);
	hw_write_ptr_phys_addr |= FIELD_PREP(GENMASK(63, 32), er_ring_reg_val);

	ring->hw_write_ptr = phys_to_virt(hw_write_ptr_phys_addr);

	/*
	 * The read pointer indicates the last ED processed by SW, so we
	 * increment the read pointer by one to get the first new ED.
	 */
	event_ring_increment(ring);

	/* Initialize loop iterator */
	ed = ring->hw_read_ptr;

	/* Unrolled ring buffer loop */
	if (ring->hw_read_ptr >= ring->hw_write_ptr) {
		for (; ed < ring->ring_buffer_start + DESCRIPTORS_PER_RING;
		     ed++) {
			ed_offset = ed - ring->ring_buffer_start;

			/* Process the ED */
			consume_ed(ed, ed_offset, success_handler, fail_handler);
		}

		/* Set ed to point to start */
		ed = ring->ring_buffer_start;
	}

	for (; ed < ring->hw_write_ptr; ed++) {
		ed_offset = ed - ring->ring_buffer_start;

		/* Process the ED */
		consume_ed(ed, ed_offset, success_handler, fail_handler);
	}

	qpace_free_er_entries(er_num);
}
EXPORT_SYMBOL_GPL(qpace_consume_er);


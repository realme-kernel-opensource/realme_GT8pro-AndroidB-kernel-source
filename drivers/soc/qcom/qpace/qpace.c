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


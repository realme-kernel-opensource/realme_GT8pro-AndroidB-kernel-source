/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

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
int qpace_urgent_compress(phys_addr_t input_addr,
			  phys_addr_t output_addr);


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
			    int input_size);

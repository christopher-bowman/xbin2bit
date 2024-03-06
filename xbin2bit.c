/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2019 Thomas Skibo. <ThomasSkibo@yahoo.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * xbit2bin.c
 *
 * Usage: xbit2bin <bitstream filename> [<output filename>]
 *
 * xbit2bin reads a Xilinx Zynq bitsream file and programs the Zynq PL (FPGA)
 * by stripping the bitstream header, byte-order swapping the bitstream
 * data if necessary, and writing the data to the devcfg(4) device.
 * Optionally, the bitstream data can be written to an ordinary file.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/endian.h>

static uint32_t buswords_noswap[] = { 0x000000bb, 0x11220044 };
static uint32_t buswords_swap[] =   { 0xbb000000, 0x44002211 };

/* Number of bytes we will read to examine header. */
#define MAXHDR 256

/* Analyze the bitstream header and return the number of bytes of header
 * to trim from the front and set the do_swap flag if we need to byte-swap
 * the bitstream.  Returns -1 if it cannot parse the header.
 */
static int
analyze_xilinx_header(uint8_t *hdr, int *do_swap)
{
	int i;
	int dummies;

	*do_swap = 0;

	/* Look for 32 dummy bytes at start of bitstream. */
	dummies = 0;
	for (i = 0; i < MAXHDR - sizeof(buswords_swap); i++)
		if (hdr[i] == 0xff)
			dummies++;
		else if (dummies >= 32)
			break;
		else
			dummies = 0;

	/* If no dummy words, bitstream is probably corrupted. */
	if (dummies < 32)
		return (-1);

	/* The first words after the dummy words should be the bus width
	 * auto-detect words.  Determine if they need byte swapping or not.
	 */
	if (memcmp(&hdr[i], buswords_noswap, sizeof(buswords_noswap)) == 0)
		return i - 32;
	else if (memcmp(&hdr[i], buswords_swap, sizeof(buswords_swap)) == 0) {
		*do_swap = 1;
		return i - 32;
	} else
		return (-1);
}

/* Byte-order swap buffer of n bytes.  Assumes buffer is 32-bit aligned. */
static void
xbit2bin_bswap(char *datbuf, int n)
{
	int i;

	for (i = 0; i * sizeof(uint32_t) < n; i++)
		*((uint32_t *)datbuf + i) = bswap32(*((uint32_t *)datbuf + i));
}

/* Convert input file fin from a bitstream to a binary file and send to fout.
 */
static int
xbit2bin(int fin, int fout)
{
	int strip;
	int do_swap;
	int n;
	uint8_t hdr[MAXHDR];
	char datbuf[64 * 1024];

	/* First read and analyze header. */
	if (read(fin, hdr, sizeof(hdr)) != sizeof(hdr)) {
		fprintf(stderr, "Trouble reading bitstream header.\n");
		perror("huh?");
		return (-1);
	}
	strip = analyze_xilinx_header(hdr, &do_swap);
	if (strip < 0) {
		fprintf(stderr, "Trouble analizing bitstream header.\n");
		return (-1);
	}

	/* Strip header. */
	n = sizeof(hdr) - strip;
	if (strip > 0)
		memmove(hdr, hdr + strip, n);

	/* Read more data to get to 32-bit word multiple. */
	if ((n & 3) != 0) {
		if (read(fin, hdr + n, 4 - (n & 3)) != 4 - (n & 3)) {
			fprintf(stderr, "Trouble reading bitstream data.\n");
			return (-1);
		}
		n += 4 - (n & 3);
	}

	/* Byte-order swap header if necessary. */
	if (do_swap)
		xbit2bin_bswap((char *)hdr, n);

	/* Output header. */
	if (write(fout, hdr, n) != n) {
		fprintf(stderr, "Trouble writing binary header.\n");
		return (-1);
	}

	/* Copy the rest, byte swapping if necessary. */
	for (;;) {
		n = read(fin, datbuf, sizeof(datbuf));
		if (n == 0)
			break;
		if (n < 0) {
			fprintf(stderr, "Trouble reading bitstream data.\n");
			return (-1);
		}

		if (do_swap)
			xbit2bin_bswap(datbuf, n);

		if (write(fout, datbuf, n) != n) {
			fprintf(stderr, "Trouble writing bitstream data.\n");
			return (-1);
		}
	}

	return (0);
}

static char usage[] = "Usage: %s <bitstream filename> [<output filename>]\n";

int
main(int argc, char *argv[])
{
	char *filename_out = "/dev/devcfg";
	int fdin;
	int fdout;

	if (argc < 2) {
		fprintf(stderr, usage, argv[0]);
		exit(1);
	}

	if (argc > 2)
		filename_out = argv[2];

	fdin = open(argv[1], O_RDONLY);
	if (fdin < 0) {
		fprintf(stderr, "Trouble opening %s for read: %s\n",
			argv[optind], strerror(errno));
		exit(1);
	}

	fdout = open(filename_out, O_WRONLY);
	if (fdout < 0) {
		fprintf(stderr, "Trouble opening %s for write: %s\n",
			filename_out, strerror(errno));
		exit(1);
	}

	if (xbit2bin(fdin, fdout) < 0)
		exit(1);

	close(fdin);
	close(fdout);

	return (0);
}

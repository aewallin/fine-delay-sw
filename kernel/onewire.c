/*
 * Access to 1w thermometer
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */

#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/delay.h>
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

#define R_CSR		0x0
#define R_CDR		0x4

#define CSR_DAT_MSK	(1<<0)
#define CSR_RST_MSK	(1<<1)
#define CSR_OVD_MSK	(1<<2)
#define CSR_CYC_MSK	(1<<3)
#define CSR_PWR_MSK	(1<<4)
#define CSR_IRQ_MSK	(1<<6)
#define CSR_IEN_MSK	(1<<7)
#define CSR_SEL_OFS	8
#define CSR_SEL_MSK	(0xF<<8)
#define CSR_POWER_OFS	16
#define CSR_POWER_MSK	(0xFFFF<<16)
#define CDR_NOR_MSK	(0xFFFF<<0)
#define CDR_OVD_OFS	16
#define CDR_OVD_MSK	(0xFFFF<<16)

#define CLK_DIV_NOR	(624/2)
#define CLK_DIV_OVD	(124/2)

#define CMD_ROM_SEARCH		0xF0
#define CMD_ROM_READ		0x33
#define CMD_ROM_MATCH		0x55
#define CMD_ROM_SKIP		0xCC
#define CMD_ROM_ALARM_SEARCH	0xEC

#define CMD_CONVERT_TEMP	0x44
#define CMD_WRITE_SCRATCHPAD	0x4E
#define CMD_READ_SCRATCHPAD	0xBE
#define CMD_COPY_SCRATCHPAD	0x48
#define CMD_RECALL_EEPROM	0xB8
#define CMD_READ_POWER_SUPPLY	0xB4

#define FD_OW_PORT 0 /* what is this slow? */

static void ow_writel(struct fd_dev *fd, uint32_t val, unsigned long reg)
{
	fmc_writel(fd->fmc, val, fd->fd_owregs_base + reg);
}

static uint32_t ow_readl(struct fd_dev *fd, unsigned long reg)
{
	return fmc_readl(fd->fmc, fd->fd_owregs_base + reg);
}

static int ow_reset(struct fd_dev *fd, int port)
{
	uint32_t reg, data;

	data = ((port << CSR_SEL_OFS) & CSR_SEL_MSK)
		| CSR_CYC_MSK | CSR_RST_MSK;
	ow_writel(fd, data, R_CSR);
	while(ow_readl(fd, R_CSR) & CSR_CYC_MSK)
		/* FIXME: timeout */;
	reg = ow_readl(fd, R_CSR);
	return ~reg & CSR_DAT_MSK;
}

static int slot(struct fd_dev *fd, int port, int bit)
{
	uint32_t reg, data;

	data = ((port<<CSR_SEL_OFS) & CSR_SEL_MSK)
		| CSR_CYC_MSK | (bit & CSR_DAT_MSK);
	ow_writel(fd, data, R_CSR);
	while(ow_readl(fd, R_CSR) & CSR_CYC_MSK)
		/* FIXME: timeout */;
	reg = ow_readl(fd, R_CSR);
	return reg & CSR_DAT_MSK;
}

static int read_bit(struct fd_dev *fd, int port)
{
	return slot(fd, port, 0x1);
}

static int write_bit(struct fd_dev *fd, int port, int bit)
{
	return slot(fd, port, bit);
}

static int ow_read_byte(struct fd_dev *fd, int port)
{
	int byte = 0, i;

	for(i = 0; i < 8; i++)
		byte |= (read_bit(fd, port) << i);
	return byte;
}

static int ow_write_byte(struct fd_dev *fd, int port, int byte)
{
	int data = 0;
	int i;

	for (i = 0; i < 8; i++){
		data |= write_bit(fd, port, (byte & 0x1)) << i;
		byte >>= 1;
	}
	return 0; /* success */
}

static int ow_write_block(struct fd_dev *fd, int port, uint8_t *block, int len)
{
	int i;

	for(i = 0; i < len; i++)
		ow_write_byte(fd, port, block[i]);
	return 0;
}

static int ow_read_block(struct fd_dev *fd, int port, uint8_t *block, int len)
{
	int i;
	for(i = 0; i < len; i++)
		block[i] = ow_read_byte(fd, port);
	return 0;
}

static int ds18x_read_serial(struct fd_dev *fd)
{
	if(!ow_reset(fd, 0)) {
		dev_err(&fd->fmc->dev, "Failure in resetting one-wire channel\n");
		return -EIO;
	}

	ow_write_byte(fd, FD_OW_PORT, CMD_ROM_READ);
	return ow_read_block(fd, FD_OW_PORT, fd->ds18_id, 8);
}

static int ds18x_access(struct fd_dev *fd)
{
	if(!ow_reset(fd, 0))
		goto out;

	if (0) {
		/* select the rom among several of them */
		if (ow_write_byte(fd, FD_OW_PORT, CMD_ROM_MATCH) < 0)
			goto out;
		return ow_write_block(fd, FD_OW_PORT, fd->ds18_id, 8);
	} else {
		/* we have one only, so skip rom */
		return ow_write_byte(fd, FD_OW_PORT, CMD_ROM_SKIP);
	}
out:
	dev_err(&fd->fmc->dev, "Failure in one-wire communication\n");
	return -EIO;
}

static void __temp_command_and_next_t(struct fd_dev *fd, int cfg_reg)
{
	int ms;

	ds18x_access(fd);
	ow_write_byte(fd, FD_OW_PORT, CMD_CONVERT_TEMP);
	/* The conversion takes some time, so mark when will it be ready */
	ms = 94 * ( 1 << (cfg_reg >> 5));
	fd->next_t = jiffies + msecs_to_jiffies(ms);
}

int fd_read_temp(struct fd_dev *fd, int verbose)
{
	int i, temp;
	unsigned long j;
	uint8_t data[9];
	struct device *dev = &fd->fmc->dev;

	/* If first conversion, ask for it first */
	if (fd->next_t == 0)
		__temp_command_and_next_t(fd, 0x7f /* we ignore: max time */);

	/* Wait for it to be ready: (FIXME: we need a time policy here) */
	j = jiffies;
	if (time_before(j, fd->next_t)) {
		/* If we cannot sleep, return the previous value */
		if (in_atomic())
			return fd->temp;
		msleep(jiffies_to_msecs(fd->next_t - j));
	}

	ds18x_access(fd);
	ow_write_byte(fd, FD_OW_PORT, CMD_READ_SCRATCHPAD);
	ow_read_block(fd, FD_OW_PORT, data, 9);

	if (verbose > 1) {
		dev_info(dev, "%s: Scratchpad: ", __func__);
		for (i = 0; i < 9; i++)
			printk("%02x%c", data[i], i == 8 ? '\n' : ':');
	}
	temp = ((int)data[1] << 8) | ((int)data[0]);
	if(temp & 0x1000)
		temp = -0x10000 + temp;

	fd->temp = temp;
	fd->temp_ready = 1;

	if (verbose) {
		dev_info(dev, "%s: Temperature 0x%x (%i bits: %i.%03i)\n", __func__,
			temp, 9 + (data[4] >> 5),
			temp / 16, (temp & 0xf) * 1000 / 16);
	}

	__temp_command_and_next_t(fd, data[4]);	/* start next conversion */
	return temp;
}

int fd_onewire_init(struct fd_dev *fd)
{
	int i;

	ow_writel(fd, ((CLK_DIV_NOR & CDR_NOR_MSK)
		       | (( CLK_DIV_OVD << CDR_OVD_OFS) & CDR_OVD_MSK)),
		  R_CDR);

	if(ds18x_read_serial(fd) < 0)
		return -EIO;

	if (fd->verbose) {
		dev_info(&fd->fmc->dev, "%s: Found DS18xx sensor: ", __func__);
		for (i = 0; i < 8; i++)
			printk("%02x%c", fd->ds18_id[i], i == 7 ? '\n' : ':');
	}
	/* read the temperature once, to ensure it works, and print it */
	fd_read_temp(fd, 2);

	return 0;
}

void fd_onewire_exit(struct fd_dev *fd)
{
	/* Nothing to do */
}

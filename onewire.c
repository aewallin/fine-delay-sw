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
#include <linux/io.h>
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

static void ow_writel(struct spec_fd *fd, uint32_t val, unsigned long reg)
{
	writel(val, fd->ow_regs + reg);
}

static uint32_t ow_readl(struct spec_fd *fd, unsigned long reg)
{
	return readl(fd->ow_regs + reg);
}

static int ow_reset(struct spec_fd *fd, int port)
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

static int slot(struct spec_fd *fd, int port, int bit)
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

static int read_bit(struct spec_fd *fd, int port)
{
	return slot(fd, port, 0x1);
}

static int write_bit(struct spec_fd *fd, int port, int bit)
{
	return slot(fd, port, bit);
}

static int ow_read_byte(struct spec_fd *fd, int port)
{
	int data = 0, i;

	for(i = 0; i < 8; i++)
		data |= (read_bit(fd, port) << i);
	return data;
}

static int ow_write_byte(struct spec_fd *fd, int port, int byte)
{
	int  data = 0;
	int   byte_old = byte, i;

	for (i = 0; i < 8; i++){
		data |= write_bit(fd, port, (byte & 0x1)) << i;
		byte >>= 1;
	}
	return byte_old == data ? 0 : -1;
}

static int ow_write_block(struct spec_fd *fd, int port, uint8_t *block, int len)
{
	int i;

	for(i = 0; i < len; i++)
		ow_write_byte(fd, port, block[i]);
	return 0;
}

static int ow_read_block(struct spec_fd *fd, int port, uint8_t *block, int len)
{
	int i;
	for(i = 0; i < len; i++)
		block[i] = ow_read_byte(fd, port);
	return 0;
}

#define ROM_SEARCH		0xF0
#define ROM_READ		0x33
#define ROM_MATCH		0x55
#define ROM_SKIP		0xCC
#define ROM_ALARM_SEARCH	0xEC

#define CONVERT_TEMP		0x44
#define WRITE_SCRATCHPAD	0x4E
#define READ_SCRATCHPAD		0xBE
#define COPY_SCRATCHPAD		0x48
#define RECALL_EEPROM		0xB8
#define READ_POWER_SUPPLY	0xB4

static uint8_t ds18x_id[8];

static int ds18x_read_serial(struct spec_fd *fd, uint8_t *id)
{
	int i;

	if(!ow_reset(fd, 0))
		return -EIO;

	ow_write_byte(fd, 0, ROM_READ);
	for(i = 0; i < 8; i++) {
		*id = ow_read_byte(fd, 0);
		id++;
	}
	return 0;
}

static int ds18x_access(struct spec_fd *fd, uint8_t *id)
{
	int i;
	if(!ow_reset(fd, 0))
		return -EIO;

	if(ow_write_byte(fd, 0, ROM_MATCH) < 0)
		return -EIO;
	for(i = 0; i < 8; i++)
		if(ow_write_byte(fd, 0, id[i]) < 0)
			return -EIO;
	return 0;
}

static int ds18x_read_temp(struct spec_fd *fd, int *temp_r)
{
	int i, temp;
	uint8_t data[9];

	if(ds18x_access(fd, ds18x_id) < 0)
		return -1;
	ow_write_byte(fd, 0, READ_SCRATCHPAD);

	for(i = 0; i < 9; i++)
		data[i] = ow_read_byte(fd, 0);

	temp = ((int)data[1] << 8) | ((int)data[0]);
	if(temp & 0x1000)
		temp = -0x10000 + temp;

	ds18x_access(fd, ds18x_id);
	ow_write_byte(fd, 0, CONVERT_TEMP);

	if(temp_r) *temp_r = temp;
	return 0;
}

int fd_read_temp(struct spec_fd *fd)
{
	int i;

	//ow_init(dev);
	if(ds18x_read_serial(fd, ds18x_id) < 0)
		return -EIO;

	pr_debug("%s: Found DS18xx sensor: ", __func__);
	for (i = 0; i < 8; i++)
		printk("%02x%c", ds18x_id[i], i == 7 ? '\n' : ':');
	return ds18x_read_temp(fd, NULL);
}

int fd_onewire_init(struct spec_fd *fd)
{
	int temp;

	ow_writel(fd, ((CLK_DIV_NOR & CDR_NOR_MSK)
		       | (( CLK_DIV_OVD << CDR_OVD_OFS) & CDR_OVD_MSK)),
		  R_CDR);

	temp = fd_read_temp(fd);
	printk("temp: %x\n", temp);

	return 0;
}

void fd_onewire_exit(struct spec_fd *fd)
{
	/* Nothing to do */
}

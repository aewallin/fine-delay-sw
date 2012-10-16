/*
 * I2C access (on-board EEPROM)
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

#include <linux/moduleparam.h>
#include <linux/io.h>
#include <linux/time.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/firmware.h>
#include <linux/jhash.h>
#include "spec.h"
#include "fine-delay.h"
#include "hw/fd_main_regs.h"

/* The eeprom is at address 0x50, and the structure lives at 6kB */
#define I2C_ADDR 0x50
#define I2C_OFFSET (6*1024)

/* At factory config time, it's possible to load a file and/or write eeprom */
static char *calibration_load;
static int calibration_save;
static int calibration_check;
static int calibration_default;

module_param(calibration_load, charp, 0444);
module_param(calibration_default, int, 0444);
module_param(calibration_save, int, 0444);
module_param(calibration_check, int, 0444);

/* Stupid dumping tool */
static void dumpstruct(char *name, void *ptr, int size)
{
	int i;
	unsigned char *p = ptr;

	printk("%s: (size 0x%x)\n", name, size);
	for (i = 0; i < size; ) {
		printk("%02x", p[i]);
		i++;
		printk(i & 3 ? " " : i & 0xf ? "  " : "\n");
	}
	if (i & 0xf)
		printk("\n");
}

static void set_sda(struct spec_fd *fd, int val)
{
	uint32_t reg;

	reg = fd_readl(fd, FD_REG_I2CR) & ~FD_I2CR_SDA_OUT;
	if (val)
		reg |= FD_I2CR_SDA_OUT;
	fd_writel(fd, reg, FD_REG_I2CR);
}

static void set_scl(struct spec_fd *fd, int val)
{
	uint32_t reg;

	reg = fd_readl(fd, FD_REG_I2CR) & ~FD_I2CR_SCL_OUT;
	if (val)
		reg |= FD_I2CR_SCL_OUT;
	fd_writel(fd, reg, FD_REG_I2CR);
}

static int get_sda(struct spec_fd *fd)
{
	return fd_readl(fd, FD_REG_I2CR) & FD_I2CR_SDA_IN ? 1 : 0;
};

static void mi2c_start(struct spec_fd *fd)
{
	set_sda(fd, 0);
	set_scl(fd, 0);
}

static void mi2c_stop(struct spec_fd *fd)
{
	set_sda(fd, 0);
	set_scl(fd, 1);
	set_sda(fd, 1);
}

int mi2c_put_byte(struct spec_fd *fd, int data)
{
	int i;
	int ack;

	for (i = 0; i < 8; i++, data<<=1) {
		set_sda(fd, data & 0x80);
		set_scl(fd, 1);
		set_scl(fd, 0);
	}

	set_sda(fd, 1);
	set_scl(fd, 1);

	ack = get_sda(fd);

	set_scl(fd, 0);
	set_sda(fd, 0);

	return ack ? -EIO : 0; /* ack low == success */
}

int mi2c_get_byte(struct spec_fd *fd, unsigned char *data, int sendack)
{
	int i;
	int indata = 0;

	/* assert: scl is low */
	set_scl(fd, 0);
	set_sda(fd, 1);
	for (i = 0; i < 8; i++) {
		set_scl(fd, 1);
		indata <<= 1;
		if (get_sda(fd))
			indata |= 0x01;
		set_scl(fd, 0);
	}

	set_sda(fd, (sendack ? 0 : 1));
	set_scl(fd, 1);
	set_scl(fd, 0);
	set_sda(fd, 0);

	*data= indata;
	return 0;
}

void mi2c_init(struct spec_fd *fd)
{
	set_scl(fd, 1);
	set_sda(fd, 1);
}

void mi2c_scan(struct spec_fd *fd)
{
	int i;
	for(i = 0; i < 256; i += 2) {
		mi2c_start(fd);
		if(!mi2c_put_byte(fd, i))
			pr_info("%s: Found i2c device at 0x%x\n",
			       KBUILD_MODNAME, i >> 1);
		mi2c_stop(fd);
	}
}

/* FIXME: this is very inefficient: read several bytes in a row instead */
int fd_eeprom_read(struct spec_fd *fd, int i2c_addr, uint32_t offset,
		void *buf, size_t size)
{
	int i;
	uint8_t *buf8 = buf;
	unsigned char c;

	for(i = 0; i < size; i++) {
		mi2c_start(fd);
		if(mi2c_put_byte(fd, i2c_addr << 1) < 0) {
			mi2c_stop(fd);
			return -EIO;
		}

		mi2c_put_byte(fd, (offset >> 8) & 0xff);
		mi2c_put_byte(fd, offset & 0xff);
		offset++;
		mi2c_stop(fd);
		mi2c_start(fd);
		mi2c_put_byte(fd, (i2c_addr << 1) | 1);
		mi2c_get_byte(fd, &c, 0);
		*buf8++ = c;
		mi2c_stop(fd);
	}
	return size;
}

int fd_eeprom_write(struct spec_fd *fd, int i2c_addr, uint32_t offset,
		 void *buf, size_t size)
{
	int i, busy;
	uint8_t *buf8 = buf;

	for(i = 0; i < size; i++) {
		mi2c_start(fd);

		if(mi2c_put_byte(fd, i2c_addr << 1) < 0) {
			mi2c_stop(fd);
			return -1;
		}
		mi2c_put_byte(fd, (offset >> 8) & 0xff);
		mi2c_put_byte(fd, offset & 0xff);
		mi2c_put_byte(fd, *buf8++);
		offset++;
		mi2c_stop(fd);

		do { /* wait until the chip becomes ready */
			mi2c_start(fd);
			busy = mi2c_put_byte(fd, i2c_addr << 1);
			mi2c_stop(fd);
		} while(busy);
	}
	return size;
}

/* The user requested to load the configuration from file */
static void fd_i2c_load_calib(struct spec_fd *fd,
			      struct fd_calib_on_eeprom *cal_ee)
{
	const struct firmware *fw;
	char *fwname, *newname = NULL;
	int err;

	/* the calibration_load string is known to be valid */

	fwname = calibration_load;
	err = request_firmware(&fw, calibration_load, fd->fmc->hwdev);
	if (err < 0) {
		dev_warn(fd->fmc->hwdev, "can't load \"%s\"\n",
			    calibration_load);
		return;
	}
	if (fw->size != sizeof(cal_ee->calib)) {
		dev_warn(fd->fmc->hwdev, "File \"%s\" has wrong size\n",
			    fwname);
	} else {
		memcpy(&cal_ee->calib, fw->data, fw->size);
		dev_info(fd->fmc->hwdev,
			 "calibration data loaded from \"%s\"\n", fwname);
	}
	release_firmware(fw);
	kfree(newname);
	return;
}


int fd_i2c_init(struct spec_fd *fd)
{
	struct fd_calib_on_eeprom *cal_ee;
	u32 hash;
	int i;

	mi2c_scan(fd);

	if (0) {
		/* Temporary - testing: read and write some stuff */
		u8 buf[8];
		int i;

		fd_eeprom_read(fd, I2C_ADDR, I2C_OFFSET, buf, 8);
		printk("read: ");
		for (i = 0; i < 8; i++)
			printk("%02x%c", buf[i], i==7 ? '\n' : ' ');

		get_random_bytes(buf, 8);
		printk("write: ");
		for (i = 0; i < 8; i++)
			printk("%02x%c", buf[i], i==7 ? '\n' : ' ');
		fd_eeprom_write(fd, I2C_ADDR, I2C_OFFSET, buf, 8);
	}

	/* Retrieve and validate the calibration */
	cal_ee = kzalloc(sizeof(*cal_ee), GFP_KERNEL);
	if (!cal_ee)
		return -ENOMEM;
	i = fd_eeprom_read(fd, I2C_ADDR, I2C_OFFSET, cal_ee, sizeof(*cal_ee));
	if (i != sizeof(*cal_ee)) {
		pr_err("%s: cannot read_eeprom\n", __func__);
		goto load;
	}
	if (calibration_check)
		dumpstruct("Calibration data from eeprom:", cal_ee,
			   sizeof(*cal_ee));

	hash = jhash(&cal_ee->calib, sizeof(cal_ee->calib), 0);

	/* FIXME: this is original-endian only (little endian I fear) */
	if ((cal_ee->size != sizeof(cal_ee->calib))
	    || (cal_ee->hash != hash)
	    || (cal_ee->version != 1)) {
		pr_err("%s: calibration on eeprom is invalid\n", __func__);
		goto load;
	}
	if (!calibration_default)
		fd->calib = cal_ee->calib; /* override compile-time default */

load:
	cal_ee->calib = fd->calib;

	if (calibration_load)
		fd_i2c_load_calib(fd, cal_ee);

	/* Fix the local copy, for verification and maybe saving */
	cal_ee->hash = jhash(&cal_ee->calib, sizeof(cal_ee->calib), 0);
	cal_ee->size = sizeof(cal_ee->calib);
	cal_ee->version = 1;

	if (calibration_save) {
		i = fd_eeprom_write(fd, I2C_ADDR, I2C_OFFSET, cal_ee,
				    sizeof(*cal_ee));
		if (i != sizeof(*cal_ee)) {
			pr_err("%s: error in writing calibration to eeprom\n",
			       __func__);
		} else {
			pr_info("%s: saved calibration to eeprom\n", __func__);
		}
	}
	if (calibration_check)
		dumpstruct("Current calibration data:", cal_ee,
			   sizeof(*cal_ee));
	kfree(cal_ee);
	return 0;
}

void fd_i2c_exit(struct spec_fd *fd)
{
	/* nothing to do */
}


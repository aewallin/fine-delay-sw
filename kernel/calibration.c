/*
 * Code related to on-eeprom calibration: retrieving, defaulting, updating.
 *
 * Copyright (C) 2013 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2 as published by the Free Software Foundation or, at your
 * option, any later version.
 */

#include <linux/moduleparam.h>
#include <linux/time.h>
#include <linux/firmware.h>
#include <linux/jhash.h>
#include "fine-delay.h"
#include "../sdb-lib/libsdbfs.h"

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

/* The user requested to load the configuration from file */
static void fd_i2c_load_calib(struct fd_dev *fd,
			      struct fd_calibration *calib, char *name)
{
	const struct firmware *fw;
	int err;

	err = request_firmware(&fw, name, fd->fmc->hwdev);
	if (err < 0) {
		dev_warn(fd->fmc->hwdev, "can't load \"%s\"\n", name);
		return;
	}
	if (fw->size != sizeof(*calib)) {
		dev_warn(fd->fmc->hwdev, "File \"%s\" has size != %zi\n",
			 name, sizeof(*calib));
	} else {
		memcpy(calib, fw->data, fw->size);
		dev_info(fd->fmc->hwdev,
			 "calibration data loaded from \"%s\"\n", name);
	}
	release_firmware(fw);
	return;
}

static struct fd_calibration fd_calib_default = {
	.magic = 0xf19ede1a,
	.version = 3,
	.date = 0x20130427,
	.frr_poly = { -165202LL, -29825595LL, 3801939743082LL },
	.zero_offset = { -38186, -38155, -38147, -38362 },
	.tdc_zero_offset = 127500,
	.vcxo_default_tune = 41711,
};

/* sdbfs-related function */
static int fd_read_calibration_eeprom(struct fmc_device *fmc,
				      struct fd_calibration *calib)
{
	int i, ret;
	static struct sdbfs fs;

	fs.data = fmc->eeprom;
	fs.datalen = fmc->eeprom_len;

	/* Look for sdb entry point at powers of 2 and onwards */
	for (i = 0x40; i < 0x1000; i *= 2) {
		fs.entrypoint = i;
		ret = sdbfs_dev_create(&fs, 0);
		if (ret == 0)
			break;
	}
	if (ret)
		return ret;

	/* Open "cali" as a device id, vendor is "FileData" -- big endian */
	ret = sdbfs_open_id(&fs, 0x61746144656c6946LL, 0x696c6163);
	if (ret)
		return ret;
	ret = sdbfs_fread(&fs, 0, (void *)calib, sizeof(*calib));
	sdbfs_dev_destroy(&fs);
	return ret;
}

/* This is the only thing called by outside */
int fd_handle_eeprom_calibration(struct fd_dev *fd)
{
	struct fd_calibration *calib;
	struct device *d = fd->fmc->hwdev;
	int i;
	u32 hash, horig;

	/* Retrieve and validate the calibration */
	calib = &fd->calib;

	i = fd_read_calibration_eeprom(fd->fmc, calib);
	if (i != sizeof(*calib))
		dev_warn(d, "Calibration NOT read from eeprom (got %i)\n", i);

	if (calibration_check)
		dumpstruct("Calibration data from eeprom:", calib,
			   sizeof(*calib));

	/* Verify hash (used later) */
	horig = be32_to_cpu(calib->hash);
	calib->hash = 0;
	hash = jhash(calib, sizeof(*calib), 0);

	/* FIXME: validate with gateware version */

	if (calibration_load) {
		fd_i2c_load_calib(fd, calib, calibration_load);
		hash = horig; /* whatever it is */
	}

	/* convert endianneess */
	calib->magic = be32_to_cpu(calib->magic);
	calib->size = be16_to_cpu(calib->size);
	calib->version = be16_to_cpu(calib->version);
	calib->date = be32_to_cpu(calib->date);
	for (i = 0; i < ARRAY_SIZE(calib->frr_poly); i++)
		calib->frr_poly[i] = be64_to_cpu(calib->frr_poly[i]);
	for (i = 0; i < ARRAY_SIZE(calib->zero_offset); i++)
		calib->zero_offset[i] = be32_to_cpu(calib->zero_offset[i]);
	calib->tdc_zero_offset = be32_to_cpu(calib->tdc_zero_offset);
	calib->vcxo_default_tune = be32_to_cpu(calib->vcxo_default_tune);

	if (calibration_default) {
		dev_info(d, "Overriding with default calibration\n");
		*calib = fd_calib_default;
		hash = horig; /* whatever it is */
	}

	dev_info(d, "calibration: version %i, date %08x\n", calib->version,
		 calib->date);
	if (calibration_check) {
		/* dump human-readable values */
		dev_info(d, "calib: magic 0x%08x\n", calib->magic);
		for (i = 0; i < ARRAY_SIZE(calib->frr_poly); i++)
			dev_info(d, "calib: poly[%i] = %lli\n", i,
				 (long long)calib->frr_poly[i]);
		for (i = 0; i < ARRAY_SIZE(calib->zero_offset); i++)
			dev_info(d, "calib: offset[%i] = %li\n", i,
				 (long)calib->zero_offset[i]);
		dev_info(d, "calib: tdc_offset %i\n", calib->tdc_zero_offset);
		dev_info(d, "calib: vcxo %i\n", calib->vcxo_default_tune);
	}

	if (hash != horig) {
		dev_err(d, "Calibration hash %08x is wrong (expected %08x)\n",
			hash, horig);
		return -EINVAL;
	}
	if (calib->version < 3) {
		dev_err(d, "Calibration version %i < 3: refusing to work\n",
			fd->calib.version);
		return -EINVAL;
	}

	if (calibration_save) {
		/* FIXME:  save to eeprom: re-convert endianness and hash */
		dev_warn(d, "Saving is not supported\n");
	}

	return 0;
}

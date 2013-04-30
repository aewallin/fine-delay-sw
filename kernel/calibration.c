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
		dev_warn(fd->fmc->hwdev, "File \"%s\" has size != %i\n",
			 name, sizeof(*calib));
	} else {
		memcpy(&calib, fw->data, fw->size);
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
	/* .. FIXME .. */
};


int fd_handle_eeprom_calibration(struct fd_dev *fd)
{
	struct fd_calibration *calib;
	struct device *d = fd->fmc->hwdev;
	//u32 hash;

	/* Retrieve and validate the calibration */
	calib = &fd->calib;

	/* FIXME: run SDB on the eeprom */
	dev_warn(d, "Calibration NOT yet read from eeprom\n");

	if (calibration_check)
		dumpstruct("Calibration data from eeprom:", calib,
			   sizeof(*calib));

	/* FIXME: verify hash */

	/* FIXME: validate with gateware version */

	if (calibration_load)
		fd_i2c_load_calib(fd, calib, calibration_load);

	/* FIXME: convert endianneess */

	if (calibration_default) {
		dev_info(d, "Overriding with default calibration\n");
		*calib = fd_calib_default;
	}

	if (calibration_check) {
		/* FIXME: dump human-readable values */
	}

	/* FIXME: recreate hash */

	if (calibration_save) {
		/* FIXME:  save to eeprom */
		dev_warn(d, "Saving is not supported\n");
	}

	return 0;
}

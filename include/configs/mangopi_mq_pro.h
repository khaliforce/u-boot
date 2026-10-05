/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2026 khaliforce <kforce@gmail.com> */

#ifndef __CONFIG_MANGOPI_MQ_PRO_H
#define __CONFIG_MANGOPI_MQ_PRO_H

#define BOOT_TARGET_DEVICES(func) \
	func(MMC, mmc, 0) \
	func(USB, usb, 0)

#include <config_distro_bootcmd.h>

#define CFG_EXTRA_ENV_SETTINGS \
	"stdin=serial,usbkbd\0" \
	"stdout=serial\0" \
	"stderr=serial\0" \
	"console=ttyS0,115200\0" \
	"fdtfile=" CONFIG_DEFAULT_DEVICE_TREE ".dtb\0" \
	"bootm_size=0x10000000\0" \
	"kernel_addr_r=0x40200000\0" \
	"fdt_addr_r=0x46000000\0" \
	"scriptaddr=0x47000000\0" \
	"pxefile_addr_r=0x47000000\0" \
	"fdtoverlay_addr_r=0x41b00000\0" \
	"ramdisk_addr_r=0x48000000\0" \
	BOOTENV

#endif

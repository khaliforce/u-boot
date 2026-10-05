// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2012-2013 Henrik Nordstrom <henrik@henriknordstrom.net>
 * Copyright (C) 2013 Luke Kenneth Casson Leighton <lkcl@lkcl.net>
 * Copyright (C) 2007-2011 Allwinner Technology Co., Ltd.
 * Copyright (C) 2018 Bin Meng <bmeng.cn@gmail.com>
 * Copyright (C) 2026 khaliforce <kforce@gmail.com>
 */

#include <cpu.h>
#include <dm.h>
#include <fdt_support.h>
#include <fdtdec.h>
#include <init.h>
#include <ram.h>
#include <spl.h>
#include <sunxi_image.h>
#include <linux/string.h>
#include <asm/csr.h>
#include <asm/global_data.h>
#include <asm/io.h>

#define D1_SPL_HEADER ((struct boot_file_head *)CONFIG_SUNXI_SRAM_ADDRESS)
#define D1_TOC_HEADER ((struct toc0_main_info *)CONFIG_SUNXI_SRAM_ADDRESS)
#define D1_BOOT_MMC0 0
#define D1_BOOT_MMC2 2
#define D1_BOOT_SPI 3
#define D1_BOOT_MMC0_HIGH 0x10
#define D1_BOOT_MMC2_HIGH 0x12

DECLARE_GLOBAL_DATA_PTR;

static int d1_boot_source(void)
{
	if (!memcmp(D1_SPL_HEADER->magic, BOOT0_MAGIC, 8))
		return readb(&D1_SPL_HEADER->boot_media);
	if (!memcmp(D1_TOC_HEADER->name, TOC0_MAIN_INFO_NAME, 8))
		return readb(&D1_TOC_HEADER->platform[0]);

	return -1;
}

u32 spl_boot_device(void)
{
	switch (d1_boot_source()) {
	case D1_BOOT_MMC0:
	case D1_BOOT_MMC0_HIGH:
		return BOOT_DEVICE_MMC1;
	case D1_BOOT_MMC2:
	case D1_BOOT_MMC2_HIGH:
		return BOOT_DEVICE_MMC2;
	case D1_BOOT_SPI:
		return BOOT_DEVICE_SPI;
	default:
		return BOOT_DEVICE_BOARD;
	}
}

static unsigned long d1_spl_size(void)
{
	if (!memcmp(D1_SPL_HEADER->magic, BOOT0_MAGIC, 8))
		return readl(&D1_SPL_HEADER->length);
	if (!memcmp(D1_TOC_HEADER->name, TOC0_MAIN_INFO_NAME, 8))
		return readl(&D1_TOC_HEADER->length);

	return 0;
}

unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc,
					unsigned long raw_sector)
{
	unsigned long size = d1_spl_size();
	unsigned long sector = max(raw_sector, size / 512);
	int source = d1_boot_source();

	if (source == D1_BOOT_MMC0_HIGH || source == D1_BOOT_MMC2_HIGH)
		sector += (128 - 8) * 2;

	printf("SPL size = %lu, sector = %lu\n", size, sector);

	return sector;
}

#if defined(CONFIG_XPL_BUILD)
int spl_board_init_f(void)
{
	struct udevice *dev;
	int ret;

	ret = cpu_probe_all();
	if (ret)
		debug("CPU init failed: %d\n", ret);

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret)
		return ret;

	/* Preserve the tested cache and prefetch state. */
	csr_set(0x7c0, 0x638000);
	csr_write(0x7c2, 0x70013);
	csr_write(0x7c1, 0x11ff);
	csr_write(0x7c5, 0x16e30c);

	return 0;
}

void spl_perform_board_fixups(struct spl_image_info *image)
{
	struct ram_info info;
	struct udevice *dev;
	int ret;

	ret = uclass_get_device(UCLASS_RAM, 0, &dev);
	if (ret)
		panic("No RAM device\n");
	ret = ram_get_info(dev, &info);
	if (ret)
		panic("No RAM info\n");
	ret = fdt_fixup_memory(image->fdt_addr, info.base, info.size);
	if (ret)
		panic("Failed to update memory DT\n");
}
#else
int board_init(void)
{
	return cpu_probe_all();
}

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

int dram_init_banksize(void)
{
	return fdtdec_setup_memory_banksize();
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	return fdt_fixup_memory(blob, gd->ram_base, gd->ram_size);
}
#endif

int board_fit_config_name_match(const char *name)
{
	struct boot_file_head *header = D1_SPL_HEADER;
	const char *wanted = CONFIG_DEFAULT_DEVICE_TREE;

	if (!memcmp(header->spl_signature, SPL_SIGNATURE, 3) &&
	    header->spl_signature[3] >= SPL_DT_HEADER_VERSION &&
	    header->dt_name_offset)
		wanted = (char *)header + header->dt_name_offset;

	if (strcmp(name, wanted))
		return -1;

	if (!memcmp(header->spl_signature, SPL_SIGNATURE, 3) &&
	    header->spl_signature[3] >= SPL_ENV_HEADER_VERSION) {
		if (strlen(name) >= sizeof(header->string_pool))
			return -EINVAL;
		if (header->spl_signature[3] < SPL_DT_HEADER_VERSION)
			header->spl_signature[3] = SPL_DT_HEADER_VERSION;
		strcpy((char *)&header->string_pool, name);
		header->dt_name_offset = offsetof(struct boot_file_head, string_pool);
	}

	return 0;
}

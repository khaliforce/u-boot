/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2026 khaliforce <kforce@gmail.com> */

#ifndef _CLK_SUNXI_D1_H
#define _CLK_SUNXI_D1_H

#include <asm/io.h>
#include <linux/bitops.h>

extern const struct ccu_desc d1_ccu_desc;

static inline void sunxi_d1_clock_init(void __iomem *base)
{
	clrsetbits_le32(base, GENMASK(15, 8),
		       (CONFIG_SYS_CLK_FREQ / 24000000 - 1) << 8);
}

static inline unsigned int sunxi_d1_get_pll6(void)
{
	u32 val = readl((void __iomem *)0x02001020);
	u32 n = ((val >> 8) & 0xff) + 1;
	u32 m = ((val >> 1) & 1) + 1;
	u32 p = ((val >> 16) & 7) + 1;

	return 24000000U * n / m / p;
}

#endif

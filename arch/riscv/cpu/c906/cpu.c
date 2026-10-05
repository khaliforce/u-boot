// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018, Bin Meng <bmeng.cn@gmail.com>
 * Copyright (C) 2026 khaliforce <kforce@gmail.com>
 */

#include <asm/csr.h>
#include <linux/bitops.h>

#include "c906_csr.h"

void harts_early_init(void)
{
	if (CONFIG_IS_ENABLED(RISCV_MMODE))
		csr_set(CSR_MXSTATUS, MXSTATUS_THEADISAEE | MXSTATUS_MM);
}

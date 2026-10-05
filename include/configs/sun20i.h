/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2026 khaliforce <kforce@gmail.com> */

#ifndef __CONFIG_SUN20I_H
#define __CONFIG_SUN20I_H

/* The CCU driver does not provide the UART clock rate. */
#define CFG_SYS_NS16550_CLK	24000000

#define CFG_SYS_SDRAM_BASE	0x40000000

#ifdef CONFIG_MANGOPI_MQ_PRO
#include <configs/mangopi_mq_pro.h>
#endif

#endif

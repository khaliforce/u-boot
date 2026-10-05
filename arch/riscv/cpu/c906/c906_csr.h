/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2026 khaliforce <kforce@gmail.com> */

#ifndef _C906_CSR_H_
#define _C906_CSR_H_

#define CSR_MXSTATUS		0x7c0
#define CSR_MHCR		0x7c1

#define MXSTATUS_THEADISAEE	BIT(22)
#define MXSTATUS_MM		BIT(15)
#define MHCR_IE			BIT(0)
#define MHCR_DE			BIT(1)

#endif

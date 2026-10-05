# SPDX-License-Identifier: GPL-2.0+

obj-$(CONFIG_DRAM_SUN20I_D1) += dram_sun20i_d1.o

ifdef CONFIG_XPL_BUILD
ifeq ($(CONFIG_SPL_DRAM_SUNXI_DEBUG),y)
CFLAGS_dram_sun20i_d1.o += -DDEBUG
endif
endif

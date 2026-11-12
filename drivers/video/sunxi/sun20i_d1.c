// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner D1 HDMI boot display.
 * PHY sequence derived from Linux sun8i_hdmi_phy.c.
 * Copyright (c) 2018 Jernej Skrabec <jernej.skrabec@siol.net>
 */

#include <dm.h>
#include <cpu_func.h>
#include <dw_hdmi.h>
#include <fdtdec.h>
#include <video.h>
#include <mapmem.h>
#include <asm/io.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <media_bus_format.h>
#include <linux/sizes.h>
#include "sun20i_d1_hdmi.h"

#define D1_WIDTH	1920
#define D1_HEIGHT	1080
#define D1_CCU_PLL_VIDEO0	0x040
#define D1_CCU_DE_CLK	0x600
#define D1_CCU_DE_BGR	0x60c
#define D1_CCU_DPSS_BGR	0xabc
#define D1_CCU_HDMI_CLK	0xb04
#define D1_CCU_HDMI_BGR	0xb1c
#define D1_CCU_TCON_CLK	0xb80
#define D1_CCU_TCON_BGR	0xb9c
#define D1_BUS_GATE	BIT(0)
#define D1_BUS_RESET	BIT(16)
#define D1_TCON_TV0_PORT	2

struct d1_video_priv {
	struct dw_hdmi hdmi;
	void __iomem *ccu;
	void __iomem *de_clk;
	void __iomem *mixer;
	void __iomem *top;
	void __iomem *tcon;
	void __iomem *phy;
};

static void phy_clear(void __iomem *regs, u32 reg, u32 bits)
{
	clrbits_le32(regs + reg, bits);
}

static void phy_set(void __iomem *regs, u32 reg, u32 bits)
{
	setbits_le32(regs + reg, bits);
}

static void phy_update(void __iomem *regs, u32 reg, u32 mask, u32 value)
{
	clrsetbits_le32(regs + reg, mask, value);
}

static void d1_phy_power_off(struct d1_video_priv *phy)
{
	void __iomem *regs = phy->phy;

	phy_clear(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENTX);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRESCK);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRES);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENP2S);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENCK);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENBI);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENCALOG);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRCAL);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENLDO_FS);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENLDO);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENIB);
	phy_set(regs, SUN20I_HDMI_PLL_CTL1_REG, SUN20I_HDMI_PLL_CTL1_RESET);
	phy_clear(regs, SUN20I_HDMI_PLL_CTL1_REG, SUN20I_HDMI_PLL_CTL1_PWRON);
	phy_clear(regs, SUN20I_HDMI_PLL_CTL0_REG, SUN20I_HDMI_PLL_CTL0_ENVBS);
}

static int d1_phy_wait(struct d1_video_priv *phy, u32 bit, const char *what)
{
	u32 value;
	int ret;

	ret = readl_poll_timeout(phy->phy + SUN20I_HDMI_PLL_STS_REG,
			 value, value & bit, 100000);
	if (ret) {
		printf("D1 HDMI: PHY %s timed out\n", what);
		printf("D1 HDMI: PHY ctl0=%08x ctl5=%08x pll0=%08x pll1=%08x status=%08x\n",
		       readl(phy->phy + SUN20I_HDMI_PHY_CTL0_REG),
		       readl(phy->phy + SUN20I_HDMI_PHY_CTL5_REG),
		       readl(phy->phy + SUN20I_HDMI_PLL_CTL0_REG),
		       readl(phy->phy + SUN20I_HDMI_PLL_CTL1_REG), value);
		printf("D1 HDMI: video-pll=%08x tcon-clock=%08x top=%08x tcon=%08x core-clock=%02x\n",
		       readl(phy->ccu + D1_CCU_PLL_VIDEO0),
		       readl(phy->ccu + D1_CCU_TCON_CLK),
		       readl(phy->top + 0x20), readl(phy->tcon),
		       readb((void *)(phy->hdmi.ioaddr + HDMI_MC_CLKDIS)));
	}

	return ret;
}

static int d1_phy_config(struct dw_hdmi *hdmi, uint pixelclock)
{
	struct d1_video_priv *phy = container_of(hdmi, struct d1_video_priv, hdmi);
	void __iomem *regs = phy->phy;
	int ret;

	if (pixelclock != 148500000)
		return -EINVAL;

	phy_update(regs, SUN20I_HDMI_PHY_CTL5_REG,
		   SUN20I_HDMI_PHY_CTL5_REG_P1OPT,
		   SUN20I_HDMI_PHY_CTL5_REG_P1OPT);
	d1_phy_power_off(phy);

	/* The PLL is in reset and powered off, so the field order does not matter. */
	phy_update(regs, SUN20I_HDMI_PLL_CTL0_REG,
		   SUN20I_HDMI_PLL_CTL0_CKO_SEL |
		   SUN20I_HDMI_PLL_CTL0_BYPASS_PPLL |
		   SUN20I_HDMI_PLL_CTL0_SLV |
		   SUN20I_HDMI_PLL_CTL0_PROP_CNTRL |
		   SUN20I_HDMI_PLL_CTL0_GMP_CNTRL |
		   SUN20I_HDMI_PLL_CTL0_VCORANGE |
		   SUN20I_HDMI_PLL_CTL0_DIV_PRE |
		   SUN20I_HDMI_PLL_CTL0_BYPASS_CLRDPTH |
		   SUN20I_HDMI_PLL_CTL0_CLR_DPTH |
		   SUN20I_HDMI_PLL_CTL0_N_CNTRL |
		   SUN20I_HDMI_PLL_CTL0_DIV2_CKBIT |
		   SUN20I_HDMI_PLL_CTL0_DIV2_CKTMDS |
		   SUN20I_HDMI_PLL_CTL0_BCR,
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL0_CKO_SEL, 3) |
		   SUN20I_HDMI_PLL_CTL0_BYPASS_PPLL |
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL0_SLV, 4) |
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL0_PROP_CNTRL, 7) |
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL0_GMP_CNTRL, 3) |
		   SUN20I_HDMI_PLL_CTL0_VCORANGE |
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL0_N_CNTRL, 1));
	phy_update(regs, SUN20I_HDMI_PLL_CTL1_REG,
		   SUN20I_HDMI_PLL_CTL1_DRV_ANA |
		   SUN20I_HDMI_PLL_CTL1_CTRL_MODLE_CLKSRC |
		   SUN20I_HDMI_PLL_CTL1_SDM_EN |
		   SUN20I_HDMI_PLL_CTL1_SCKREF |
		   SUN20I_HDMI_PLL_CTL1_REF_CNTRL |
		   SUN20I_HDMI_PLL_CTL1_PCNT_EN |
		   SUN20I_HDMI_PLL_CTL1_PCNT_N |
		   SUN20I_HDMI_PLL_CTL1_PIXEL_REP,
		   SUN20I_HDMI_PLL_CTL1_DRV_ANA |
		   FIELD_PREP(SUN20I_HDMI_PLL_CTL1_PCNT_N, 1));
	phy_set(regs, SUN20I_HDMI_PLL_CTL1_REG, SUN20I_HDMI_PLL_CTL1_PWRON);
	phy_clear(regs, SUN20I_HDMI_PLL_CTL1_REG, SUN20I_HDMI_PLL_CTL1_RESET);
	udelay(10);

	phy_update(regs, SUN20I_HDMI_PHY_CTL1_REG, 0x003f0000, 0);
	phy_update(regs, SUN20I_HDMI_PHY_CTL2_REG, 0x00ffffff, 0);
	phy_update(regs, SUN20I_HDMI_PHY_CTL3_REG, 0x0000ffff, 0xffff);
	phy_update(regs, SUN20I_HDMI_PHY_CTL4_REG, 0x1fffffff, 0x0c0d0d0d);

	phy_update(regs, SUN20I_HDMI_PHY_CTL6_REG,
		   SUN20I_HDMI_PHY_CTL6_SWITCH_CLKCH_DATA |
		   SUN20I_HDMI_PHY_CTL6_CLK_GREATE0_340M |
		   SUN20I_HDMI_PHY_CTL6_CLK_GREATE1_340M |
		   SUN20I_HDMI_PHY_CTL6_CLK_GREATE2_340M,
		   SUN20I_HDMI_PHY_CTL6_CLK_GREATE0_340M |
		   SUN20I_HDMI_PHY_CTL6_CLK_GREATE1_340M);
	phy_update(regs, SUN20I_HDMI_PHY_CTL7_REG,
		   SUN20I_HDMI_PHY_CTL7_CLK_GREATE3_340M |
		   SUN20I_HDMI_PHY_CTL7_CLK_LOW_340M,
		   FIELD_PREP(SUN20I_HDMI_PHY_CTL7_CLK_LOW_340M, 0x3e0));
	phy_set(regs, SUN20I_HDMI_PHY_CTL6_REG, SUN20I_HDMI_PHY_CTL6_EN_CKDAT);
	phy_clear(regs, SUN20I_HDMI_PHY_CTL1_REG, SUN20I_HDMI_PHY_CTL1_RES_SCKTMDS);
	phy_update(regs, SUN20I_HDMI_PHY_CTL0_REG,
		   SUN20I_HDMI_PHY_CTL0_REG_CSMPS |
		   SUN20I_HDMI_PHY_CTL0_REG_CK_TEST_SEL |
		   SUN20I_HDMI_PHY_CTL0_REG_CK_SEL,
		   FIELD_PREP(SUN20I_HDMI_PHY_CTL0_REG_CSMPS, 2) |
		   SUN20I_HDMI_PHY_CTL0_REG_CK_SEL);
	phy_clear(regs, SUN20I_HDMI_INDEB_CTRL_REG,
		  SUN20I_HDMI_INDEB_CTRL_TXDATA_DEBUGMODE);

	/* Power-up sequence: keep one step per write. */
	phy_update(regs, SUN20I_HDMI_PHY_CTL4_REG,
		   SUN20I_HDMI_PHY_CTL4_REG_SLV,
		   FIELD_PREP(SUN20I_HDMI_PHY_CTL4_REG_SLV, 4));
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENIB);
	udelay(10);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENLDO);
	udelay(10);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENLDO_FS);
	udelay(10);
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRCAL);
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENCALOG);
	ret = d1_phy_wait(phy, SUN20I_HDMI_PLL_STS_PHY_RCALEND2D_STS,
			  "calibration");
	if (ret)
		goto err_power_off;

	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENBI);
	ret = d1_phy_wait(phy, SUN20I_HDMI_PLL_STS_PLL_LOCK_STATUS,
			  "PLL lock");
	if (ret)
		goto err_power_off;

	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENCK);
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENP2S);
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRES);
	phy_set(regs, SUN20I_HDMI_PHY_CTL5_REG, SUN20I_HDMI_PHY_CTL5_ENRESCK);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_ENTX);
	ret = d1_phy_wait(phy, SUN20I_HDMI_PLL_STS_TX_READY_DLY_STATUS,
			  "transmitter");
	if (ret)
		goto err_power_off;

	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_SDA_EN);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_SCL_EN);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_HPD_EN);
	phy_set(regs, SUN20I_HDMI_PHY_CTL0_REG, SUN20I_HDMI_PHY_CTL0_REG_DEN);
	phy_set(regs, SUN20I_HDMI_PLL_CTL0_REG, SUN20I_HDMI_PLL_CTL0_ENVBS);

	return 0;

err_power_off:
	d1_phy_power_off(phy);
	return ret;
}

static const struct dw_hdmi_phy_ops d1_phy_ops = {
	.phy_set = d1_phy_config,
};

static int d1_clocks_enable(struct d1_video_priv *priv)
{
	void __iomem *ccu = priv->ccu;
	u32 val;
	int ret;

	/* PLL_VIDEO0_4X = 24 MHz * 99 / 2 = 1188 MHz. */
	writel(BIT(31) | BIT(30) | (98 << 8) | BIT(1),
	       ccu + D1_CCU_PLL_VIDEO0);
	udelay(5);
	setbits_le32(ccu + D1_CCU_PLL_VIDEO0, BIT(29));
	ret = readl_poll_timeout(ccu + D1_CCU_PLL_VIDEO0,
				 val, val & BIT(28), 10000);
	if (ret) {
		clrbits_le32(ccu + D1_CCU_PLL_VIDEO0,
			     BIT(31) | BIT(30) | BIT(29) | BIT(27));
		return ret;
	}
	setbits_le32(ccu + D1_CCU_PLL_VIDEO0, BIT(27));

	/* DE = 1188 / 4 MHz; TCON TV = PLL_VIDEO0 / 2 = 148.5 MHz. */
	writel(BIT(31) | BIT(24) | 3, ccu + D1_CCU_DE_CLK);
	writel(BIT(31) | 1, ccu + D1_CCU_TCON_CLK);
	setbits_le32(ccu + D1_CCU_DE_BGR, D1_BUS_GATE | D1_BUS_RESET);
	setbits_le32(ccu + D1_CCU_DPSS_BGR, D1_BUS_GATE | D1_BUS_RESET);
	setbits_le32(ccu + D1_CCU_TCON_BGR, D1_BUS_GATE | D1_BUS_RESET);
	setbits_le32(ccu + D1_CCU_HDMI_CLK, BIT(31));
	setbits_le32(ccu + D1_CCU_HDMI_BGR,
		     D1_BUS_GATE | D1_BUS_RESET | BIT(17));

	clrbits_le32(priv->de_clk + 0x0c, GENMASK(3, 0));
	setbits_le32(priv->de_clk + 0x08, BIT(0));
	setbits_le32(priv->de_clk + 0x04, BIT(0));
	setbits_le32(priv->de_clk + 0x00, BIT(0));
	clrbits_le32(priv->de_clk + 0x10, BIT(0));

	/* TCON TV0 is port 2. Port 1 belongs to the second LCD controller. */
	writel(D1_TCON_TV0_PORT, priv->top + 0x1c);
	writel(((D1_TCON_TV0_PORT - 1) << 28) | BIT(20), priv->top + 0x20);
	setbits_le32(priv->phy + 0x04, BIT(31));
	clrsetbits_le32(priv->phy + 0x04, 0xffff0000, 0x80c00000);
	udelay(1000);

	return 0;
}

static void d1_clocks_disable(struct d1_video_priv *priv)
{
	writel(0, priv->tcon);
	writel(0, priv->mixer);
	d1_phy_power_off(priv);
	clrbits_le32(priv->de_clk + 0x00, BIT(0));
	clrbits_le32(priv->de_clk + 0x04, BIT(0));
	clrbits_le32(priv->de_clk + 0x08, BIT(0));
	clrbits_le32(priv->ccu + D1_CCU_HDMI_BGR,
		     D1_BUS_GATE | D1_BUS_RESET | BIT(17));
	clrbits_le32(priv->ccu + D1_CCU_TCON_BGR,
		     D1_BUS_GATE | D1_BUS_RESET);
	clrbits_le32(priv->ccu + D1_CCU_DPSS_BGR,
		     D1_BUS_GATE | D1_BUS_RESET);
	clrbits_le32(priv->ccu + D1_CCU_DE_BGR,
		     D1_BUS_GATE | D1_BUS_RESET);
	clrbits_le32(priv->ccu + D1_CCU_HDMI_CLK, BIT(31));
	clrbits_le32(priv->ccu + D1_CCU_TCON_CLK, BIT(31));
	clrbits_le32(priv->ccu + D1_CCU_DE_CLK, BIT(31));
	clrbits_le32(priv->ccu + D1_CCU_PLL_VIDEO0,
		     BIT(31) | BIT(30) | BIT(29) | BIT(27));
}

static void d1_mixer_enable(struct d1_video_priv *priv, ulong address)
{
	void __iomem *mixer = priv->mixer;
	const u32 size = ((D1_HEIGHT - 1) << 16) | (D1_WIDTH - 1);
	static const u32 bypass[] = {
		0x20000, 0x30000, 0xa0000, 0xa2000, 0xa4000,
		0xa6000, 0xa8000, 0xaa000, 0xb0000,
	};
	int i;

	for (i = 0; i < 0x6000; i += 4)
		writel(0, mixer + i);
	for (i = 0; i < ARRAY_SIZE(bypass); i++)
		writel(0, mixer + bypass[i]);

	writel(size, mixer + 0x000c);
	writel(0xff000000, mixer + 0x1004);
	writel(size, mixer + 0x1008);
	writel(1, mixer + 0x1080);
	writel(0xff000000, mixer + 0x1088);
	writel(size, mixer + 0x108c);
	writel(0x03010301, mixer + 0x1090);
	writel(0x101, mixer + 0x1000);

	/* UI channel 0 follows VI channel 0 at offset 0x3000. */
	writel(BIT(0) | (4 << 8) | (0xffU << 24), mixer + 0x3000);
	writel(size, mixer + 0x3004);
	writel(D1_WIDTH * 4, mixer + 0x300c);
	writel(address, mixer + 0x3010);
	writel(size, mixer + 0x3088);
	writel(BIT(0), mixer);
	writel(BIT(0), mixer + 0x0008);
}

static void d1_tcon_enable(struct d1_video_priv *priv)
{
	void __iomem *tcon = priv->tcon;
	u32 size = ((D1_WIDTH - 1) << 16) | (D1_HEIGHT - 1);

	writel(0, tcon);
	writel(0, tcon + 0x04);
	writel(0, tcon + 0x90);
	writel(size, tcon + 0x94);
	writel(size, tcon + 0x98);
	writel(size, tcon + 0x9c);
	writel(((2200 - 1) << 16) | (192 - 1), tcon + 0xa0);
	writel((2250 << 16) | (41 - 1), tcon + 0xa4);
	writel(((44 - 1) << 16) | (5 - 1), tcon + 0xa8);
	/* D1 stores the TV signal polarity in the channel 0 register. */
	writel(BIT(25) | BIT(24), tcon + 0x88);
	writel(0, tcon + 0xf0);
	writel(0, tcon + 0xf4);
	writel(BIT(31) | (30 << 4), tcon + 0x90);
	writel(BIT(31) | BIT(0), tcon);
}

static int d1_video_probe(struct udevice *dev)
{
	struct d1_video_priv *priv = dev_get_priv(dev);
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *video = dev_get_uclass_priv(dev);
	struct display_timing timing = {
		.pixelclock.typ = 148500000,
		.hactive.typ = D1_WIDTH,
		.hfront_porch.typ = 88,
		.hback_porch.typ = 148,
		.hsync_len.typ = 44,
		.vactive.typ = D1_HEIGHT,
		.vfront_porch.typ = 4,
		.vback_porch.typ = 36,
		.vsync_len.typ = 5,
		.flags = DISPLAY_FLAGS_HSYNC_HIGH | DISPLAY_FLAGS_VSYNC_HIGH,
		.hdmi_monitor = true,
	};
	int ret;

	if (plat->base > U32_MAX || !plat->base)
		return -ERANGE;

	ret = d1_clocks_enable(priv);
	if (ret) {
		printf("D1 HDMI: video PLL timed out\n");
		return ret;
	}

	setbits_le32(priv->phy + SUN20I_HDMI_PHY_CTL0_REG,
		     SUN20I_HDMI_PHY_CTL0_HPD_EN);
	dw_hdmi_init(&priv->hdmi);
	ret = dw_hdmi_detect_hpd(&priv->hdmi);
	if (ret)
		goto disable;

	memset(map_sysmem(plat->base, plat->size), 0, plat->size);
	flush_dcache_range(plat->base, plat->base + plat->size);
	d1_mixer_enable(priv, plat->base);
	d1_tcon_enable(priv);
	clrbits_8((void *)(priv->hdmi.ioaddr + HDMI_MC_CLKDIS),
		  HDMI_MC_CLKDIS_PIXELCLK_DISABLE | HDMI_MC_CLKDIS_TMDSCLK_DISABLE);
	udelay(1000);
	ret = dw_hdmi_enable(&priv->hdmi, &timing);
	if (ret)
		goto disable;

	video->xsize = D1_WIDTH;
	video->ysize = D1_HEIGHT;
	video->bpix = VIDEO_BPP32;
	video->format = VIDEO_X8R8G8B8;
	video_set_flush_dcache(dev, true);
	printf("D1 HDMI: 1920x1080 at 60 Hz\n");
	return 0;

disable:
	d1_clocks_disable(priv);
	return ret;
}

static int d1_video_of_to_plat(struct udevice *dev)
{
	struct d1_video_priv *priv = dev_get_priv(dev);
	struct dw_hdmi *hdmi = &priv->hdmi;

	priv->ccu = dev_remap_addr_name(dev, "ccu");
	priv->de_clk = dev_remap_addr_name(dev, "de-clocks");
	priv->mixer = dev_remap_addr_name(dev, "mixer");
	priv->top = dev_remap_addr_name(dev, "tcon-top");
	priv->tcon = dev_remap_addr_name(dev, "tcon-tv");
	priv->phy = dev_remap_addr_name(dev, "phy");
	hdmi->ioaddr = dev_read_addr_name(dev, "hdmi");
	if (!priv->ccu || !priv->de_clk || !priv->mixer || !priv->top ||
	    !priv->tcon || !priv->phy || hdmi->ioaddr == FDT_ADDR_T_NONE)
		return -EINVAL;

	hdmi->reg_io_width = 1;
	hdmi->i2c_clk_high = 0xd8;
	hdmi->i2c_clk_low = 0xfe;
	hdmi->ops = &d1_phy_ops;
	hdmi->hdmi_data.enc_in_bus_format = MEDIA_BUS_FMT_RGB888_1X24;
	hdmi->hdmi_data.enc_out_bus_format = MEDIA_BUS_FMT_RGB888_1X24;

	return 0;
}

static int d1_video_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = D1_WIDTH * D1_HEIGHT * 4;
	plat->align = SZ_4K;
	return 0;
}

static int d1_video_remove(struct udevice *dev)
{
	struct d1_video_priv *priv = dev_get_priv(dev);

	/* Stop physical-address DMA before Linux enables the display IOMMU. */
	writel(0, priv->mixer);
	readl(priv->mixer);
	mdelay(20);
	printf("D1 HDMI: framebuffer DMA stopped for OS handoff\n");
	return 0;
}

static const struct udevice_id d1_video_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-hdmi-display" },
	{ }
};

U_BOOT_DRIVER(sun20i_d1_video) = {
	.name = "sun20i_d1_video",
	.id = UCLASS_VIDEO,
	.of_match = d1_video_ids,
	.bind = d1_video_bind,
	.of_to_plat = d1_video_of_to_plat,
	.probe = d1_video_probe,
	.remove = d1_video_remove,
	.priv_auto = sizeof(struct d1_video_priv),
	.flags = DM_FLAG_ACTIVE_DMA,
};

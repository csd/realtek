/*
################################################################################
#
# r8168 is the Linux device driver released for RealTek RTL8168B/8111B,
# RTL8168C/8111C, RTL8168CP/8111CP, RTL8168D/8111D, RTL8168DP/8111DP, and
# RTL8168E/8111E Gigabit Ethernet controllers with PCI-Express interface.
#
# Copyright(c) 2010 Realtek Semiconductor Corp. All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, see <http://www.gnu.org/licenses/>.
#
# Author:
# Realtek NIC software team <nicfae@realtek.com>
# No. 2, Innovation Road II, Hsinchu Science Park, Hsinchu 300, Taiwan
#
################################################################################
*/

/*
 *  This product is covered by one or more of the following patents:
 *  US5,307,459, US5,434,872, US5,732,094, US6,570,884, US6,115,776, and US6,327,625.
 */

/*
 * This driver is modified from r8169.c in Linux kernel 2.6.18
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
#include <linux/dma-mapping.h>
#include <linux/moduleparam.h>
#endif//LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "r8168.h"
#include "r8168_asf.h"
#include "rtl_eeprom.h"
#include "rtltool.h"

/* Maximum events (Rx packets, etc.) to handle at each interrupt. */
static const int max_interrupt_work = 20;

/* Maximum number of multicast addresses to filter (vs. Rx-all-multicast).
   The RTL chips use a 64 element hash table based on the Ethernet CRC. */
static const int multicast_filter_limit = 32;

#define _R(NAME,MAC,RCR,MASK, JumFrameSz) \
	{ .name = NAME, .mcfg = MAC, .RCR_Cfg = RCR, .RxConfigMask = MASK, .jumbo_frame_sz = JumFrameSz }

static const struct {
	const char *name;
	u8 mcfg;
	u32 RCR_Cfg;
	u32 RxConfigMask;	/* Clears the bits supported by this chip */
	u32 jumbo_frame_sz;
} rtl_chip_info[] = {
	_R("RTL8168B/8111B",
	   CFG_METHOD_1,
	   (Reserved2_data << Reserved2_shift) | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_4k),

	_R("RTL8168B/8111B",
	   CFG_METHOD_2,
	   (Reserved2_data << Reserved2_shift) | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_4k),

	_R("RTL8168B/8111B",
	   CFG_METHOD_3,
	   (Reserved2_data << Reserved2_shift) | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_4k),

	_R("RTL8168C/8111C",
	   CFG_METHOD_4, RxCfg_128_int_en | RxCfg_fet_multi_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_6k),

	_R("RTL8168C/8111C",
	   CFG_METHOD_5,
	   RxCfg_128_int_en | RxCfg_fet_multi_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_6k),

	_R("RTL8168C/8111C",
	   CFG_METHOD_6,
	   RxCfg_128_int_en | RxCfg_fet_multi_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_6k),

	_R("RTL8168CP/8111CP",
	   CFG_METHOD_7,
	   RxCfg_128_int_en | RxCfg_fet_multi_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_6k),

	_R("RTL8168CP/8111CP",
	   CFG_METHOD_8,
	   RxCfg_128_int_en | RxCfg_fet_multi_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_6k),

	_R("RTL8168D/8111D",
	   CFG_METHOD_9,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168D/8111D",
	   CFG_METHOD_10,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168DP/8111DP",
	   CFG_METHOD_11,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168DP/8111DP",
	   CFG_METHOD_12,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168DP/8111DP",
	   CFG_METHOD_13,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168E/8111E",
	   CFG_METHOD_14,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),

	_R("RTL8168E/8111E",
	   CFG_METHOD_15,
	   RxCfg_128_int_en | (RX_DMA_BURST << RxCfgDMAShift),
	   0xff7e1880,
	   Jumbo_Frame_9k),
};
#undef _R

static struct pci_device_id rtl8168_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_REALTEK,	0x8168), },
	{0,},
};

MODULE_DEVICE_TABLE(pci, rtl8168_pci_tbl);

static int rx_copybreak = 200;
static int use_dac;
static struct {
	u32 msg_enable;
} debug = { -1 };

/* media options */
#define MAX_UNITS 8
static int speed[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int duplex[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int autoneg[MAX_UNITS] = { -1, -1, -1, -1, -1, -1, -1, -1 };

MODULE_AUTHOR("Realtek and the Linux r8168 crew <netdev@vger.kernel.org>");
MODULE_DESCRIPTION("RealTek RTL-8168 Gigabit Ethernet driver");

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
MODULE_PARM(speed, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(autoneg, "1-" __MODULE_STRING(MAX_UNITS) "i");
#else
static int num_speed = 0;
static int num_duplex = 0;
static int num_autoneg = 0;

module_param_array(speed, int, &num_speed, 0);
module_param_array(duplex, int, &num_duplex, 0);
module_param_array(autoneg, int, &num_autoneg, 0);
#endif

MODULE_PARM_DESC(speed, "force phy operation. Deprecated by ethtool (8).");
MODULE_PARM_DESC(duplex, "force phy operation. Deprecated by ethtool (8).");
MODULE_PARM_DESC(autoneg, "force phy operation. Deprecated by ethtool (8).");

module_param(rx_copybreak, int, 0);
MODULE_PARM_DESC(rx_copybreak, "Copy breakpoint for copy-only-tiny-frames");
module_param(use_dac, int, 0);
MODULE_PARM_DESC(use_dac, "Enable PCI DAC. Unsafe on 32 bit PCI slot.");

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
module_param_named(debug, debug.msg_enable, int, 0);
MODULE_PARM_DESC(debug, "Debug verbosity level (0=none, ..., 16=all)");
#endif//LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)

MODULE_LICENSE("GPL");

MODULE_VERSION(RTL8168_VERSION);

static void rtl8168_sleep_rx_enable(struct net_device *dev);
static void rtl8168_dsm(struct net_device *dev, int dev_state);

static void rtl8168_esd_timer(unsigned long __opaque);
static void rtl8168_link_timer(unsigned long __opaque);
static void rtl8168_tx_clear(struct rtl8168_private *tp);
static void rtl8168_rx_clear(struct rtl8168_private *tp);

static int rtl8168_open(struct net_device *dev);
static int rtl8168_start_xmit(struct sk_buff *skb, struct net_device *dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t rtl8168_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
#else
static irqreturn_t rtl8168_interrupt(int irq, void *dev_instance);
#endif
static int rtl8168_init_ring(struct net_device *dev);
static void rtl8168_hw_start(struct net_device *dev);
static int rtl8168_close(struct net_device *dev);
static void rtl8168_set_rx_mode(struct net_device *dev);
static void rtl8168_tx_timeout(struct net_device *dev);
static struct net_device_stats *rtl8168_get_stats(struct net_device *dev);
static int rtl8168_rx_interrupt(struct net_device *, struct rtl8168_private *, void __iomem *, u32 budget);
static int rtl8168_change_mtu(struct net_device *dev, int new_mtu);
static void rtl8168_down(struct net_device *dev);

static int rtl8168_set_mac_address(struct net_device *dev, void *p);
void rtl8168_rar_set(struct rtl8168_private *tp, uint8_t *addr, uint32_t index);
static void rtl8168_tx_desc_init(struct rtl8168_private *tp);
static void rtl8168_rx_desc_init(struct rtl8168_private *tp);

static void rtl8168_nic_reset(struct net_device *dev);

static void rtl8168_phy_power_up (struct net_device *dev);
static void rtl8168_phy_power_down (struct net_device *dev);
static int rtl8168_set_speed(struct net_device *dev, u8 autoneg,  u16 speed, u8 duplex);

#ifdef CONFIG_R8168_NAPI
static int rtl8168_poll(napi_ptr napi, napi_budget budget);
#endif

static u16 rtl8168_intr_mask = SYSErr | LinkChg | RxDescUnavail | TxErr | TxOK | RxErr | RxOK;
static const u16 rtl8168_napi_event =
	RxOK | RxDescUnavail | RxFIFOOver | TxOK | TxErr;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#undef ethtool_ops
#define ethtool_ops _kc_ethtool_ops

struct _kc_ethtool_ops {
	int  (*get_settings)(struct net_device *, struct ethtool_cmd *);
	int  (*set_settings)(struct net_device *, struct ethtool_cmd *);
	void (*get_drvinfo)(struct net_device *, struct ethtool_drvinfo *);
	int  (*get_regs_len)(struct net_device *);
	void (*get_regs)(struct net_device *, struct ethtool_regs *, void *);
	void (*get_wol)(struct net_device *, struct ethtool_wolinfo *);
	int  (*set_wol)(struct net_device *, struct ethtool_wolinfo *);
	u32  (*get_msglevel)(struct net_device *);
	void (*set_msglevel)(struct net_device *, u32);
	int  (*nway_reset)(struct net_device *);
	u32  (*get_link)(struct net_device *);
	int  (*get_eeprom_len)(struct net_device *);
	int  (*get_eeprom)(struct net_device *, struct ethtool_eeprom *, u8 *);
	int  (*set_eeprom)(struct net_device *, struct ethtool_eeprom *, u8 *);
	int  (*get_coalesce)(struct net_device *, struct ethtool_coalesce *);
	int  (*set_coalesce)(struct net_device *, struct ethtool_coalesce *);
	void (*get_ringparam)(struct net_device *, struct ethtool_ringparam *);
	int  (*set_ringparam)(struct net_device *, struct ethtool_ringparam *);
	void (*get_pauseparam)(struct net_device *,
	                       struct ethtool_pauseparam*);
	int  (*set_pauseparam)(struct net_device *,
	                       struct ethtool_pauseparam*);
	u32  (*get_rx_csum)(struct net_device *);
	int  (*set_rx_csum)(struct net_device *, u32);
	u32  (*get_tx_csum)(struct net_device *);
	int  (*set_tx_csum)(struct net_device *, u32);
	u32  (*get_sg)(struct net_device *);
	int  (*set_sg)(struct net_device *, u32);
	u32  (*get_tso)(struct net_device *);
	int  (*set_tso)(struct net_device *, u32);
	int  (*self_test_count)(struct net_device *);
	void (*self_test)(struct net_device *, struct ethtool_test *, u64 *);
	void (*get_strings)(struct net_device *, u32 stringset, u8 *);
	int  (*phys_id)(struct net_device *, u32);
	int  (*get_stats_count)(struct net_device *);
	void (*get_ethtool_stats)(struct net_device *, struct ethtool_stats *,
	                          u64 *);
} *ethtool_ops = NULL;

#undef SET_ETHTOOL_OPS
#define SET_ETHTOOL_OPS(netdev, ops) (ethtool_ops = (ops))

#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)


//#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,5)
#ifndef netif_msg_init
#define netif_msg_init _kc_netif_msg_init
/* copied from linux kernel 2.6.20 include/linux/netdevice.h */
static inline u32 netif_msg_init(int debug_value, int default_msg_enable_bits)
{
	/* use default */
	if (debug_value < 0 || debug_value >= (sizeof(u32) * 8))
		return default_msg_enable_bits;
	if (debug_value == 0)	/* no output */
		return 0;
	/* set low N bits */
	return (1 << debug_value) - 1;
}

#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,5)

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)
static inline void eth_copy_and_sum (struct sk_buff *dest,
				     const unsigned char *src,
				     int len, int base)
{
	memcpy (dest->data, src, len);
}
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,22)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
/* copied from linux kernel 2.6.20 /include/linux/time.h */
/* Parameters used to convert the timespec values: */
#define MSEC_PER_SEC	1000L

/* copied from linux kernel 2.6.20 /include/linux/jiffies.h */
/*
 * Change timeval to jiffies, trying to avoid the
 * most obvious overflows..
 *
 * And some not so obvious.
 *
 * Note that we don't want to return MAX_LONG, because
 * for various timeout reasons we often end up having
 * to wait "jiffies+1" in order to guarantee that we wait
 * at _least_ "jiffies" - so "jiffies+1" had better still
 * be positive.
 */
#define MAX_JIFFY_OFFSET ((~0UL >> 1)-1)

/*
 * Convert jiffies to milliseconds and back.
 *
 * Avoid unnecessary multiplications/divisions in the
 * two most common HZ cases:
 */
static inline unsigned int _kc_jiffies_to_msecs(const unsigned long j)
{
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (MSEC_PER_SEC / HZ) * j;
#elif HZ > MSEC_PER_SEC && !(HZ % MSEC_PER_SEC)
	return (j + (HZ / MSEC_PER_SEC) - 1)/(HZ / MSEC_PER_SEC);
#else
	return (j * MSEC_PER_SEC) / HZ;
#endif
}

static inline unsigned long _kc_msecs_to_jiffies(const unsigned int m)
{
	if (m > _kc_jiffies_to_msecs(MAX_JIFFY_OFFSET))
		return MAX_JIFFY_OFFSET;
#if HZ <= MSEC_PER_SEC && !(MSEC_PER_SEC % HZ)
	return (m + (MSEC_PER_SEC / HZ) - 1) / (MSEC_PER_SEC / HZ);
#elif HZ > MSEC_PER_SEC && !(HZ % MSEC_PER_SEC)
	return m * (HZ / MSEC_PER_SEC);
#else
	return (m * HZ + MSEC_PER_SEC - 1) / MSEC_PER_SEC;
#endif
}
#endif	//LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)

/* copied from linux kernel 2.6.12.6 /include/linux/pm.h */
typedef int __bitwise pci_power_t;

/* copied from linux kernel 2.6.12.6 /include/linux/pci.h */
typedef u32 __bitwise pm_message_t;

#define PCI_D0	((pci_power_t __force) 0)
#define PCI_D1	((pci_power_t __force) 1)
#define PCI_D2	((pci_power_t __force) 2)
#define PCI_D3hot	((pci_power_t __force) 3)
#define PCI_D3cold	((pci_power_t __force) 4)
#define PCI_POWER_ERROR	((pci_power_t __force) -1)

/* copied from linux kernel 2.6.12.6 /drivers/pci/pci.c */
/**
 * pci_choose_state - Choose the power state of a PCI device
 * @dev: PCI device to be suspended
 * @state: target sleep state for the whole system. This is the value
 *	that is passed to suspend() function.
 *
 * Returns PCI power state suitable for given device and given system
 * message.
 */

pci_power_t pci_choose_state(struct pci_dev *dev, pm_message_t state)
{
	if (!pci_find_capability(dev, PCI_CAP_ID_PM))
		return PCI_D0;

	switch (state) {
	case 0: return PCI_D0;
	case 3: return PCI_D3hot;
	default:
		printk("They asked me for state %d\n", state);
//		BUG();
	}
	return PCI_D0;
}
#endif	//LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
/**
 * msleep_interruptible - sleep waiting for waitqueue interruptions
 * @msecs: Time in milliseconds to sleep for
 */
#define msleep_interruptible _kc_msleep_interruptible
unsigned long _kc_msleep_interruptible(unsigned int msecs)
{
	unsigned long timeout = _kc_msecs_to_jiffies(msecs);

	while (timeout && !signal_pending(current)) {
		set_current_state(TASK_INTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
	}
	return _kc_jiffies_to_msecs(timeout);
}
#endif	//LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)
/* copied from linux kernel 2.6.20 include/linux/sched.h */
#ifndef __sched
#define __sched		__attribute__((__section__(".sched.text")))
#endif

/* copied from linux kernel 2.6.20 kernel/timer.c */
signed long __sched schedule_timeout_uninterruptible(signed long timeout)
{
	__set_current_state(TASK_UNINTERRUPTIBLE);
	return schedule_timeout(timeout);
}

/* copied from linux kernel 2.6.20 include/linux/mii.h */
#undef if_mii
#define if_mii _kc_if_mii
static inline struct mii_ioctl_data *if_mii(struct ifreq *rq)
{
	return (struct mii_ioctl_data *) &rq->ifr_ifru;
}
#endif	//LINUX_VERSION_CODE < KERNEL_VERSION(2,6,7)

static void mdio_write(struct rtl8168_private *tp,
	   u32 RegAddr,
	   u32 value)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int i;

	if(tp->mcfg==CFG_METHOD_11)
	{
		RTL_W32(OCPDR, OCPDR_Write |
			(RegAddr & OCPDR_Reg_Mask) << OCPDR_GPHY_Reg_shift |
			(value & OCPDR_Data_Mask));
		RTL_W32(OCPAR, OCPAR_GPHY_Write);
		RTL_W32(EPHY_RXER_NUM, 0);

		for (i = 0; i < 100; i++)
		{
			mdelay(1);
			if (!(RTL_R32(OCPAR) & OCPAR_Flag))
				break;
		}
	}
	else
	{
		if(tp->mcfg==CFG_METHOD_12)
		{
			RTL_W32(0xD0, RTL_R32(0xD0)&~0x00020000);
		}

		RTL_W32(PHYAR, PHYAR_Write |
			(RegAddr & PHYAR_Reg_Mask) << PHYAR_Reg_shift |
			(value & PHYAR_Data_Mask));

		for (i = 0; i < 10; i++) {
			udelay(100);

			/* Check if the RTL8168 has completed writing to the specified MII register */
			if (!(RTL_R32(PHYAR) & PHYAR_Flag))
				break;
		}
		udelay(20);

		if(tp->mcfg==CFG_METHOD_12)
		{
			RTL_W32(0xD0, RTL_R32(0xD0)|0x00020000);
		}
	}
}

static u32 mdio_read(struct rtl8168_private *tp,
	  u32 RegAddr)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int i, value = -1;

	if(tp->mcfg==CFG_METHOD_11)
	{
		RTL_W32(OCPDR, OCPDR_Read |
			(RegAddr & OCPDR_Reg_Mask) << OCPDR_GPHY_Reg_shift);
		RTL_W32(OCPAR, OCPAR_GPHY_Write);
		RTL_W32(EPHY_RXER_NUM, 0);

		for (i = 0; i < 100; i++)
		{
			mdelay(1);
			if (!(RTL_R32(OCPAR) & OCPAR_Flag))
				break;
		}

		mdelay(1);
		RTL_W32(OCPAR, OCPAR_GPHY_Read);
		RTL_W32(EPHY_RXER_NUM, 0);

		for (i = 0; i < 100; i++)
		{
			mdelay(1);
			if (RTL_R32(OCPAR) & OCPAR_Flag)
				break;
		}

		value = (int) (RTL_R32(OCPDR) & OCPDR_Data_Mask);
	}
	else
	{
		if(tp->mcfg==CFG_METHOD_12)
		{
			RTL_W32(0xD0, RTL_R32(0xD0)&~0x00020000);
		}

		RTL_W32(PHYAR,
			PHYAR_Read | (RegAddr & PHYAR_Reg_Mask) << PHYAR_Reg_shift);

		for (i = 0; i < 10; i++) {
			udelay(100);

			/* Check if the RTL8168 has completed retrieving data from the specified MII register */
			if (RTL_R32(PHYAR) & PHYAR_Flag) {
				value = (int) (RTL_R32(PHYAR) & PHYAR_Data_Mask);
				break;
			}
		}
		udelay(20);

		if(tp->mcfg==CFG_METHOD_12)
		{
			RTL_W32(0xD0, RTL_R32(0xD0)|0x00020000);
		}
	}

	return value;
}

static u32 OCP_read(struct rtl8168_private *tp, u8 mask, u16 Reg)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int	i;

	RTL_W32(OCPAR, ((u32)mask&0xF)<<12 | (Reg&0xFFF));
	for(i=0;i<20;i++)
	{
		udelay(100);
		if (RTL_R32(OCPAR) & OCPAR_Flag)
			break;
	}
	return RTL_R32(OCPDR);
}

static void OCP_write(struct rtl8168_private *tp, u8 mask, u16 Reg, u32 data)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int	i;

	RTL_W32(OCPDR, data);
	RTL_W32(OCPAR, OCPAR_Flag | ((u32)mask&0xF)<<12 | (Reg&0xFFF));
	for(i=0;i<20;i++)
	{
		udelay(100);
		if( (RTL_R32(OCPAR)&OCPAR_Flag) == 0)
			break;
	}
}

static void OOB_mutex_lock(struct rtl8168_private *tp)
{

	OCP_write(tp, 0x8, 0x14, 0x01000000);

	while(OCP_read(tp, 0xF, 0x014)&0x00FF0000)
	{
		if(OCP_read(tp, 0xF, 0x09C)&0x000000FF)
		{
			OCP_write(tp, 0x8, 0x14, 0x00000000);

			while(OCP_read(tp, 0xF, 0x09C)&0x000000FF);

			OCP_write(tp, 0x8, 0x14, 0x01000000);
		}
	}
}

static void OOB_mutex_unlock(struct rtl8168_private *tp)
{

	OCP_write(tp, 0x1, 0x9C, 0x00000001);
	OCP_write(tp, 0x8, 0x14, 0x00000000);
}

static void OOB_notify(struct rtl8168_private *tp, u8 cmd)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int	i;

	RTL_W8(ERIDR, cmd);
	RTL_W32(ERIAR, 0x800010E8);
	mdelay(2);
	for(i=0;i<5;i++)
	{
		udelay(100);
		if ( !(RTL_R32(ERIDR) & ERIAR_Flag))
			break;
	}

	OCP_write(tp, 0x1, 0x30, 0x00000001);
}

static void rtl8168_mac_loopback_test(struct rtl8168_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	struct net_device *dev = tp->dev;
	struct sk_buff *skb, *rx_skb;
	dma_addr_t mapping;
	struct TxDesc *txd;
	struct RxDesc *rxd;
	int	i;
	static u8	pattern;
	void	*tmpAddr;
	u16	type;
	u32	len, rx_len, rx_cmd;

	if(OCP_read(tp, 0xF, 0x010)&0x00008000)
		return;

	pattern = 0x5A;
	len = 60;
	type = 0x0008;
	txd = tp->TxDescArray;
	rxd = tp->RxDescArray;
	rx_skb = tp->Rx_skbuff[0];
	RTL_W32(TxConfig, (RTL_R32(TxConfig)&~0x00060000)|0x00020000);

	do{
		skb = dev_alloc_skb(len + NET_IP_ALIGN);
		if(unlikely(!skb))
			dev_printk(KERN_NOTICE, &tp->pci_dev->dev, "-ENOMEM;\n");
	}while(unlikely(skb==NULL));
	skb_reserve(skb, NET_IP_ALIGN);

	memcpy(skb_put(skb,dev->addr_len), dev->dev_addr, dev->addr_len);
	memcpy(skb_put(skb,dev->addr_len), dev->dev_addr, dev->addr_len);
	memcpy(skb_put(skb,sizeof(type)), &type, sizeof(type));
	tmpAddr = skb_put(skb,len-14);

	mapping = pci_map_single(tp->pci_dev, skb->data, len, PCI_DMA_TODEVICE);
	pci_dma_sync_single_for_cpu(tp->pci_dev, le64_to_cpu(mapping), len, PCI_DMA_TODEVICE);
	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = 0;
	while(1)
	{
		memset(tmpAddr, pattern++, len-14);
		pci_dma_sync_single_for_device(tp->pci_dev, le64_to_cpu(mapping), len, PCI_DMA_TODEVICE);
		txd->opts1 = cpu_to_le32(DescOwn | FirstFrag | LastFrag | len);

		RTL_W32(RxConfig, RTL_R32(RxConfig)  | AcceptMyPhys);

		smp_wmb();
		RTL_W8(TxPoll, NPQ);	/* set polling bit */

		for(i=0;i<50;i++)
		{
			udelay(200);
			rx_cmd = le32_to_cpu(rxd->opts1);
			if((rx_cmd&DescOwn)==0)
				break;
		}

		RTL_W32(RxConfig, RTL_R32(RxConfig) & ~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys |  AcceptAllPhys));

		rx_len = rx_cmd & 0x3FF;
		rx_len -= 4;
		rxd->opts1 = cpu_to_le32(DescOwn | tp->rx_buf_sz);

		pci_dma_sync_single_for_cpu(tp->pci_dev, le64_to_cpu(mapping), len, PCI_DMA_TODEVICE);

		if(rx_len==len)
		{
			pci_dma_sync_single_for_cpu(tp->pci_dev, le64_to_cpu(rxd->addr), tp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			i = memcmp(skb->data, rx_skb->data, rx_len);
			pci_dma_sync_single_for_device(tp->pci_dev, le64_to_cpu(rxd->addr), tp->rx_buf_sz, PCI_DMA_FROMDEVICE);
			if(i==0)
			{
//				dev_printk(KERN_INFO, &tp->pci_dev->dev, "loopback test finished\n",rx_len,len);
				break;
			}
		}

		rtl8168_nic_reset(dev);
		RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);
	}
	tp->dirty_tx++;
	tp->dirty_rx++;
	tp->cur_tx++;
	tp->cur_rx++;
	pci_unmap_single(tp->pci_dev, le64_to_cpu(mapping), len, PCI_DMA_TODEVICE);
	RTL_W32(TxConfig, RTL_R32(TxConfig)&~0x00060000);
	dev_kfree_skb_any(skb);
	RTL_W16(IntrStatus, 0xFFBF);
}

static void rtl8168_driver_start(struct rtl8168_private *tp)
{
	int	timeout;

	OOB_notify(tp, OOB_CMD_DRIVER_START);

	for(timeout=0;timeout<10;timeout++)
	{
		mdelay(10);
		if(OCP_read(tp, 0xF, 0x010)&0x00000800)
			break;
	}
}

static void rtl8168_driver_stop(struct rtl8168_private *tp)
{
	int	timeout;

	OOB_notify(tp, OOB_CMD_DRIVER_STOP);

	for(timeout=0;timeout<10;timeout++)
	{
		mdelay(10);
		if((OCP_read(tp, 0xF, 0x010)&0x00000800)==0)
			break;
	}
}

static void
rtl8168_ephy_write(void __iomem *ioaddr,
		   int RegAddr,
		   int value)
{
	int i;

	RTL_W32(EPHYAR,
		EPHYAR_Write |
		(RegAddr & EPHYAR_Reg_Mask) << EPHYAR_Reg_shift |
		(value & EPHYAR_Data_Mask));

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8168 has completed EPHY write */
		if (!(RTL_R32(EPHYAR) & EPHYAR_Flag))
			break;
	}

	udelay(20);
}

static u16
rtl8168_ephy_read(void __iomem *ioaddr,
		  int RegAddr)
{
	int i;
	u16 value = 0xffff;

	RTL_W32(EPHYAR,
		EPHYAR_Read | (RegAddr & EPHYAR_Reg_Mask) << EPHYAR_Reg_shift);

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8168 has completed EPHY read */
		if (RTL_R32(EPHYAR) & EPHYAR_Flag) {
			value = (u16) (RTL_R32(EPHYAR) & EPHYAR_Data_Mask);
			break;
		}
	}

	udelay(20);

	return value;
}

static void
rtl8168_csi_write(void __iomem *ioaddr,
		   int addr,
		   int value)
{
	int i;

	RTL_W32(CSIDR, value);
	RTL_W32(CSIAR,
		CSIAR_Write |
		CSIAR_ByteEn << CSIAR_ByteEn_shift |
		(addr & CSIAR_Addr_Mask));

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8168 has completed CSI write */
		if (!(RTL_R32(CSIAR) & CSIAR_Flag))
			break;
	}

	udelay(20);
}

u32 rtl8168_eri_read(void __iomem *ioaddr, int addr, int len, int type)
{
	int i, val_shift, shift = 0;
	u32 value1 = 0, value2 = 0, mask;

	if (len > 4 || len <= 0)
		return -1;

	while (len > 0) {
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;

		RTL_W32(ERIAR,
			ERIAR_Read |
			type << ERIAR_Type_shift |
			ERIAR_ByteEn << ERIAR_ByteEn_shift |
			addr);

		for (i = 0; i < 10; i++) {
			udelay(100);

			/* Check if the RTL8168 has completed ERI read */
			if (RTL_R32(ERIAR) & ERIAR_Flag)
				break;
		}

		if (len == 1)		mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)	mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)	mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else			mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;

		value1 = RTL_R32(ERIDR) & mask;
		value2 |= (value1 >> val_shift * 8) << shift * 8;

		if (len <= 4 - val_shift)
			len = 0;
		else {
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}

	return value2;
}

int rtl8168_eri_write(void __iomem *ioaddr, int addr, int len, u32 value, int type)
{

	int i, val_shift, shift = 0;
	u32 value1 = 0, mask;

	if (len > 4 || len <= 0)
		return -1;

	while(len > 0) {
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;

		if (len == 1)		mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)	mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)	mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else			mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;

		value1 = rtl8168_eri_read(ioaddr, addr, 4, type) & ~mask;
		value1 |= ((value << val_shift * 8) >> shift * 8);

		RTL_W32(ERIDR, value1);
		RTL_W32(ERIAR,
			ERIAR_Write |
			type << ERIAR_Type_shift |
			ERIAR_ByteEn << ERIAR_ByteEn_shift |
			addr);

		for (i = 0; i < 10; i++) {
			udelay(100);

			/* Check if the RTL8168 has completed ERI write */
			if (!(RTL_R32(ERIAR) & ERIAR_Flag))
				break;
		}

		if (len <= 4 - val_shift)
			len = 0;
		else {
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}

	return 0;
}

static int
rtl8168_csi_read(void __iomem *ioaddr,
		 int addr)
{
	int i, value = -1;

	RTL_W32(CSIAR,
		CSIAR_Read |
		CSIAR_ByteEn << CSIAR_ByteEn_shift |
		(addr & CSIAR_Addr_Mask));

	for (i = 0; i < 10; i++) {
		udelay(100);

		/* Check if the RTL8168 has completed CSI read */
		if (RTL_R32(CSIAR) & CSIAR_Flag) {
			value = (int)RTL_R32(CSIDR);
			break;
		}
	}

	udelay(20);

	return value;
}

static void
rtl8168_irq_mask_and_ack(void __iomem *ioaddr)
{
	RTL_W16(IntrMask, 0x0000);
}

static void
rtl8168_asic_down(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	rtl8168_irq_mask_and_ack(ioaddr);
	rtl8168_nic_reset(dev);
}

static void
rtl8168_nic_reset(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int	i;

	RTL_W32(RxConfig, RTL_R32(RxConfig) & ~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys |  AcceptAllPhys));

	if(tp->mcfg==CFG_METHOD_11 || tp->mcfg==CFG_METHOD_12)
	{
		while(RTL_R8(TxPoll)&NPQ)
		{
			udelay(20);
		}
	}
	else if ((tp->mcfg != CFG_METHOD_1) &&
	    (tp->mcfg != CFG_METHOD_2) &&
	    (tp->mcfg != CFG_METHOD_3)) {
		RTL_W8(ChipCmd, StopReq | CmdRxEnb | CmdTxEnb);
		udelay(100);
	}

	/* Soft reset the chip. */
	RTL_W8(ChipCmd, CmdReset);

	/* Check that the chip has finished the reset. */
	for (i = 100; i > 0; i--) {
		udelay(100);
		if ((RTL_R8(ChipCmd) & CmdReset) == 0)
			break;
	}

	if (tp->mcfg == CFG_METHOD_11)
	{
		OOB_mutex_lock(tp);
		OCP_write(tp, 0x3, 0x10, OCP_read(tp, 0xF, 0x010)&~0x00004000);
		OOB_mutex_unlock(tp);

		OOB_notify(tp, OOB_CMD_RESET);

		for(i=0;i<10;i++)
		{
			mdelay(10);
			if(OCP_read(tp, 0xF, 0x010)&0x00004000)
				break;
		}

		for(i=0;i<5;i++)
		{
			if( (OCP_read(tp, 0xF, 0x034) & 0xFFFF) == 0)
				break;
		}
	}
}

static unsigned int
rtl8168_xmii_reset_pending(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;
	unsigned int retval;

	spin_lock_irqsave(&tp->phy_lock, flags);
	mdio_write(tp, 0x1f, 0x0000);
	retval = mdio_read(tp, MII_BMCR) & BMCR_RESET;
	spin_unlock_irqrestore(&tp->phy_lock, flags);

	return retval;
}

static unsigned int
rtl8168_xmii_link_ok(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int retval;

	retval = RTL_R8(PHYstatus) & LinkStatus;

	return retval;
}

static void
rtl8168_xmii_reset_enable(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;
	int i, val = 0;

	spin_lock_irqsave(&tp->phy_lock, flags);
	mdio_write(tp, 0x1f, 0x0000);
	mdio_write(tp, MII_BMCR, mdio_read(tp, MII_BMCR) | BMCR_RESET);
	spin_unlock_irqrestore(&tp->phy_lock, flags);

	for(i = 0; i < 2500; i++) {
		spin_lock_irqsave(&tp->phy_lock, flags);
		val = mdio_read(tp, MII_BMSR) & BMCR_RESET;
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		if(!val)
			return;

		mdelay(1);
	}
}

static void
rtl8168dp_10mbps_gphy_para(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 status = RTL_R8(PHYstatus);
	unsigned long flags;

	spin_lock_irqsave(&tp->phy_lock, flags);
	if ((status & LinkStatus) && (status & _10bps)) {
		mdio_write(tp, 0x1f, 0x0000);
		mdio_write(tp, 0x10, 0x04EE);
	} else {
		mdio_write(tp, 0x1f, 0x0000);
		mdio_write(tp, 0x10, 0x01EE);
	}
	spin_unlock_irqrestore(&tp->phy_lock, flags);
}

static void
rtl8168_check_link_status(struct net_device *dev,
			  struct rtl8168_private *tp,
			  void __iomem *ioaddr)
{
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
	if (tp->link_ok(dev)) {
		netif_carrier_on(dev);
		if (netif_msg_ifup(tp))
			printk(KERN_INFO PFX "%s: link up\n", dev->name);
	} else {
		if (netif_msg_ifdown(tp))
			printk(KERN_INFO PFX "%s: link down\n", dev->name);
		netif_carrier_off(dev);
	}
	spin_unlock_irqrestore(&tp->lock, flags);

	if(tp->mcfg == CFG_METHOD_11)
	{
		rtl8168dp_10mbps_gphy_para(dev);
	}
}

static void
rtl8168_link_option(int idx,
		    u8 *aut,
		    u16 *spd,
		    u8 *dup)
{
	unsigned char opt_speed;
	unsigned char opt_duplex;
	unsigned char opt_autoneg;

	opt_speed = ((idx < MAX_UNITS) && (idx >= 0)) ? speed[idx] : 0xff;
	opt_duplex = ((idx < MAX_UNITS) && (idx >= 0)) ? duplex[idx] : 0xff;
	opt_autoneg = ((idx < MAX_UNITS) && (idx >= 0)) ? autoneg[idx] : 0xff;

	if ((opt_speed == 0xff) |
	    (opt_duplex == 0xff) |
	    (opt_autoneg == 0xff)) {
		*spd = SPEED_1000;
		*dup = DUPLEX_FULL;
		*aut = AUTONEG_ENABLE;
	} else {
		*spd = speed[idx];
		*dup = duplex[idx];
		*aut = autoneg[idx];
	}
}

static void
rtl8168_powerdown_pll(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	if (tp->mcfg==CFG_METHOD_11)
		return;

	if (((tp->mcfg == CFG_METHOD_7) || (tp->mcfg == CFG_METHOD_8)) && (RTL_R16(CPlusCmd) & ASF))
		return;

	spin_lock_irqsave(&tp->phy_lock, flags);
	mdio_write(tp, 0x1F, 0x0000);
	mdio_write(tp, 0x00, 0x0000);
	spin_unlock_irqrestore(&tp->phy_lock, flags);
	if (tp->wol_enabled == WOL_ENABLED)
	{
		RTL_W32(RxConfig, RTL_R32(RxConfig) | AcceptBroadcast | AcceptMulticast | AcceptMyPhys);
		return;
	}

	rtl8168_phy_power_down(dev);

	switch (tp->mcfg) {
	case CFG_METHOD_9:
	case CFG_METHOD_10:
	case CFG_METHOD_12:
	case CFG_METHOD_14:
	case CFG_METHOD_15:
		RTL_W8(PMCH, RTL_R8(PMCH) & ~BIT_7);
		break;
	}
}

static void rtl8168_powerup_pll(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	if (tp->mcfg == CFG_METHOD_11)
		return;

	switch (tp->mcfg) {
	case CFG_METHOD_9:
	case CFG_METHOD_10:
	case CFG_METHOD_12:
	case CFG_METHOD_14:
	case CFG_METHOD_15:
		RTL_W8(PMCH, RTL_R8(PMCH) | BIT_7);
		break;
	}

	rtl8168_phy_power_up(dev);
	rtl8168_set_speed(dev, tp->autoneg, tp->speed, tp->duplex);
}

static void
rtl8168_get_wol(struct net_device *dev,
		struct ethtool_wolinfo *wol)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 options;

	wol->wolopts = 0;

#define WAKE_ANY (WAKE_PHY | WAKE_MAGIC | WAKE_UCAST | WAKE_BCAST | WAKE_MCAST)
	wol->supported = WAKE_ANY;

	spin_lock_irq(&tp->lock);

	options = RTL_R8(Config1);
	if (!(options & PMEnable))
		goto out_unlock;

	options = RTL_R8(Config3);
	if (options & LinkUp)
		wol->wolopts |= WAKE_PHY;
	if (options & MagicPacket)
		wol->wolopts |= WAKE_MAGIC;

	options = RTL_R8(Config5);
	if (options & UWF)
		wol->wolopts |= WAKE_UCAST;
	if (options & BWF)
	        wol->wolopts |= WAKE_BCAST;
	if (options & MWF)
	        wol->wolopts |= WAKE_MCAST;

out_unlock:
	spin_unlock_irq(&tp->lock);
}

static int
rtl8168_set_wol(struct net_device *dev,
		struct ethtool_wolinfo *wol)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int i;
	static struct {
		u32 opt;
		u16 reg;
		u8  mask;
	} cfg[] = {
		{ WAKE_ANY,   Config1, PMEnable },
		{ WAKE_PHY,   Config3, LinkUp },
		{ WAKE_MAGIC, Config3, MagicPacket },
		{ WAKE_UCAST, Config5, UWF },
		{ WAKE_BCAST, Config5, BWF },
		{ WAKE_MCAST, Config5, MWF },
		{ WAKE_ANY,   Config5, LanWake }
	};

	spin_lock_irq(&tp->lock);

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	for (i = 0; i < ARRAY_SIZE(cfg); i++) {
		u8 options = RTL_R8(cfg[i].reg) & ~cfg[i].mask;
		if (wol->wolopts & cfg[i].opt)
			options |= cfg[i].mask;
		RTL_W8(cfg[i].reg, options);
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	tp->wol_enabled = (wol->wolopts) ? WOL_ENABLED : WOL_DISABLED;

	spin_unlock_irq(&tp->lock);

	return 0;
}

static void
rtl8168_get_drvinfo(struct net_device *dev,
		    struct ethtool_drvinfo *info)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	strcpy(info->driver, MODULENAME);
	strcpy(info->version, RTL8168_VERSION);
	strcpy(info->bus_info, pci_name(tp->pci_dev));
	info->regdump_len = R8168_REGS_SIZE;
	info->eedump_len = tp->eeprom_len;
}

static int
rtl8168_get_regs_len(struct net_device *dev)
{
	return R8168_REGS_SIZE;
}

static int
rtl8168_set_speed_xmii(struct net_device *dev,
		       u8 autoneg,
		       u16 speed,
		       u8 duplex)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	int auto_nego = 0;
	int giga_ctrl = 0;
	int bmcr_true_force = 0;
	unsigned long flags;

	if ((speed != SPEED_1000) &&
	    (speed != SPEED_100) &&
	    (speed != SPEED_10)) {
		speed = SPEED_1000;
		duplex = DUPLEX_FULL;
	}

	if ((autoneg == AUTONEG_ENABLE) || (speed == SPEED_1000)) {
		/*n-way force*/
		if ((speed == SPEED_10) && (duplex == DUPLEX_HALF)) {
			auto_nego |= ADVERTISE_10HALF;
		} else if ((speed == SPEED_10) && (duplex == DUPLEX_FULL)) {
			auto_nego |= ADVERTISE_10HALF |
				     ADVERTISE_10FULL;
		} else if ((speed == SPEED_100) && (duplex == DUPLEX_HALF)) {
			auto_nego |= ADVERTISE_100HALF |
				     ADVERTISE_10HALF |
				     ADVERTISE_10FULL;
		} else if ((speed == SPEED_100) && (duplex == DUPLEX_FULL)) {
			auto_nego |= ADVERTISE_100HALF |
				     ADVERTISE_100FULL |
				     ADVERTISE_10HALF |
				     ADVERTISE_10FULL;
		} else if (speed == SPEED_1000) {
			giga_ctrl |= ADVERTISE_1000HALF |
				     ADVERTISE_1000FULL;

			auto_nego |= ADVERTISE_100HALF |
				     ADVERTISE_100FULL |
				     ADVERTISE_10HALF |
				     ADVERTISE_10FULL;
		}

		//disable flow contorol
		auto_nego &= ~ADVERTISE_PAUSE_CAP;
		auto_nego &= ~ADVERTISE_PAUSE_ASYM;

		tp->phy_auto_nego_reg = auto_nego;
		tp->phy_1000_ctrl_reg = giga_ctrl;

		tp->autoneg = autoneg;
		tp->speed = speed;
		tp->duplex = duplex;

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1f, 0x0000);
		mdio_write(tp, MII_ADVERTISE, auto_nego);
		mdio_write(tp, MII_CTRL1000, giga_ctrl);
		mdio_write(tp, MII_BMCR, BMCR_RESET | BMCR_ANENABLE | BMCR_ANRESTART);
		spin_unlock_irqrestore(&tp->phy_lock, flags);
		mdelay(20);
	} else {
		/*true force*/
#ifndef BMCR_SPEED100
#define BMCR_SPEED100	0x0040
#endif

#ifndef BMCR_SPEED10
#define BMCR_SPEED10	0x0000
#endif
		if ((speed == SPEED_10) && (duplex == DUPLEX_HALF)) {
			bmcr_true_force = BMCR_SPEED10;
		} else if ((speed == SPEED_10) && (duplex == DUPLEX_FULL)) {
			bmcr_true_force = BMCR_SPEED10 | BMCR_FULLDPLX;
		} else if ((speed == SPEED_100) && (duplex == DUPLEX_HALF)) {
			bmcr_true_force = BMCR_SPEED100;
		} else if ((speed == SPEED_100) && (duplex == DUPLEX_FULL)) {
			bmcr_true_force = BMCR_SPEED100 | BMCR_FULLDPLX;
		}

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1f, 0x0000);
		mdio_write(tp, MII_BMCR, bmcr_true_force);
		spin_unlock_irqrestore(&tp->phy_lock, flags);
	}

	if (tp->mcfg == CFG_METHOD_11)
		rtl8168dp_10mbps_gphy_para(dev);

	return 0;
}

static int
rtl8168_set_speed(struct net_device *dev,
		  u8 autoneg,
		  u16 speed,
		  u8 duplex)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	int ret;

	ret = tp->set_speed(dev, autoneg, speed, duplex);

	return ret;
}

static int
rtl8168_set_settings(struct net_device *dev,
		     struct ethtool_cmd *cmd)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&tp->lock, flags);
	ret = rtl8168_set_speed(dev, cmd->autoneg, cmd->speed, cmd->duplex);
	spin_unlock_irqrestore(&tp->lock, flags);

	return ret;
}

static u32
rtl8168_get_tx_csum(struct net_device *dev)
{
	return (dev->features & NETIF_F_IP_CSUM) != 0;
}

static u32
rtl8168_get_rx_csum(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	return tp->cp_cmd & RxChkSum;
}

static int
rtl8168_set_tx_csum(struct net_device *dev,
		    u32 data)
{
	if (data)
		dev->features |= NETIF_F_IP_CSUM;
	else
		dev->features &= ~NETIF_F_IP_CSUM;

	return 0;
}

static int
rtl8168_set_rx_csum(struct net_device *dev,
		    u32 data)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);

	if (data)
		tp->cp_cmd |= RxChkSum;
	else
		tp->cp_cmd &= ~RxChkSum;

	RTL_W16(CPlusCmd, tp->cp_cmd);

	spin_unlock_irqrestore(&tp->lock, flags);

	return 0;
}

#ifdef CONFIG_R8168_VLAN

static inline u32
rtl8168_tx_vlan_tag(struct rtl8168_private *tp,
		    struct sk_buff *skb)
{
	return (tp->vlgrp && vlan_tx_tag_present(skb)) ?
		TxVlanTag | swab16(vlan_tx_tag_get(skb)) : 0x00;
}

static void
rtl8168_vlan_rx_register(struct net_device *dev,
			 struct vlan_group *grp)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
	tp->vlgrp = grp;
	if (tp->vlgrp)
		tp->cp_cmd |= RxVlan;
	else
		tp->cp_cmd &= ~RxVlan;
	RTL_W16(CPlusCmd, tp->cp_cmd);
	RTL_R16(CPlusCmd);
	spin_unlock_irqrestore(&tp->lock, flags);
}

static void
rtl8168_vlan_rx_kill_vid(struct net_device *dev,
			 unsigned short vid)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	if (tp->vlgrp)
		tp->vlgrp->vlan_devices[vid] = NULL;
#else
	vlan_group_set_device(tp->vlgrp, vid, NULL);
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,21)
	spin_unlock_irqrestore(&tp->lock, flags);
}

static int
rtl8168_rx_vlan_skb(struct rtl8168_private *tp,
		    struct RxDesc *desc,
		    struct sk_buff *skb)
{
	u32 opts2 = le32_to_cpu(desc->opts2);
	int ret;

	if (tp->vlgrp && (opts2 & RxVlanTag)) {
		rtl8168_rx_hwaccel_skb(skb, tp->vlgrp,
				       swab16(opts2 & 0xffff));
		ret = 0;
	} else
		ret = -1;
	desc->opts2 = 0;
	return ret;
}

#else /* !CONFIG_R8168_VLAN */

static inline u32
rtl8168_tx_vlan_tag(struct rtl8168_private *tp,
		    struct sk_buff *skb)
{
	return 0;
}

static int
rtl8168_rx_vlan_skb(struct rtl8168_private *tp,
		    struct RxDesc *desc,
		    struct sk_buff *skb)
{
	return -1;
}

#endif

static void rtl8168_gset_xmii(struct net_device *dev,
		  struct ethtool_cmd *cmd)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 status;
	unsigned long flags;

	cmd->supported = SUPPORTED_10baseT_Half |
			 SUPPORTED_10baseT_Full |
			 SUPPORTED_100baseT_Half |
			 SUPPORTED_100baseT_Full |
			 SUPPORTED_1000baseT_Full |
			 SUPPORTED_Autoneg |
		         SUPPORTED_TP;

	spin_lock_irqsave(&tp->phy_lock, flags);
	cmd->autoneg = (mdio_read(tp, MII_BMCR) & BMCR_ANENABLE) ? 1 : 0;
	spin_unlock_irqrestore(&tp->phy_lock, flags);
	cmd->advertising = ADVERTISED_TP | ADVERTISED_Autoneg;

	if (tp->phy_auto_nego_reg & ADVERTISE_10HALF)
		cmd->advertising |= ADVERTISED_10baseT_Half;
	if (tp->phy_auto_nego_reg & ADVERTISE_10FULL)
		cmd->advertising |= ADVERTISED_10baseT_Full;
	if (tp->phy_auto_nego_reg & ADVERTISE_100HALF)
		cmd->advertising |= ADVERTISED_100baseT_Half;
	if (tp->phy_auto_nego_reg & ADVERTISE_100FULL)
		cmd->advertising |= ADVERTISED_100baseT_Full;
	if (tp->phy_1000_ctrl_reg & ADVERTISE_1000FULL)
		cmd->advertising |= ADVERTISED_1000baseT_Full;

	status = RTL_R8(PHYstatus);

	if (status & _1000bpsF)
		cmd->speed = SPEED_1000;
	else if (status & _100bps)
		cmd->speed = SPEED_100;
	else if (status & _10bps)
		cmd->speed = SPEED_10;

	if (status & TxFlowCtrl)
		cmd->advertising |= ADVERTISED_Asym_Pause;

	if (status & RxFlowCtrl)
		cmd->advertising |= ADVERTISED_Pause;

	cmd->duplex = ((status & _1000bpsF) || (status & FullDup)) ?
		      DUPLEX_FULL : DUPLEX_HALF;

	tp->autoneg = cmd->autoneg;
	tp->speed = cmd->speed;
	tp->duplex = cmd->duplex;

}

static int
rtl8168_get_settings(struct net_device *dev,
		     struct ethtool_cmd *cmd)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->lock, flags);

	tp->get_settings(dev, cmd);

	spin_unlock_irqrestore(&tp->lock, flags);
	return 0;
}

static void rtl8168_get_regs(struct net_device *dev, struct ethtool_regs *regs,
			     void *p)
{
        struct rtl8168_private *tp = netdev_priv(dev);
        unsigned long flags;

        if (regs->len > R8168_REGS_SIZE)
        	regs->len = R8168_REGS_SIZE;

        spin_lock_irqsave(&tp->lock, flags);
        memcpy_fromio(p, tp->mmio_addr, regs->len);
        spin_unlock_irqrestore(&tp->lock, flags);
}

static u32
rtl8168_get_msglevel(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	return tp->msg_enable;
}

static void
rtl8168_set_msglevel(struct net_device *dev,
		     u32 value)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	tp->msg_enable = value;
}

static const char rtl8168_gstrings[][ETH_GSTRING_LEN] = {
	"tx_packets",
	"rx_packets",
	"tx_errors",
	"rx_errors",
	"rx_missed",
	"align_errors",
	"tx_single_collisions",
	"tx_multi_collisions",
	"unicast",
	"broadcast",
	"multicast",
	"tx_aborted",
	"tx_underrun",
};

struct rtl8168_counters {
	u64	tx_packets;
	u64	rx_packets;
	u64	tx_errors;
	u32	rx_errors;
	u16	rx_missed;
	u16	align_errors;
	u32	tx_one_collision;
	u32	tx_multi_collision;
	u64	rx_unicast;
	u64	rx_broadcast;
	u32	rx_multicast;
	u16	tx_aborted;
	u16	tx_underun;
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
static int rtl8168_get_stats_count(struct net_device *dev)
{
	return ARRAY_SIZE(rtl8168_gstrings);
}
#endif

static void
rtl8168_get_ethtool_stats(struct net_device *dev,
			  struct ethtool_stats *stats,
			  u64 *data)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct rtl8168_counters *counters;
	dma_addr_t paddr;
	u32 cmd;

	ASSERT_RTNL();

	counters = pci_alloc_consistent(tp->pci_dev, sizeof(*counters), &paddr);
	if (!counters)
		return;

	RTL_W32(CounterAddrHigh, (u64)paddr >> 32);
	cmd = (u64)paddr & DMA_32BIT_MASK;
	RTL_W32(CounterAddrLow, cmd);
	RTL_W32(CounterAddrLow, cmd | CounterDump);

	while (RTL_R32(CounterAddrLow) & CounterDump) {
		if (msleep_interruptible(1))
			break;
	}

	RTL_W32(CounterAddrLow, 0);
	RTL_W32(CounterAddrHigh, 0);

	data[0]	= le64_to_cpu(counters->tx_packets);
	data[1] = le64_to_cpu(counters->rx_packets);
	data[2] = le64_to_cpu(counters->tx_errors);
	data[3] = le32_to_cpu(counters->rx_errors);
	data[4] = le16_to_cpu(counters->rx_missed);
	data[5] = le16_to_cpu(counters->align_errors);
	data[6] = le32_to_cpu(counters->tx_one_collision);
	data[7] = le32_to_cpu(counters->tx_multi_collision);
	data[8] = le64_to_cpu(counters->rx_unicast);
	data[9] = le64_to_cpu(counters->rx_broadcast);
	data[10] = le32_to_cpu(counters->rx_multicast);
	data[11] = le16_to_cpu(counters->tx_aborted);
	data[12] = le16_to_cpu(counters->tx_underun);
pci_free_consistent(tp->pci_dev, sizeof(*counters), counters, paddr);
}

static void
rtl8168_get_strings(struct net_device *dev,
		    u32 stringset,
		    u8 *data)
{
	switch(stringset) {
	case ETH_SS_STATS:
		memcpy(data, *rtl8168_gstrings, sizeof(rtl8168_gstrings));
		break;
	}
}
static int rtl_get_eeprom_len(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	return tp->eeprom_len;
}

static int rtl_get_eeprom(struct net_device *dev, struct ethtool_eeprom *eeprom, u8 *buf)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	int	i,j,ret;
	int	start_w, end_w;
	int	VPD_addr, VPD_data;
	u32 *eeprom_buff;
	u16 tmp;
	void __iomem *ioaddr = tp->mmio_addr;

	if(tp->eeprom_type==EEPROM_TYPE_NONE)
	{
		dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Detect none EEPROM\n");
		return -EOPNOTSUPP;
	}
	else if (eeprom->len == 0 || (eeprom->offset+eeprom->len) > tp->eeprom_len)
	{
		dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Invalid parameter\n");
		return -EINVAL;
	}

	switch(tp->mcfg)
	{
		case CFG_METHOD_9:
		case CFG_METHOD_10:
			VPD_addr = 0xCE;
			VPD_data = 0xD0;
			break;

		case CFG_METHOD_4:
		case CFG_METHOD_5:
		case CFG_METHOD_6:
		case CFG_METHOD_7:
		case CFG_METHOD_8:
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			VPD_addr = 0xD2;
			VPD_data = 0xD4;
			break;

		case CFG_METHOD_1:
		case CFG_METHOD_2:
		case CFG_METHOD_3:
		default:
			return -EOPNOTSUPP;
	}

	start_w = eeprom->offset >> 2;
	end_w = (eeprom->offset + eeprom->len - 1) >> 2;

	eeprom_buff = kmalloc(sizeof(u32)*(end_w - start_w + 1), GFP_KERNEL);
	if (!eeprom_buff)
	{
		return -ENOMEM;
	}

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	for(i=start_w; i<=end_w; i++)
	{
		pci_write_config_word(tp->pci_dev, VPD_addr, (u16)i*4);
		ret = -EFAULT;
		for(j=0;j<10;j++)
		{
			udelay(400);
			pci_read_config_word(tp->pci_dev, VPD_addr, &tmp);
			if(tmp&0x8000)
			{
				ret = 0;
				break;
			}
		}

		if(ret)
		{
			break;
		}

		pci_read_config_dword(tp->pci_dev, VPD_data, &eeprom_buff[i-start_w]);
	}
	RTL_W8(Cfg9346, Cfg9346_Lock);

	if(!ret)
	{
		memcpy(buf, (u8 *)eeprom_buff + (eeprom->offset & 3), eeprom->len);
	}

	kfree(eeprom_buff);

	return ret;
}

#undef ethtool_op_get_link
#define ethtool_op_get_link _kc_ethtool_op_get_link
u32 _kc_ethtool_op_get_link(struct net_device *dev)
{
	return netif_carrier_ok(dev) ? 1 : 0;
}

#undef ethtool_op_get_sg
#define ethtool_op_get_sg _kc_ethtool_op_get_sg
u32 _kc_ethtool_op_get_sg(struct net_device *dev)
{
#ifdef NETIF_F_SG
	return (dev->features & NETIF_F_SG) != 0;
#else
	return 0;
#endif
}

#undef ethtool_op_set_sg
#define ethtool_op_set_sg _kc_ethtool_op_set_sg
int _kc_ethtool_op_set_sg(struct net_device *dev, u32 data)
{
#ifdef NETIF_F_SG
	if (data)
		dev->features |= NETIF_F_SG;
	else
		dev->features &= ~NETIF_F_SG;
#endif

	return 0;
}

static struct ethtool_ops rtl8168_ethtool_ops = {
	.get_drvinfo		= rtl8168_get_drvinfo,
	.get_regs_len		= rtl8168_get_regs_len,
	.get_link		= ethtool_op_get_link,
	.get_settings		= rtl8168_get_settings,
	.set_settings		= rtl8168_set_settings,
	.get_msglevel		= rtl8168_get_msglevel,
	.set_msglevel		= rtl8168_set_msglevel,
	.get_rx_csum		= rtl8168_get_rx_csum,
	.set_rx_csum		= rtl8168_set_rx_csum,
	.get_tx_csum		= rtl8168_get_tx_csum,
	.set_tx_csum		= rtl8168_set_tx_csum,
	.get_sg			= ethtool_op_get_sg,
	.set_sg			= ethtool_op_set_sg,
#ifdef NETIF_F_TSO
	.get_tso		= ethtool_op_get_tso,
	.set_tso		= ethtool_op_set_tso,
#endif
	.get_regs		= rtl8168_get_regs,
	.get_wol		= rtl8168_get_wol,
	.set_wol		= rtl8168_set_wol,
	.get_strings		= rtl8168_get_strings,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33)
	.get_stats_count	= rtl8168_get_stats_count,
#endif
	.get_ethtool_stats	= rtl8168_get_ethtool_stats,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
#ifdef ETHTOOL_GPERMADDR
	.get_perm_addr		= ethtool_op_get_perm_addr,
#endif
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23)
	.get_eeprom		= rtl_get_eeprom,
	.get_eeprom_len		= rtl_get_eeprom_len,
};

#if 0
static int rtl8168_enable_EEE(struct rtl8168_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int	ret;
	unsigned long flags;
	__u16	data;

	ret = 0;
	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1F, 0x0007);
			mdio_write(tp, 0x1E, 0x0020);
			data = mdio_read(tp, 0x15) | 0x1000;
			mdio_write(tp, 0x15, data);
			mdio_write(tp, 0x1F, 0x0006);
			mdio_write(tp, 0x00, 0x5A30);
			mdio_write(tp, 0x1F, 0x0000);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
			if(RTL_R8(MACDBG)&0x80)
			{
				data = RTL_R16(CustomLED);
				spin_lock_irqsave(&tp->phy_lock, flags);
				mdio_write(tp, 0x1F, 0x0005);
				mdio_write(tp, 0x05, 0x8AC8);
				mdio_write(tp, 0x06, data);
				mdio_write(tp, 0x05, 0x8B82);
				data = mdio_read(tp, 0x06) | 0x0010;
				mdio_write(tp, 0x05, 0x8B82);
				mdio_write(tp, 0x06, data);
				mdio_write(tp, 0x1F, 0x0000);
				spin_unlock_irqrestore(&tp->phy_lock, flags);
			}
			break;

		default:
			dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Not Support EEE\n");
			ret = -EOPNOTSUPP;
	}

	return ret;
}

static int rtl8168_disable_EEE(struct rtl8168_private *tp)
{
	void __iomem *ioaddr = tp->mmio_addr;
	int	ret;
	unsigned long flags;
	__u16	data;

	ret = 0;
	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1F, 0x0007);
			mdio_write(tp, 0x1E, 0x0020);
			data = mdio_read(tp, 0x15) & ~0x1000;
			mdio_write(tp, 0x15, data);
			mdio_write(tp, 0x1F, 0x0006);
			mdio_write(tp, 0x00, 0x5A00);
			mdio_write(tp, 0x1F, 0x0000);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
			if(RTL_R8(MACDBG)&0x80)
			{
				data = RTL_R16(CustomLED);
				spin_lock_irqsave(&tp->phy_lock, flags);
				mdio_write(tp, 0x1F, 0x0005);
				mdio_write(tp, 0x05, 0x8B82);
				data = mdio_read(tp, 0x06) & ~0x0010;
				mdio_write(tp, 0x05, 0x8B82);
				mdio_write(tp, 0x06, data);
				mdio_write(tp, 0x1F, 0x0000);
				spin_unlock_irqrestore(&tp->phy_lock, flags);
			}
			break;

		default:
			dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Not Support EEE\n");
			ret = -EOPNOTSUPP;
			break;
	}

	return ret;
}

static int rtl8168_enable_green_feature(struct rtl8168_private *tp)
{
	__u16	data;

	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1F, 0x0003);
			data = mdio_read(tp, 0x10) | 0x0400;
			mdio_write(tp, 0x10, data);
			data = mdio_read(tp, 0x19) | 0x0001;
			mdio_write(tp, 0x19, data);
			mdio_write(tp, 0x1F, 0x0005);
			data = mdio_read(tp, 0x01) & ~0x0100;
			mdio_write(tp, 0x01, data);
			mdio_write(tp, 0x1F, 0x0000);
			mdio_write(tp, 0x00, 0x9200);
			mdelay(20);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
			break;

		default:
			dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Not Support Green Feature\n");
			break;
	}

	return 0;
}

static int rtl8168_disable_green_feature(struct rtl8168_private *tp)
{
	__u16	data;

	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1F, 0x0005);
			data = mdio_read(tp, 0x01) | 0x0100;
			mdio_write(tp, 0x01, data);
			mdio_write(tp, 0x1F, 0x0003);
			data = mdio_read(tp, 0x10) & ~0x0400;
			mdio_write(tp, 0x10, data);
			data = mdio_read(tp, 0x19) & ~0x0001;
			mdio_write(tp, 0x19, data);
			mdio_write(tp, 0x1F, 0x0002);
			data = mdio_read(tp, 0x06) & 0x8FFF;
			data |= 0x3000;
			mdio_write(tp, 0x06, data);
			data = mdio_read(tp, 0x0D) & 0xF8FF;
			data |= 0x0500;
			mdio_write(tp, 0x0D, data);
			mdio_write(tp, 0x1F, 0x0000);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
			break;

		default:
			dev_printk(KERN_DEBUG, &tp->pci_dev->dev, "Not Support Green Feature\n");
			break;
	}

	return 0;
}

#endif

static int
rtl8168_get_mac_version(struct rtl8168_private *tp,
			void __iomem *ioaddr)
{
	u32 reg,val32;
	u32 ICVerID;

	val32 = RTL_R32(TxConfig)  ;
	reg = val32 & 0x7c800000;
	ICVerID = val32 & 0x00700000;

	switch(reg) {
		case 0x30000000:
			tp->mcfg = CFG_METHOD_1;
			tp->efuse = EFUSE_NOT_SUPPORT;
			return 0;
		case 0x38000000:
			if(ICVerID == 0x00000000) {
				tp->mcfg = CFG_METHOD_2;
			} else if(ICVerID == 0x00500000) {
				tp->mcfg = CFG_METHOD_3;
			} else {
				tp->mcfg = CFG_METHOD_3;
			}
			tp->efuse = EFUSE_NOT_SUPPORT;
			return 0;
		case 0x3C000000:
			if(ICVerID == 0x00000000) {
				tp->mcfg = CFG_METHOD_4;
			} else if(ICVerID == 0x00200000) {
				tp->mcfg = CFG_METHOD_5;
			} else if(ICVerID == 0x00400000) {
				tp->mcfg = CFG_METHOD_6;
			} else {
				tp->mcfg = CFG_METHOD_6;
			}
			tp->efuse = EFUSE_NOT_SUPPORT;
			return 0;
		case 0x3C800000:
			if (ICVerID == 0x00100000){
				tp->mcfg = CFG_METHOD_7;
			} else if (ICVerID == 0x00300000){
				tp->mcfg = CFG_METHOD_8;
			} else {
				tp->mcfg = CFG_METHOD_8;
			}
			tp->efuse = EFUSE_NOT_SUPPORT;
			return 0;
		case 0x28000000:
			if(ICVerID == 0x00100000) {
				tp->mcfg = CFG_METHOD_9;
			} else if(ICVerID == 0x00300000) {
				tp->mcfg = CFG_METHOD_10;
			} else {
				tp->mcfg = CFG_METHOD_10;
			}
			tp->efuse = EFUSE_SUPPORT;
			return 0;
		case 0x28800000:
			if(ICVerID == 0x00000000)
				tp->mcfg = CFG_METHOD_11;
			else if(ICVerID == 0x00200000)
			{
				tp->mcfg = CFG_METHOD_12;
				RTL_W32(0xD0, RTL_R32(0xD0)|0x00020000);
			}
			else// if(ICVerID == 0x00300000)
				tp->mcfg = CFG_METHOD_13;
			tp->efuse = EFUSE_SUPPORT;
			return 0;
		case 0x2C000000:
			if (ICVerID == 0x00100000){
				tp->mcfg = CFG_METHOD_14;
			}
			else if(ICVerID == 0x00200000)
			{
				tp->mcfg = CFG_METHOD_15;
			}
			else
			{
				tp->mcfg = CFG_METHOD_15;
			}
			tp->efuse = EFUSE_SUPPORT;
			return 0;
		default:
			tp->mcfg = CFG_METHOD_UNKNOWN;
			printk("unknown chip version (%x)\n",reg);
			return -1;
	}
}

static void
rtl8168_print_mac_version(struct rtl8168_private *tp)
{
	int i;
	for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--) {
		if (tp->mcfg == rtl_chip_info[i].mcfg){
			dprintk("mcfg == %s (%04d)\n", rtl_chip_info[i].name,
				  rtl_chip_info[i].mcfg);
			return;
		}
	}

	dprintk("mac_version == Unknown\n");
}

static u8
rtl8168_efuse_read(struct net_device *dev, u16 reg)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	u8 efuse_data;
	u32 temp;
	int cnt;

	if (tp->efuse == EFUSE_NOT_SUPPORT)
		return EFUSE_READ_FAIL;

	temp = EFUSE_READ | ((reg & EFUSE_Reg_Mask) << EFUSE_Reg_Shift);
	RTL_W32(EFUSEAR, temp);

	do {
		udelay(100);
		temp = RTL_R32(EFUSEAR);
		cnt++;
	} while (!(temp & EFUSE_READ_OK) && (temp < EFUSE_Check_Cnt));

	if (temp == EFUSE_Check_Cnt)
		efuse_data = EFUSE_READ_FAIL;
	else
		efuse_data = (u8)(RTL_R32(EFUSEAR) & EFUSE_Data_Mask);

	return efuse_data;
}

static void
rtl8168_hw_phy_config(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	unsigned int gphy_val,i;

	spin_lock_irqsave(&tp->phy_lock, flags);

	if (tp->mcfg == CFG_METHOD_1) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x0B, 0x94B0);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0x6096);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x0D, 0xF8A0);
	} else if (tp->mcfg == CFG_METHOD_2) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x0B, 0x94B0);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0x6096);

		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_3) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x0B, 0x94B0);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0x6096);

		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_4) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x12, 0x2300);
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x16, 0x000A);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0xC096);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x00, 0x88DE);
		mdio_write(tp, 0x01, 0x82B1);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x08, 0x9E30);
		mdio_write(tp, 0x09, 0x01F0);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0A, 0x5500);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x03, 0x7002);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0C, 0x00C8);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x14, mdio_read(tp, 0x14) | (1 << 5));
		mdio_write(tp, 0x0D, mdio_read(tp, 0x0D) & ~(1 << 5));
	} else if (tp->mcfg == CFG_METHOD_5) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x12, 0x2300);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x16, 0x0F0A);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x00, 0x88DE);
		mdio_write(tp, 0x01, 0x82B1);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0C, 0x7EB8);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x06, 0x0761);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x03, 0x802F);
		mdio_write(tp, 0x02, 0x4F02);
		mdio_write(tp, 0x01, 0x0409);
		mdio_write(tp, 0x00, 0xF099);
		mdio_write(tp, 0x04, 0x9800);
		mdio_write(tp, 0x04, 0x9000);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x16, mdio_read(tp, 0x16) | (1 << 0));

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x14, mdio_read(tp, 0x14) | (1 << 5));
		mdio_write(tp, 0x0D, mdio_read(tp, 0x0D) & ~(1 << 5));

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x1D, 0x3D98);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);
		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_6) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x12, 0x2300);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x16, 0x0F0A);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x00, 0x88DE);
		mdio_write(tp, 0x01, 0x82B1);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0C, 0x7EB8);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x06, 0x0761);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x06, 0x5461);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x16, mdio_read(tp, 0x16) | (1 << 0));

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x14, mdio_read(tp, 0x14) | (1 << 5));
		mdio_write(tp, 0x0D, mdio_read(tp, 0x0D) & ~(1 << 5));

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x1D, 0x3D98);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1f, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);
		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_7) {
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x14, mdio_read(tp, 0x14) | (1 << 5));
		mdio_write(tp, 0x0D, mdio_read(tp, 0x0D) & ~(1 << 5));

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x1D, 0x3D98);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x14, 0xCAA3);
		mdio_write(tp, 0x1C, 0x000A);
		mdio_write(tp, 0x18, 0x65D0);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x17, 0xB580);
		mdio_write(tp, 0x18, 0xFF54);
		mdio_write(tp, 0x19, 0x3954);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0D, 0x310C);
		mdio_write(tp, 0x0E, 0x310C);
		mdio_write(tp, 0x0F, 0x311C);
		mdio_write(tp, 0x06, 0x0761);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x18, 0xFF55);
		mdio_write(tp, 0x19, 0x3955);
		mdio_write(tp, 0x18, 0xFF54);
		mdio_write(tp, 0x19, 0x3954);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);

		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_8) {
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x14, mdio_read(tp, 0x14) | (1 << 5));
		mdio_write(tp, 0x0D, mdio_read(tp, 0x0D) & ~(1 << 5));

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x14, 0xCAA3);
		mdio_write(tp, 0x1C, 0x000A);
		mdio_write(tp, 0x18, 0x65D0);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x17, 0xB580);
		mdio_write(tp, 0x18, 0xFF54);
		mdio_write(tp, 0x19, 0x3954);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x0D, 0x310C);
		mdio_write(tp, 0x0E, 0x310C);
		mdio_write(tp, 0x0F, 0x311C);
		mdio_write(tp, 0x06, 0x0761);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x18, 0xFF55);
		mdio_write(tp, 0x19, 0x3955);
		mdio_write(tp, 0x18, 0xFF54);
		mdio_write(tp, 0x19, 0x3954);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);

		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x16, mdio_read(tp, 0x16) | (1 << 0));

		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_9) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x06, 0x4064);
		mdio_write(tp, 0x07, 0x2863);
		mdio_write(tp, 0x08, 0x059C);
		mdio_write(tp, 0x09, 0x26B4);
		mdio_write(tp, 0x0A, 0x6A19);
		mdio_write(tp, 0x0B, 0xDCC8);
		mdio_write(tp, 0x10, 0xF06D);
		mdio_write(tp, 0x14, 0x7F68);
		mdio_write(tp, 0x18, 0x7FD9);
		mdio_write(tp, 0x1C, 0xF0FF);
		mdio_write(tp, 0x1D, 0x3D9C);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0xF49F);
		mdio_write(tp, 0x13, 0x070B);
		mdio_write(tp, 0x1A, 0x05AD);
		mdio_write(tp, 0x14, 0x94C0);

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x0B) & 0xFF00;
		gphy_val |= 0x10;
		mdio_write(tp, 0x0B, gphy_val);
		gphy_val = mdio_read(tp, 0x0C) & 0x00FF;
		gphy_val |= 0xA200;
		mdio_write(tp, 0x0C, gphy_val);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x06, 0x5561);
		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x05, 0x8332);
		mdio_write(tp, 0x06, 0x5561);

		if (rtl8168_efuse_read(dev, 0x01) == 0xb1) {
			mdio_write(tp, 0x1F, 0x0002);
			mdio_write(tp, 0x05, 0x669A);
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x05, 0x8330);
			mdio_write(tp, 0x06, 0x669A);

			mdio_write(tp, 0x1F, 0x0002);
			gphy_val = mdio_read(tp, 0x0D);
			if ((gphy_val & 0x00FF) != 0x006C) {
				gphy_val &= 0xFF00;
				mdio_write(tp, 0x1F, 0x0002);
				mdio_write(tp, 0x0D, gphy_val | 0x0065);
				mdio_write(tp, 0x0D, gphy_val | 0x0066);
				mdio_write(tp, 0x0D, gphy_val | 0x0067);
				mdio_write(tp, 0x0D, gphy_val | 0x0068);
				mdio_write(tp, 0x0D, gphy_val | 0x0069);
				mdio_write(tp, 0x0D, gphy_val | 0x006A);
				mdio_write(tp, 0x0D, gphy_val | 0x006B);
				mdio_write(tp, 0x0D, gphy_val | 0x006C);
			}
		} else {
			mdio_write(tp, 0x1F, 0x0002);
			mdio_write(tp, 0x05, 0x6662);
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x05, 0x8330);
			mdio_write(tp, 0x06, 0x6662);
		}

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x0D);
		gphy_val |= BIT_9;
		gphy_val |= BIT_8;
		mdio_write(tp, 0x0D, gphy_val);
		gphy_val = mdio_read(tp, 0x0F);
		gphy_val |= BIT_4;
		mdio_write(tp, 0x0F, gphy_val);

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x02);
		gphy_val &= ~BIT_10;
		gphy_val &= ~BIT_9;
		gphy_val |= BIT_8;
		mdio_write(tp, 0x02, gphy_val);
		gphy_val = mdio_read(tp, 0x03);
		gphy_val &= ~BIT_15;
		gphy_val &= ~BIT_14;
		gphy_val &= ~BIT_13;
		mdio_write(tp, 0x03, gphy_val);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x05, 0x001B);
		if (mdio_read(tp, 0x06) == 0xBF00) {
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			mdio_write(tp, 0x05, 0x8000);
			mdio_write(tp, 0x06, 0xf8f9);
			mdio_write(tp, 0x06, 0xfaef);
			mdio_write(tp, 0x06, 0x59ee);
			mdio_write(tp, 0x06, 0xf8ea);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xf8eb);
			mdio_write(tp, 0x06, 0x00e0);
			mdio_write(tp, 0x06, 0xf87c);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x7d59);
			mdio_write(tp, 0x06, 0x0fef);
			mdio_write(tp, 0x06, 0x0139);
			mdio_write(tp, 0x06, 0x029e);
			mdio_write(tp, 0x06, 0x06ef);
			mdio_write(tp, 0x06, 0x1039);
			mdio_write(tp, 0x06, 0x089f);
			mdio_write(tp, 0x06, 0x2aee);
			mdio_write(tp, 0x06, 0xf8ea);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xf8eb);
			mdio_write(tp, 0x06, 0x01e0);
			mdio_write(tp, 0x06, 0xf87c);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x7d58);
			mdio_write(tp, 0x06, 0x409e);
			mdio_write(tp, 0x06, 0x0f39);
			mdio_write(tp, 0x06, 0x46aa);
			mdio_write(tp, 0x06, 0x0bbf);
			mdio_write(tp, 0x06, 0x8290);
			mdio_write(tp, 0x06, 0xd682);
			mdio_write(tp, 0x06, 0x9802);
			mdio_write(tp, 0x06, 0x014f);
			mdio_write(tp, 0x06, 0xae09);
			mdio_write(tp, 0x06, 0xbf82);
			mdio_write(tp, 0x06, 0x98d6);
			mdio_write(tp, 0x06, 0x82a0);
			mdio_write(tp, 0x06, 0x0201);
			mdio_write(tp, 0x06, 0x4fef);
			mdio_write(tp, 0x06, 0x95fe);
			mdio_write(tp, 0x06, 0xfdfc);
			mdio_write(tp, 0x06, 0x05f8);
			mdio_write(tp, 0x06, 0xf9fa);
			mdio_write(tp, 0x06, 0xeef8);
			mdio_write(tp, 0x06, 0xea00);
			mdio_write(tp, 0x06, 0xeef8);
			mdio_write(tp, 0x06, 0xeb00);
			mdio_write(tp, 0x06, 0xe2f8);
			mdio_write(tp, 0x06, 0x7ce3);
			mdio_write(tp, 0x06, 0xf87d);
			mdio_write(tp, 0x06, 0xa511);
			mdio_write(tp, 0x06, 0x1112);
			mdio_write(tp, 0x06, 0xd240);
			mdio_write(tp, 0x06, 0xd644);
			mdio_write(tp, 0x06, 0x4402);
			mdio_write(tp, 0x06, 0x8217);
			mdio_write(tp, 0x06, 0xd2a0);
			mdio_write(tp, 0x06, 0xd6aa);
			mdio_write(tp, 0x06, 0xaa02);
			mdio_write(tp, 0x06, 0x8217);
			mdio_write(tp, 0x06, 0xae0f);
			mdio_write(tp, 0x06, 0xa544);
			mdio_write(tp, 0x06, 0x4402);
			mdio_write(tp, 0x06, 0xae4d);
			mdio_write(tp, 0x06, 0xa5aa);
			mdio_write(tp, 0x06, 0xaa02);
			mdio_write(tp, 0x06, 0xae47);
			mdio_write(tp, 0x06, 0xaf82);
			mdio_write(tp, 0x06, 0x13ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0x0fee);
			mdio_write(tp, 0x06, 0x834c);
			mdio_write(tp, 0x06, 0x0fee);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0x8351);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0x834a);
			mdio_write(tp, 0x06, 0xffee);
			mdio_write(tp, 0x06, 0x834b);
			mdio_write(tp, 0x06, 0xffe0);
			mdio_write(tp, 0x06, 0x8330);
			mdio_write(tp, 0x06, 0xe183);
			mdio_write(tp, 0x06, 0x3158);
			mdio_write(tp, 0x06, 0xfee4);
			mdio_write(tp, 0x06, 0xf88a);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x8be0);
			mdio_write(tp, 0x06, 0x8332);
			mdio_write(tp, 0x06, 0xe183);
			mdio_write(tp, 0x06, 0x3359);
			mdio_write(tp, 0x06, 0x0fe2);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0x0c24);
			mdio_write(tp, 0x06, 0x5af0);
			mdio_write(tp, 0x06, 0x1e12);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x8ce5);
			mdio_write(tp, 0x06, 0xf88d);
			mdio_write(tp, 0x06, 0xaf82);
			mdio_write(tp, 0x06, 0x13e0);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0x10e4);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x009f);
			mdio_write(tp, 0x06, 0x0ae0);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0xa010);
			mdio_write(tp, 0x06, 0xa5ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x01e0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7805);
			mdio_write(tp, 0x06, 0x9e9a);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x049e);
			mdio_write(tp, 0x06, 0x10e0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7803);
			mdio_write(tp, 0x06, 0x9e0f);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x019e);
			mdio_write(tp, 0x06, 0x05ae);
			mdio_write(tp, 0x06, 0x0caf);
			mdio_write(tp, 0x06, 0x81f8);
			mdio_write(tp, 0x06, 0xaf81);
			mdio_write(tp, 0x06, 0xa3af);
			mdio_write(tp, 0x06, 0x81dc);
			mdio_write(tp, 0x06, 0xaf82);
			mdio_write(tp, 0x06, 0x13ee);
			mdio_write(tp, 0x06, 0x8348);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0x8349);
			mdio_write(tp, 0x06, 0x00e0);
			mdio_write(tp, 0x06, 0x8351);
			mdio_write(tp, 0x06, 0x10e4);
			mdio_write(tp, 0x06, 0x8351);
			mdio_write(tp, 0x06, 0x5801);
			mdio_write(tp, 0x06, 0x9fea);
			mdio_write(tp, 0x06, 0xd000);
			mdio_write(tp, 0x06, 0xd180);
			mdio_write(tp, 0x06, 0x1f66);
			mdio_write(tp, 0x06, 0xe2f8);
			mdio_write(tp, 0x06, 0xeae3);
			mdio_write(tp, 0x06, 0xf8eb);
			mdio_write(tp, 0x06, 0x5af8);
			mdio_write(tp, 0x06, 0x1e20);
			mdio_write(tp, 0x06, 0xe6f8);
			mdio_write(tp, 0x06, 0xeae5);
			mdio_write(tp, 0x06, 0xf8eb);
			mdio_write(tp, 0x06, 0xd302);
			mdio_write(tp, 0x06, 0xb3fe);
			mdio_write(tp, 0x06, 0xe2f8);
			mdio_write(tp, 0x06, 0x7cef);
			mdio_write(tp, 0x06, 0x325b);
			mdio_write(tp, 0x06, 0x80e3);
			mdio_write(tp, 0x06, 0xf87d);
			mdio_write(tp, 0x06, 0x9e03);
			mdio_write(tp, 0x06, 0x7dff);
			mdio_write(tp, 0x06, 0xff0d);
			mdio_write(tp, 0x06, 0x581c);
			mdio_write(tp, 0x06, 0x551a);
			mdio_write(tp, 0x06, 0x6511);
			mdio_write(tp, 0x06, 0xa190);
			mdio_write(tp, 0x06, 0xd3e2);
			mdio_write(tp, 0x06, 0x8348);
			mdio_write(tp, 0x06, 0xe383);
			mdio_write(tp, 0x06, 0x491b);
			mdio_write(tp, 0x06, 0x56ab);
			mdio_write(tp, 0x06, 0x08ef);
			mdio_write(tp, 0x06, 0x56e6);
			mdio_write(tp, 0x06, 0x8348);
			mdio_write(tp, 0x06, 0xe783);
			mdio_write(tp, 0x06, 0x4910);
			mdio_write(tp, 0x06, 0xd180);
			mdio_write(tp, 0x06, 0x1f66);
			mdio_write(tp, 0x06, 0xa004);
			mdio_write(tp, 0x06, 0xb9e2);
			mdio_write(tp, 0x06, 0x8348);
			mdio_write(tp, 0x06, 0xe383);
			mdio_write(tp, 0x06, 0x49ef);
			mdio_write(tp, 0x06, 0x65e2);
			mdio_write(tp, 0x06, 0x834a);
			mdio_write(tp, 0x06, 0xe383);
			mdio_write(tp, 0x06, 0x4b1b);
			mdio_write(tp, 0x06, 0x56aa);
			mdio_write(tp, 0x06, 0x0eef);
			mdio_write(tp, 0x06, 0x56e6);
			mdio_write(tp, 0x06, 0x834a);
			mdio_write(tp, 0x06, 0xe783);
			mdio_write(tp, 0x06, 0x4be2);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0xe683);
			mdio_write(tp, 0x06, 0x4ce0);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0xa000);
			mdio_write(tp, 0x06, 0x0caf);
			mdio_write(tp, 0x06, 0x81dc);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4d10);
			mdio_write(tp, 0x06, 0xe483);
			mdio_write(tp, 0x06, 0x4dae);
			mdio_write(tp, 0x06, 0x0480);
			mdio_write(tp, 0x06, 0xe483);
			mdio_write(tp, 0x06, 0x4de0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7803);
			mdio_write(tp, 0x06, 0x9e0b);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x049e);
			mdio_write(tp, 0x06, 0x04ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x02e0);
			mdio_write(tp, 0x06, 0x8332);
			mdio_write(tp, 0x06, 0xe183);
			mdio_write(tp, 0x06, 0x3359);
			mdio_write(tp, 0x06, 0x0fe2);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0x0c24);
			mdio_write(tp, 0x06, 0x5af0);
			mdio_write(tp, 0x06, 0x1e12);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x8ce5);
			mdio_write(tp, 0x06, 0xf88d);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x30e1);
			mdio_write(tp, 0x06, 0x8331);
			mdio_write(tp, 0x06, 0x6801);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x8ae5);
			mdio_write(tp, 0x06, 0xf88b);
			mdio_write(tp, 0x06, 0xae37);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e03);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4ce1);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0x1b01);
			mdio_write(tp, 0x06, 0x9e04);
			mdio_write(tp, 0x06, 0xaaa1);
			mdio_write(tp, 0x06, 0xaea8);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e04);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4f00);
			mdio_write(tp, 0x06, 0xaeab);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4f78);
			mdio_write(tp, 0x06, 0x039f);
			mdio_write(tp, 0x06, 0x14ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x05d2);
			mdio_write(tp, 0x06, 0x40d6);
			mdio_write(tp, 0x06, 0x5554);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x17d2);
			mdio_write(tp, 0x06, 0xa0d6);
			mdio_write(tp, 0x06, 0xba00);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x17fe);
			mdio_write(tp, 0x06, 0xfdfc);
			mdio_write(tp, 0x06, 0x05f8);
			mdio_write(tp, 0x06, 0xe0f8);
			mdio_write(tp, 0x06, 0x60e1);
			mdio_write(tp, 0x06, 0xf861);
			mdio_write(tp, 0x06, 0x6802);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x60e5);
			mdio_write(tp, 0x06, 0xf861);
			mdio_write(tp, 0x06, 0xe0f8);
			mdio_write(tp, 0x06, 0x48e1);
			mdio_write(tp, 0x06, 0xf849);
			mdio_write(tp, 0x06, 0x580f);
			mdio_write(tp, 0x06, 0x1e02);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x48e5);
			mdio_write(tp, 0x06, 0xf849);
			mdio_write(tp, 0x06, 0xd000);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x5bbf);
			mdio_write(tp, 0x06, 0x8350);
			mdio_write(tp, 0x06, 0xef46);
			mdio_write(tp, 0x06, 0xdc19);
			mdio_write(tp, 0x06, 0xddd0);
			mdio_write(tp, 0x06, 0x0102);
			mdio_write(tp, 0x06, 0x825b);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x77e0);
			mdio_write(tp, 0x06, 0xf860);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x6158);
			mdio_write(tp, 0x06, 0xfde4);
			mdio_write(tp, 0x06, 0xf860);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x61fc);
			mdio_write(tp, 0x06, 0x04f9);
			mdio_write(tp, 0x06, 0xfafb);
			mdio_write(tp, 0x06, 0xc6bf);
			mdio_write(tp, 0x06, 0xf840);
			mdio_write(tp, 0x06, 0xbe83);
			mdio_write(tp, 0x06, 0x50a0);
			mdio_write(tp, 0x06, 0x0101);
			mdio_write(tp, 0x06, 0x071b);
			mdio_write(tp, 0x06, 0x89cf);
			mdio_write(tp, 0x06, 0xd208);
			mdio_write(tp, 0x06, 0xebdb);
			mdio_write(tp, 0x06, 0x19b2);
			mdio_write(tp, 0x06, 0xfbff);
			mdio_write(tp, 0x06, 0xfefd);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xe0f8);
			mdio_write(tp, 0x06, 0x48e1);
			mdio_write(tp, 0x06, 0xf849);
			mdio_write(tp, 0x06, 0x6808);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x48e5);
			mdio_write(tp, 0x06, 0xf849);
			mdio_write(tp, 0x06, 0x58f7);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x48e5);
			mdio_write(tp, 0x06, 0xf849);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0x4d20);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x4e22);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x4ddf);
			mdio_write(tp, 0x06, 0xff01);
			mdio_write(tp, 0x06, 0x4edd);
			mdio_write(tp, 0x06, 0xff01);
			mdio_write(tp, 0x06, 0xf8fa);
			mdio_write(tp, 0x06, 0xfbef);
			mdio_write(tp, 0x06, 0x79bf);
			mdio_write(tp, 0x06, 0xf822);
			mdio_write(tp, 0x06, 0xd819);
			mdio_write(tp, 0x06, 0xd958);
			mdio_write(tp, 0x06, 0x849f);
			mdio_write(tp, 0x06, 0x09bf);
			mdio_write(tp, 0x06, 0x82be);
			mdio_write(tp, 0x06, 0xd682);
			mdio_write(tp, 0x06, 0xc602);
			mdio_write(tp, 0x06, 0x014f);
			mdio_write(tp, 0x06, 0xef97);
			mdio_write(tp, 0x06, 0xfffe);
			mdio_write(tp, 0x06, 0xfc05);
			mdio_write(tp, 0x06, 0x17ff);
			mdio_write(tp, 0x06, 0xfe01);
			mdio_write(tp, 0x06, 0x1700);
			mdio_write(tp, 0x06, 0x0102);
			mdio_write(tp, 0x05, 0x83d8);
			mdio_write(tp, 0x06, 0x8051);
			mdio_write(tp, 0x05, 0x83d6);
			mdio_write(tp, 0x06, 0x82a0);
			mdio_write(tp, 0x05, 0x83d4);
			mdio_write(tp, 0x06, 0x8000);
			mdio_write(tp, 0x02, 0x2010);
			mdio_write(tp, 0x03, 0xdc00);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x0b, 0x0600);
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x00fc);
			mdio_write(tp, 0x1f, 0x0000);
		}
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x0D, 0xF880);
		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_10) {
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x06, 0x4064);
		mdio_write(tp, 0x07, 0x2863);
		mdio_write(tp, 0x08, 0x059C);
		mdio_write(tp, 0x09, 0x26B4);
		mdio_write(tp, 0x0A, 0x6A19);
		mdio_write(tp, 0x0B, 0xDCC8);
		mdio_write(tp, 0x10, 0xF06D);
		mdio_write(tp, 0x14, 0x7F68);
		mdio_write(tp, 0x18, 0x7FD9);
		mdio_write(tp, 0x1C, 0xF0FF);
		mdio_write(tp, 0x1D, 0x3D9C);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x12, 0xF49F);
		mdio_write(tp, 0x13, 0x070B);
		mdio_write(tp, 0x1A, 0x05AD);
		mdio_write(tp, 0x14, 0x94C0);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x06, 0x5561);
		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x05, 0x8332);
		mdio_write(tp, 0x06, 0x5561);

		if (rtl8168_efuse_read(dev, 0x01) == 0xb1) {
			mdio_write(tp, 0x1F, 0x0002);
			mdio_write(tp, 0x05, 0x669A);
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x05, 0x8330);
			mdio_write(tp, 0x06, 0x669A);

			mdio_write(tp, 0x1F, 0x0002);
			gphy_val = mdio_read(tp, 0x0D);
			if ((gphy_val & 0x00FF) != 0x006C) {
				gphy_val &= 0xFF00;
				mdio_write(tp, 0x1F, 0x0002);
				mdio_write(tp, 0x0D, gphy_val | 0x0065);
				mdio_write(tp, 0x0D, gphy_val | 0x0066);
				mdio_write(tp, 0x0D, gphy_val | 0x0067);
				mdio_write(tp, 0x0D, gphy_val | 0x0068);
				mdio_write(tp, 0x0D, gphy_val | 0x0069);
				mdio_write(tp, 0x0D, gphy_val | 0x006A);
				mdio_write(tp, 0x0D, gphy_val | 0x006B);
				mdio_write(tp, 0x0D, gphy_val | 0x006C);
			}
		} else {
			mdio_write(tp, 0x1F, 0x0002);
			mdio_write(tp, 0x05, 0x2642);
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x05, 0x8330);
			mdio_write(tp, 0x06, 0x2642);
		}

		if (rtl8168_efuse_read(dev, 0x30) == 0x98) {
			mdio_write(tp, 0x1F, 0x0000);
			mdio_write(tp, 0x11, mdio_read(tp, 0x11) & ~BIT_1);
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x01, mdio_read(tp, 0x01) | BIT_9);
		} else if (rtl8168_efuse_read(dev, 0x30) == 0x90) {
			mdio_write(tp, 0x1F, 0x0005);
			mdio_write(tp, 0x01, mdio_read(tp, 0x01) & ~BIT_9);
			mdio_write(tp, 0x1F, 0x0000);
			mdio_write(tp, 0x16, 0x5101);
		}

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x02);
		gphy_val &= ~BIT_10;
		gphy_val &= ~BIT_9;
		gphy_val |= BIT_8;
		mdio_write(tp, 0x02, gphy_val);
		gphy_val = mdio_read(tp, 0x03);
		gphy_val &= ~BIT_15;
		gphy_val &= ~BIT_14;
		gphy_val &= ~BIT_13;
		mdio_write(tp, 0x03, gphy_val);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x0F);
		gphy_val |= BIT_4;
		gphy_val |= BIT_2;
		gphy_val |= BIT_1;
		gphy_val |= BIT_0;
		mdio_write(tp, 0x0F, gphy_val);
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x05, 0x001B);
		if (mdio_read(tp, 0x06) == 0xB300) {
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			mdio_write(tp, 0x05, 0x8000);
			mdio_write(tp, 0x06, 0xf8f9);
			mdio_write(tp, 0x06, 0xfaee);
			mdio_write(tp, 0x06, 0xf8ea);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xf8eb);
			mdio_write(tp, 0x06, 0x00e2);
			mdio_write(tp, 0x06, 0xf87c);
			mdio_write(tp, 0x06, 0xe3f8);
			mdio_write(tp, 0x06, 0x7da5);
			mdio_write(tp, 0x06, 0x1111);
			mdio_write(tp, 0x06, 0x12d2);
			mdio_write(tp, 0x06, 0x40d6);
			mdio_write(tp, 0x06, 0x4444);
			mdio_write(tp, 0x06, 0x0281);
			mdio_write(tp, 0x06, 0xc6d2);
			mdio_write(tp, 0x06, 0xa0d6);
			mdio_write(tp, 0x06, 0xaaaa);
			mdio_write(tp, 0x06, 0x0281);
			mdio_write(tp, 0x06, 0xc6ae);
			mdio_write(tp, 0x06, 0x0fa5);
			mdio_write(tp, 0x06, 0x4444);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x4da5);
			mdio_write(tp, 0x06, 0xaaaa);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x47af);
			mdio_write(tp, 0x06, 0x81c2);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e00);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4d0f);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4c0f);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4f00);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x5100);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4aff);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4bff);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x30e1);
			mdio_write(tp, 0x06, 0x8331);
			mdio_write(tp, 0x06, 0x58fe);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x8ae5);
			mdio_write(tp, 0x06, 0xf88b);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x32e1);
			mdio_write(tp, 0x06, 0x8333);
			mdio_write(tp, 0x06, 0x590f);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x4d0c);
			mdio_write(tp, 0x06, 0x245a);
			mdio_write(tp, 0x06, 0xf01e);
			mdio_write(tp, 0x06, 0x12e4);
			mdio_write(tp, 0x06, 0xf88c);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x8daf);
			mdio_write(tp, 0x06, 0x81c2);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4f10);
			mdio_write(tp, 0x06, 0xe483);
			mdio_write(tp, 0x06, 0x4fe0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7800);
			mdio_write(tp, 0x06, 0x9f0a);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4fa0);
			mdio_write(tp, 0x06, 0x10a5);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e01);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x059e);
			mdio_write(tp, 0x06, 0x9ae0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7804);
			mdio_write(tp, 0x06, 0x9e10);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x039e);
			mdio_write(tp, 0x06, 0x0fe0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7801);
			mdio_write(tp, 0x06, 0x9e05);
			mdio_write(tp, 0x06, 0xae0c);
			mdio_write(tp, 0x06, 0xaf81);
			mdio_write(tp, 0x06, 0xa7af);
			mdio_write(tp, 0x06, 0x8152);
			mdio_write(tp, 0x06, 0xaf81);
			mdio_write(tp, 0x06, 0x8baf);
			mdio_write(tp, 0x06, 0x81c2);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4800);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4900);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x5110);
			mdio_write(tp, 0x06, 0xe483);
			mdio_write(tp, 0x06, 0x5158);
			mdio_write(tp, 0x06, 0x019f);
			mdio_write(tp, 0x06, 0xead0);
			mdio_write(tp, 0x06, 0x00d1);
			mdio_write(tp, 0x06, 0x801f);
			mdio_write(tp, 0x06, 0x66e2);
			mdio_write(tp, 0x06, 0xf8ea);
			mdio_write(tp, 0x06, 0xe3f8);
			mdio_write(tp, 0x06, 0xeb5a);
			mdio_write(tp, 0x06, 0xf81e);
			mdio_write(tp, 0x06, 0x20e6);
			mdio_write(tp, 0x06, 0xf8ea);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0xebd3);
			mdio_write(tp, 0x06, 0x02b3);
			mdio_write(tp, 0x06, 0xfee2);
			mdio_write(tp, 0x06, 0xf87c);
			mdio_write(tp, 0x06, 0xef32);
			mdio_write(tp, 0x06, 0x5b80);
			mdio_write(tp, 0x06, 0xe3f8);
			mdio_write(tp, 0x06, 0x7d9e);
			mdio_write(tp, 0x06, 0x037d);
			mdio_write(tp, 0x06, 0xffff);
			mdio_write(tp, 0x06, 0x0d58);
			mdio_write(tp, 0x06, 0x1c55);
			mdio_write(tp, 0x06, 0x1a65);
			mdio_write(tp, 0x06, 0x11a1);
			mdio_write(tp, 0x06, 0x90d3);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x48e3);
			mdio_write(tp, 0x06, 0x8349);
			mdio_write(tp, 0x06, 0x1b56);
			mdio_write(tp, 0x06, 0xab08);
			mdio_write(tp, 0x06, 0xef56);
			mdio_write(tp, 0x06, 0xe683);
			mdio_write(tp, 0x06, 0x48e7);
			mdio_write(tp, 0x06, 0x8349);
			mdio_write(tp, 0x06, 0x10d1);
			mdio_write(tp, 0x06, 0x801f);
			mdio_write(tp, 0x06, 0x66a0);
			mdio_write(tp, 0x06, 0x04b9);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x48e3);
			mdio_write(tp, 0x06, 0x8349);
			mdio_write(tp, 0x06, 0xef65);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x4ae3);
			mdio_write(tp, 0x06, 0x834b);
			mdio_write(tp, 0x06, 0x1b56);
			mdio_write(tp, 0x06, 0xaa0e);
			mdio_write(tp, 0x06, 0xef56);
			mdio_write(tp, 0x06, 0xe683);
			mdio_write(tp, 0x06, 0x4ae7);
			mdio_write(tp, 0x06, 0x834b);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x4de6);
			mdio_write(tp, 0x06, 0x834c);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4da0);
			mdio_write(tp, 0x06, 0x000c);
			mdio_write(tp, 0x06, 0xaf81);
			mdio_write(tp, 0x06, 0x8be0);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0x10e4);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0xae04);
			mdio_write(tp, 0x06, 0x80e4);
			mdio_write(tp, 0x06, 0x834d);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x4e78);
			mdio_write(tp, 0x06, 0x039e);
			mdio_write(tp, 0x06, 0x0be0);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x7804);
			mdio_write(tp, 0x06, 0x9e04);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e02);
			mdio_write(tp, 0x06, 0xe083);
			mdio_write(tp, 0x06, 0x32e1);
			mdio_write(tp, 0x06, 0x8333);
			mdio_write(tp, 0x06, 0x590f);
			mdio_write(tp, 0x06, 0xe283);
			mdio_write(tp, 0x06, 0x4d0c);
			mdio_write(tp, 0x06, 0x245a);
			mdio_write(tp, 0x06, 0xf01e);
			mdio_write(tp, 0x06, 0x12e4);
			mdio_write(tp, 0x06, 0xf88c);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x8de0);
			mdio_write(tp, 0x06, 0x8330);
			mdio_write(tp, 0x06, 0xe183);
			mdio_write(tp, 0x06, 0x3168);
			mdio_write(tp, 0x06, 0x01e4);
			mdio_write(tp, 0x06, 0xf88a);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x8bae);
			mdio_write(tp, 0x06, 0x37ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x03e0);
			mdio_write(tp, 0x06, 0x834c);
			mdio_write(tp, 0x06, 0xe183);
			mdio_write(tp, 0x06, 0x4d1b);
			mdio_write(tp, 0x06, 0x019e);
			mdio_write(tp, 0x06, 0x04aa);
			mdio_write(tp, 0x06, 0xa1ae);
			mdio_write(tp, 0x06, 0xa8ee);
			mdio_write(tp, 0x06, 0x834e);
			mdio_write(tp, 0x06, 0x04ee);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0x00ae);
			mdio_write(tp, 0x06, 0xabe0);
			mdio_write(tp, 0x06, 0x834f);
			mdio_write(tp, 0x06, 0x7803);
			mdio_write(tp, 0x06, 0x9f14);
			mdio_write(tp, 0x06, 0xee83);
			mdio_write(tp, 0x06, 0x4e05);
			mdio_write(tp, 0x06, 0xd240);
			mdio_write(tp, 0x06, 0xd655);
			mdio_write(tp, 0x06, 0x5402);
			mdio_write(tp, 0x06, 0x81c6);
			mdio_write(tp, 0x06, 0xd2a0);
			mdio_write(tp, 0x06, 0xd6ba);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x81c6);
			mdio_write(tp, 0x06, 0xfefd);
			mdio_write(tp, 0x06, 0xfc05);
			mdio_write(tp, 0x06, 0xf8e0);
			mdio_write(tp, 0x06, 0xf860);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x6168);
			mdio_write(tp, 0x06, 0x02e4);
			mdio_write(tp, 0x06, 0xf860);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x61e0);
			mdio_write(tp, 0x06, 0xf848);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x4958);
			mdio_write(tp, 0x06, 0x0f1e);
			mdio_write(tp, 0x06, 0x02e4);
			mdio_write(tp, 0x06, 0xf848);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x49d0);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x820a);
			mdio_write(tp, 0x06, 0xbf83);
			mdio_write(tp, 0x06, 0x50ef);
			mdio_write(tp, 0x06, 0x46dc);
			mdio_write(tp, 0x06, 0x19dd);
			mdio_write(tp, 0x06, 0xd001);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x0a02);
			mdio_write(tp, 0x06, 0x8226);
			mdio_write(tp, 0x06, 0xe0f8);
			mdio_write(tp, 0x06, 0x60e1);
			mdio_write(tp, 0x06, 0xf861);
			mdio_write(tp, 0x06, 0x58fd);
			mdio_write(tp, 0x06, 0xe4f8);
			mdio_write(tp, 0x06, 0x60e5);
			mdio_write(tp, 0x06, 0xf861);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0xf9fa);
			mdio_write(tp, 0x06, 0xfbc6);
			mdio_write(tp, 0x06, 0xbff8);
			mdio_write(tp, 0x06, 0x40be);
			mdio_write(tp, 0x06, 0x8350);
			mdio_write(tp, 0x06, 0xa001);
			mdio_write(tp, 0x06, 0x0107);
			mdio_write(tp, 0x06, 0x1b89);
			mdio_write(tp, 0x06, 0xcfd2);
			mdio_write(tp, 0x06, 0x08eb);
			mdio_write(tp, 0x06, 0xdb19);
			mdio_write(tp, 0x06, 0xb2fb);
			mdio_write(tp, 0x06, 0xfffe);
			mdio_write(tp, 0x06, 0xfd04);
			mdio_write(tp, 0x06, 0xf8e0);
			mdio_write(tp, 0x06, 0xf848);
			mdio_write(tp, 0x06, 0xe1f8);
			mdio_write(tp, 0x06, 0x4968);
			mdio_write(tp, 0x06, 0x08e4);
			mdio_write(tp, 0x06, 0xf848);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x4958);
			mdio_write(tp, 0x06, 0xf7e4);
			mdio_write(tp, 0x06, 0xf848);
			mdio_write(tp, 0x06, 0xe5f8);
			mdio_write(tp, 0x06, 0x49fc);
			mdio_write(tp, 0x06, 0x044d);
			mdio_write(tp, 0x06, 0x2000);
			mdio_write(tp, 0x06, 0x024e);
			mdio_write(tp, 0x06, 0x2200);
			mdio_write(tp, 0x06, 0x024d);
			mdio_write(tp, 0x06, 0xdfff);
			mdio_write(tp, 0x06, 0x014e);
			mdio_write(tp, 0x06, 0xddff);
			mdio_write(tp, 0x06, 0x01f8);
			mdio_write(tp, 0x06, 0xfafb);
			mdio_write(tp, 0x06, 0xef79);
			mdio_write(tp, 0x06, 0xbff8);
			mdio_write(tp, 0x06, 0x22d8);
			mdio_write(tp, 0x06, 0x19d9);
			mdio_write(tp, 0x06, 0x5884);
			mdio_write(tp, 0x06, 0x9f09);
			mdio_write(tp, 0x06, 0xbf82);
			mdio_write(tp, 0x06, 0x6dd6);
			mdio_write(tp, 0x06, 0x8275);
			mdio_write(tp, 0x06, 0x0201);
			mdio_write(tp, 0x06, 0x4fef);
			mdio_write(tp, 0x06, 0x97ff);
			mdio_write(tp, 0x06, 0xfefc);
			mdio_write(tp, 0x06, 0x0517);
			mdio_write(tp, 0x06, 0xfffe);
			mdio_write(tp, 0x06, 0x0117);
			mdio_write(tp, 0x06, 0x0001);
			mdio_write(tp, 0x06, 0x0200);
			mdio_write(tp, 0x05, 0x83d8);
			mdio_write(tp, 0x06, 0x8000);
			mdio_write(tp, 0x05, 0x83d6);
			mdio_write(tp, 0x06, 0x824f);
			mdio_write(tp, 0x02, 0x2010);
			mdio_write(tp, 0x03, 0xdc00);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x0b, 0x0600);
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x00fc);
			mdio_write(tp, 0x1f, 0x0000);
		}
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x0D, 0xF880);
		mdio_write(tp, 0x1F, 0x0000);
	} else if (tp->mcfg == CFG_METHOD_11) {
		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x10, 0x0008);
		mdio_write(tp, 0x0D, 0x006C);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x0B, 0xA4D8);
		mdio_write(tp, 0x09, 0x281C);
		mdio_write(tp, 0x07, 0x2883);
		mdio_write(tp, 0x0A, 0x6B35);
		mdio_write(tp, 0x1D, 0x3DA4);
		mdio_write(tp, 0x1C, 0xEFFD);
		mdio_write(tp, 0x14, 0x7F52);
		mdio_write(tp, 0x18, 0x7FC6);
		mdio_write(tp, 0x08, 0x0601);
		mdio_write(tp, 0x06, 0x4063);
		mdio_write(tp, 0x10, 0xF074);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x13, 0x0789);
		mdio_write(tp, 0x12, 0xF4BD);
		mdio_write(tp, 0x1A, 0x04FD);
		mdio_write(tp, 0x14, 0x84B0);
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, 0x00, 0x9200);

		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x01, 0x0340);
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x04, 0x4000);
		mdio_write(tp, 0x03, 0x1D21);
		mdio_write(tp, 0x02, 0x0C32);
		mdio_write(tp, 0x01, 0x0200);
		mdio_write(tp, 0x00, 0x5554);
		mdio_write(tp, 0x04, 0x4800);
		mdio_write(tp, 0x04, 0x4000);
		mdio_write(tp, 0x04, 0xF000);
		mdio_write(tp, 0x03, 0xDF01);
		mdio_write(tp, 0x02, 0xDF20);
		mdio_write(tp, 0x01, 0x101A);
		mdio_write(tp, 0x00, 0xA0FF);
		mdio_write(tp, 0x04, 0xF800);
		mdio_write(tp, 0x04, 0xF000);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0007);
		mdio_write(tp, 0x1E, 0x0023);
		mdio_write(tp, 0x16, 0x0000);
		mdio_write(tp, 0x1F, 0x0000);

		gphy_val = mdio_read(tp, 0x0D);
		gphy_val |= BIT_5;
		mdio_write(tp, 0x0D, gphy_val);
	} else if (tp->mcfg == CFG_METHOD_12) {
		// TO DO:
		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x17, 0x0CC0);

		mdio_write(tp, 0x1F, 0x0007);
		mdio_write(tp, 0x1E, 0x002D);
		mdio_write(tp, 0x18, 0x0040);
		mdio_write(tp, 0x1F, 0x0000);

		gphy_val = mdio_read(tp, 0x0D);
		gphy_val |= BIT_5;
		mdio_write(tp, 0x0D, gphy_val);
	} else if (tp->mcfg == CFG_METHOD_13) {
		// TO DO:
	} else if (tp->mcfg == CFG_METHOD_14 || tp->mcfg == CFG_METHOD_15) {
		spin_unlock_irqrestore(&tp->phy_lock, flags);

		RTL_W8(0xF3, RTL_R8(0xF3) | BIT_2);

		if(tp->mcfg == CFG_METHOD_14)
		{
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x00, 0x1800);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x17, 0x0117);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1E, 0x002C);
			mdio_write(tp, 0x1B, 0x5000);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x16, 0x4104);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x1E);
				gphy_val &= 0x03FF;
				if(gphy_val==0x000C)
					break;
			}
			mdio_write(tp, 0x1f, 0x0005);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x07);
				if((gphy_val & BIT_5)==0)
					break;
			}
			gphy_val = mdio_read(tp, 0x07);
			if(gphy_val & BIT_5)
			{
		 		mdio_write(tp, 0x1f, 0x0007);
		 		mdio_write(tp, 0x1e, 0x00a1);
		 		mdio_write(tp, 0x17, 0x1000);
		 		mdio_write(tp, 0x17, 0x0000);
		 		mdio_write(tp, 0x17, 0x2000);
		 		mdio_write(tp, 0x1e, 0x002f);
		 		mdio_write(tp, 0x18, 0x9bfb);
		 		mdio_write(tp, 0x1f, 0x0005);
		 		mdio_write(tp, 0x07, 0x0000);
		 		mdio_write(tp, 0x1f, 0x0000);
			}
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			gphy_val = mdio_read(tp, 0x00);
			gphy_val &= ~(BIT_7);
			mdio_write(tp, 0x00, gphy_val);
			mdio_write(tp, 0x1f, 0x0002);
			gphy_val = mdio_read(tp, 0x08);
			gphy_val &= ~(BIT_7);
			mdio_write(tp, 0x08, gphy_val);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x16, 0x0306);
			mdio_write(tp, 0x16, 0x0307);
			mdio_write(tp, 0x15, 0x000e);
			mdio_write(tp, 0x19, 0x000a);
			mdio_write(tp, 0x15, 0x0010);
			mdio_write(tp, 0x19, 0x0008);
			mdio_write(tp, 0x15, 0x0018);
			mdio_write(tp, 0x19, 0x4801);
			mdio_write(tp, 0x15, 0x0019);
			mdio_write(tp, 0x19, 0x6801);
			mdio_write(tp, 0x15, 0x001a);
			mdio_write(tp, 0x19, 0x66a1);
			mdio_write(tp, 0x15, 0x001f);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0020);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0021);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0022);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0023);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0024);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0025);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x0026);
			mdio_write(tp, 0x19, 0x40ea);
			mdio_write(tp, 0x15, 0x0027);
			mdio_write(tp, 0x19, 0x4503);
			mdio_write(tp, 0x15, 0x0028);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0029);
			mdio_write(tp, 0x19, 0xa631);
			mdio_write(tp, 0x15, 0x002a);
			mdio_write(tp, 0x19, 0x9717);
			mdio_write(tp, 0x15, 0x002b);
			mdio_write(tp, 0x19, 0x302c);
			mdio_write(tp, 0x15, 0x002c);
			mdio_write(tp, 0x19, 0x4802);
			mdio_write(tp, 0x15, 0x002d);
			mdio_write(tp, 0x19, 0x58da);
			mdio_write(tp, 0x15, 0x002e);
			mdio_write(tp, 0x19, 0x400d);
			mdio_write(tp, 0x15, 0x002f);
			mdio_write(tp, 0x19, 0x4488);
			mdio_write(tp, 0x15, 0x0030);
			mdio_write(tp, 0x19, 0x9e00);
			mdio_write(tp, 0x15, 0x0031);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0032);
			mdio_write(tp, 0x19, 0x6481);
			mdio_write(tp, 0x15, 0x0033);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0034);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0035);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0036);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0037);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0038);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0039);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x003a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x003b);
			mdio_write(tp, 0x19, 0x63e8);
			mdio_write(tp, 0x15, 0x003c);
			mdio_write(tp, 0x19, 0x7d00);
			mdio_write(tp, 0x15, 0x003d);
			mdio_write(tp, 0x19, 0x59d4);
			mdio_write(tp, 0x15, 0x003e);
			mdio_write(tp, 0x19, 0x63f8);
			mdio_write(tp, 0x15, 0x0040);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x0041);
			mdio_write(tp, 0x19, 0x30de);
			mdio_write(tp, 0x15, 0x0044);
			mdio_write(tp, 0x19, 0x480f);
			mdio_write(tp, 0x15, 0x0045);
			mdio_write(tp, 0x19, 0x6800);
			mdio_write(tp, 0x15, 0x0046);
			mdio_write(tp, 0x19, 0x6680);
			mdio_write(tp, 0x15, 0x0047);
			mdio_write(tp, 0x19, 0x7c10);
			mdio_write(tp, 0x15, 0x0048);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0049);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004b);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004c);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004d);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004e);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004f);
			mdio_write(tp, 0x19, 0x40ea);
			mdio_write(tp, 0x15, 0x0050);
			mdio_write(tp, 0x19, 0x4503);
			mdio_write(tp, 0x15, 0x0051);
			mdio_write(tp, 0x19, 0x58ca);
			mdio_write(tp, 0x15, 0x0052);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0053);
			mdio_write(tp, 0x19, 0x63d8);
			mdio_write(tp, 0x15, 0x0054);
			mdio_write(tp, 0x19, 0x66a0);
			mdio_write(tp, 0x15, 0x0055);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0056);
			mdio_write(tp, 0x19, 0x3000);
			mdio_write(tp, 0x15, 0x006E);
			mdio_write(tp, 0x19, 0x9afa);
			mdio_write(tp, 0x15, 0x00a1);
			mdio_write(tp, 0x19, 0x3044);
			mdio_write(tp, 0x15, 0x00ab);
			mdio_write(tp, 0x19, 0x5820);
			mdio_write(tp, 0x15, 0x00ac);
			mdio_write(tp, 0x19, 0x5e04);
			mdio_write(tp, 0x15, 0x00ad);
			mdio_write(tp, 0x19, 0xb60c);
			mdio_write(tp, 0x15, 0x00af);
			mdio_write(tp, 0x19, 0x000a);
			mdio_write(tp, 0x15, 0x00b2);
			mdio_write(tp, 0x19, 0x30b9);
			mdio_write(tp, 0x15, 0x00b9);
			mdio_write(tp, 0x19, 0x4408);
			mdio_write(tp, 0x15, 0x00ba);
			mdio_write(tp, 0x19, 0x480b);
			mdio_write(tp, 0x15, 0x00bb);
			mdio_write(tp, 0x19, 0x5e00);
			mdio_write(tp, 0x15, 0x00bc);
			mdio_write(tp, 0x19, 0x405f);
			mdio_write(tp, 0x15, 0x00bd);
			mdio_write(tp, 0x19, 0x4448);
			mdio_write(tp, 0x15, 0x00be);
			mdio_write(tp, 0x19, 0x4020);
			mdio_write(tp, 0x15, 0x00bf);
			mdio_write(tp, 0x19, 0x4468);
			mdio_write(tp, 0x15, 0x00c0);
			mdio_write(tp, 0x19, 0x9c02);
			mdio_write(tp, 0x15, 0x00c1);
			mdio_write(tp, 0x19, 0x58a0);
			mdio_write(tp, 0x15, 0x00c2);
			mdio_write(tp, 0x19, 0xb605);
			mdio_write(tp, 0x15, 0x00c3);
			mdio_write(tp, 0x19, 0xc0d3);
			mdio_write(tp, 0x15, 0x00c4);
			mdio_write(tp, 0x19, 0x00e6);
			mdio_write(tp, 0x15, 0x00c5);
			mdio_write(tp, 0x19, 0xdaec);
			mdio_write(tp, 0x15, 0x00c6);
			mdio_write(tp, 0x19, 0x00fa);
			mdio_write(tp, 0x15, 0x00c7);
			mdio_write(tp, 0x19, 0x9df9);
			mdio_write(tp, 0x15, 0x00c8);
			mdio_write(tp, 0x19, 0x307a);
			mdio_write(tp, 0x15, 0x0112);
			mdio_write(tp, 0x19, 0x6421);
			mdio_write(tp, 0x15, 0x0113);
			mdio_write(tp, 0x19, 0x7c08);
			mdio_write(tp, 0x15, 0x0114);
			mdio_write(tp, 0x19, 0x63f0);
			mdio_write(tp, 0x15, 0x0115);
			mdio_write(tp, 0x19, 0x4003);
			mdio_write(tp, 0x15, 0x0116);
			mdio_write(tp, 0x19, 0x4418);
			mdio_write(tp, 0x15, 0x0117);
			mdio_write(tp, 0x19, 0x9b00);
			mdio_write(tp, 0x15, 0x0118);
			mdio_write(tp, 0x19, 0x6461);
			mdio_write(tp, 0x15, 0x0119);
			mdio_write(tp, 0x19, 0x64e1);
			mdio_write(tp, 0x15, 0x011a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0150);
			mdio_write(tp, 0x19, 0x6461);
			mdio_write(tp, 0x15, 0x0151);
			mdio_write(tp, 0x19, 0x4003);
			mdio_write(tp, 0x15, 0x0152);
			mdio_write(tp, 0x19, 0x4540);
			mdio_write(tp, 0x15, 0x0153);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0155);
			mdio_write(tp, 0x19, 0x6421);
			mdio_write(tp, 0x15, 0x0156);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x021e);
			mdio_write(tp, 0x19, 0x5410);
			mdio_write(tp, 0x15, 0x0225);
			mdio_write(tp, 0x19, 0x5400);
			mdio_write(tp, 0x15, 0x023D);
			mdio_write(tp, 0x19, 0x4050);
			mdio_write(tp, 0x15, 0x0295);
			mdio_write(tp, 0x19, 0x6c08);
			mdio_write(tp, 0x15, 0x02bd);
			mdio_write(tp, 0x19, 0xa523);
			mdio_write(tp, 0x15, 0x02be);
			mdio_write(tp, 0x19, 0x32ca);
			mdio_write(tp, 0x15, 0x02ca);
			mdio_write(tp, 0x19, 0x48b3);
			mdio_write(tp, 0x15, 0x02cb);
			mdio_write(tp, 0x19, 0x4020);
			mdio_write(tp, 0x15, 0x02cc);
			mdio_write(tp, 0x19, 0x4823);
			mdio_write(tp, 0x15, 0x02cd);
			mdio_write(tp, 0x19, 0x4510);
			mdio_write(tp, 0x15, 0x02ce);
			mdio_write(tp, 0x19, 0xb63a);
			mdio_write(tp, 0x15, 0x02cf);
			mdio_write(tp, 0x19, 0x7dc8);
			mdio_write(tp, 0x15, 0x02d6);
			mdio_write(tp, 0x19, 0x9bf8);
			mdio_write(tp, 0x15, 0x02d8);
			mdio_write(tp, 0x19, 0x85f6);
			mdio_write(tp, 0x15, 0x02d9);
			mdio_write(tp, 0x19, 0x32e0);
			mdio_write(tp, 0x15, 0x02e0);
			mdio_write(tp, 0x19, 0x4834);
			mdio_write(tp, 0x15, 0x02e1);
			mdio_write(tp, 0x19, 0x6c08);
			mdio_write(tp, 0x15, 0x02e2);
			mdio_write(tp, 0x19, 0x4020);
			mdio_write(tp, 0x15, 0x02e3);
			mdio_write(tp, 0x19, 0x4824);
			mdio_write(tp, 0x15, 0x02e4);
			mdio_write(tp, 0x19, 0x4520);
			mdio_write(tp, 0x15, 0x02e5);
			mdio_write(tp, 0x19, 0x4008);
			mdio_write(tp, 0x15, 0x02e6);
			mdio_write(tp, 0x19, 0x4560);
			mdio_write(tp, 0x15, 0x02e7);
			mdio_write(tp, 0x19, 0x9d04);
			mdio_write(tp, 0x15, 0x02e8);
			mdio_write(tp, 0x19, 0x48c4);
			mdio_write(tp, 0x15, 0x02e9);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x02ea);
			mdio_write(tp, 0x19, 0x4844);
			mdio_write(tp, 0x15, 0x02eb);
			mdio_write(tp, 0x19, 0x7dc8);
			mdio_write(tp, 0x15, 0x02f0);
			mdio_write(tp, 0x19, 0x9cf7);
			mdio_write(tp, 0x15, 0x02f1);
			mdio_write(tp, 0x19, 0xdf94);
			mdio_write(tp, 0x15, 0x02f2);
			mdio_write(tp, 0x19, 0x0002);
			mdio_write(tp, 0x15, 0x02f3);
			mdio_write(tp, 0x19, 0x6810);
			mdio_write(tp, 0x15, 0x02f4);
			mdio_write(tp, 0x19, 0xb614);
			mdio_write(tp, 0x15, 0x02f5);
			mdio_write(tp, 0x19, 0xc42b);
			mdio_write(tp, 0x15, 0x02f6);
			mdio_write(tp, 0x19, 0x00d4);
			mdio_write(tp, 0x15, 0x02f7);
			mdio_write(tp, 0x19, 0xc455);
			mdio_write(tp, 0x15, 0x02f8);
			mdio_write(tp, 0x19, 0x0093);
			mdio_write(tp, 0x15, 0x02f9);
			mdio_write(tp, 0x19, 0x92ee);
			mdio_write(tp, 0x15, 0x02fa);
			mdio_write(tp, 0x19, 0xefed);
			mdio_write(tp, 0x15, 0x02fb);
			mdio_write(tp, 0x19, 0x3312);
			mdio_write(tp, 0x15, 0x0312);
			mdio_write(tp, 0x19, 0x49b5);
			mdio_write(tp, 0x15, 0x0313);
			mdio_write(tp, 0x19, 0x7d00);
			mdio_write(tp, 0x15, 0x0314);
			mdio_write(tp, 0x19, 0x4d00);
			mdio_write(tp, 0x15, 0x0315);
			mdio_write(tp, 0x19, 0x6810);
			mdio_write(tp, 0x15, 0x031e);
			mdio_write(tp, 0x19, 0x404f);
			mdio_write(tp, 0x15, 0x031f);
			mdio_write(tp, 0x19, 0x44c8);
			mdio_write(tp, 0x15, 0x0320);
			mdio_write(tp, 0x19, 0xd64f);
			mdio_write(tp, 0x15, 0x0321);
			mdio_write(tp, 0x19, 0x00e7);
			mdio_write(tp, 0x15, 0x0322);
			mdio_write(tp, 0x19, 0x7c08);
			mdio_write(tp, 0x15, 0x0323);
			mdio_write(tp, 0x19, 0x8203);
			mdio_write(tp, 0x15, 0x0324);
			mdio_write(tp, 0x19, 0x4d48);
			mdio_write(tp, 0x15, 0x0325);
			mdio_write(tp, 0x19, 0x3327);
			mdio_write(tp, 0x15, 0x0326);
			mdio_write(tp, 0x19, 0x4d40);
			mdio_write(tp, 0x15, 0x0327);
			mdio_write(tp, 0x19, 0xc8d7);
			mdio_write(tp, 0x15, 0x0328);
			mdio_write(tp, 0x19, 0x0003);
			mdio_write(tp, 0x15, 0x0329);
			mdio_write(tp, 0x19, 0x7c20);
			mdio_write(tp, 0x15, 0x032a);
			mdio_write(tp, 0x19, 0x4c20);
			mdio_write(tp, 0x15, 0x032b);
			mdio_write(tp, 0x19, 0xc8ed);
			mdio_write(tp, 0x15, 0x032c);
			mdio_write(tp, 0x19, 0x00f4);
			mdio_write(tp, 0x15, 0x032d);
			mdio_write(tp, 0x19, 0x82b3);
			mdio_write(tp, 0x15, 0x032e);
			mdio_write(tp, 0x19, 0xd11d);
			mdio_write(tp, 0x15, 0x032f);
			mdio_write(tp, 0x19, 0x00b1);
			mdio_write(tp, 0x15, 0x0330);
			mdio_write(tp, 0x19, 0xde18);
			mdio_write(tp, 0x15, 0x0331);
			mdio_write(tp, 0x19, 0x0008);
			mdio_write(tp, 0x15, 0x0332);
			mdio_write(tp, 0x19, 0x91ee);
			mdio_write(tp, 0x15, 0x0333);
			mdio_write(tp, 0x19, 0x3339);
			mdio_write(tp, 0x15, 0x033a);
			mdio_write(tp, 0x19, 0x4064);
			mdio_write(tp, 0x15, 0x0340);
			mdio_write(tp, 0x19, 0x9e06);
			mdio_write(tp, 0x15, 0x0341);
			mdio_write(tp, 0x19, 0x7c08);
			mdio_write(tp, 0x15, 0x0342);
			mdio_write(tp, 0x19, 0x8203);
			mdio_write(tp, 0x15, 0x0343);
			mdio_write(tp, 0x19, 0x4d48);
			mdio_write(tp, 0x15, 0x0344);
			mdio_write(tp, 0x19, 0x3346);
			mdio_write(tp, 0x15, 0x0345);
			mdio_write(tp, 0x19, 0x4d40);
			mdio_write(tp, 0x15, 0x0346);
			mdio_write(tp, 0x19, 0xd11d);
			mdio_write(tp, 0x15, 0x0347);
			mdio_write(tp, 0x19, 0x0099);
			mdio_write(tp, 0x15, 0x0348);
			mdio_write(tp, 0x19, 0xbb17);
			mdio_write(tp, 0x15, 0x0349);
			mdio_write(tp, 0x19, 0x8102);
			mdio_write(tp, 0x15, 0x034a);
			mdio_write(tp, 0x19, 0x334d);
			mdio_write(tp, 0x15, 0x034b);
			mdio_write(tp, 0x19, 0xa22c);
			mdio_write(tp, 0x15, 0x034c);
			mdio_write(tp, 0x19, 0x3397);
			mdio_write(tp, 0x15, 0x034d);
			mdio_write(tp, 0x19, 0x91f2);
			mdio_write(tp, 0x15, 0x034e);
			mdio_write(tp, 0x19, 0xc218);
			mdio_write(tp, 0x15, 0x034f);
			mdio_write(tp, 0x19, 0x00f0);
			mdio_write(tp, 0x15, 0x0350);
			mdio_write(tp, 0x19, 0x3397);
			mdio_write(tp, 0x15, 0x0351);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0364);
			mdio_write(tp, 0x19, 0xbc05);
			mdio_write(tp, 0x15, 0x0367);
			mdio_write(tp, 0x19, 0xa1fc);
			mdio_write(tp, 0x15, 0x0368);
			mdio_write(tp, 0x19, 0x3377);
			mdio_write(tp, 0x15, 0x0369);
			mdio_write(tp, 0x19, 0x328b);
			mdio_write(tp, 0x15, 0x036a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0377);
			mdio_write(tp, 0x19, 0x4b97);
			mdio_write(tp, 0x15, 0x0378);
			mdio_write(tp, 0x19, 0x6818);
			mdio_write(tp, 0x15, 0x0379);
			mdio_write(tp, 0x19, 0x4b07);
			mdio_write(tp, 0x15, 0x037a);
			mdio_write(tp, 0x19, 0x40ac);
			mdio_write(tp, 0x15, 0x037b);
			mdio_write(tp, 0x19, 0x4445);
			mdio_write(tp, 0x15, 0x037c);
			mdio_write(tp, 0x19, 0x404e);
			mdio_write(tp, 0x15, 0x037d);
			mdio_write(tp, 0x19, 0x4461);
			mdio_write(tp, 0x15, 0x037e);
			mdio_write(tp, 0x19, 0x9c09);
			mdio_write(tp, 0x15, 0x037f);
			mdio_write(tp, 0x19, 0x63da);
			mdio_write(tp, 0x15, 0x0380);
			mdio_write(tp, 0x19, 0x5440);
			mdio_write(tp, 0x15, 0x0381);
			mdio_write(tp, 0x19, 0x4b98);
			mdio_write(tp, 0x15, 0x0382);
			mdio_write(tp, 0x19, 0x7c60);
			mdio_write(tp, 0x15, 0x0383);
			mdio_write(tp, 0x19, 0x4c00);
			mdio_write(tp, 0x15, 0x0384);
			mdio_write(tp, 0x19, 0x4b08);
			mdio_write(tp, 0x15, 0x0385);
			mdio_write(tp, 0x19, 0x63d8);
			mdio_write(tp, 0x15, 0x0386);
			mdio_write(tp, 0x19, 0x338d);
			mdio_write(tp, 0x15, 0x0387);
			mdio_write(tp, 0x19, 0xd64f);
			mdio_write(tp, 0x15, 0x0388);
			mdio_write(tp, 0x19, 0x0080);
			mdio_write(tp, 0x15, 0x0389);
			mdio_write(tp, 0x19, 0x820c);
			mdio_write(tp, 0x15, 0x038a);
			mdio_write(tp, 0x19, 0xa10b);
			mdio_write(tp, 0x15, 0x038b);
			mdio_write(tp, 0x19, 0x9df3);
			mdio_write(tp, 0x15, 0x038c);
			mdio_write(tp, 0x19, 0x3395);
			mdio_write(tp, 0x15, 0x038d);
			mdio_write(tp, 0x19, 0xd64f);
			mdio_write(tp, 0x15, 0x038e);
			mdio_write(tp, 0x19, 0x00f9);
			mdio_write(tp, 0x15, 0x038f);
			mdio_write(tp, 0x19, 0xc017);
			mdio_write(tp, 0x15, 0x0390);
			mdio_write(tp, 0x19, 0x0005);
			mdio_write(tp, 0x15, 0x0391);
			mdio_write(tp, 0x19, 0x6c0b);
			mdio_write(tp, 0x15, 0x0392);
			mdio_write(tp, 0x19, 0xa103);
			mdio_write(tp, 0x15, 0x0393);
			mdio_write(tp, 0x19, 0x6c08);
			mdio_write(tp, 0x15, 0x0394);
			mdio_write(tp, 0x19, 0x9df9);
			mdio_write(tp, 0x15, 0x0395);
			mdio_write(tp, 0x19, 0x6c08);
			mdio_write(tp, 0x15, 0x0396);
			mdio_write(tp, 0x19, 0x3397);
			mdio_write(tp, 0x15, 0x0399);
			mdio_write(tp, 0x19, 0x6810);
			mdio_write(tp, 0x15, 0x03a4);
			mdio_write(tp, 0x19, 0x7c08);
			mdio_write(tp, 0x15, 0x03a5);
			mdio_write(tp, 0x19, 0x8203);
			mdio_write(tp, 0x15, 0x03a6);
			mdio_write(tp, 0x19, 0x4d08);
			mdio_write(tp, 0x15, 0x03a7);
			mdio_write(tp, 0x19, 0x33a9);
			mdio_write(tp, 0x15, 0x03a8);
			mdio_write(tp, 0x19, 0x4d00);
			mdio_write(tp, 0x15, 0x03a9);
			mdio_write(tp, 0x19, 0x9bfa);
			mdio_write(tp, 0x15, 0x03aa);
			mdio_write(tp, 0x19, 0x33b6);
			mdio_write(tp, 0x15, 0x03bb);
			mdio_write(tp, 0x19, 0x4056);
			mdio_write(tp, 0x15, 0x03bc);
			mdio_write(tp, 0x19, 0x44e9);
			mdio_write(tp, 0x15, 0x03bd);
			mdio_write(tp, 0x19, 0x4054);
			mdio_write(tp, 0x15, 0x03be);
			mdio_write(tp, 0x19, 0x44f8);
			mdio_write(tp, 0x15, 0x03bf);
			mdio_write(tp, 0x19, 0xd64f);
			mdio_write(tp, 0x15, 0x03c0);
			mdio_write(tp, 0x19, 0x0037);
			mdio_write(tp, 0x15, 0x03c1);
			mdio_write(tp, 0x19, 0xbd37);
			mdio_write(tp, 0x15, 0x03c2);
			mdio_write(tp, 0x19, 0x9cfd);
			mdio_write(tp, 0x15, 0x03c3);
			mdio_write(tp, 0x19, 0xc639);
			mdio_write(tp, 0x15, 0x03c4);
			mdio_write(tp, 0x19, 0x0011);
			mdio_write(tp, 0x15, 0x03c5);
			mdio_write(tp, 0x19, 0x9b03);
			mdio_write(tp, 0x15, 0x03c6);
			mdio_write(tp, 0x19, 0x7c01);
			mdio_write(tp, 0x15, 0x03c7);
			mdio_write(tp, 0x19, 0x4c01);
			mdio_write(tp, 0x15, 0x03c8);
			mdio_write(tp, 0x19, 0x9e03);
			mdio_write(tp, 0x15, 0x03c9);
			mdio_write(tp, 0x19, 0x7c20);
			mdio_write(tp, 0x15, 0x03ca);
			mdio_write(tp, 0x19, 0x4c20);
			mdio_write(tp, 0x15, 0x03cb);
			mdio_write(tp, 0x19, 0x9af4);
			mdio_write(tp, 0x15, 0x03cc);
			mdio_write(tp, 0x19, 0x7c12);
			mdio_write(tp, 0x15, 0x03cd);
			mdio_write(tp, 0x19, 0x4c52);
			mdio_write(tp, 0x15, 0x03ce);
			mdio_write(tp, 0x19, 0x4470);
			mdio_write(tp, 0x15, 0x03cf);
			mdio_write(tp, 0x19, 0x7c12);
			mdio_write(tp, 0x15, 0x03d0);
			mdio_write(tp, 0x19, 0x4c40);
			mdio_write(tp, 0x15, 0x03d1);
			mdio_write(tp, 0x19, 0x33bf);
			mdio_write(tp, 0x15, 0x03d6);
			mdio_write(tp, 0x19, 0x4047);
			mdio_write(tp, 0x15, 0x03d7);
			mdio_write(tp, 0x19, 0x4469);
			mdio_write(tp, 0x15, 0x03d8);
			mdio_write(tp, 0x19, 0x492b);
			mdio_write(tp, 0x15, 0x03d9);
			mdio_write(tp, 0x19, 0x4479);
			mdio_write(tp, 0x15, 0x03da);
			mdio_write(tp, 0x19, 0x7c09);
			mdio_write(tp, 0x15, 0x03db);
			mdio_write(tp, 0x19, 0x8203);
			mdio_write(tp, 0x15, 0x03dc);
			mdio_write(tp, 0x19, 0x4d48);
			mdio_write(tp, 0x15, 0x03dd);
			mdio_write(tp, 0x19, 0x33df);
			mdio_write(tp, 0x15, 0x03de);
			mdio_write(tp, 0x19, 0x4d40);
			mdio_write(tp, 0x15, 0x03df);
			mdio_write(tp, 0x19, 0xd64f);
			mdio_write(tp, 0x15, 0x03e0);
			mdio_write(tp, 0x19, 0x0017);
			mdio_write(tp, 0x15, 0x03e1);
			mdio_write(tp, 0x19, 0xbd17);
			mdio_write(tp, 0x15, 0x03e2);
			mdio_write(tp, 0x19, 0x9b03);
			mdio_write(tp, 0x15, 0x03e3);
			mdio_write(tp, 0x19, 0x7c20);
			mdio_write(tp, 0x15, 0x03e4);
			mdio_write(tp, 0x19, 0x4c20);
			mdio_write(tp, 0x15, 0x03e5);
			mdio_write(tp, 0x19, 0x88f5);
			mdio_write(tp, 0x15, 0x03e6);
			mdio_write(tp, 0x19, 0xc428);
			mdio_write(tp, 0x15, 0x03e7);
			mdio_write(tp, 0x19, 0x0008);
			mdio_write(tp, 0x15, 0x03e8);
			mdio_write(tp, 0x19, 0x9af2);
			mdio_write(tp, 0x15, 0x03e9);
			mdio_write(tp, 0x19, 0x7c12);
			mdio_write(tp, 0x15, 0x03ea);
			mdio_write(tp, 0x19, 0x4c52);
			mdio_write(tp, 0x15, 0x03eb);
			mdio_write(tp, 0x19, 0x4470);
			mdio_write(tp, 0x15, 0x03ec);
			mdio_write(tp, 0x19, 0x7c12);
			mdio_write(tp, 0x15, 0x03ed);
			mdio_write(tp, 0x19, 0x4c40);
			mdio_write(tp, 0x15, 0x03ee);
			mdio_write(tp, 0x19, 0x33da);
			mdio_write(tp, 0x15, 0x03ef);
			mdio_write(tp, 0x19, 0x3312);
			mdio_write(tp, 0x16, 0x0306);
			mdio_write(tp, 0x16, 0x0300);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x17, 0x2179);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0040);
			mdio_write(tp, 0x18, 0x0645);
			mdio_write(tp, 0x19, 0xe200);
			mdio_write(tp, 0x18, 0x0655);
			mdio_write(tp, 0x19, 0x9000);
			mdio_write(tp, 0x18, 0x0d05);
			mdio_write(tp, 0x19, 0xbe00);
			mdio_write(tp, 0x18, 0x0d15);
			mdio_write(tp, 0x19, 0xd300);
			mdio_write(tp, 0x18, 0x0d25);
			mdio_write(tp, 0x19, 0xfe00);
			mdio_write(tp, 0x18, 0x0d35);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x0d45);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x0d55);
			mdio_write(tp, 0x19, 0x1000);
			mdio_write(tp, 0x18, 0x0d65);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x0d75);
			mdio_write(tp, 0x19, 0x8200);
			mdio_write(tp, 0x18, 0x0d85);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x0d95);
			mdio_write(tp, 0x19, 0x7000);
			mdio_write(tp, 0x18, 0x0da5);
			mdio_write(tp, 0x19, 0x0f00);
			mdio_write(tp, 0x18, 0x0db5);
			mdio_write(tp, 0x19, 0x0100);
			mdio_write(tp, 0x18, 0x0dc5);
			mdio_write(tp, 0x19, 0x9b00);
			mdio_write(tp, 0x18, 0x0dd5);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x0de5);
			mdio_write(tp, 0x19, 0xe000);
			mdio_write(tp, 0x18, 0x0df5);
			mdio_write(tp, 0x19, 0xef00);
			mdio_write(tp, 0x18, 0x16d5);
			mdio_write(tp, 0x19, 0xe200);
			mdio_write(tp, 0x18, 0x16e5);
			mdio_write(tp, 0x19, 0xab00);
			mdio_write(tp, 0x18, 0x2904);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x2914);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x2924);
			mdio_write(tp, 0x19, 0x0100);
			mdio_write(tp, 0x18, 0x2934);
			mdio_write(tp, 0x19, 0x2000);
			mdio_write(tp, 0x18, 0x2944);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2954);
			mdio_write(tp, 0x19, 0x4600);
			mdio_write(tp, 0x18, 0x2964);
			mdio_write(tp, 0x19, 0xfc00);
			mdio_write(tp, 0x18, 0x2974);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2984);
			mdio_write(tp, 0x19, 0x5000);
			mdio_write(tp, 0x18, 0x2994);
			mdio_write(tp, 0x19, 0x9d00);
			mdio_write(tp, 0x18, 0x29a4);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x29b4);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x29c4);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x29d4);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x29e4);
			mdio_write(tp, 0x19, 0x2000);
			mdio_write(tp, 0x18, 0x29f4);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2a04);
			mdio_write(tp, 0x19, 0xe600);
			mdio_write(tp, 0x18, 0x2a14);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x2a24);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2a34);
			mdio_write(tp, 0x19, 0x5000);
			mdio_write(tp, 0x18, 0x2a44);
			mdio_write(tp, 0x19, 0x8500);
			mdio_write(tp, 0x18, 0x2a54);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x2a64);
			mdio_write(tp, 0x19, 0xac00);
			mdio_write(tp, 0x18, 0x2a74);
			mdio_write(tp, 0x19, 0x0800);
			mdio_write(tp, 0x18, 0x2a84);
			mdio_write(tp, 0x19, 0xfc00);
			mdio_write(tp, 0x18, 0x2a94);
			mdio_write(tp, 0x19, 0xe000);
			mdio_write(tp, 0x18, 0x2aa4);
			mdio_write(tp, 0x19, 0x7400);
			mdio_write(tp, 0x18, 0x2ab4);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x2ac4);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x2ad4);
			mdio_write(tp, 0x19, 0x0100);
			mdio_write(tp, 0x18, 0x2ae4);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x2af4);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2b04);
			mdio_write(tp, 0x19, 0x4400);
			mdio_write(tp, 0x18, 0x2b14);
			mdio_write(tp, 0x19, 0xfc00);
			mdio_write(tp, 0x18, 0x2b24);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2b34);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x2b44);
			mdio_write(tp, 0x19, 0x9d00);
			mdio_write(tp, 0x18, 0x2b54);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x2b64);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x2b74);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x2b84);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2b94);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x2ba4);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2bb4);
			mdio_write(tp, 0x19, 0xfc00);
			mdio_write(tp, 0x18, 0x2bc4);
			mdio_write(tp, 0x19, 0xff00);
			mdio_write(tp, 0x18, 0x2bd4);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2be4);
			mdio_write(tp, 0x19, 0x4000);
			mdio_write(tp, 0x18, 0x2bf4);
			mdio_write(tp, 0x19, 0x8900);
			mdio_write(tp, 0x18, 0x2c04);
			mdio_write(tp, 0x19, 0x8300);
			mdio_write(tp, 0x18, 0x2c14);
			mdio_write(tp, 0x19, 0xe000);
			mdio_write(tp, 0x18, 0x2c24);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x18, 0x2c34);
			mdio_write(tp, 0x19, 0xac00);
			mdio_write(tp, 0x18, 0x2c44);
			mdio_write(tp, 0x19, 0x0800);
			mdio_write(tp, 0x18, 0x2c54);
			mdio_write(tp, 0x19, 0xfa00);
			mdio_write(tp, 0x18, 0x2c64);
			mdio_write(tp, 0x19, 0xe100);
			mdio_write(tp, 0x18, 0x2c74);
			mdio_write(tp, 0x19, 0x7f00);
			mdio_write(tp, 0x18, 0x0001);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x17, 0x2100);
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			mdio_write(tp, 0x05, 0x8000);
			mdio_write(tp, 0x06, 0xd480);
			mdio_write(tp, 0x06, 0xc1e4);
			mdio_write(tp, 0x06, 0x8b9a);
			mdio_write(tp, 0x06, 0xe58b);
			mdio_write(tp, 0x06, 0x9bee);
			mdio_write(tp, 0x06, 0x8b83);
			mdio_write(tp, 0x06, 0x41bf);
			mdio_write(tp, 0x06, 0x8b88);
			mdio_write(tp, 0x06, 0xec00);
			mdio_write(tp, 0x06, 0x19a9);
			mdio_write(tp, 0x06, 0x8b90);
			mdio_write(tp, 0x06, 0xf9ee);
			mdio_write(tp, 0x06, 0xfff6);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xfff7);
			mdio_write(tp, 0x06, 0xffe0);
			mdio_write(tp, 0x06, 0xe140);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x41f7);
			mdio_write(tp, 0x06, 0x2ff6);
			mdio_write(tp, 0x06, 0x28e4);
			mdio_write(tp, 0x06, 0xe140);
			mdio_write(tp, 0x06, 0xe5e1);
			mdio_write(tp, 0x06, 0x41f7);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x020c);
			mdio_write(tp, 0x06, 0x0202);
			mdio_write(tp, 0x06, 0x1d02);
			mdio_write(tp, 0x06, 0x0230);
			mdio_write(tp, 0x06, 0x0202);
			mdio_write(tp, 0x06, 0x4002);
			mdio_write(tp, 0x06, 0x028b);
			mdio_write(tp, 0x06, 0x0280);
			mdio_write(tp, 0x06, 0x6c02);
			mdio_write(tp, 0x06, 0x8085);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x88e1);
			mdio_write(tp, 0x06, 0x8b89);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8a1e);
			mdio_write(tp, 0x06, 0x01e1);
			mdio_write(tp, 0x06, 0x8b8b);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8c1e);
			mdio_write(tp, 0x06, 0x01e1);
			mdio_write(tp, 0x06, 0x8b8d);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8e1e);
			mdio_write(tp, 0x06, 0x01a0);
			mdio_write(tp, 0x06, 0x00c7);
			mdio_write(tp, 0x06, 0xaec3);
			mdio_write(tp, 0x06, 0xf8e0);
			mdio_write(tp, 0x06, 0x8b8d);
			mdio_write(tp, 0x06, 0xad20);
			mdio_write(tp, 0x06, 0x10ee);
			mdio_write(tp, 0x06, 0x8b8d);
			mdio_write(tp, 0x06, 0x0002);
			mdio_write(tp, 0x06, 0x1310);
			mdio_write(tp, 0x06, 0x0280);
			mdio_write(tp, 0x06, 0xc602);
			mdio_write(tp, 0x06, 0x1f0c);
			mdio_write(tp, 0x06, 0x0227);
			mdio_write(tp, 0x06, 0x49fc);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x8ead);
			mdio_write(tp, 0x06, 0x200b);
			mdio_write(tp, 0x06, 0xf620);
			mdio_write(tp, 0x06, 0xe48b);
			mdio_write(tp, 0x06, 0x8e02);
			mdio_write(tp, 0x06, 0x852d);
			mdio_write(tp, 0x06, 0x021b);
			mdio_write(tp, 0x06, 0x67ad);
			mdio_write(tp, 0x06, 0x2211);
			mdio_write(tp, 0x06, 0xf622);
			mdio_write(tp, 0x06, 0xe48b);
			mdio_write(tp, 0x06, 0x8e02);
			mdio_write(tp, 0x06, 0x2ba5);
			mdio_write(tp, 0x06, 0x022a);
			mdio_write(tp, 0x06, 0x2402);
			mdio_write(tp, 0x06, 0x82e5);
			mdio_write(tp, 0x06, 0x022a);
			mdio_write(tp, 0x06, 0xf0ad);
			mdio_write(tp, 0x06, 0x2511);
			mdio_write(tp, 0x06, 0xf625);
			mdio_write(tp, 0x06, 0xe48b);
			mdio_write(tp, 0x06, 0x8e02);
			mdio_write(tp, 0x06, 0x8445);
			mdio_write(tp, 0x06, 0x0204);
			mdio_write(tp, 0x06, 0x0302);
			mdio_write(tp, 0x06, 0x19cc);
			mdio_write(tp, 0x06, 0x022b);
			mdio_write(tp, 0x06, 0x5bfc);
			mdio_write(tp, 0x06, 0x04ee);
			mdio_write(tp, 0x06, 0x8b8d);
			mdio_write(tp, 0x06, 0x0105);
			mdio_write(tp, 0x06, 0xf8f9);
			mdio_write(tp, 0x06, 0xfae0);
			mdio_write(tp, 0x06, 0x8b81);
			mdio_write(tp, 0x06, 0xac26);
			mdio_write(tp, 0x06, 0x08e0);
			mdio_write(tp, 0x06, 0x8b81);
			mdio_write(tp, 0x06, 0xac21);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x6bee);
			mdio_write(tp, 0x06, 0xe0ea);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xe0eb);
			mdio_write(tp, 0x06, 0x00e2);
			mdio_write(tp, 0x06, 0xe07c);
			mdio_write(tp, 0x06, 0xe3e0);
			mdio_write(tp, 0x06, 0x7da5);
			mdio_write(tp, 0x06, 0x1111);
			mdio_write(tp, 0x06, 0x15d2);
			mdio_write(tp, 0x06, 0x60d6);
			mdio_write(tp, 0x06, 0x6666);
			mdio_write(tp, 0x06, 0x0207);
			mdio_write(tp, 0x06, 0x6cd2);
			mdio_write(tp, 0x06, 0xa0d6);
			mdio_write(tp, 0x06, 0xaaaa);
			mdio_write(tp, 0x06, 0x0207);
			mdio_write(tp, 0x06, 0x6c02);
			mdio_write(tp, 0x06, 0x201d);
			mdio_write(tp, 0x06, 0xae44);
			mdio_write(tp, 0x06, 0xa566);
			mdio_write(tp, 0x06, 0x6602);
			mdio_write(tp, 0x06, 0xae38);
			mdio_write(tp, 0x06, 0xa5aa);
			mdio_write(tp, 0x06, 0xaa02);
			mdio_write(tp, 0x06, 0xae32);
			mdio_write(tp, 0x06, 0xeee0);
			mdio_write(tp, 0x06, 0xea04);
			mdio_write(tp, 0x06, 0xeee0);
			mdio_write(tp, 0x06, 0xeb06);
			mdio_write(tp, 0x06, 0xe2e0);
			mdio_write(tp, 0x06, 0x7ce3);
			mdio_write(tp, 0x06, 0xe07d);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x38e1);
			mdio_write(tp, 0x06, 0xe039);
			mdio_write(tp, 0x06, 0xad2e);
			mdio_write(tp, 0x06, 0x21ad);
			mdio_write(tp, 0x06, 0x3f13);
			mdio_write(tp, 0x06, 0xe0e4);
			mdio_write(tp, 0x06, 0x14e1);
			mdio_write(tp, 0x06, 0xe415);
			mdio_write(tp, 0x06, 0x6880);
			mdio_write(tp, 0x06, 0xe4e4);
			mdio_write(tp, 0x06, 0x14e5);
			mdio_write(tp, 0x06, 0xe415);
			mdio_write(tp, 0x06, 0x0220);
			mdio_write(tp, 0x06, 0x1dae);
			mdio_write(tp, 0x06, 0x0bac);
			mdio_write(tp, 0x06, 0x3e02);
			mdio_write(tp, 0x06, 0xae06);
			mdio_write(tp, 0x06, 0x0281);
			mdio_write(tp, 0x06, 0x4602);
			mdio_write(tp, 0x06, 0x2057);
			mdio_write(tp, 0x06, 0xfefd);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0xf8e0);
			mdio_write(tp, 0x06, 0x8b81);
			mdio_write(tp, 0x06, 0xad26);
			mdio_write(tp, 0x06, 0x0302);
			mdio_write(tp, 0x06, 0x20a7);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x81ad);
			mdio_write(tp, 0x06, 0x2109);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x2eac);
			mdio_write(tp, 0x06, 0x2003);
			mdio_write(tp, 0x06, 0x0281);
			mdio_write(tp, 0x06, 0x61fc);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x81ac);
			mdio_write(tp, 0x06, 0x2505);
			mdio_write(tp, 0x06, 0x0222);
			mdio_write(tp, 0x06, 0xaeae);
			mdio_write(tp, 0x06, 0x0302);
			mdio_write(tp, 0x06, 0x8172);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0xf8f9);
			mdio_write(tp, 0x06, 0xfaef);
			mdio_write(tp, 0x06, 0x69fa);
			mdio_write(tp, 0x06, 0xe086);
			mdio_write(tp, 0x06, 0x20a0);
			mdio_write(tp, 0x06, 0x8016);
			mdio_write(tp, 0x06, 0xe086);
			mdio_write(tp, 0x06, 0x21e1);
			mdio_write(tp, 0x06, 0x8b33);
			mdio_write(tp, 0x06, 0x1b10);
			mdio_write(tp, 0x06, 0x9e06);
			mdio_write(tp, 0x06, 0x0223);
			mdio_write(tp, 0x06, 0x91af);
			mdio_write(tp, 0x06, 0x8252);
			mdio_write(tp, 0x06, 0xee86);
			mdio_write(tp, 0x06, 0x2081);
			mdio_write(tp, 0x06, 0xaee4);
			mdio_write(tp, 0x06, 0xa081);
			mdio_write(tp, 0x06, 0x1402);
			mdio_write(tp, 0x06, 0x2399);
			mdio_write(tp, 0x06, 0xbf25);
			mdio_write(tp, 0x06, 0xcc02);
			mdio_write(tp, 0x06, 0x2d21);
			mdio_write(tp, 0x06, 0xee86);
			mdio_write(tp, 0x06, 0x2100);
			mdio_write(tp, 0x06, 0xee86);
			mdio_write(tp, 0x06, 0x2082);
			mdio_write(tp, 0x06, 0xaf82);
			mdio_write(tp, 0x06, 0x52a0);
			mdio_write(tp, 0x06, 0x8232);
			mdio_write(tp, 0x06, 0xe086);
			mdio_write(tp, 0x06, 0x21e1);
			mdio_write(tp, 0x06, 0x8b32);
			mdio_write(tp, 0x06, 0x1b10);
			mdio_write(tp, 0x06, 0x9e06);
			mdio_write(tp, 0x06, 0x0223);
			mdio_write(tp, 0x06, 0x91af);
			mdio_write(tp, 0x06, 0x8252);
			mdio_write(tp, 0x06, 0xee86);
			mdio_write(tp, 0x06, 0x2100);
			mdio_write(tp, 0x06, 0xd000);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0x5910);
			mdio_write(tp, 0x06, 0xa004);
			mdio_write(tp, 0x06, 0xf9e0);
			mdio_write(tp, 0x06, 0x861f);
			mdio_write(tp, 0x06, 0xa000);
			mdio_write(tp, 0x06, 0x07ee);
			mdio_write(tp, 0x06, 0x8620);
			mdio_write(tp, 0x06, 0x83af);
			mdio_write(tp, 0x06, 0x8178);
			mdio_write(tp, 0x06, 0x0224);
			mdio_write(tp, 0x06, 0x0102);
			mdio_write(tp, 0x06, 0x2399);
			mdio_write(tp, 0x06, 0xae72);
			mdio_write(tp, 0x06, 0xa083);
			mdio_write(tp, 0x06, 0x4b1f);
			mdio_write(tp, 0x06, 0x55d0);
			mdio_write(tp, 0x06, 0x04bf);
			mdio_write(tp, 0x06, 0x8615);
			mdio_write(tp, 0x06, 0x1a90);
			mdio_write(tp, 0x06, 0x0c54);
			mdio_write(tp, 0x06, 0xd91e);
			mdio_write(tp, 0x06, 0x31b0);
			mdio_write(tp, 0x06, 0xf4e0);
			mdio_write(tp, 0x06, 0xe022);
			mdio_write(tp, 0x06, 0xe1e0);
			mdio_write(tp, 0x06, 0x23ad);
			mdio_write(tp, 0x06, 0x2e0c);
			mdio_write(tp, 0x06, 0xef02);
			mdio_write(tp, 0x06, 0xef12);
			mdio_write(tp, 0x06, 0x0e44);
			mdio_write(tp, 0x06, 0xef23);
			mdio_write(tp, 0x06, 0x0e54);
			mdio_write(tp, 0x06, 0xef21);
			mdio_write(tp, 0x06, 0xe6e4);
			mdio_write(tp, 0x06, 0x2ae7);
			mdio_write(tp, 0x06, 0xe42b);
			mdio_write(tp, 0x06, 0xe2e4);
			mdio_write(tp, 0x06, 0x28e3);
			mdio_write(tp, 0x06, 0xe429);
			mdio_write(tp, 0x06, 0x6d20);
			mdio_write(tp, 0x06, 0x00e6);
			mdio_write(tp, 0x06, 0xe428);
			mdio_write(tp, 0x06, 0xe7e4);
			mdio_write(tp, 0x06, 0x29bf);
			mdio_write(tp, 0x06, 0x25ca);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0x21ee);
			mdio_write(tp, 0x06, 0x8620);
			mdio_write(tp, 0x06, 0x84ee);
			mdio_write(tp, 0x06, 0x8621);
			mdio_write(tp, 0x06, 0x00af);
			mdio_write(tp, 0x06, 0x8178);
			mdio_write(tp, 0x06, 0xa084);
			mdio_write(tp, 0x06, 0x19e0);
			mdio_write(tp, 0x06, 0x8621);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x341b);
			mdio_write(tp, 0x06, 0x109e);
			mdio_write(tp, 0x06, 0x0602);
			mdio_write(tp, 0x06, 0x2391);
			mdio_write(tp, 0x06, 0xaf82);
			mdio_write(tp, 0x06, 0x5202);
			mdio_write(tp, 0x06, 0x241f);
			mdio_write(tp, 0x06, 0xee86);
			mdio_write(tp, 0x06, 0x2085);
			mdio_write(tp, 0x06, 0xae08);
			mdio_write(tp, 0x06, 0xa085);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x0302);
			mdio_write(tp, 0x06, 0x2442);
			mdio_write(tp, 0x06, 0xfeef);
			mdio_write(tp, 0x06, 0x96fe);
			mdio_write(tp, 0x06, 0xfdfc);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xf9fa);
			mdio_write(tp, 0x06, 0xef69);
			mdio_write(tp, 0x06, 0xfad1);
			mdio_write(tp, 0x06, 0x801f);
			mdio_write(tp, 0x06, 0x66e2);
			mdio_write(tp, 0x06, 0xe0ea);
			mdio_write(tp, 0x06, 0xe3e0);
			mdio_write(tp, 0x06, 0xeb5a);
			mdio_write(tp, 0x06, 0xf81e);
			mdio_write(tp, 0x06, 0x20e6);
			mdio_write(tp, 0x06, 0xe0ea);
			mdio_write(tp, 0x06, 0xe5e0);
			mdio_write(tp, 0x06, 0xebd3);
			mdio_write(tp, 0x06, 0x05b3);
			mdio_write(tp, 0x06, 0xfee2);
			mdio_write(tp, 0x06, 0xe07c);
			mdio_write(tp, 0x06, 0xe3e0);
			mdio_write(tp, 0x06, 0x7dad);
			mdio_write(tp, 0x06, 0x3703);
			mdio_write(tp, 0x06, 0x7dff);
			mdio_write(tp, 0x06, 0xff0d);
			mdio_write(tp, 0x06, 0x581c);
			mdio_write(tp, 0x06, 0x55f8);
			mdio_write(tp, 0x06, 0xef46);
			mdio_write(tp, 0x06, 0x0282);
			mdio_write(tp, 0x06, 0xc7ef);
			mdio_write(tp, 0x06, 0x65ef);
			mdio_write(tp, 0x06, 0x54fc);
			mdio_write(tp, 0x06, 0xac30);
			mdio_write(tp, 0x06, 0x2b11);
			mdio_write(tp, 0x06, 0xa188);
			mdio_write(tp, 0x06, 0xcabf);
			mdio_write(tp, 0x06, 0x860e);
			mdio_write(tp, 0x06, 0xef10);
			mdio_write(tp, 0x06, 0x0c11);
			mdio_write(tp, 0x06, 0x1a91);
			mdio_write(tp, 0x06, 0xda19);
			mdio_write(tp, 0x06, 0xdbf8);
			mdio_write(tp, 0x06, 0xef46);
			mdio_write(tp, 0x06, 0x021e);
			mdio_write(tp, 0x06, 0x17ef);
			mdio_write(tp, 0x06, 0x54fc);
			mdio_write(tp, 0x06, 0xad30);
			mdio_write(tp, 0x06, 0x0fef);
			mdio_write(tp, 0x06, 0x5689);
			mdio_write(tp, 0x06, 0xde19);
			mdio_write(tp, 0x06, 0xdfe2);
			mdio_write(tp, 0x06, 0x861f);
			mdio_write(tp, 0x06, 0xbf86);
			mdio_write(tp, 0x06, 0x161a);
			mdio_write(tp, 0x06, 0x90de);
			mdio_write(tp, 0x06, 0xfeef);
			mdio_write(tp, 0x06, 0x96fe);
			mdio_write(tp, 0x06, 0xfdfc);
			mdio_write(tp, 0x06, 0x04ac);
			mdio_write(tp, 0x06, 0x2707);
			mdio_write(tp, 0x06, 0xac37);
			mdio_write(tp, 0x06, 0x071a);
			mdio_write(tp, 0x06, 0x54ae);
			mdio_write(tp, 0x06, 0x11ac);
			mdio_write(tp, 0x06, 0x3707);
			mdio_write(tp, 0x06, 0xae00);
			mdio_write(tp, 0x06, 0x1a54);
			mdio_write(tp, 0x06, 0xac37);
			mdio_write(tp, 0x06, 0x07d0);
			mdio_write(tp, 0x06, 0x01d5);
			mdio_write(tp, 0x06, 0xffff);
			mdio_write(tp, 0x06, 0xae02);
			mdio_write(tp, 0x06, 0xd000);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x83ad);
			mdio_write(tp, 0x06, 0x2444);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x22e1);
			mdio_write(tp, 0x06, 0xe023);
			mdio_write(tp, 0x06, 0xad22);
			mdio_write(tp, 0x06, 0x3be0);
			mdio_write(tp, 0x06, 0x8abe);
			mdio_write(tp, 0x06, 0xa000);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x28de);
			mdio_write(tp, 0x06, 0xae42);
			mdio_write(tp, 0x06, 0xa001);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x28f1);
			mdio_write(tp, 0x06, 0xae3a);
			mdio_write(tp, 0x06, 0xa002);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x8344);
			mdio_write(tp, 0x06, 0xae32);
			mdio_write(tp, 0x06, 0xa003);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x299a);
			mdio_write(tp, 0x06, 0xae2a);
			mdio_write(tp, 0x06, 0xa004);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x29ae);
			mdio_write(tp, 0x06, 0xae22);
			mdio_write(tp, 0x06, 0xa005);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x29d7);
			mdio_write(tp, 0x06, 0xae1a);
			mdio_write(tp, 0x06, 0xa006);
			mdio_write(tp, 0x06, 0x0502);
			mdio_write(tp, 0x06, 0x29fe);
			mdio_write(tp, 0x06, 0xae12);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xc000);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xc100);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xc600);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xbe00);
			mdio_write(tp, 0x06, 0xae00);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0xf802);
			mdio_write(tp, 0x06, 0x2a67);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x22e1);
			mdio_write(tp, 0x06, 0xe023);
			mdio_write(tp, 0x06, 0x0d06);
			mdio_write(tp, 0x06, 0x5803);
			mdio_write(tp, 0x06, 0xa002);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x2da0);
			mdio_write(tp, 0x06, 0x0102);
			mdio_write(tp, 0x06, 0xae2d);
			mdio_write(tp, 0x06, 0xa000);
			mdio_write(tp, 0x06, 0x4de0);
			mdio_write(tp, 0x06, 0xe200);
			mdio_write(tp, 0x06, 0xe1e2);
			mdio_write(tp, 0x06, 0x01ad);
			mdio_write(tp, 0x06, 0x2444);
			mdio_write(tp, 0x06, 0xe08a);
			mdio_write(tp, 0x06, 0xc2e4);
			mdio_write(tp, 0x06, 0x8ac4);
			mdio_write(tp, 0x06, 0xe08a);
			mdio_write(tp, 0x06, 0xc3e4);
			mdio_write(tp, 0x06, 0x8ac5);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xbe03);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x83ad);
			mdio_write(tp, 0x06, 0x253a);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xbe05);
			mdio_write(tp, 0x06, 0xae34);
			mdio_write(tp, 0x06, 0xe08a);
			mdio_write(tp, 0x06, 0xceae);
			mdio_write(tp, 0x06, 0x03e0);
			mdio_write(tp, 0x06, 0x8acf);
			mdio_write(tp, 0x06, 0xe18a);
			mdio_write(tp, 0x06, 0xc249);
			mdio_write(tp, 0x06, 0x05e5);
			mdio_write(tp, 0x06, 0x8ac4);
			mdio_write(tp, 0x06, 0xe18a);
			mdio_write(tp, 0x06, 0xc349);
			mdio_write(tp, 0x06, 0x05e5);
			mdio_write(tp, 0x06, 0x8ac5);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xbe05);
			mdio_write(tp, 0x06, 0x022a);
			mdio_write(tp, 0x06, 0xb6ac);
			mdio_write(tp, 0x06, 0x2012);
			mdio_write(tp, 0x06, 0x0283);
			mdio_write(tp, 0x06, 0xbaac);
			mdio_write(tp, 0x06, 0x200c);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xc100);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xc600);
			mdio_write(tp, 0x06, 0xee8a);
			mdio_write(tp, 0x06, 0xbe02);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0xd000);
			mdio_write(tp, 0x06, 0x0283);
			mdio_write(tp, 0x06, 0xcc59);
			mdio_write(tp, 0x06, 0x0f39);
			mdio_write(tp, 0x06, 0x02aa);
			mdio_write(tp, 0x06, 0x04d0);
			mdio_write(tp, 0x06, 0x01ae);
			mdio_write(tp, 0x06, 0x02d0);
			mdio_write(tp, 0x06, 0x0004);
			mdio_write(tp, 0x06, 0xf9fa);
			mdio_write(tp, 0x06, 0xe2e2);
			mdio_write(tp, 0x06, 0xd2e3);
			mdio_write(tp, 0x06, 0xe2d3);
			mdio_write(tp, 0x06, 0xf95a);
			mdio_write(tp, 0x06, 0xf7e6);
			mdio_write(tp, 0x06, 0xe2d2);
			mdio_write(tp, 0x06, 0xe7e2);
			mdio_write(tp, 0x06, 0xd3e2);
			mdio_write(tp, 0x06, 0xe02c);
			mdio_write(tp, 0x06, 0xe3e0);
			mdio_write(tp, 0x06, 0x2df9);
			mdio_write(tp, 0x06, 0x5be0);
			mdio_write(tp, 0x06, 0x1e30);
			mdio_write(tp, 0x06, 0xe6e0);
			mdio_write(tp, 0x06, 0x2ce7);
			mdio_write(tp, 0x06, 0xe02d);
			mdio_write(tp, 0x06, 0xe2e2);
			mdio_write(tp, 0x06, 0xcce3);
			mdio_write(tp, 0x06, 0xe2cd);
			mdio_write(tp, 0x06, 0xf95a);
			mdio_write(tp, 0x06, 0x0f6a);
			mdio_write(tp, 0x06, 0x50e6);
			mdio_write(tp, 0x06, 0xe2cc);
			mdio_write(tp, 0x06, 0xe7e2);
			mdio_write(tp, 0x06, 0xcde0);
			mdio_write(tp, 0x06, 0xe03c);
			mdio_write(tp, 0x06, 0xe1e0);
			mdio_write(tp, 0x06, 0x3def);
			mdio_write(tp, 0x06, 0x64fd);
			mdio_write(tp, 0x06, 0xe0e2);
			mdio_write(tp, 0x06, 0xcce1);
			mdio_write(tp, 0x06, 0xe2cd);
			mdio_write(tp, 0x06, 0x580f);
			mdio_write(tp, 0x06, 0x5af0);
			mdio_write(tp, 0x06, 0x1e02);
			mdio_write(tp, 0x06, 0xe4e2);
			mdio_write(tp, 0x06, 0xcce5);
			mdio_write(tp, 0x06, 0xe2cd);
			mdio_write(tp, 0x06, 0xfde0);
			mdio_write(tp, 0x06, 0xe02c);
			mdio_write(tp, 0x06, 0xe1e0);
			mdio_write(tp, 0x06, 0x2d59);
			mdio_write(tp, 0x06, 0xe05b);
			mdio_write(tp, 0x06, 0x1f1e);
			mdio_write(tp, 0x06, 0x13e4);
			mdio_write(tp, 0x06, 0xe02c);
			mdio_write(tp, 0x06, 0xe5e0);
			mdio_write(tp, 0x06, 0x2dfd);
			mdio_write(tp, 0x06, 0xe0e2);
			mdio_write(tp, 0x06, 0xd2e1);
			mdio_write(tp, 0x06, 0xe2d3);
			mdio_write(tp, 0x06, 0x58f7);
			mdio_write(tp, 0x06, 0x5a08);
			mdio_write(tp, 0x06, 0x1e02);
			mdio_write(tp, 0x06, 0xe4e2);
			mdio_write(tp, 0x06, 0xd2e5);
			mdio_write(tp, 0x06, 0xe2d3);
			mdio_write(tp, 0x06, 0xef46);
			mdio_write(tp, 0x06, 0xfefd);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xf9fa);
			mdio_write(tp, 0x06, 0xef69);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x22e1);
			mdio_write(tp, 0x06, 0xe023);
			mdio_write(tp, 0x06, 0x58c4);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x6e1f);
			mdio_write(tp, 0x06, 0x109e);
			mdio_write(tp, 0x06, 0x58e4);
			mdio_write(tp, 0x06, 0x8b6e);
			mdio_write(tp, 0x06, 0xad22);
			mdio_write(tp, 0x06, 0x22ac);
			mdio_write(tp, 0x06, 0x2755);
			mdio_write(tp, 0x06, 0xac26);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0x1ad1);
			mdio_write(tp, 0x06, 0x06bf);
			mdio_write(tp, 0x06, 0x3bba);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x07bf);
			mdio_write(tp, 0x06, 0x3bbd);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x07bf);
			mdio_write(tp, 0x06, 0x3bc0);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1ae);
			mdio_write(tp, 0x06, 0x30d1);
			mdio_write(tp, 0x06, 0x03bf);
			mdio_write(tp, 0x06, 0x3bc3);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x00bf);
			mdio_write(tp, 0x06, 0x3bc6);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x00bf);
			mdio_write(tp, 0x06, 0x84e9);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x0fbf);
			mdio_write(tp, 0x06, 0x3bba);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x01bf);
			mdio_write(tp, 0x06, 0x3bbd);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x01bf);
			mdio_write(tp, 0x06, 0x3bc0);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1ef);
			mdio_write(tp, 0x06, 0x96fe);
			mdio_write(tp, 0x06, 0xfdfc);
			mdio_write(tp, 0x06, 0x04d1);
			mdio_write(tp, 0x06, 0x00bf);
			mdio_write(tp, 0x06, 0x3bc3);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d0);
			mdio_write(tp, 0x06, 0x1102);
			mdio_write(tp, 0x06, 0x2bfb);
			mdio_write(tp, 0x06, 0x5903);
			mdio_write(tp, 0x06, 0xef01);
			mdio_write(tp, 0x06, 0xd100);
			mdio_write(tp, 0x06, 0xa000);
			mdio_write(tp, 0x06, 0x02d1);
			mdio_write(tp, 0x06, 0x01bf);
			mdio_write(tp, 0x06, 0x3bc6);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1d1);
			mdio_write(tp, 0x06, 0x11ad);
			mdio_write(tp, 0x06, 0x2002);
			mdio_write(tp, 0x06, 0x0c11);
			mdio_write(tp, 0x06, 0xad21);
			mdio_write(tp, 0x06, 0x020c);
			mdio_write(tp, 0x06, 0x12bf);
			mdio_write(tp, 0x06, 0x84e9);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1ae);
			mdio_write(tp, 0x06, 0xc870);
			mdio_write(tp, 0x06, 0xe426);
			mdio_write(tp, 0x06, 0x0284);
			mdio_write(tp, 0x06, 0xf005);
			mdio_write(tp, 0x06, 0xf8fa);
			mdio_write(tp, 0x06, 0xef69);
			mdio_write(tp, 0x06, 0xe0e2);
			mdio_write(tp, 0x06, 0xfee1);
			mdio_write(tp, 0x06, 0xe2ff);
			mdio_write(tp, 0x06, 0xad2d);
			mdio_write(tp, 0x06, 0x1ae0);
			mdio_write(tp, 0x06, 0xe14e);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x4fac);
			mdio_write(tp, 0x06, 0x2d22);
			mdio_write(tp, 0x06, 0xf603);
			mdio_write(tp, 0x06, 0x0203);
			mdio_write(tp, 0x06, 0x3bf7);
			mdio_write(tp, 0x06, 0x03f7);
			mdio_write(tp, 0x06, 0x06bf);
			mdio_write(tp, 0x06, 0x8561);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0x21ae);
			mdio_write(tp, 0x06, 0x11e0);
			mdio_write(tp, 0x06, 0xe14e);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x4fad);
			mdio_write(tp, 0x06, 0x2d08);
			mdio_write(tp, 0x06, 0xbf85);
			mdio_write(tp, 0x06, 0x6c02);
			mdio_write(tp, 0x06, 0x2d21);
			mdio_write(tp, 0x06, 0xf606);
			mdio_write(tp, 0x06, 0xef96);
			mdio_write(tp, 0x06, 0xfefc);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xfaef);
			mdio_write(tp, 0x06, 0x69e0);
			mdio_write(tp, 0x06, 0xe000);
			mdio_write(tp, 0x06, 0xe1e0);
			mdio_write(tp, 0x06, 0x01ad);
			mdio_write(tp, 0x06, 0x271f);
			mdio_write(tp, 0x06, 0xd101);
			mdio_write(tp, 0x06, 0xbf85);
			mdio_write(tp, 0x06, 0x5e02);
			mdio_write(tp, 0x06, 0x2dc1);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x20e1);
			mdio_write(tp, 0x06, 0xe021);
			mdio_write(tp, 0x06, 0xad20);
			mdio_write(tp, 0x06, 0x0ed1);
			mdio_write(tp, 0x06, 0x00bf);
			mdio_write(tp, 0x06, 0x855e);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0xc1bf);
			mdio_write(tp, 0x06, 0x3b96);
			mdio_write(tp, 0x06, 0x022d);
			mdio_write(tp, 0x06, 0x21ef);
			mdio_write(tp, 0x06, 0x96fe);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0x00e2);
			mdio_write(tp, 0x06, 0x34a7);
			mdio_write(tp, 0x06, 0x25e5);
			mdio_write(tp, 0x06, 0x0a1d);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x2ce5);
			mdio_write(tp, 0x06, 0x0a6d);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x1de5);
			mdio_write(tp, 0x06, 0x0a1c);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x2da7);
			mdio_write(tp, 0x06, 0x5500);
			mdio_write(tp, 0x05, 0x8b94);
			mdio_write(tp, 0x06, 0x84ec);
			gphy_val = mdio_read(tp, 0x01);
			gphy_val |= BIT_0;
			mdio_write(tp, 0x01, gphy_val);
			mdio_write(tp, 0x00, 0x0005);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x1f, 0x0005);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x00);
				if(gphy_val & BIT_7)
					break;
			}
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x17, 0x0116);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0028);
			mdio_write(tp, 0x15, 0x0010);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0020);
			mdio_write(tp, 0x15, 0x0100);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0041);
			mdio_write(tp, 0x15, 0x0802);
			mdio_write(tp, 0x16, 0x2185);
			mdio_write(tp, 0x1f, 0x0000);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
		}
		else if(tp->mcfg == CFG_METHOD_15)
		{
			spin_lock_irqsave(&tp->phy_lock, flags);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x00, 0x1800);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x17, 0x0117);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1E, 0x002C);
			mdio_write(tp, 0x1B, 0x5000);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x16, 0x4104);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x1E);
				gphy_val &= 0x03FF;
				if(gphy_val == 0x000C)
					break;
			}
			mdio_write(tp, 0x1f, 0x0005);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x07);
				if((gphy_val & BIT_5)==0)
					break;
			}
			gphy_val = mdio_read(tp, 0x07);
			if(gphy_val & BIT_5)
			{
	 			mdio_write(tp, 0x1f, 0x0007);
	 			mdio_write(tp, 0x1e, 0x00a1);
	 			mdio_write(tp, 0x17, 0x1000);
	 			mdio_write(tp, 0x17, 0x0000);
	 			mdio_write(tp, 0x17, 0x2000);
	 			mdio_write(tp, 0x1e, 0x002f);
	 			mdio_write(tp, 0x18, 0x9bfb);
	 			mdio_write(tp, 0x1f, 0x0005);
	 			mdio_write(tp, 0x07, 0x0000);
	 			mdio_write(tp, 0x1f, 0x0000);
			}
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			gphy_val = mdio_read(tp, 0x00);
			gphy_val &= ~(BIT_7);
			mdio_write(tp, 0x00, gphy_val);
			mdio_write(tp, 0x1f, 0x0002);
			gphy_val = mdio_read(tp, 0x08);
			gphy_val &= ~(BIT_7);
			mdio_write(tp, 0x08, gphy_val);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x16, 0x0306);
			mdio_write(tp, 0x16, 0x0307);
			mdio_write(tp, 0x15, 0x000e);
			mdio_write(tp, 0x19, 0x000a);
			mdio_write(tp, 0x15, 0x0010);
			mdio_write(tp, 0x19, 0x0008);
			mdio_write(tp, 0x15, 0x0018);
			mdio_write(tp, 0x19, 0x4801);
			mdio_write(tp, 0x15, 0x0019);
			mdio_write(tp, 0x19, 0x6801);
			mdio_write(tp, 0x15, 0x001a);
			mdio_write(tp, 0x19, 0x66a1);
			mdio_write(tp, 0x15, 0x001f);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0020);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0021);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0022);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0023);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0024);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0025);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x0026);
			mdio_write(tp, 0x19, 0x40ea);
			mdio_write(tp, 0x15, 0x0027);
			mdio_write(tp, 0x19, 0x4503);
			mdio_write(tp, 0x15, 0x0028);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0029);
			mdio_write(tp, 0x19, 0xa631);
			mdio_write(tp, 0x15, 0x002a);
			mdio_write(tp, 0x19, 0x9717);
			mdio_write(tp, 0x15, 0x002b);
			mdio_write(tp, 0x19, 0x302c);
			mdio_write(tp, 0x15, 0x002c);
			mdio_write(tp, 0x19, 0x4802);
			mdio_write(tp, 0x15, 0x002d);
			mdio_write(tp, 0x19, 0x58da);
			mdio_write(tp, 0x15, 0x002e);
			mdio_write(tp, 0x19, 0x400d);
			mdio_write(tp, 0x15, 0x002f);
			mdio_write(tp, 0x19, 0x4488);
			mdio_write(tp, 0x15, 0x0030);
			mdio_write(tp, 0x19, 0x9e00);
			mdio_write(tp, 0x15, 0x0031);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0032);
			mdio_write(tp, 0x19, 0x6481);
			mdio_write(tp, 0x15, 0x0033);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0034);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0035);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0036);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0037);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0038);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0039);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x003a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x003b);
			mdio_write(tp, 0x19, 0x63e8);
			mdio_write(tp, 0x15, 0x003c);
			mdio_write(tp, 0x19, 0x7d00);
			mdio_write(tp, 0x15, 0x003d);
			mdio_write(tp, 0x19, 0x59d4);
			mdio_write(tp, 0x15, 0x003e);
			mdio_write(tp, 0x19, 0x63f8);
			mdio_write(tp, 0x15, 0x0040);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x0041);
			mdio_write(tp, 0x19, 0x30de);
			mdio_write(tp, 0x15, 0x0044);
			mdio_write(tp, 0x19, 0x480f);
			mdio_write(tp, 0x15, 0x0045);
			mdio_write(tp, 0x19, 0x6800);
			mdio_write(tp, 0x15, 0x0046);
			mdio_write(tp, 0x19, 0x6680);
			mdio_write(tp, 0x15, 0x0047);
			mdio_write(tp, 0x19, 0x7c10);
			mdio_write(tp, 0x15, 0x0048);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0049);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004b);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004c);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004d);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004e);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x004f);
			mdio_write(tp, 0x19, 0x40ea);
			mdio_write(tp, 0x15, 0x0050);
			mdio_write(tp, 0x19, 0x4503);
			mdio_write(tp, 0x15, 0x0051);
			mdio_write(tp, 0x19, 0x58ca);
			mdio_write(tp, 0x15, 0x0052);
			mdio_write(tp, 0x19, 0x63c8);
			mdio_write(tp, 0x15, 0x0053);
			mdio_write(tp, 0x19, 0x63d8);
			mdio_write(tp, 0x15, 0x0054);
			mdio_write(tp, 0x19, 0x66a0);
			mdio_write(tp, 0x15, 0x0055);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0056);
			mdio_write(tp, 0x19, 0x3000);
			mdio_write(tp, 0x15, 0x00a1);
			mdio_write(tp, 0x19, 0x3044);
			mdio_write(tp, 0x15, 0x00ab);
			mdio_write(tp, 0x19, 0x5820);
			mdio_write(tp, 0x15, 0x00ac);
			mdio_write(tp, 0x19, 0x5e04);
			mdio_write(tp, 0x15, 0x00ad);
			mdio_write(tp, 0x19, 0xb60c);
			mdio_write(tp, 0x15, 0x00af);
			mdio_write(tp, 0x19, 0x000a);
			mdio_write(tp, 0x15, 0x00b2);
			mdio_write(tp, 0x19, 0x30b9);
			mdio_write(tp, 0x15, 0x00b9);
			mdio_write(tp, 0x19, 0x4408);
			mdio_write(tp, 0x15, 0x00ba);
			mdio_write(tp, 0x19, 0x480b);
			mdio_write(tp, 0x15, 0x00bb);
			mdio_write(tp, 0x19, 0x5e00);
			mdio_write(tp, 0x15, 0x00bc);
			mdio_write(tp, 0x19, 0x405f);
			mdio_write(tp, 0x15, 0x00bd);
			mdio_write(tp, 0x19, 0x4448);
			mdio_write(tp, 0x15, 0x00be);
			mdio_write(tp, 0x19, 0x4020);
			mdio_write(tp, 0x15, 0x00bf);
			mdio_write(tp, 0x19, 0x4468);
			mdio_write(tp, 0x15, 0x00c0);
			mdio_write(tp, 0x19, 0x9c02);
			mdio_write(tp, 0x15, 0x00c1);
			mdio_write(tp, 0x19, 0x58a0);
			mdio_write(tp, 0x15, 0x00c2);
			mdio_write(tp, 0x19, 0xb605);
			mdio_write(tp, 0x15, 0x00c3);
			mdio_write(tp, 0x19, 0xc0d3);
			mdio_write(tp, 0x15, 0x00c4);
			mdio_write(tp, 0x19, 0x00e6);
			mdio_write(tp, 0x15, 0x00c5);
			mdio_write(tp, 0x19, 0xdaec);
			mdio_write(tp, 0x15, 0x00c6);
			mdio_write(tp, 0x19, 0x00fa);
			mdio_write(tp, 0x15, 0x00c7);
			mdio_write(tp, 0x19, 0x9df9);
			mdio_write(tp, 0x15, 0x0112);
			mdio_write(tp, 0x19, 0x6421);
			mdio_write(tp, 0x15, 0x0113);
			mdio_write(tp, 0x19, 0x7c08);
			mdio_write(tp, 0x15, 0x0114);
			mdio_write(tp, 0x19, 0x63f0);
			mdio_write(tp, 0x15, 0x0115);
			mdio_write(tp, 0x19, 0x4003);
			mdio_write(tp, 0x15, 0x0116);
			mdio_write(tp, 0x19, 0x4418);
			mdio_write(tp, 0x15, 0x0117);
			mdio_write(tp, 0x19, 0x9b00);
			mdio_write(tp, 0x15, 0x0118);
			mdio_write(tp, 0x19, 0x6461);
			mdio_write(tp, 0x15, 0x0119);
			mdio_write(tp, 0x19, 0x64e1);
			mdio_write(tp, 0x15, 0x011a);
			mdio_write(tp, 0x19, 0x0000);
			mdio_write(tp, 0x15, 0x0150);
			mdio_write(tp, 0x19, 0x6461);
			mdio_write(tp, 0x15, 0x0151);
			mdio_write(tp, 0x19, 0x4003);
			mdio_write(tp, 0x15, 0x0152);
			mdio_write(tp, 0x19, 0x4540);
			mdio_write(tp, 0x15, 0x0153);
			mdio_write(tp, 0x19, 0x9f00);
			mdio_write(tp, 0x15, 0x0155);
			mdio_write(tp, 0x19, 0x6421);
			mdio_write(tp, 0x15, 0x0156);
			mdio_write(tp, 0x19, 0x64a1);
			mdio_write(tp, 0x15, 0x03bd);
			mdio_write(tp, 0x19, 0x405e);
			mdio_write(tp, 0x16, 0x0306);
			mdio_write(tp, 0x16, 0x0300);
			mdio_write(tp, 0x1f, 0x0005);
			mdio_write(tp, 0x05, 0xfff6);
			mdio_write(tp, 0x06, 0x0080);
			mdio_write(tp, 0x05, 0x8000);
			mdio_write(tp, 0x06, 0x0280);
			mdio_write(tp, 0x06, 0x48f7);
			mdio_write(tp, 0x06, 0x00e0);
			mdio_write(tp, 0x06, 0xfff7);
			mdio_write(tp, 0x06, 0xa080);
			mdio_write(tp, 0x06, 0x02ae);
			mdio_write(tp, 0x06, 0xf602);
			mdio_write(tp, 0x06, 0x0200);
			mdio_write(tp, 0x06, 0x0202);
			mdio_write(tp, 0x06, 0x1102);
			mdio_write(tp, 0x06, 0x0224);
			mdio_write(tp, 0x06, 0x0202);
			mdio_write(tp, 0x06, 0x3402);
			mdio_write(tp, 0x06, 0x027f);
			mdio_write(tp, 0x06, 0x0202);
			mdio_write(tp, 0x06, 0x9202);
			mdio_write(tp, 0x06, 0x8074);
			mdio_write(tp, 0x06, 0xe08b);
			mdio_write(tp, 0x06, 0x88e1);
			mdio_write(tp, 0x06, 0x8b89);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8a1e);
			mdio_write(tp, 0x06, 0x01e1);
			mdio_write(tp, 0x06, 0x8b8b);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8c1e);
			mdio_write(tp, 0x06, 0x01e1);
			mdio_write(tp, 0x06, 0x8b8d);
			mdio_write(tp, 0x06, 0x1e01);
			mdio_write(tp, 0x06, 0xe18b);
			mdio_write(tp, 0x06, 0x8e1e);
			mdio_write(tp, 0x06, 0x01a0);
			mdio_write(tp, 0x06, 0x00c7);
			mdio_write(tp, 0x06, 0xaebb);
			mdio_write(tp, 0x06, 0xd480);
			mdio_write(tp, 0x06, 0xe4e4);
			mdio_write(tp, 0x06, 0x8b94);
			mdio_write(tp, 0x06, 0xe58b);
			mdio_write(tp, 0x06, 0x95bf);
			mdio_write(tp, 0x06, 0x8b88);
			mdio_write(tp, 0x06, 0xec00);
			mdio_write(tp, 0x06, 0x19a9);
			mdio_write(tp, 0x06, 0x8b90);
			mdio_write(tp, 0x06, 0xf9ee);
			mdio_write(tp, 0x06, 0xfff6);
			mdio_write(tp, 0x06, 0x00ee);
			mdio_write(tp, 0x06, 0xfff7);
			mdio_write(tp, 0x06, 0xffe0);
			mdio_write(tp, 0x06, 0xe140);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x41f7);
			mdio_write(tp, 0x06, 0x2ff6);
			mdio_write(tp, 0x06, 0x28e4);
			mdio_write(tp, 0x06, 0xe140);
			mdio_write(tp, 0x06, 0xe5e1);
			mdio_write(tp, 0x06, 0x4104);
			mdio_write(tp, 0x06, 0xf8e0);
			mdio_write(tp, 0x06, 0x8b8e);
			mdio_write(tp, 0x06, 0xad20);
			mdio_write(tp, 0x06, 0x0ef6);
			mdio_write(tp, 0x06, 0x20e4);
			mdio_write(tp, 0x06, 0x8b8e);
			mdio_write(tp, 0x06, 0x0280);
			mdio_write(tp, 0x06, 0xb302);
			mdio_write(tp, 0x06, 0x1bf4);
			mdio_write(tp, 0x06, 0x022c);
			mdio_write(tp, 0x06, 0x9cad);
			mdio_write(tp, 0x06, 0x2211);
			mdio_write(tp, 0x06, 0xf622);
			mdio_write(tp, 0x06, 0xe48b);
			mdio_write(tp, 0x06, 0x8e02);
			mdio_write(tp, 0x06, 0x2c46);
			mdio_write(tp, 0x06, 0x022a);
			mdio_write(tp, 0x06, 0xc502);
			mdio_write(tp, 0x06, 0x2920);
			mdio_write(tp, 0x06, 0x022b);
			mdio_write(tp, 0x06, 0x91ad);
			mdio_write(tp, 0x06, 0x2511);
			mdio_write(tp, 0x06, 0xf625);
			mdio_write(tp, 0x06, 0xe48b);
			mdio_write(tp, 0x06, 0x8e02);
			mdio_write(tp, 0x06, 0x035a);
			mdio_write(tp, 0x06, 0x0204);
			mdio_write(tp, 0x06, 0x3a02);
			mdio_write(tp, 0x06, 0x1a59);
			mdio_write(tp, 0x06, 0x022b);
			mdio_write(tp, 0x06, 0xfcfc);
			mdio_write(tp, 0x06, 0x04f8);
			mdio_write(tp, 0x06, 0xfaef);
			mdio_write(tp, 0x06, 0x69e0);
			mdio_write(tp, 0x06, 0xe000);
			mdio_write(tp, 0x06, 0xe1e0);
			mdio_write(tp, 0x06, 0x01ad);
			mdio_write(tp, 0x06, 0x271f);
			mdio_write(tp, 0x06, 0xd101);
			mdio_write(tp, 0x06, 0xbf81);
			mdio_write(tp, 0x06, 0x3b02);
			mdio_write(tp, 0x06, 0x2f50);
			mdio_write(tp, 0x06, 0xe0e0);
			mdio_write(tp, 0x06, 0x20e1);
			mdio_write(tp, 0x06, 0xe021);
			mdio_write(tp, 0x06, 0xad20);
			mdio_write(tp, 0x06, 0x0ed1);
			mdio_write(tp, 0x06, 0x00bf);
			mdio_write(tp, 0x06, 0x813b);
			mdio_write(tp, 0x06, 0x022f);
			mdio_write(tp, 0x06, 0x50bf);
			mdio_write(tp, 0x06, 0x3d39);
			mdio_write(tp, 0x06, 0x022e);
			mdio_write(tp, 0x06, 0xb0ef);
			mdio_write(tp, 0x06, 0x96fe);
			mdio_write(tp, 0x06, 0xfc04);
			mdio_write(tp, 0x06, 0x0280);
			mdio_write(tp, 0x06, 0xe805);
			mdio_write(tp, 0x06, 0xf8fa);
			mdio_write(tp, 0x06, 0xef69);
			mdio_write(tp, 0x06, 0xe0e2);
			mdio_write(tp, 0x06, 0xfee1);
			mdio_write(tp, 0x06, 0xe2ff);
			mdio_write(tp, 0x06, 0xad2d);
			mdio_write(tp, 0x06, 0x1ae0);
			mdio_write(tp, 0x06, 0xe14e);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x4fac);
			mdio_write(tp, 0x06, 0x2d22);
			mdio_write(tp, 0x06, 0xf603);
			mdio_write(tp, 0x06, 0x0203);
			mdio_write(tp, 0x06, 0x36f7);
			mdio_write(tp, 0x06, 0x03f7);
			mdio_write(tp, 0x06, 0x06bf);
			mdio_write(tp, 0x06, 0x8125);
			mdio_write(tp, 0x06, 0x022e);
			mdio_write(tp, 0x06, 0xb0ae);
			mdio_write(tp, 0x06, 0x11e0);
			mdio_write(tp, 0x06, 0xe14e);
			mdio_write(tp, 0x06, 0xe1e1);
			mdio_write(tp, 0x06, 0x4fad);
			mdio_write(tp, 0x06, 0x2d08);
			mdio_write(tp, 0x06, 0xbf81);
			mdio_write(tp, 0x06, 0x3002);
			mdio_write(tp, 0x06, 0x2eb0);
			mdio_write(tp, 0x06, 0xf606);
			mdio_write(tp, 0x06, 0xef96);
			mdio_write(tp, 0x06, 0xfefc);
			mdio_write(tp, 0x06, 0x04a7);
			mdio_write(tp, 0x06, 0x25e5);
			mdio_write(tp, 0x06, 0x0a1d);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x2ce5);
			mdio_write(tp, 0x06, 0x0a6d);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x1de5);
			mdio_write(tp, 0x06, 0x0a1c);
			mdio_write(tp, 0x06, 0xe50a);
			mdio_write(tp, 0x06, 0x2da7);
			mdio_write(tp, 0x06, 0x5500);
			mdio_write(tp, 0x06, 0xe234);
			gphy_val = mdio_read(tp, 0x01);
			gphy_val |= BIT_0;
			mdio_write(tp, 0x01, gphy_val);
			mdio_write(tp, 0x00, 0x0005);
			mdio_write(tp, 0x1f, 0x0000);
			mdio_write(tp, 0x1f, 0x0005);
			for(i=0;i<200;i++)
			{
				udelay(100);
				gphy_val = mdio_read(tp, 0x00);
				if(gphy_val & BIT_7)
					break;
			}
			mdio_write(tp, 0x1f, 0x0007);
			mdio_write(tp, 0x1e, 0x0023);
			mdio_write(tp, 0x17, 0x0116);
			mdio_write(tp, 0x1f, 0x0000);
			spin_unlock_irqrestore(&tp->phy_lock, flags);
		}

		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0007);
		mdio_write(tp, 0x1E, 0x0023);
		mdio_write(tp, 0x17, 0x0116);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1f, 0x0005);
		mdio_write(tp, 0x05, 0x8b80);
		mdio_write(tp, 0x06, 0xc896);
		mdio_write(tp, 0x1f, 0x0000);

		mdio_write(tp, 0x1F, 0x0001);
		mdio_write(tp, 0x0B, 0x8C60);
		mdio_write(tp, 0x07, 0x2872);
		mdio_write(tp, 0x1C, 0xEFFF);
		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x14, 0x94B0);
		mdio_write(tp, 0x1F, 0x0000);

		mdio_write(tp, 0x1F, 0x0002);
		gphy_val = mdio_read(tp, 0x08) & 0x00FF;
		mdio_write(tp, 0x08, gphy_val | 0x8000);

		mdio_write(tp, 0x1F, 0x0007);
		mdio_write(tp, 0x1E, 0x002D);
		gphy_val = mdio_read(tp, 0x18);
		mdio_write(tp, 0x18, gphy_val | 0x0010);
		mdio_write(tp, 0x1F, 0x0000);
		gphy_val = mdio_read(tp, 0x14);
		mdio_write(tp, 0x14, gphy_val | 0x8000);

		mdio_write(tp, 0x1F, 0x0002);
		mdio_write(tp, 0x00, 0x080B);
		mdio_write(tp, 0x0B, 0x09D7);
		mdio_write(tp, 0x1f, 0x0000);
		mdio_write(tp, 0x15, 0x1006);

		mdio_write(tp, 0x1F, 0x0003);
		mdio_write(tp, 0x19, 0x7F46);
		mdio_write(tp, 0x1F, 0x0005);
		mdio_write(tp, 0x05, 0x8AD2);
		mdio_write(tp, 0x06, 0x6810);
		mdio_write(tp, 0x05, 0x8AD4);
		mdio_write(tp, 0x06, 0x8002);
		mdio_write(tp, 0x05, 0x8ADE);
		mdio_write(tp, 0x06, 0x8025);
		mdio_write(tp, 0x1F, 0x0000);
	}

	mdio_write(tp, 0x1F, 0x0000);

	spin_unlock_irqrestore(&tp->phy_lock, flags);
}

static inline void rtl8168_delete_esd_timer(struct net_device *dev, struct timer_list *timer)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	spin_lock_irq(&tp->lock);
	del_timer_sync(timer);
	spin_unlock_irq(&tp->lock);
}

static inline void rtl8168_request_esd_timer(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->esd_timer;

	init_timer(timer);
	timer->expires = jiffies + RTL8168_ESD_TIMEOUT;
	timer->data = (unsigned long)(dev);
	timer->function = rtl8168_esd_timer;
	add_timer(timer);
}

static inline void rtl8168_delete_link_timer(struct net_device *dev, struct timer_list *timer)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	spin_lock_irq(&tp->lock);
	del_timer_sync(timer);
	spin_unlock_irq(&tp->lock);
}

static inline void rtl8168_request_link_timer(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->link_timer;

	init_timer(timer);
	timer->expires = jiffies + RTL8168_LINK_TIMEOUT;
	timer->data = (unsigned long)(dev);
	timer->function = rtl8168_link_timer;
	add_timer(timer);
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void
rtl8168_netpoll(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;

	disable_irq(pdev->irq);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
	rtl8168_interrupt(pdev->irq, dev, NULL);
#else
	rtl8168_interrupt(pdev->irq, dev);
#endif
	enable_irq(pdev->irq);
}
#endif

static void
rtl8168_release_board(struct pci_dev *pdev,
		      struct net_device *dev,
		      void __iomem *ioaddr)
{
	rtl8168_phy_power_down(dev);
	iounmap(ioaddr);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	free_netdev(dev);
}

/**
 * rtl8168_set_mac_address - Change the Ethernet Address of the NIC
 * @dev: network interface device structure
 * @p:   pointer to an address structure
 *
 * Return 0 on success, negative on failure
 **/
static int
rtl8168_set_mac_address(struct net_device *dev,
			void *p)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	memcpy(tp->mac_addr, addr->sa_data, dev->addr_len);

	rtl8168_rar_set(tp, tp->mac_addr, 0);

	return 0;
}

/******************************************************************************
 * rtl8168_rar_set - Puts an ethernet address into a receive address register.
 *
 * tp - The private data structure for driver
 * addr - Address to put into receive address register
 * index - Receive address register to write
 *****************************************************************************/
void
rtl8168_rar_set(struct rtl8168_private *tp,
		uint8_t *addr,
		uint32_t index)
{
	void __iomem *ioaddr = tp->mmio_addr;
	uint32_t rar_low = 0;
	uint32_t rar_high = 0;

	rar_low = ((uint32_t) addr[0] |
		  ((uint32_t) addr[1] << 8) |
		  ((uint32_t) addr[2] << 16) |
		  ((uint32_t) addr[3] << 24));

	rar_high = ((uint32_t) addr[4] |
		   ((uint32_t) addr[5] << 8));

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W32(MAC0, rar_low);
	RTL_W32(MAC4, rar_high);
	RTL_W8(Cfg9346, Cfg9346_Lock);
}

#ifdef ETHTOOL_OPS_COMPAT
static int ethtool_get_settings(struct net_device *dev, void *useraddr)
{
	struct ethtool_cmd cmd = { ETHTOOL_GSET };
	int err;

	if (!ethtool_ops->get_settings)
		return -EOPNOTSUPP;

	err = ethtool_ops->get_settings(dev, &cmd);
	if (err < 0)
		return err;

	if (copy_to_user(useraddr, &cmd, sizeof(cmd)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_settings(struct net_device *dev, void *useraddr)
{
	struct ethtool_cmd cmd;

	if (!ethtool_ops->set_settings)
		return -EOPNOTSUPP;

	if (copy_from_user(&cmd, useraddr, sizeof(cmd)))
		return -EFAULT;

	return ethtool_ops->set_settings(dev, &cmd);
}

static int ethtool_get_drvinfo(struct net_device *dev, void *useraddr)
{
	struct ethtool_drvinfo info;
	struct ethtool_ops *ops = ethtool_ops;

	if (!ops->get_drvinfo)
		return -EOPNOTSUPP;

	memset(&info, 0, sizeof(info));
	info.cmd = ETHTOOL_GDRVINFO;
	ops->get_drvinfo(dev, &info);

	if (ops->self_test_count)
		info.testinfo_len = ops->self_test_count(dev);
	if (ops->get_stats_count)
		info.n_stats = ops->get_stats_count(dev);
	if (ops->get_regs_len)
		info.regdump_len = ops->get_regs_len(dev);
	if (ops->get_eeprom_len)
		info.eedump_len = ops->get_eeprom_len(dev);

	if (copy_to_user(useraddr, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_regs(struct net_device *dev, char *useraddr)
{
	struct ethtool_regs regs;
	struct ethtool_ops *ops = ethtool_ops;
	void *regbuf;
	int reglen, ret;

	if (!ops->get_regs || !ops->get_regs_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&regs, useraddr, sizeof(regs)))
		return -EFAULT;

	reglen = ops->get_regs_len(dev);
	if (regs.len > reglen)
		regs.len = reglen;

	regbuf = kmalloc(reglen, GFP_USER);
	if (!regbuf)
		return -ENOMEM;

	ops->get_regs(dev, &regs, regbuf);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &regs, sizeof(regs)))
		goto out;
	useraddr += offsetof(struct ethtool_regs, data);
	if (copy_to_user(useraddr, regbuf, reglen))
		goto out;
	ret = 0;

out:
	kfree(regbuf);
	return ret;
}

static int ethtool_get_wol(struct net_device *dev, char *useraddr)
{
	struct ethtool_wolinfo wol = { ETHTOOL_GWOL };

	if (!ethtool_ops->get_wol)
		return -EOPNOTSUPP;

	ethtool_ops->get_wol(dev, &wol);

	if (copy_to_user(useraddr, &wol, sizeof(wol)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_wol(struct net_device *dev, char *useraddr)
{
	struct ethtool_wolinfo wol;

	if (!ethtool_ops->set_wol)
		return -EOPNOTSUPP;

	if (copy_from_user(&wol, useraddr, sizeof(wol)))
		return -EFAULT;

	return ethtool_ops->set_wol(dev, &wol);
}

static int ethtool_get_msglevel(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GMSGLVL };

	if (!ethtool_ops->get_msglevel)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_msglevel(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_msglevel(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata;

	if (!ethtool_ops->set_msglevel)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	ethtool_ops->set_msglevel(dev, edata.data);
	return 0;
}

static int ethtool_nway_reset(struct net_device *dev)
{
	if (!ethtool_ops->nway_reset)
		return -EOPNOTSUPP;

	return ethtool_ops->nway_reset(dev);
}

static int ethtool_get_link(struct net_device *dev, void *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GLINK };

	if (!ethtool_ops->get_link)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_link(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_get_eeprom(struct net_device *dev, void *useraddr)
{
	struct ethtool_eeprom eeprom;
	struct ethtool_ops *ops = ethtool_ops;
	u8 *data;
	int ret;

	if (!ops->get_eeprom || !ops->get_eeprom_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > ops->get_eeprom_len(dev))
		return -EINVAL;

	data = kmalloc(eeprom.len, GFP_USER);
	if (!data)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(data, useraddr + sizeof(eeprom), eeprom.len))
		goto out;

	ret = ops->get_eeprom(dev, &eeprom, data);
	if (ret)
		goto out;

	ret = -EFAULT;
	if (copy_to_user(useraddr, &eeprom, sizeof(eeprom)))
		goto out;
	if (copy_to_user(useraddr + sizeof(eeprom), data, eeprom.len))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_set_eeprom(struct net_device *dev, void *useraddr)
{
	struct ethtool_eeprom eeprom;
	struct ethtool_ops *ops = ethtool_ops;
	u8 *data;
	int ret;

	if (!ops->set_eeprom || !ops->get_eeprom_len)
		return -EOPNOTSUPP;

	if (copy_from_user(&eeprom, useraddr, sizeof(eeprom)))
		return -EFAULT;

	/* Check for wrap and zero */
	if (eeprom.offset + eeprom.len <= eeprom.offset)
		return -EINVAL;

	/* Check for exceeding total eeprom len */
	if (eeprom.offset + eeprom.len > ops->get_eeprom_len(dev))
		return -EINVAL;

	data = kmalloc(eeprom.len, GFP_USER);
	if (!data)
		return -ENOMEM;

	ret = -EFAULT;
	if (copy_from_user(data, useraddr + sizeof(eeprom), eeprom.len))
		goto out;

	ret = ops->set_eeprom(dev, &eeprom, data);
	if (ret)
		goto out;

	if (copy_to_user(useraddr + sizeof(eeprom), data, eeprom.len))
		ret = -EFAULT;

out:
	kfree(data);
	return ret;
}

static int ethtool_get_coalesce(struct net_device *dev, void *useraddr)
{
	struct ethtool_coalesce coalesce = { ETHTOOL_GCOALESCE };

	if (!ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;

	ethtool_ops->get_coalesce(dev, &coalesce);

	if (copy_to_user(useraddr, &coalesce, sizeof(coalesce)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_coalesce(struct net_device *dev, void *useraddr)
{
	struct ethtool_coalesce coalesce;

	if (!ethtool_ops->get_coalesce)
		return -EOPNOTSUPP;

	if (copy_from_user(&coalesce, useraddr, sizeof(coalesce)))
		return -EFAULT;

	return ethtool_ops->set_coalesce(dev, &coalesce);
}

static int ethtool_get_ringparam(struct net_device *dev, void *useraddr)
{
	struct ethtool_ringparam ringparam = { ETHTOOL_GRINGPARAM };

	if (!ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;

	ethtool_ops->get_ringparam(dev, &ringparam);

	if (copy_to_user(useraddr, &ringparam, sizeof(ringparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_ringparam(struct net_device *dev, void *useraddr)
{
	struct ethtool_ringparam ringparam;

	if (!ethtool_ops->get_ringparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&ringparam, useraddr, sizeof(ringparam)))
		return -EFAULT;

	return ethtool_ops->set_ringparam(dev, &ringparam);
}

static int ethtool_get_pauseparam(struct net_device *dev, void *useraddr)
{
	struct ethtool_pauseparam pauseparam = { ETHTOOL_GPAUSEPARAM };

	if (!ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	ethtool_ops->get_pauseparam(dev, &pauseparam);

	if (copy_to_user(useraddr, &pauseparam, sizeof(pauseparam)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_pauseparam(struct net_device *dev, void *useraddr)
{
	struct ethtool_pauseparam pauseparam;

	if (!ethtool_ops->get_pauseparam)
		return -EOPNOTSUPP;

	if (copy_from_user(&pauseparam, useraddr, sizeof(pauseparam)))
		return -EFAULT;

	return ethtool_ops->set_pauseparam(dev, &pauseparam);
}

static int ethtool_get_rx_csum(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GRXCSUM };

	if (!ethtool_ops->get_rx_csum)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_rx_csum(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_rx_csum(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata;

	if (!ethtool_ops->set_rx_csum)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	ethtool_ops->set_rx_csum(dev, edata.data);
	return 0;
}

static int ethtool_get_tx_csum(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GTXCSUM };

	if (!ethtool_ops->get_tx_csum)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_tx_csum(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_tx_csum(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata;

	if (!ethtool_ops->set_tx_csum)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	return ethtool_ops->set_tx_csum(dev, edata.data);
}

static int ethtool_get_sg(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GSG };

	if (!ethtool_ops->get_sg)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_sg(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_sg(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata;

	if (!ethtool_ops->set_sg)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	return ethtool_ops->set_sg(dev, edata.data);
}

static int ethtool_get_tso(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata = { ETHTOOL_GTSO };

	if (!ethtool_ops->get_tso)
		return -EOPNOTSUPP;

	edata.data = ethtool_ops->get_tso(dev);

	if (copy_to_user(useraddr, &edata, sizeof(edata)))
		return -EFAULT;
	return 0;
}

static int ethtool_set_tso(struct net_device *dev, char *useraddr)
{
	struct ethtool_value edata;

	if (!ethtool_ops->set_tso)
		return -EOPNOTSUPP;

	if (copy_from_user(&edata, useraddr, sizeof(edata)))
		return -EFAULT;

	return ethtool_ops->set_tso(dev, edata.data);
}

static int ethtool_self_test(struct net_device *dev, char *useraddr)
{
	struct ethtool_test test;
	struct ethtool_ops *ops = ethtool_ops;
	u64 *data;
	int ret;

	if (!ops->self_test || !ops->self_test_count)
		return -EOPNOTSUPP;

	if (copy_from_user(&test, useraddr, sizeof(test)))
		return -EFAULT;

	test.len = ops->self_test_count(dev);
	data = kmalloc(test.len * sizeof(u64), GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->self_test(dev, &test, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &test, sizeof(test)))
		goto out;
	useraddr += sizeof(test);
	if (copy_to_user(useraddr, data, test.len * sizeof(u64)))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_get_strings(struct net_device *dev, void *useraddr)
{
	struct ethtool_gstrings gstrings;
	struct ethtool_ops *ops = ethtool_ops;
	u8 *data;
	int ret;

	if (!ops->get_strings)
		return -EOPNOTSUPP;

	if (copy_from_user(&gstrings, useraddr, sizeof(gstrings)))
		return -EFAULT;

	switch (gstrings.string_set) {
	case ETH_SS_TEST:
		if (!ops->self_test_count)
			return -EOPNOTSUPP;
		gstrings.len = ops->self_test_count(dev);
		break;
	case ETH_SS_STATS:
		if (!ops->get_stats_count)
			return -EOPNOTSUPP;
		gstrings.len = ops->get_stats_count(dev);
		break;
	default:
		return -EINVAL;
	}

	data = kmalloc(gstrings.len * ETH_GSTRING_LEN, GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->get_strings(dev, gstrings.string_set, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &gstrings, sizeof(gstrings)))
		goto out;
	useraddr += sizeof(gstrings);
	if (copy_to_user(useraddr, data, gstrings.len * ETH_GSTRING_LEN))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_phys_id(struct net_device *dev, void *useraddr)
{
	struct ethtool_value id;

	if (!ethtool_ops->phys_id)
		return -EOPNOTSUPP;

	if (copy_from_user(&id, useraddr, sizeof(id)))
		return -EFAULT;

	return ethtool_ops->phys_id(dev, id.data);
}

static int ethtool_get_stats(struct net_device *dev, void *useraddr)
{
	struct ethtool_stats stats;
	struct ethtool_ops *ops = ethtool_ops;
	u64 *data;
	int ret;

	if (!ops->get_ethtool_stats || !ops->get_stats_count)
		return -EOPNOTSUPP;

	if (copy_from_user(&stats, useraddr, sizeof(stats)))
		return -EFAULT;

	stats.n_stats = ops->get_stats_count(dev);
	data = kmalloc(stats.n_stats * sizeof(u64), GFP_USER);
	if (!data)
		return -ENOMEM;

	ops->get_ethtool_stats(dev, &stats, data);

	ret = -EFAULT;
	if (copy_to_user(useraddr, &stats, sizeof(stats)))
		goto out;
	useraddr += sizeof(stats);
	if (copy_to_user(useraddr, data, stats.n_stats * sizeof(u64)))
		goto out;
	ret = 0;

out:
	kfree(data);
	return ret;
}

static int ethtool_ioctl(struct ifreq *ifr)
{
	struct net_device *dev = __dev_get_by_name(ifr->ifr_name);
	void *useraddr = (void *) ifr->ifr_data;
	u32 ethcmd;

	/*
	 * XXX: This can be pushed down into the ethtool_* handlers that
	 * need it.  Keep existing behaviour for the moment.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (!dev || !netif_device_present(dev))
		return -ENODEV;

	if (copy_from_user(&ethcmd, useraddr, sizeof (ethcmd)))
		return -EFAULT;

	switch (ethcmd) {
	case ETHTOOL_GSET:
		return ethtool_get_settings(dev, useraddr);
	case ETHTOOL_SSET:
		return ethtool_set_settings(dev, useraddr);
	case ETHTOOL_GDRVINFO:
		return ethtool_get_drvinfo(dev, useraddr);
	case ETHTOOL_GREGS:
		return ethtool_get_regs(dev, useraddr);
	case ETHTOOL_GWOL:
		return ethtool_get_wol(dev, useraddr);
	case ETHTOOL_SWOL:
		return ethtool_set_wol(dev, useraddr);
	case ETHTOOL_GMSGLVL:
		return ethtool_get_msglevel(dev, useraddr);
	case ETHTOOL_SMSGLVL:
		return ethtool_set_msglevel(dev, useraddr);
	case ETHTOOL_NWAY_RST:
		return ethtool_nway_reset(dev);
	case ETHTOOL_GLINK:
		return ethtool_get_link(dev, useraddr);
	case ETHTOOL_GEEPROM:
		return ethtool_get_eeprom(dev, useraddr);
	case ETHTOOL_SEEPROM:
		return ethtool_set_eeprom(dev, useraddr);
	case ETHTOOL_GCOALESCE:
		return ethtool_get_coalesce(dev, useraddr);
	case ETHTOOL_SCOALESCE:
		return ethtool_set_coalesce(dev, useraddr);
	case ETHTOOL_GRINGPARAM:
		return ethtool_get_ringparam(dev, useraddr);
	case ETHTOOL_SRINGPARAM:
		return ethtool_set_ringparam(dev, useraddr);
	case ETHTOOL_GPAUSEPARAM:
		return ethtool_get_pauseparam(dev, useraddr);
	case ETHTOOL_SPAUSEPARAM:
		return ethtool_set_pauseparam(dev, useraddr);
	case ETHTOOL_GRXCSUM:
		return ethtool_get_rx_csum(dev, useraddr);
	case ETHTOOL_SRXCSUM:
		return ethtool_set_rx_csum(dev, useraddr);
	case ETHTOOL_GTXCSUM:
		return ethtool_get_tx_csum(dev, useraddr);
	case ETHTOOL_STXCSUM:
		return ethtool_set_tx_csum(dev, useraddr);
	case ETHTOOL_GSG:
		return ethtool_get_sg(dev, useraddr);
	case ETHTOOL_SSG:
		return ethtool_set_sg(dev, useraddr);
	case ETHTOOL_GTSO:
		return ethtool_get_tso(dev, useraddr);
	case ETHTOOL_STSO:
		return ethtool_set_tso(dev, useraddr);
	case ETHTOOL_TEST:
		return ethtool_self_test(dev, useraddr);
	case ETHTOOL_GSTRINGS:
		return ethtool_get_strings(dev, useraddr);
	case ETHTOOL_PHYS_ID:
		return ethtool_phys_id(dev, useraddr);
	case ETHTOOL_GSTATS:
		return ethtool_get_stats(dev, useraddr);
	default:
		return -EOPNOTSUPP;
	}

	return -EOPNOTSUPP;
}
#endif //ETHTOOL_OPS_COMPAT

static int
rtl8168_do_ioctl(struct net_device *dev,
		 struct ifreq *ifr,
		 int cmd)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct mii_ioctl_data *data = if_mii(ifr);
	unsigned long flags;
	int	ret;

	ret = 0;
	switch (cmd) {
	case SIOCGMIIPHY:
		data->phy_id = 32; /* Internal PHY */
		break;

	case SIOCGMIIREG:
		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0000);
		data->val_out = mdio_read(tp, data->reg_num);
		spin_unlock_irqrestore(&tp->phy_lock, flags);
		break;

	case SIOCSMIIREG:
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		spin_lock_irqsave(&tp->phy_lock, flags);
		mdio_write(tp, 0x1F, 0x0000);
		mdio_write(tp, data->reg_num, data->val_in);
		spin_unlock_irqrestore(&tp->phy_lock, flags);
		break;

#ifdef ETHTOOL_OPS_COMPAT
	case SIOCETHTOOL:
		ret = ethtool_ioctl(ifr);
		break;
#endif
	case SIOCDEVPRIVATE_RTLASF:
		if (!netif_running(dev))
		{
			ret = -ENODEV;
			break;
		}

		ret = rtl8168_asf_ioctl(dev, ifr);
		break;

	case SIOCRTLTOOL:
	{
		struct rtltool_cmd	my_cmd;

		if (!capable(CAP_NET_ADMIN))
		{
			ret = -EPERM;
			break;
		}

		if (copy_from_user(&my_cmd, ifr->ifr_data, sizeof(struct rtltool_cmd)))
		{
			ret = -EFAULT;
			break;
		}

		switch(my_cmd.cmd)
		{
			case RTLTOOL_READ_MAC:
				if(my_cmd.len==1)
				{
					my_cmd.data = readb(tp->mmio_addr+my_cmd.offset);
				}
				else if(my_cmd.len==2)
				{
					my_cmd.data = readw(tp->mmio_addr+(my_cmd.offset&~1));
				}
				else if(my_cmd.len==4)
				{
					my_cmd.data = readl(tp->mmio_addr+(my_cmd.offset&~3));
				}
				else
				{
					ret = -EOPNOTSUPP;
					break;
				}

				if (copy_to_user(ifr->ifr_data, &my_cmd, sizeof(struct rtltool_cmd)))
				{
					ret = -EFAULT;
					break;
				}

				break;

			case RTLTOOL_WRITE_MAC:
				if(my_cmd.len==1)
				{
					writeb(my_cmd.data, tp->mmio_addr+my_cmd.offset);
				}
				else if(my_cmd.len==2)
				{
					writew(my_cmd.data, tp->mmio_addr+(my_cmd.offset&~1));
				}
				else if(my_cmd.len==4)
				{
					writel(my_cmd.data, tp->mmio_addr+(my_cmd.offset&~3));
				}
				else
				{
					ret = -EOPNOTSUPP;
					break;
				}

				break;

			case RTLTOOL_READ_PHY:
				spin_lock_irqsave(&tp->phy_lock, flags);
				my_cmd.data = mdio_read(tp, my_cmd.offset);
				spin_unlock_irqrestore(&tp->phy_lock, flags);

				if (copy_to_user(ifr->ifr_data, &my_cmd, sizeof(struct rtltool_cmd)))
				{
					ret = -EFAULT;
					break;
				}

				break;

			case RTLTOOL_WRITE_PHY:
				spin_lock_irqsave(&tp->phy_lock, flags);
				mdio_write(tp, my_cmd.offset, my_cmd.data);
				spin_unlock_irqrestore(&tp->phy_lock, flags);
				break;

			case RTLTOOL_READ_EPHY:
				my_cmd.data = rtl8168_ephy_read(tp->mmio_addr, my_cmd.offset);

				if (copy_to_user(ifr->ifr_data, &my_cmd, sizeof(struct rtltool_cmd)))
				{
					ret = -EFAULT;
					break;
				}

				break;

			case RTLTOOL_WRITE_EPHY:
				rtl8168_ephy_write(tp->mmio_addr, my_cmd.offset, my_cmd.data);
				break;

			default:
				ret = -EOPNOTSUPP;
				break;
		}

		break;
	}

	default:
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static void
rtl8168_phy_power_up (struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->phy_lock, flags);
	mdio_write(tp, 0x1F, 0x0000);
	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			break;
		default:
			mdio_write(tp, 0x0E, 0x0000);
			break;
	}
	mdio_write(tp, MII_BMCR, BMCR_ANENABLE);
	spin_unlock_irqrestore(&tp->phy_lock, flags);
}

static void
rtl8168_phy_power_down (struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned long flags;

	spin_lock_irqsave(&tp->phy_lock, flags);
	mdio_write(tp, 0x1F, 0x0000);
	switch(tp->mcfg)
	{
		case CFG_METHOD_14:
		case CFG_METHOD_15:
			mdio_write(tp, MII_BMCR, BMCR_ANENABLE|BMCR_PDOWN);
			break;
		default:
			mdio_write(tp, 0x0E, 0x0200);
			mdio_write(tp, MII_BMCR, BMCR_PDOWN);
			break;
	}
	spin_unlock_irqrestore(&tp->phy_lock, flags);
}

static int __devinit
rtl8168_init_board(struct pci_dev *pdev,
		   struct net_device **dev_out,
		   void __iomem **ioaddr_out)
{
	void __iomem *ioaddr;
	struct net_device *dev;
	struct rtl8168_private *tp;
	int rc = -ENOMEM, i, acpi_idle_state = 0, pm_cap;

	assert(ioaddr_out != NULL);

	/* dev zeroed in alloc_etherdev */
	dev = alloc_etherdev(sizeof (*tp));
	if (dev == NULL) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_drv(&debug))
			dev_err(&pdev->dev, "unable to alloc new ethernet\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		goto err_out;
	}

	SET_MODULE_OWNER(dev);
	SET_NETDEV_DEV(dev, &pdev->dev);
	tp = netdev_priv(dev);
	tp->dev = dev;
	tp->msg_enable = netif_msg_init(debug.msg_enable, R8168_MSG_DEFAULT);

	/* enable device (incl. PCI PM wakeup and hotplug setup) */
	rc = pci_enable_device(pdev);
	if (rc < 0) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "enable failure\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		goto err_out_free_dev;
	}

	rc = pci_set_mwi(pdev);
	if (rc < 0)
		goto err_out_disable;

	/* save power state before pci_enable_device overwrites it */
	pm_cap = pci_find_capability(pdev, PCI_CAP_ID_PM);
	if (pm_cap) {
		u16 pwr_command;

		pci_read_config_word(pdev, pm_cap + PCI_PM_CTRL, &pwr_command);
		acpi_idle_state = pwr_command & PCI_PM_CTRL_STATE_MASK;
	} else {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp)) {
			dev_err(&pdev->dev, "PowerManagement capability not found.\n");
		}
#else
		printk("PowerManagement capability not found.\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)

	}

	/* make sure PCI base addr 1 is MMIO */
	if (!(pci_resource_flags(pdev, 2) & IORESOURCE_MEM)) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "region #1 not an MMIO resource, aborting\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		rc = -ENODEV;
		goto err_out_mwi;
	}
	/* check for weird/broken PCI region reporting */
	if (pci_resource_len(pdev, 2) < R8168_REGS_SIZE) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "Invalid PCI region size(s), aborting\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		rc = -ENODEV;
		goto err_out_mwi;
	}

	rc = pci_request_regions(pdev, MODULENAME);
	if (rc < 0) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "could not request regions.\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		goto err_out_mwi;
	}

	if ((sizeof(dma_addr_t) > 4) &&
	    !pci_set_dma_mask(pdev, DMA_64BIT_MASK) && use_dac) {
		dev->features |= NETIF_F_HIGHDMA;
	} else {
		rc = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
		if (rc < 0) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
			if (netif_msg_probe(tp))
				dev_err(&pdev->dev, "DMA configuration failed.\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
			goto err_out_free_res;
		}
	}

	pci_set_master(pdev);

	/* ioremap MMIO region */
	ioaddr = ioremap(pci_resource_start(pdev, 2), R8168_REGS_SIZE);
	if (ioaddr == NULL) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp))
			dev_err(&pdev->dev, "cannot remap MMIO, aborting\n");
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		rc = -EIO;
		goto err_out_free_res;
	}

	/* Identify chip attached to board */
	rc = rtl8168_get_mac_version(tp, ioaddr);
	if (rc < 0)
		goto err_out_free_res;

	rtl8168_print_mac_version(tp);

	for (i = ARRAY_SIZE(rtl_chip_info) - 1; i >= 0; i--) {
		if (tp->mcfg == rtl_chip_info[i].mcfg)
			break;
	}

	if (i < 0) {
		/* Unknown chip: assume array element #0, original RTL-8168 */
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		if (netif_msg_probe(tp)) {
			dev_printk(KERN_DEBUG, &pdev->dev, "unknown chip version, assuming %s\n", rtl_chip_info[0].name);
		}
#else
		printk("Realtek unknown chip version, assuming %s\n", rtl_chip_info[0].name);
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
		i++;
	}

	tp->chipset = i;

	RTL_W8(Cfg9346, Cfg9346_Unlock);
	RTL_W8(Config1, RTL_R8(Config1) | PMEnable);
//	RTL_W8(Config5, RTL_R8(Config5) & PMEStatus);
	RTL_W8(Cfg9346, Cfg9346_Lock);

	*ioaddr_out = ioaddr;
	*dev_out = dev;
out:
	return rc;

err_out_free_res:
	pci_release_regions(pdev);

err_out_mwi:
	pci_clear_mwi(pdev);

err_out_disable:
	pci_disable_device(pdev);

err_out_free_dev:
	free_netdev(dev);
err_out:
	*ioaddr_out = NULL;
	*dev_out = NULL;
	goto out;
}

static void
rtl8168_esd_timer(unsigned long __opaque)
{
	struct net_device *dev = (struct net_device *)__opaque;
	struct rtl8168_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	struct timer_list *timer = &tp->esd_timer;
	unsigned long timeout = RTL8168_ESD_TIMEOUT;
	u8 cmd;
	u8 cls;
	u16 io_base_l;
	u16 io_base_h;
	u16 mem_base_l;
	u16 mem_base_h;
	u8 ilr;
	u16 resv_0x20_l;
	u16 resv_0x20_h;
	u16 resv_0x24_l;
	u16 resv_0x24_h;

	tp->esd_flag = 0;

	pci_read_config_byte(pdev, PCI_COMMAND, &cmd);
	if (cmd != tp->pci_cfg_space.cmd) {
		pci_write_config_byte(pdev, PCI_COMMAND, tp->pci_cfg_space.cmd);
		tp->esd_flag = 1;
	}

	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &cls);
	if (cls != tp->pci_cfg_space.cls) {
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, tp->pci_cfg_space.cls);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_0, &io_base_l);
	if (io_base_l != tp->pci_cfg_space.io_base_l) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_0, tp->pci_cfg_space.io_base_l);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_0 + 2, &io_base_h);
	if (io_base_h != tp->pci_cfg_space.io_base_h) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_0 + 2, tp->pci_cfg_space.io_base_h);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_2, &mem_base_l);
	if (mem_base_l != tp->pci_cfg_space.mem_base_l) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_2, tp->pci_cfg_space.mem_base_l);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_2 + 2, &mem_base_h);
	if (mem_base_h != tp->pci_cfg_space.mem_base_h) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_2 + 2, tp->pci_cfg_space.mem_base_h);
		tp->esd_flag = 1;
	}

	pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &ilr);
	if (ilr != tp->pci_cfg_space.ilr) {
		pci_write_config_byte(pdev, PCI_INTERRUPT_LINE, tp->pci_cfg_space.ilr);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_4, &resv_0x20_l);
	if (resv_0x20_l != tp->pci_cfg_space.resv_0x20_l) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_4, tp->pci_cfg_space.resv_0x20_l);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_4 + 2, &resv_0x20_h);
	if (resv_0x20_h != tp->pci_cfg_space.resv_0x20_h) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_4 + 2, tp->pci_cfg_space.resv_0x20_h);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_5, &resv_0x24_l);
	if (resv_0x24_l != tp->pci_cfg_space.resv_0x24_l) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_5, tp->pci_cfg_space.resv_0x24_l);
		tp->esd_flag = 1;
	}

	pci_read_config_word(pdev, PCI_BASE_ADDRESS_5 + 2, &resv_0x24_h);
	if (resv_0x24_h != tp->pci_cfg_space.resv_0x24_h) {
		pci_write_config_word(pdev, PCI_BASE_ADDRESS_5 + 2, tp->pci_cfg_space.resv_0x24_h);
		tp->esd_flag = 1;
	}

	if (tp->esd_flag != 0) {
		rtl8168_tx_clear(tp);
		rtl8168_rx_clear(tp);
		rtl8168_open(dev);
		tp->esd_flag = 0;
	}

	mod_timer(timer, jiffies + timeout);
}

static void
rtl8168_link_timer(unsigned long __opaque)
{
	struct net_device *dev = (struct net_device *)__opaque;
	struct rtl8168_private *tp = netdev_priv(dev);
	struct timer_list *timer = &tp->link_timer;

	if (tp->link_ok(dev) != tp->old_link_status)
		rtl8168_check_link_status(dev, tp, tp->mmio_addr);

	tp->old_link_status = tp->link_ok(dev);

	mod_timer(timer, jiffies + RTL8168_LINK_TIMEOUT);
}

/* Cfg9346_Unlock assumed. */
static unsigned rtl8168_try_msi(struct pci_dev *pdev, void __iomem *ioaddr)
{
	unsigned msi = 0;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13)
	if (pci_enable_msi(pdev)) {
			dev_info(&pdev->dev, "no MSI. Back to INTx.\n");
	} else {
			msi |= RTL_FEATURE_MSI;
	}
#endif

	return msi;
}

static void rtl8168_disable_msi(struct pci_dev *pdev, struct rtl8168_private *tp)
{
	if (tp->features & RTL_FEATURE_MSI) {
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13)
		pci_disable_msi(pdev);
#endif
		tp->features &= ~RTL_FEATURE_MSI;
	}
}

#ifdef HAVE_NET_DEVICE_OPS
static const struct net_device_ops rtl8168_netdev_ops = {
	.ndo_open		= rtl8168_open,
	.ndo_stop		= rtl8168_close,
	.ndo_get_stats		= rtl8168_get_stats,
	.ndo_start_xmit		= rtl8168_start_xmit,
	.ndo_tx_timeout		= rtl8168_tx_timeout,
	.ndo_change_mtu		= rtl8168_change_mtu,
	.ndo_set_mac_address	= rtl8168_set_mac_address,
	.ndo_do_ioctl		= rtl8168_do_ioctl,
	.ndo_set_multicast_list	= rtl8168_set_rx_mode,
#ifdef CONFIG_R8168_VLAN
	.ndo_vlan_rx_register	= rtl8168_vlan_rx_register,
#endif
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= rtl8168_netpoll,
#endif
};
#endif //HAVE_NET_DEVICE_OPS

static int __devinit
rtl8168_init_one(struct pci_dev *pdev,
		 const struct pci_device_id *ent)
{
	struct net_device *dev = NULL;
	struct rtl8168_private *tp;
	void __iomem *ioaddr = NULL;
	static int board_idx = -1;
	u8 autoneg, duplex;
	u16 speed;
	u16 mac_addr[4];

	int i, rc;

	assert(pdev != NULL);
	assert(ent != NULL);

	board_idx++;

	if (netif_msg_drv(&debug)) {
		printk(KERN_INFO "%s Gigabit Ethernet driver %s loaded\n",
		       MODULENAME, RTL8168_VERSION);
	}

	rc = rtl8168_init_board(pdev, &dev, &ioaddr);
	if (rc)
		return rc;

	tp = netdev_priv(dev);
	assert(ioaddr != NULL);

	tp->mmio_addr = ioaddr;
	tp->set_speed = rtl8168_set_speed_xmii;
	tp->get_settings = rtl8168_gset_xmii;
	tp->phy_reset_enable = rtl8168_xmii_reset_enable;
	tp->phy_reset_pending = rtl8168_xmii_reset_pending;
	tp->link_ok = rtl8168_xmii_link_ok;

	tp->features |= rtl8168_try_msi(pdev, ioaddr);

        if ((tp->mcfg == CFG_METHOD_9) || (tp->mcfg == CFG_METHOD_10)) {
             RTL_W8(DBG_reg, RTL_R8(DBG_reg) | BIT_1 | BIT_7);
        }

	/* Get production from EEPROM */
	rtl_eeprom_type(tp);
        if (tp->eeprom_type != EEPROM_TYPE_NONE) {
            	/* Get MAC address from EEPROM */
        	mac_addr[0] = rtl_eeprom_read_sc(tp, 7);
   	        mac_addr[1] = rtl_eeprom_read_sc(tp, 8);
	        mac_addr[2] = rtl_eeprom_read_sc(tp, 9);
	        mac_addr[3] = 0;
	        RTL_W8(Cfg9346, Cfg9346_Unlock);
	        RTL_W32(MAC0, (mac_addr[1] << 16) | mac_addr[0]);
	        RTL_W32(MAC4, (mac_addr[3] << 16) | mac_addr[2]);
   	        RTL_W8(Cfg9346, Cfg9346_Lock);
        }

	for (i = 0; i < MAC_ADDR_LEN; i++) {
		dev->dev_addr[i] = RTL_R8(MAC0 + i);
		tp->org_mac_addr[i] = dev->dev_addr[i]; /* keep the original MAC address */
	}
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,13)
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);
#endif
//	memcpy(dev->dev_addr, dev->dev_addr, dev->addr_len);

	RTL_NET_DEVICE_OPS(rtl8168_netdev_ops);

	SET_ETHTOOL_OPS(dev, &rtl8168_ethtool_ops);

	dev->watchdog_timeo = RTL8168_TX_TIMEOUT;
	dev->irq = pdev->irq;
	dev->base_addr = (unsigned long) ioaddr;

#ifdef CONFIG_R8168_NAPI
	RTL_NAPI_CONFIG(dev, tp, rtl8168_poll, R8168_NAPI_WEIGHT);
#endif

#ifdef CONFIG_R8168_VLAN
	dev->features |= NETIF_F_HW_VLAN_TX | NETIF_F_HW_VLAN_RX;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	dev->vlan_rx_kill_vid = rtl8168_vlan_rx_kill_vid;
#endif //LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
#endif

	dev->features |= NETIF_F_IP_CSUM;
	tp->cp_cmd |= RxChkSum;
	tp->cp_cmd |= RTL_R16(CPlusCmd);

	tp->intr_mask = rtl8168_intr_mask;
	tp->pci_dev = pdev;

	tp->max_jumbo_frame_size = rtl_chip_info[tp->chipset].jumbo_frame_sz;

	spin_lock_init(&tp->lock);
	spin_lock_init(&tp->phy_lock);

	pci_set_drvdata(pdev, dev);

	if (netif_msg_probe(tp)) {
		printk(KERN_INFO "%s: %s at 0x%lx, "
		       "%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x, "
		       "IRQ %d\n",
		       dev->name,
		       rtl_chip_info[ent->driver_data].name,
		       dev->base_addr,
		       dev->dev_addr[0], dev->dev_addr[1],
		       dev->dev_addr[2], dev->dev_addr[3],
		       dev->dev_addr[4], dev->dev_addr[5], dev->irq);
	}

	if(tp->mcfg == CFG_METHOD_11 || tp->mcfg==CFG_METHOD_12)
	{
		rtl8168_driver_start(tp);
	}
	rtl8168_phy_power_up (dev);
	rtl8168_hw_phy_config(dev);

	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0x40);

	rtl8168_link_option(board_idx, &autoneg, &speed, &duplex);

	rtl8168_set_speed(dev, autoneg, speed, duplex);

	rc = register_netdev(dev);
	if (rc) {
		rtl8168_release_board(pdev, dev, ioaddr);
		return rc;
	}

	printk(KERN_INFO "%s: This product is covered by one or more of the following patents: US5,307,459, US5,434,872, US5,732,094, US6,570,884, US6,115,776, and US6,327,625.\n", MODULENAME);

	if (netif_msg_probe(tp)) {
		printk(KERN_DEBUG "%s: Identified chip type is '%s'.\n",
		       dev->name, rtl_chip_info[tp->chipset].name);
	}

	printk("%s", GPL_CLAIM);

	return 0;
}

static void __devexit
rtl8168_remove_one(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8168_private *tp = netdev_priv(dev);

	assert(dev != NULL);
	assert(tp != NULL);

	if(tp->mcfg == CFG_METHOD_11 || tp->mcfg==CFG_METHOD_12)
	{
		rtl8168_driver_stop(tp);
	}
	flush_scheduled_work();

	unregister_netdev(dev);
	rtl8168_disable_msi(pdev, tp);
	rtl8168_release_board(pdev, dev, tp->mmio_addr);
	pci_set_drvdata(pdev, NULL);
}

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11)
static void rtl8168_shutdown(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8168_private *tp = netdev_priv(dev);

	if(tp->TxDescArray!=NULL && tp->RxDescArray!=NULL)
	{
		rtl8168_down(dev);
		free_irq(dev->irq, dev);
	}
	rtl8168_disable_msi(pdev, tp);
}
#endif

static void
rtl8168_set_rxbufsize(struct rtl8168_private *tp,
		      struct net_device *dev)
{
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned int mtu = dev->mtu;

	tp->rx_buf_sz = (mtu > ETH_DATA_LEN) ? mtu + ETH_HLEN + 8 : RX_BUF_SIZE;

	RTL_W16(RxMaxSize, tp->rx_buf_sz);
}

static int rtl8168_open(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	int retval;

	rtl8168_set_rxbufsize(tp, dev);

	retval = -ENOMEM;

	/*
	 * Rx and Tx desscriptors needs 256 bytes alignment.
	 * pci_alloc_consistent provides more.
	 */
	tp->TxDescArray = pci_alloc_consistent(pdev, R8168_TX_RING_BYTES,
					       &tp->TxPhyAddr);
	if (!tp->TxDescArray)
		goto out;

	tp->RxDescArray = pci_alloc_consistent(pdev, R8168_RX_RING_BYTES,
					       &tp->RxPhyAddr);
	if (!tp->RxDescArray)
		goto err_free_tx;

	memset(tp->TxDescArray, 0, R8168_TX_RING_BYTES);
	memset(tp->RxDescArray, 0, R8168_RX_RING_BYTES);
	retval = rtl8168_init_ring(dev);
	if (retval < 0)
		goto err_free_rx;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	INIT_WORK(&tp->task, NULL, dev);
#else
	INIT_DELAYED_WORK(&tp->task, NULL);
#endif

#ifdef	CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	RTL_NAPI_ENABLE(dev, &tp->napi);
#endif
#endif

	rtl8168_powerup_pll(dev);
	rtl8168_hw_start(dev);

	if (tp->esd_flag == 0) {
		rtl8168_request_esd_timer(dev);
	}

	rtl8168_request_link_timer(dev);

	rtl8168_dsm(dev, DSM_IF_UP);

	rtl8168_check_link_status(dev, tp, tp->mmio_addr);

	retval = request_irq(dev->irq, rtl8168_interrupt, (tp->features & RTL_FEATURE_MSI) ? 0 : SA_SHIRQ, dev->name, dev);
	if(retval<0)
		goto err_free_rx;

out:
	return retval;

err_free_rx:
	pci_free_consistent(pdev, R8168_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
err_free_tx:
	pci_free_consistent(pdev, R8168_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
	goto out;
}

static void
rtl8168_hw_reset(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	/* Disable interrupts */
	rtl8168_irq_mask_and_ack(ioaddr);

	rtl8168_nic_reset(dev);
}

static void
rtl8168_dsm(struct net_device *dev, int dev_state)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	switch (dev_state) {
	case DSM_MAC_INIT:
		if ((tp->mcfg == CFG_METHOD_5) || (tp->mcfg == CFG_METHOD_6)) {
			if (RTL_R8(MACDBG) & 0x80) {
				RTL_W8(GPIO, RTL_R8(GPIO) | GPIO_en);
			} else {
				RTL_W8(GPIO, RTL_R8(GPIO) & ~GPIO_en);
			}
		}

		break;
	case DSM_NIC_GOTO_D3:
	case DSM_IF_DOWN:
		if ((tp->mcfg == CFG_METHOD_5) || (tp->mcfg == CFG_METHOD_6))
		{
			if (RTL_R8(MACDBG) & 0x80)
				RTL_W8(GPIO, RTL_R8(GPIO) & ~GPIO_en);
		}
		break;

	case DSM_NIC_RESUME_D3:
	case DSM_IF_UP:
		if ((tp->mcfg == CFG_METHOD_5) || (tp->mcfg == CFG_METHOD_6))
		{
			if (RTL_R8(MACDBG) & 0x80)
				RTL_W8(GPIO, RTL_R8(GPIO) | GPIO_en);
		}

		break;
	}

}

static void
rtl8168_hw_start(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	struct pci_dev *pdev = tp->pci_dev;
	u8 device_control, options1, options2;
	u16 ephy_data;
	u32 csi_tmp;

	netif_stop_queue(dev);
	rtl8168_nic_reset(dev);

	RTL_W8(Cfg9346, Cfg9346_Unlock);

	RTL_W8(MTPS, Reserved1_data);

	tp->cp_cmd |= PktCntrDisable | INTT_1;
	RTL_W16(CPlusCmd, tp->cp_cmd);

	RTL_W16(IntrMitigate, 0x5151);

	//Work around for RxFIFO overflow
	if (tp->mcfg == CFG_METHOD_1) {
		rtl8168_intr_mask |= RxFIFOOver | PCSTimeout;
		rtl8168_intr_mask &= ~RxDescUnavail;
	}

	RTL_W32(TxDescStartAddrLow, ((u64) tp->TxPhyAddr & DMA_32BIT_MASK));
	RTL_W32(TxDescStartAddrHigh, ((u64) tp->TxPhyAddr >> 32));
	RTL_W32(RxDescAddrLow, ((u64) tp->RxPhyAddr & DMA_32BIT_MASK));
	RTL_W32(RxDescAddrHigh, ((u64) tp->RxPhyAddr >> 32));

	/* Set Rx Config register */
	//rtl8168_set_rx_mode(dev);
	RTL_W32(RxConfig, RTL_R32(RxConfig) & ~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys));


	/* Set DMA burst size and Interframe Gap Time */
	if (tp->mcfg == CFG_METHOD_1) {
		RTL_W32(TxConfig, (TX_DMA_BURST_512 << TxDMAShift) |
				  (InterFrameGap << TxInterFrameGapShift));
	} else {
		RTL_W32(TxConfig, (TX_DMA_BURST_unlimited << TxDMAShift) |
				  (InterFrameGap << TxInterFrameGapShift));
	}

	/* Clear the interrupt status register. */
	RTL_W16(IntrStatus, 0xFFFF);

	if (tp->mcfg == CFG_METHOD_4) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);

		RTL_W8(DBG_reg, (0x0E << 4) | Fix_Nak_1 | Fix_Nak_2);

		/*Set EPHY registers	begin*/
		/*Set EPHY register offset 0x02 bit 11 to 0 and bit 12 to 1*/
		ephy_data = rtl8168_ephy_read(ioaddr, 0x02);
		ephy_data &= ~(1 << 11);
		ephy_data |= (1 << 12);
		rtl8168_ephy_write(ioaddr, 0x02, ephy_data);

		/*Set EPHY register offset 0x03 bit 1 to 1*/
		ephy_data = rtl8168_ephy_read(ioaddr, 0x03);
		ephy_data |= (1 << 1);
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data);

		/*Set EPHY register offset 0x06 bit 7 to 0*/
		ephy_data = rtl8168_ephy_read(ioaddr, 0x06);
		ephy_data &= ~(1 << 7);
		rtl8168_ephy_write(ioaddr, 0x06, ephy_data);
		/*Set EPHY registers	end*/

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		//disable clock request.
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload disable
			dev->features &= ~NETIF_F_IP_CSUM;

			//rx checksum offload disable
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload enable
			dev->features |= NETIF_F_IP_CSUM;

			//rx checksum offload enable
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}
	} else if (tp->mcfg == CFG_METHOD_5) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);

		/******set EPHY registers for RTL8168CP	begin******/
		//Set EPHY register offset 0x01 bit 0 to 1.
		ephy_data = rtl8168_ephy_read(ioaddr, 0x01);
		ephy_data |= (1 << 0);
		rtl8168_ephy_write(ioaddr, 0x01, ephy_data);

		//Set EPHY register offset 0x03 bit 10 to 0, bit 9 to 1 and bit 5 to 1.
		ephy_data = rtl8168_ephy_read(ioaddr, 0x03);
		ephy_data &= ~(1 << 10);
		ephy_data |= (1 << 9);
		ephy_data |= (1 << 5);
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data);
		/******set EPHY registers for RTL8168CP	end******/

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		//disable clock request.
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload disable
			dev->features &= ~NETIF_F_IP_CSUM;

			//rx checksum offload disable
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload enable
			dev->features |= NETIF_F_IP_CSUM;

			//rx checksum offload enable
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}
	} else if (tp->mcfg == CFG_METHOD_6) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		//disable clock request.
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload disable
			dev->features &= ~NETIF_F_IP_CSUM;

			//rx checksum offload disable
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload enable
			dev->features |= NETIF_F_IP_CSUM;

			//rx checksum offload enable
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}
	} else if (tp->mcfg == CFG_METHOD_7) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);
		rtl8168_eri_write(ioaddr, 0x1EC, 1, 0x07, ERIAR_ASF);

		//disable clock request.
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload disable
			dev->features &= ~NETIF_F_IP_CSUM;

			//rx checksum offload disable
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload enable
			dev->features |= NETIF_F_IP_CSUM;

			//rx checksum offload enable
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}
	} else if (tp->mcfg == CFG_METHOD_8) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);
		rtl8168_eri_write(ioaddr, 0x1EC, 1, 0x07, ERIAR_ASF);

		//disable clock request.
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		RTL_W8(0xD1, 0x20);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload disable
			dev->features &= ~NETIF_F_IP_CSUM;

			//rx checksum offload disable
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			//tx checksum offload enable
			dev->features |= NETIF_F_IP_CSUM;

			//rx checksum offload enable
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}

	} else if (tp->mcfg == CFG_METHOD_9) {
		/*set PCI configuration space offset 0x70F to 0x13*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x13000000);

		/* disable clock request. */
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W8(Config3, RTL_R8(Config3) & ~BIT_4);
		RTL_W8(DBG_reg, RTL_R8(DBG_reg) | BIT_7 | BIT_1);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			/* Set PCI configuration space offset 0x79 to 0x20 */
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			/* tx checksum offload disable */
			dev->features &= ~NETIF_F_IP_CSUM;

			/* rx checksum offload disable */
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			/* Set PCI configuration space offset 0x79 to 0x50 */
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			/* tx checksum offload enable */
			dev->features |= NETIF_F_IP_CSUM;

			/* rx checksum offload enable */
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}

		/* set EPHY registers */
		rtl8168_ephy_write(ioaddr, 0x01, 0x7C7D);
		rtl8168_ephy_write(ioaddr, 0x02, 0x091F);
		rtl8168_ephy_write(ioaddr, 0x06, 0xB271);
		rtl8168_ephy_write(ioaddr, 0x07, 0xCE00);
	} else if (tp->mcfg == CFG_METHOD_10) {
		/*set PCI configuration space offset 0x70F to 0x13*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x13000000);

		RTL_W8(DBG_reg, RTL_R8(DBG_reg) | BIT_7 | BIT_1);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | Jumbo_En1);

			/* Set PCI configuration space offset 0x79 to 0x20 */
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x20;
			pci_write_config_byte(pdev, 0x79, device_control);

			/* tx checksum offload disable */
			dev->features &= ~NETIF_F_IP_CSUM;

			/* rx checksum offload disable */
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~Jumbo_En1);

			/* Set PCI configuration space offset 0x79 to 0x50 */
			pci_read_config_byte(pdev, 0x79, &device_control);
			device_control &= ~0x70;
			device_control |= 0x50;
			pci_write_config_byte(pdev, 0x79, device_control);

			/* tx checksum offload enable */
			dev->features |= NETIF_F_IP_CSUM;

			/* rx checksum offload enable */
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}

		RTL_W8(Config1, (RTL_R8(Config1)&0xC0) | 0x1F);

		/* set EPHY registers */
		rtl8168_ephy_write(ioaddr, 0x01, 0x6C7F);
		rtl8168_ephy_write(ioaddr, 0x02, 0x011F);
		rtl8168_ephy_write(ioaddr, 0x03, 0xC1B2);
		rtl8168_ephy_write(ioaddr, 0x1A, 0x0546);
		rtl8168_ephy_write(ioaddr, 0x1C, 0x80C4);
		rtl8168_ephy_write(ioaddr, 0x1D, 0x78E4);
		rtl8168_ephy_write(ioaddr, 0x0A, 0x8100);

		/* disable clock request. */
		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W8(0xF3, RTL_R8(0xF3) | BIT_2);

	} else if (tp->mcfg == CFG_METHOD_11) {
		/*set PCI configuration space offset 0x70F to 0x37*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x17000000);

		/* Set PCI configuration space offset 0x79 to 0x50 */
		pci_read_config_byte(pdev, 0x79, &device_control);
		device_control &= ~0x70;
		device_control |= 0x50;
		pci_write_config_byte(pdev, 0x79, device_control);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);

			/* tx checksum offload disable */
			dev->features &= ~NETIF_F_IP_CSUM;

			/* rx checksum offload disable */
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);

			/* tx checksum offload enable */
			dev->features |= NETIF_F_IP_CSUM;

			/* rx checksum offload enable */
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}

		pci_write_config_byte(pdev, 0x81, 0x00);

		RTL_W8(Config1, (RTL_R8(Config1)&0xC0)|0x1F);

	} else if (tp->mcfg == CFG_METHOD_12) {
		/*set PCI configuration space offset 0x70F to 0x37*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x17000000);

		/* Set PCI configuration space offset 0x79 to 0x50 */
		pci_read_config_byte(pdev, 0x79, &device_control);
		device_control &= ~0x70;
		device_control |= 0x50;
		pci_write_config_byte(pdev, 0x79, device_control);

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);

			/* tx checksum offload disable */
			dev->features &= ~NETIF_F_IP_CSUM;

			/* rx checksum offload disable */
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);

			/* tx checksum offload enable */
			dev->features |= NETIF_F_IP_CSUM;

			/* rx checksum offload enable */
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}

		ephy_data = rtl8168_ephy_read(ioaddr, 0x0B);
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data|0x48);
		ephy_data = rtl8168_ephy_read(ioaddr, 0x19);
		ephy_data &= 0x20;
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data|0x50);
		ephy_data = rtl8168_ephy_read(ioaddr, 0x0C);
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data|0x20);

		pci_write_config_byte(pdev, 0x81, 0x01);

		RTL_W8(Config1, (RTL_R8(Config1)&0xC0)|0x1F);

	} else if (tp->mcfg == CFG_METHOD_14 || tp->mcfg == CFG_METHOD_15) {
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = rtl8168_csi_read(ioaddr, 0x70c) & 0x00ffffff;
		rtl8168_csi_write(ioaddr, 0x70c, csi_tmp | 0x27000000);

		/* set EPHY registers */
		ephy_data = rtl8168_ephy_read(ioaddr, 0x00) & ~0x0200;
		ephy_data |= 0x0100;
		rtl8168_ephy_write(ioaddr, 0x00, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x00);
		ephy_data |= 0x0004;
		rtl8168_ephy_write(ioaddr, 0x00, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x06) & ~0x0002;
		ephy_data |= 0x0001;
		rtl8168_ephy_write(ioaddr, 0x06, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x06);
		ephy_data |= 0x0030;
		rtl8168_ephy_write(ioaddr, 0x06, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x07);
		ephy_data |= 0x2000;
		rtl8168_ephy_write(ioaddr, 0x07, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x00);
		ephy_data |= 0x0020;
		rtl8168_ephy_write(ioaddr, 0x00, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x03) & ~0x5800;
		ephy_data |= 0x2000;
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x03);
		ephy_data |= 0x0001;
		rtl8168_ephy_write(ioaddr, 0x03, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x01) & ~0x0800;
		ephy_data |= 0x1000;
		rtl8168_ephy_write(ioaddr, 0x01, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x07);
		ephy_data |= 0x4000;
		rtl8168_ephy_write(ioaddr, 0x07, ephy_data);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x1E);
		ephy_data |= 0x2000;
		rtl8168_ephy_write(ioaddr, 0x1E, ephy_data);

		rtl8168_ephy_write(ioaddr, 0x19, 0xFE6C);

		ephy_data = rtl8168_ephy_read(ioaddr, 0x0A);
		ephy_data |= 0x0040;
		rtl8168_ephy_write(ioaddr, 0x0A, ephy_data);

		tp->cp_cmd &= 0x2063;
		if (dev->mtu > ETH_DATA_LEN) {
			RTL_W8(MTPS, 0x24);
			RTL_W8(Config3, RTL_R8(Config3) | Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) | 0x01);
			/* Set PCI configuration space offset 0x79 to 0x20 */
			pci_write_config_byte(pdev, 0x79, 0x20);

			/* tx checksum offload disable */
			dev->features &= ~NETIF_F_IP_CSUM;

			/* rx checksum offload disable */
			tp->cp_cmd &= ~RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		} else {
			RTL_W8(MTPS, 0x0C);
			RTL_W8(Config3, RTL_R8(Config3) & ~Jumbo_En0);
			RTL_W8(Config4, RTL_R8(Config4) & ~0x01);
			/* Set PCI configuration space offset 0x79 to 0x50 */
			pci_write_config_byte(pdev, 0x79, 0x50);


			/* tx checksum offload enable */
			dev->features |= NETIF_F_IP_CSUM;

			/* rx checksum offload enable */
			tp->cp_cmd |= RxChkSum;
			RTL_W16(CPlusCmd, tp->cp_cmd);
		}
		rtl8168_set_rxbufsize(tp, dev);


//		RTL_W8(0xF2, RTL_R8(0xF2) | BIT_0);
//		RTL_W32(CounterAddrLow, RTL_R32(CounterAddrLow) | BIT_0);

		RTL_W8(0xF3, RTL_R8(0xF3) | BIT_5);
		RTL_W8(0xF3, RTL_R8(0xF3) & ~BIT_5);

//		RTL_W8(0xD3, RTL_R8(0xD3) | BIT_3 | BIT_2);

		RTL_W8(0xD0, RTL_R8(0xD0) | BIT_7 | BIT_6);

		RTL_W8(0xF1, RTL_R8(0xF1) | BIT_7 | BIT_6 | BIT_5 | BIT_4 | BIT_2 | BIT_1);

		RTL_W8(Config5, (RTL_R8(Config5)&~0x08) | BIT_0);
		RTL_W8(Config2, RTL_R8(Config2) | BIT_7);

		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);
	} else if (tp->mcfg == CFG_METHOD_1) {
		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		if (dev->mtu > ETH_DATA_LEN) {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x28;
			pci_write_config_byte(pdev, 0x69, device_control);
		} else {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x58;
			pci_write_config_byte(pdev, 0x69, device_control);
		}
	} else if (tp->mcfg == CFG_METHOD_2) {
		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x28;
			pci_write_config_byte(pdev, 0x69, device_control);

			RTL_W8(Config4, RTL_R8(Config4) | (1 << 0));
		} else {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x58;
			pci_write_config_byte(pdev, 0x69, device_control);

			RTL_W8(Config4, RTL_R8(Config4) & ~(1 << 0));
		}
	} else if (tp->mcfg == CFG_METHOD_3) {
		RTL_W8(Config3, RTL_R8(Config3) & ~Beacon_en);

		RTL_W16(CPlusCmd, RTL_R16(CPlusCmd) &
			~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en |
			  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));

		RTL_W8(MTPS, Reserved1_data);
		if (dev->mtu > ETH_DATA_LEN) {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x28;
			pci_write_config_byte(pdev, 0x69, device_control);

			RTL_W8(Config4, RTL_R8(Config4) | (1 << 0));
		} else {
			pci_read_config_byte(pdev, 0x69, &device_control);
			device_control &= ~0x70;
			device_control |= 0x58;
			pci_write_config_byte(pdev, 0x69, device_control);

			RTL_W8(Config4, RTL_R8(Config4) & ~(1 << 0));
		}
	}

	if ((tp->mcfg == CFG_METHOD_1) || (tp->mcfg == CFG_METHOD_2) || (tp->mcfg == CFG_METHOD_3)) {
		/* csum offload command for RTL8168B/8111B */
		tp->tx_tcp_csum_cmd = TxIPCS | TxTCPCS;
		tp->tx_udp_csum_cmd = TxIPCS | TxUDPCS;
		tp->tx_ip_csum_cmd = TxIPCS;
	} else {
		/* csum offload command for RTL8168C/8111C and RTL8168CP/8111CP */
		tp->tx_tcp_csum_cmd = TxIPCS_C | TxTCPCS_C;
		tp->tx_udp_csum_cmd = TxIPCS_C | TxUDPCS_C;
		tp->tx_ip_csum_cmd = TxIPCS_C;
	}

	RTL_W8(ChipCmd, CmdTxEnb | CmdRxEnb);

	if(tp->mcfg==CFG_METHOD_11 || tp->mcfg==CFG_METHOD_12)
	{
		rtl8168_mac_loopback_test(tp);
	}

	/* Set Rx Config register */
	rtl8168_set_rx_mode(dev);

	if (tp->rx_fifo_overflow == 0) {
		/* Enable all known interrupts by setting the interrupt mask. */
		RTL_W16(IntrMask, rtl8168_intr_mask);
		netif_start_queue(dev);
	}

	RTL_W8(Cfg9346, Cfg9346_Lock);

	if (!tp->pci_cfg_is_read) {
		pci_read_config_byte(pdev, PCI_COMMAND, &tp->pci_cfg_space.cmd);
		pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &tp->pci_cfg_space.cls);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_0, &tp->pci_cfg_space.io_base_l);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_0 + 2, &tp->pci_cfg_space.io_base_h);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_2, &tp->pci_cfg_space.mem_base_l);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_2 + 2, &tp->pci_cfg_space.mem_base_h);
		pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &tp->pci_cfg_space.ilr);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_4, &tp->pci_cfg_space.resv_0x20_l);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_4 + 2, &tp->pci_cfg_space.resv_0x20_h);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_5, &tp->pci_cfg_space.resv_0x24_l);
		pci_read_config_word(pdev, PCI_BASE_ADDRESS_5 + 2, &tp->pci_cfg_space.resv_0x24_h);

		tp->pci_cfg_is_read = 1;
	}

	rtl8168_dsm(dev, DSM_MAC_INIT);

	options1 = RTL_R8(Config3);
	options2 = RTL_R8(Config5);
	if ((options1 & LinkUp) || (options1 & MagicPacket) || (options2 & UWF) || (options2 & BWF) || (options2 & MWF))
		tp->wol_enabled = WOL_ENABLED;
	else
		tp->wol_enabled = WOL_DISABLED;

	udelay(10);
}

static int
rtl8168_change_mtu(struct net_device *dev,
		   int new_mtu)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	int ret = 0;

	if (new_mtu < ETH_ZLEN || new_mtu > tp->max_jumbo_frame_size)
		return -EINVAL;

	if (!netif_running(dev))
		goto out;

	rtl8168_down(dev);

	dev->mtu = new_mtu;

	rtl8168_set_rxbufsize(tp, dev);

	ret = rtl8168_init_ring(dev);

	if (ret < 0)
		goto out;

#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	RTL_NAPI_ENABLE(dev, &tp->napi);
#endif
#endif//CONFIG_R8168_NAPI

	rtl8168_hw_start(dev);

	rtl8168_set_speed(dev, AUTONEG_ENABLE, SPEED_1000, AUTONEG_ENABLE);

out:
	return ret;
}

static inline void
rtl8168_make_unusable_by_asic(struct RxDesc *desc)
{
	desc->addr = 0x0badbadbadbadbadull;
	desc->opts1 &= ~cpu_to_le32(DescOwn | RsvdMask);
}

static void
rtl8168_free_rx_skb(struct rtl8168_private *tp,
		    struct sk_buff **sk_buff,
		    struct RxDesc *desc)
{
	struct pci_dev *pdev = tp->pci_dev;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), tp->rx_buf_sz,
			 PCI_DMA_FROMDEVICE);
	dev_kfree_skb(*sk_buff);
	*sk_buff = NULL;
	rtl8168_make_unusable_by_asic(desc);
}

static inline void
rtl8168_mark_to_asic(struct RxDesc *desc,
		     u32 rx_buf_sz)
{
	u32 eor = le32_to_cpu(desc->opts1) & RingEnd;

	desc->opts1 = cpu_to_le32(DescOwn | eor | rx_buf_sz);
}

static inline void
rtl8168_map_to_asic(struct RxDesc *desc,
		    dma_addr_t mapping,
		    u32 rx_buf_sz)
{
	desc->addr = cpu_to_le64(mapping);
	wmb();
	rtl8168_mark_to_asic(desc, rx_buf_sz);
}

static int
rtl8168_alloc_rx_skb(struct pci_dev *pdev,
		     struct sk_buff **sk_buff,
		     struct RxDesc *desc,
		     int rx_buf_sz)
{
	struct sk_buff *skb;
	dma_addr_t mapping;
	int ret = 0;

	skb = dev_alloc_skb(rx_buf_sz + NET_IP_ALIGN);
	if (!skb)
		goto err_out;

	skb_reserve(skb, NET_IP_ALIGN);
	*sk_buff = skb;

	mapping = pci_map_single(pdev, skb->data, rx_buf_sz,
				 PCI_DMA_FROMDEVICE);

	rtl8168_map_to_asic(desc, mapping, rx_buf_sz);

out:
	return ret;

err_out:
	ret = -ENOMEM;
	rtl8168_make_unusable_by_asic(desc);
	goto out;
}

static void
rtl8168_rx_clear(struct rtl8168_private *tp)
{
	int i;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if (tp->Rx_skbuff[i]) {
			rtl8168_free_rx_skb(tp, tp->Rx_skbuff + i,
					    tp->RxDescArray + i);
		}
	}
}

static u32
rtl8168_rx_fill(struct rtl8168_private *tp,
		struct net_device *dev,
		u32 start,
		u32 end)
{
	u32 cur;

	for (cur = start; end - cur > 0; cur++) {
		int ret, i = cur % NUM_RX_DESC;

		if (tp->Rx_skbuff[i])
			continue;

		ret = rtl8168_alloc_rx_skb(tp->pci_dev, tp->Rx_skbuff + i,
					   tp->RxDescArray + i, tp->rx_buf_sz);
		if (ret < 0)
			break;
	}
	return cur - start;
}

static inline void
rtl8168_mark_as_last_descriptor(struct RxDesc *desc)
{
	desc->opts1 |= cpu_to_le32(RingEnd);
}

static void
rtl8168_init_ring_indexes(struct rtl8168_private *tp)
{
	tp->dirty_tx = 0;
	tp->dirty_rx = 0;
	tp->cur_tx = 0;
	tp->cur_rx = 0;
}

static void
rtl8168_tx_desc_init(struct rtl8168_private *tp)
{
	int i = 0;

	memset(tp->TxDescArray, 0x0, NUM_TX_DESC * sizeof(struct TxDesc));

	for (i = 0; i < NUM_TX_DESC; i++)
		if(i == (NUM_TX_DESC - 1))
			tp->TxDescArray[i].opts1 = cpu_to_le32(RingEnd);
}

static void
rtl8168_rx_desc_offset0_init(struct rtl8168_private *tp, int own)
{
	int i = 0;
	int ownbit = 0;

	if (own)
		ownbit = DescOwn;

	for (i = 0; i < NUM_RX_DESC; i++) {
		if(i == (NUM_RX_DESC - 1))
			tp->RxDescArray[i].opts1 = cpu_to_le32((ownbit | RingEnd) | (unsigned long)tp->rx_buf_sz);
		else
			tp->RxDescArray[i].opts1 = cpu_to_le32(ownbit | (unsigned long)tp->rx_buf_sz);
	}
}

static void
rtl8168_rx_desc_init(struct rtl8168_private *tp)
{
	memset(tp->RxDescArray, 0x0, NUM_RX_DESC * sizeof(struct RxDesc));

	rtl8168_rx_desc_offset0_init(tp, 1);
}

static int
rtl8168_init_ring(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	rtl8168_init_ring_indexes(tp);

	memset(tp->tx_skb, 0x0, NUM_TX_DESC * sizeof(struct ring_info));
	memset(tp->Rx_skbuff, 0x0, NUM_RX_DESC * sizeof(struct sk_buff *));

	rtl8168_tx_desc_init(tp);
	rtl8168_rx_desc_init(tp);

	if (rtl8168_rx_fill(tp, dev, 0, NUM_RX_DESC) != NUM_RX_DESC)
		goto err_out;

	rtl8168_mark_as_last_descriptor(tp->RxDescArray + NUM_RX_DESC - 1);

	return 0;

err_out:
	rtl8168_rx_clear(tp);
	return -ENOMEM;
}

static void
rtl8168_unmap_tx_skb(struct pci_dev *pdev,
		     struct ring_info *tx_skb,
		     struct TxDesc *desc)
{
	unsigned int len = tx_skb->len;

	pci_unmap_single(pdev, le64_to_cpu(desc->addr), len, PCI_DMA_TODEVICE);
	desc->opts1 = 0x00;
	desc->opts2 = 0x00;
	desc->addr = 0x00;
	tx_skb->len = 0;
}

static void
rtl8168_tx_clear(struct rtl8168_private *tp)
{
	unsigned int i;
	struct net_device *dev = tp->dev;

	for (i = tp->dirty_tx; i < tp->dirty_tx + NUM_TX_DESC; i++) {
		unsigned int entry = i % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		unsigned int len = tx_skb->len;

		if (len) {
			struct sk_buff *skb = tx_skb->skb;

			rtl8168_unmap_tx_skb(tp->pci_dev, tx_skb,
					     tp->TxDescArray + entry);
			if (skb) {
				dev_kfree_skb(skb);
				tx_skb->skb = NULL;
			}
			RTLDEV->stats.tx_dropped++;
		}
	}
	tp->cur_tx = tp->dirty_tx = 0;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void rtl8168_schedule_work(struct net_device *dev, void (*task)(void *))
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	struct rtl8168_private *tp = netdev_priv(dev);

	PREPARE_WORK(&tp->task, task, dev);
	schedule_delayed_work(&tp->task, 4);
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
}
#else
static void rtl8168_schedule_work(struct net_device *dev, work_func_t task)
{
	struct rtl8168_private *tp = netdev_priv(dev);

	PREPARE_DELAYED_WORK(&tp->task, task);
	schedule_delayed_work(&tp->task, 4);
}
#endif

static void
rtl8168_wait_for_quiescence(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	synchronize_irq(dev->irq);

	/* Wait for any pending NAPI task to complete */
#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	RTL_NAPI_DISABLE(dev, &tp->napi);
#endif
#endif//CONFIG_R8168_NAPI

	rtl8168_irq_mask_and_ack(ioaddr);

#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	RTL_NAPI_ENABLE(dev, &tp->napi);
#endif
#endif//CONFIG_R8168_NAPI
}

#if 0
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void rtl8168_reinit_task(void *_data)
#else
static void rtl8168_reinit_task(struct work_struct *work)
#endif
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
	struct net_device *dev = _data;
#else
	struct rtl8168_private *tp =
		container_of(work, struct rtl8168_private, task.work);
	struct net_device *dev = tp->dev;
#endif
	int ret;

	if (netif_running(dev)) {
		rtl8168_wait_for_quiescence(dev);
		rtl8168_close(dev);
	}

	ret = rtl8168_open(dev);
	if (unlikely(ret < 0)) {
		if (net_ratelimit()) {
			struct rtl8168_private *tp = netdev_priv(dev);

			if (netif_msg_drv(tp)) {
				printk(PFX KERN_ERR
				       "%s: reinit failure (status = %d)."
				       " Rescheduling.\n", dev->name, ret);
			}
		}
		rtl8168_schedule_work(dev, rtl8168_reinit_task);
	}
}
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
static void rtl8168_reset_task(void *_data)
{
	struct net_device *dev = _data;
	struct rtl8168_private *tp = netdev_priv(dev);
#else
static void rtl8168_reset_task(struct work_struct *work)
{
	struct rtl8168_private *tp =
		container_of(work, struct rtl8168_private, task.work);
	struct net_device *dev = tp->dev;
#endif

	if (!netif_running(dev))
		return;

	rtl8168_wait_for_quiescence(dev);

	rtl8168_rx_interrupt(dev, tp, tp->mmio_addr, ~(u32)0);
	rtl8168_tx_clear(tp);

	if (tp->dirty_rx == tp->cur_rx) {
		rtl8168_init_ring_indexes(tp);
		rtl8168_hw_start(dev);
		netif_wake_queue(dev);
	} else {
		if (net_ratelimit()) {
			struct rtl8168_private *tp = netdev_priv(dev);

			if (netif_msg_intr(tp)) {
				printk(PFX KERN_EMERG
				       "%s: Rx buffers shortage\n", dev->name);
			}
		}
		rtl8168_schedule_work(dev, rtl8168_reset_task);
	}
}

static void
rtl8168_tx_timeout(struct net_device *dev)
{
	rtl8168_hw_reset(dev);

	/* Let's wait a bit while any (async) irq lands on */
	rtl8168_schedule_work(dev, rtl8168_reset_task);
}

static int
rtl8168_xmit_frags(struct rtl8168_private *tp,
		   struct sk_buff *skb,
		   u32 opts1)
{
	struct skb_shared_info *info = skb_shinfo(skb);
	unsigned int cur_frag, entry;
	struct TxDesc *txd = NULL;

	entry = tp->cur_tx;
	for (cur_frag = 0; cur_frag < info->nr_frags; cur_frag++) {
		skb_frag_t *frag = info->frags + cur_frag;
		dma_addr_t mapping;
		u32 status, len;
		void *addr;

		entry = (entry + 1) % NUM_TX_DESC;

		txd = tp->TxDescArray + entry;
		len = frag->size;
		addr = ((void *) page_address(frag->page)) + frag->page_offset;
		mapping = pci_map_single(tp->pci_dev, addr, len, PCI_DMA_TODEVICE);

		/* anti gcc 2.95.3 bugware (sic) */
		status = opts1 | len | (RingEnd * !((entry + 1) % NUM_TX_DESC));

		txd->opts1 = cpu_to_le32(status);
		txd->addr = cpu_to_le64(mapping);

		tp->tx_skb[entry].len = len;
	}

	if (cur_frag) {
		tp->tx_skb[entry].skb = skb;
		txd->opts1 |= cpu_to_le32(LastFrag);
	}

	return cur_frag;
}

static inline u32
rtl8168_tso(struct sk_buff *skb,
	    struct net_device *dev)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	if (dev->features & NETIF_F_TSO) {
		u32 mss = skb_shinfo(skb)->gso_size;

		if (mss)
			return LargeSend | ((mss & MSSMask) << MSSShift);
	}
#endif //LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)

	return 0;
}

static inline u32
rtl8168_tx_csum(struct sk_buff *skb,
		struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	const struct iphdr *ip = skb->nh.iph;
#else
	const struct iphdr *ip = ip_hdr(skb);
#endif

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		if (ip->protocol == IPPROTO_TCP)
			return tp->tx_tcp_csum_cmd;
		else if (ip->protocol == IPPROTO_UDP)
			return tp->tx_udp_csum_cmd;
		else if (ip->protocol == IPPROTO_IP)
			return tp->tx_ip_csum_cmd;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
		WARN_ON(1);	/* we need a WARN() */
#endif
	}

	return 0;
}

static int
rtl8168_start_xmit(struct sk_buff *skb,
		   struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	unsigned int frags, entry = tp->cur_tx % NUM_TX_DESC;
	struct TxDesc *txd = tp->TxDescArray + entry;
	void __iomem *ioaddr = tp->mmio_addr;
	dma_addr_t mapping;
	u32 status1, status2, len;
	u32 opts1 = 0;
	u32 opts2 = 0;
	int ret = NETDEV_TX_OK;

	//Work around for rx fifo overflow
	if (tp->rx_fifo_overflow == 1)
		goto err_stop;

	if (unlikely(TX_BUFFS_AVAIL(tp) < skb_shinfo(skb)->nr_frags)) {
		if (netif_msg_drv(tp)) {
			printk(KERN_ERR
			       "%s: BUG! Tx Ring full when queue awake!\n",
			       dev->name);
		}
		goto err_stop;
	}

	if (unlikely(le32_to_cpu(txd->opts1) & DescOwn))
		goto err_stop;

	opts1 = DescOwn | rtl8168_tso(skb, dev);

	if (dev->features & NETIF_F_IP_CSUM) {
		if ((tp->mcfg == CFG_METHOD_1) || (tp->mcfg == CFG_METHOD_2) || (tp->mcfg == CFG_METHOD_3))
			opts1 |= rtl8168_tx_csum(skb, dev);
		else
			opts2 = rtl8168_tx_csum(skb, dev);
	}

	frags = rtl8168_xmit_frags(tp, skb, opts1);
	if (frags) {
		len = skb_headlen(skb);
		opts1 |= FirstFrag;
	} else {
		len = skb->len;

		opts1 |= FirstFrag | LastFrag;
		tp->tx_skb[entry].skb = skb;
	}

	mapping = pci_map_single(tp->pci_dev, skb->data, len, PCI_DMA_TODEVICE);

	tp->tx_skb[entry].len = len;
	txd->addr = cpu_to_le64(mapping);
	txd->opts2 = cpu_to_le32(rtl8168_tx_vlan_tag(tp, skb));

	wmb();

	/* anti gcc 2.95.3 bugware (sic) */
	status1 = opts1 | len | (RingEnd * !((entry + 1) % NUM_TX_DESC));
	status2 = opts2;
	txd->opts1 = cpu_to_le32(status1);
	txd->opts2 = cpu_to_le32(status2);

	dev->trans_start = jiffies;

	tp->cur_tx += frags + 1;

	smp_wmb();

	RTL_W8(TxPoll, NPQ);	/* set polling bit */

	if (TX_BUFFS_AVAIL(tp) < MAX_SKB_FRAGS) {
		netif_stop_queue(dev);
		smp_rmb();
		if (TX_BUFFS_AVAIL(tp) >= MAX_SKB_FRAGS)
			netif_wake_queue(dev);
	}

out:
	return ret;
err_stop:
	netif_stop_queue(dev);
	ret = NETDEV_TX_BUSY;
	RTLDEV->stats.tx_dropped++;
	goto out;
}

static void
rtl8168_pcierr_interrupt(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;
	u16 pci_status, pci_cmd;

	pci_read_config_word(pdev, PCI_COMMAND, &pci_cmd);
	pci_read_config_word(pdev, PCI_STATUS, &pci_status);

	if (netif_msg_intr(tp)) {
		printk(KERN_ERR
		       "%s: PCI error (cmd = 0x%04x, status = 0x%04x).\n",
		       dev->name, pci_cmd, pci_status);
	}

	/*
	 * The recovery sequence below admits a very elaborated explanation:
	 * - it seems to work;
	 * - I did not see what else could be done.
	 *
	 * Feel free to adjust to your needs.
	 */
	pci_write_config_word(pdev, PCI_COMMAND,
			      pci_cmd | PCI_COMMAND_SERR | PCI_COMMAND_PARITY);

	pci_write_config_word(pdev, PCI_STATUS,
		pci_status & (PCI_STATUS_DETECTED_PARITY |
		PCI_STATUS_SIG_SYSTEM_ERROR | PCI_STATUS_REC_MASTER_ABORT |
		PCI_STATUS_REC_TARGET_ABORT | PCI_STATUS_SIG_TARGET_ABORT));

	rtl8168_hw_reset(dev);
}

static void
rtl8168_tx_interrupt(struct net_device *dev,
		     struct rtl8168_private *tp,
		     void __iomem *ioaddr)
{
	unsigned int dirty_tx, tx_left;

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	dirty_tx = tp->dirty_tx;
	smp_rmb();
	tx_left = tp->cur_tx - dirty_tx;

	while (tx_left > 0) {
		unsigned int entry = dirty_tx % NUM_TX_DESC;
		struct ring_info *tx_skb = tp->tx_skb + entry;
		u32 len = tx_skb->len;
		u32 status;

		rmb();
		status = le32_to_cpu(tp->TxDescArray[entry].opts1);
		if (status & DescOwn)
			break;

		RTLDEV->stats.tx_bytes += len;
		RTLDEV->stats.tx_packets++;

		rtl8168_unmap_tx_skb(tp->pci_dev,
				     tx_skb,
				     tp->TxDescArray + entry);

		if (status & LastFrag) {
			dev_kfree_skb_irq(tx_skb->skb);
			tx_skb->skb = NULL;
		}
		dirty_tx++;
		tx_left--;
	}

	if (tp->dirty_tx != dirty_tx) {
		tp->dirty_tx = dirty_tx;
		smp_wmb();
		if (netif_queue_stopped(dev) &&
		    (TX_BUFFS_AVAIL(tp) >= MAX_SKB_FRAGS)) {
			netif_wake_queue(dev);
		}
	}
}

static inline int
rtl8168_fragmented_frame(u32 status)
{
	return (status & (FirstFrag | LastFrag)) != (FirstFrag | LastFrag);
}

static inline void
rtl8168_rx_csum(struct rtl8168_private *tp,
		struct sk_buff *skb,
		struct RxDesc *desc)
{
	u32 opts1 = le32_to_cpu(desc->opts1);
	u32 opts2 = le32_to_cpu(desc->opts2);
	u32 status = opts1 & RxProtoMask;

	if ((tp->mcfg == CFG_METHOD_1) ||
	    (tp->mcfg == CFG_METHOD_2) ||
	    (tp->mcfg == CFG_METHOD_3)) {
		/* rx csum offload for RTL8168B/8111B */
		if (((status == RxProtoTCP) && !(opts1 & RxTCPF)) ||
		    ((status == RxProtoUDP) && !(opts1 & RxUDPF)) ||
		    ((status == RxProtoIP) && !(opts1 & RxIPF)))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	} else {
		/* rx csum offload for RTL8168C/8111C and RTL8168CP/8111CP */
		if (((status == RxTCPT) && !(opts1 & RxTCPF)) ||
		    ((status == RxUDPT) && !(opts1 & RxUDPF)) ||
		    ((status == 0) && (opts2 & RxV4F) && !(opts1 & RxIPF)))
			skb->ip_summed = CHECKSUM_UNNECESSARY;
		else
			skb->ip_summed = CHECKSUM_NONE;
	}
}

static inline int
rtl8168_try_rx_copy(struct sk_buff **sk_buff,
		    int pkt_size,
		    struct RxDesc *desc,
		    int rx_buf_sz)
{
	int ret = -1;

	if (pkt_size < rx_copybreak) {
		struct sk_buff *skb;

		skb = dev_alloc_skb(pkt_size + NET_IP_ALIGN);
		if (skb) {
			skb_reserve(skb, 2);
			eth_copy_and_sum(skb, sk_buff[0]->data, pkt_size, 0);
			*sk_buff = skb;
			rtl8168_mark_to_asic(desc, rx_buf_sz);
			ret = 0;
		}
	}
	return ret;
}

static int
rtl8168_rx_interrupt(struct net_device *dev,
		     struct rtl8168_private *tp,
		     void __iomem *ioaddr, u32 budget)
{
	unsigned int cur_rx, rx_left;
	unsigned int delta, count = 0;
	u32 rx_quota = RTL_RX_QUOTA(dev, budget);

	assert(dev != NULL);
	assert(tp != NULL);
	assert(ioaddr != NULL);

	cur_rx = tp->cur_rx;
	rx_left = NUM_RX_DESC + tp->dirty_rx - cur_rx;
	rx_left = rtl8168_rx_quota(rx_left, (u32) rx_quota);

	if ((tp->RxDescArray == NULL) || (tp->Rx_skbuff == NULL)) {
		goto rx_out;
	}

	for (; rx_left > 0; rx_left--, cur_rx++) {
		unsigned int entry = cur_rx % NUM_RX_DESC;
		struct RxDesc *desc = tp->RxDescArray + entry;
		u32 status;

		rmb();
		status = le32_to_cpu(desc->opts1);

		if (status & DescOwn)
			break;
		if (unlikely(status & RxRES)) {
			if (netif_msg_rx_err(tp)) {
				printk(KERN_INFO
				       "%s: Rx ERROR. status = %08x\n",
				       dev->name, status);
			}

			RTLDEV->stats.rx_errors++;

			if (status & (RxRWT | RxRUNT))
				RTLDEV->stats.rx_length_errors++;
			if (status & RxCRC)
				RTLDEV->stats.rx_crc_errors++;
			rtl8168_mark_to_asic(desc, tp->rx_buf_sz);
		} else {
			struct sk_buff *skb = tp->Rx_skbuff[entry];
			int pkt_size = (status & 0x00003FFF) - 4;
			void (*pci_action)(struct pci_dev *, dma_addr_t,
				size_t, int) = pci_dma_sync_single_for_device;

			/*
			 * The driver does not support incoming fragmented
			 * frames. They are seen as a symptom of over-mtu
			 * sized frames.
			 */
			if (unlikely(rtl8168_fragmented_frame(status))) {
				RTLDEV->stats.rx_dropped++;
				RTLDEV->stats.rx_length_errors++;
				rtl8168_mark_to_asic(desc, tp->rx_buf_sz);
				continue;
			}

			if (tp->cp_cmd & RxChkSum)
				rtl8168_rx_csum(tp, skb, desc);

			pci_dma_sync_single_for_cpu(tp->pci_dev,
				le64_to_cpu(desc->addr), tp->rx_buf_sz,
				PCI_DMA_FROMDEVICE);

			if (rtl8168_try_rx_copy(&skb, pkt_size, desc,
						tp->rx_buf_sz)) {
				pci_action = pci_unmap_single;
				tp->Rx_skbuff[entry] = NULL;
			}

			pci_action(tp->pci_dev, le64_to_cpu(desc->addr),
				   tp->rx_buf_sz, PCI_DMA_FROMDEVICE);

			skb->dev = dev;
			skb_put(skb, pkt_size);
			skb->protocol = eth_type_trans(skb, dev);

			if (rtl8168_rx_vlan_skb(tp, desc, skb) < 0)
				rtl8168_rx_skb(skb);

			dev->last_rx = jiffies;
			RTLDEV->stats.rx_bytes += pkt_size;
			RTLDEV->stats.rx_packets++;
		}
	}

	count = cur_rx - tp->cur_rx;
	tp->cur_rx = cur_rx;

	delta = rtl8168_rx_fill(tp, dev, tp->dirty_rx, tp->cur_rx);
	if (!delta && count && netif_msg_intr(tp))
		printk(KERN_INFO "%s: no Rx buffer allocated\n", dev->name);
	tp->dirty_rx += delta;

	/*
	 * FIXME: until there is periodic timer to try and refill the ring,
	 * a temporary shortage may definitely kill the Rx process.
	 * - disable the asic to try and avoid an overflow and kick it again
	 *   after refill ?
	 * - how do others driver handle this condition (Uh oh...).
	 */
	if ((tp->dirty_rx + NUM_RX_DESC == tp->cur_rx) && netif_msg_intr(tp))
		printk(KERN_EMERG "%s: Rx buffers exhausted\n", dev->name);

rx_out:
	return count;
}

/*
 *The interrupt handler does all of the Rx thread work and cleans up after
 *the Tx thread.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
static irqreturn_t rtl8168_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
#else
static irqreturn_t rtl8168_interrupt(int irq, void *dev_instance)
#endif
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct rtl8168_private *tp = netdev_priv(dev);
	int boguscnt = max_interrupt_work;
	void __iomem *ioaddr = tp->mmio_addr;
	int status;
	int handled = 0;
	u16 intr_clean_mask = SYSErr | PCSTimeout | SWInt |
			      LinkChg | RxDescUnavail |
			      TxErr | TxOK | RxErr | RxOK;

	RTL_W16(IntrMask, 0x0000);

	do {
		status = RTL_R16(IntrStatus);

		/* hotplug/major error/no more work/shared irq */
		if ((status == 0xFFFF) || !status)
			break;

		handled = 1;

		if (unlikely(!netif_running(dev))) {
			rtl8168_asic_down(dev);
			goto out;
		}

		status &= (tp->intr_mask | TxDescUnavail);
		RTL_W16(IntrStatus, intr_clean_mask);

		if (!(status & rtl8168_intr_mask))
			break;

		//Work around for rx fifo overflow
		if (unlikely(status & RxFIFOOver))
			if (tp->mcfg == CFG_METHOD_1) {
				tp->rx_fifo_overflow = 1;
				netif_stop_queue(dev);
				udelay(300);
				rtl8168_rx_clear(tp);
				rtl8168_init_ring(dev);
				rtl8168_hw_start(dev);
				RTL_W16(IntrStatus, RxFIFOOver);
				netif_wake_queue(dev);
				tp->rx_fifo_overflow = 0;
			}

		if (unlikely(status & SYSErr)) {
			rtl8168_pcierr_interrupt(dev);
			break;
		}

		if (status & LinkChg)
			rtl8168_check_link_status(dev, tp, ioaddr);

		if ((status & TxOK) && (status & TxDescUnavail)) {
			RTL_W8(TxPoll, NPQ);	/* set polling bit */
			RTL_W16(IntrStatus, TxDescUnavail);
		}
#ifdef CONFIG_R8168_NAPI
		if (status & rtl8168_napi_event) {
			tp->intr_mask = rtl8168_intr_mask & ~rtl8168_napi_event;
			RTL_W16(IntrMask, rtl8168_intr_mask & tp->intr_mask);

			if (likely(RTL_NETIF_RX_SCHEDULE_PREP(dev, &tp->napi))) {
				__RTL_NETIF_RX_SCHEDULE(dev, &tp->napi);
			} else if (netif_msg_intr(tp)) {
				printk(KERN_INFO "%s: interrupt %04x in poll\n",
				       dev->name, status);
			}
		}
		break;
#else
		/* Rx interrupt */
		if (status & (RxOK | RxDescUnavail | RxFIFOOver)) {
			rtl8168_rx_interrupt(dev, tp, tp->mmio_addr, ~(u32)0);
		}
		/* Tx interrupt */
		if (status & (TxOK | TxErr))
			rtl8168_tx_interrupt(dev, tp, ioaddr);
#endif

		boguscnt--;
	} while (boguscnt > 0);

	if (boguscnt <= 0) {
		if (netif_msg_intr(tp) && net_ratelimit() ) {
			printk(KERN_WARNING
			       "%s: Too much work at interrupt!\n", dev->name);
		}
		/* Clear all interrupt sources. */
		RTL_W16(IntrStatus, 0xffff);
	}

out:
	RTL_W16(IntrMask, tp->intr_mask);

	return IRQ_RETVAL(handled);
}

#ifdef CONFIG_R8168_NAPI
static int rtl8168_poll(napi_ptr napi, napi_budget budget)
{
	struct rtl8168_private *tp = RTL_GET_PRIV(napi, struct rtl8168_private);
	void __iomem *ioaddr = tp->mmio_addr;
	RTL_GET_NETDEV(tp)
	unsigned int work_to_do = RTL_NAPI_QUOTA(budget, dev);
	unsigned int work_done;

	work_done = rtl8168_rx_interrupt(dev, tp, ioaddr, (u32) budget);
	rtl8168_tx_interrupt(dev, tp, ioaddr);

	RTL_NAPI_QUOTA_UPDATE(dev, work_done, budget);

	if (work_done < work_to_do) {
		RTL_NETIF_RX_COMPLETE(dev, napi);
		tp->intr_mask = rtl8168_intr_mask;
		/*
		 * 20040426: the barrier is not strictly required but the
		 * behavior of the irq handler could be less predictable
		 * without it. Btw, the lack of flush for the posted pci
		 * write is safe - FR
		 */
		smp_wmb();
		RTL_W16(IntrMask, rtl8168_intr_mask);
	}

	return RTL_NAPI_RETURN_VALUE;
}
#endif//CONFIG_R8168_NAPI

static void
rtl8168_sleep_rx_enable(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;

	if ((tp->mcfg == CFG_METHOD_1) || (tp->mcfg == CFG_METHOD_2)) {
		RTL_W8(ChipCmd, CmdReset);
		rtl8168_rx_desc_offset0_init(tp, 0);
		RTL_W8(ChipCmd, CmdRxEnb);
	}
}

static void rtl8168_down(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	int	count;

	rtl8168_dsm(dev, DSM_IF_DOWN);

	netif_stop_queue(dev);

	rtl8168_delete_esd_timer(dev, &tp->esd_timer);
	rtl8168_delete_link_timer(dev, &tp->link_timer);

#ifdef CONFIG_R8168_NAPI
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,23)
	napi_disable(&tp->napi);
#endif
#endif//CONFIG_R8168_NAPI

	for(count=0; count<100; count++)
	{
		spin_lock_irq(&tp->lock);

		rtl8168_asic_down(dev);

		rtl8168_sleep_rx_enable(dev);

		spin_unlock_irq(&tp->lock);

		synchronize_irq(dev->irq);

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23)) && (LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0))
		if (count==0) {
#ifdef CONFIG_R8169_NAPI
			netif_poll_disable(dev);
#endif
		}
#endif

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11)
	/* Give a racing hard_start_xmit a few cycles to complete. */
		synchronize_sched();  /* FIXME: should this be synchronize_irq()? */
#endif

	/*
	 * And now for the 50k$ question: are IRQ disabled or not ?
	 *
	 * Two paths lead here:
	 * 1) dev->close
	 *    -> netif_running() is available to sync the current code and the
	 *       IRQ handler. See rtl8168_interrupt for details.
	 * 2) dev->change_mtu
	 *    -> rtl8168_poll can not be issued again and re-enable the
	 *       interruptions. Let's simply issue the IRQ down sequence again.
	 */
		if (RTL_R16(IntrMask)==0)
			break;
	}

	if(tp->mcfg == CFG_METHOD_14 || tp->mcfg == CFG_METHOD_15)
	{
		rtl8168_ephy_write(ioaddr, 0x19, 0xFF64);
	}

	/* restore the original MAC address */
	rtl8168_rar_set(tp, tp->org_mac_addr, 0);

	rtl8168_tx_clear(tp);

	rtl8168_rx_clear(tp);

	rtl8168_powerdown_pll(dev);
}

static int
rtl8168_close(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	struct pci_dev *pdev = tp->pci_dev;

	rtl8168_down(dev);

	free_irq(dev->irq, dev);

	pci_free_consistent(pdev, R8168_RX_RING_BYTES, tp->RxDescArray,
			    tp->RxPhyAddr);
	pci_free_consistent(pdev, R8168_TX_RING_BYTES, tp->TxDescArray,
			    tp->TxPhyAddr);
	tp->TxDescArray = NULL;
	tp->RxDescArray = NULL;

	return 0;
}

static void
rtl8168_set_rx_mode(struct net_device *dev)
{
	struct rtl8168_private *tp = netdev_priv(dev);
	void __iomem *ioaddr = tp->mmio_addr;
	unsigned long flags;
	u32 mc_filter[2];	/* Multicast hash filter */
	int i, j, k, rx_mode;
	u32 tmp = 0;

	if (dev->flags & IFF_PROMISC) {
		/* Unconditionally log net taps. */
		if (netif_msg_link(tp)) {
			printk(KERN_NOTICE "%s: Promiscuous mode enabled.\n",
			       dev->name);
		}
		rx_mode =
		    AcceptBroadcast | AcceptMulticast | AcceptMyPhys |
		    AcceptAllPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else if ((dev->mc_count > multicast_filter_limit)
		   || (dev->flags & IFF_ALLMULTI)) {
		/* Too many to filter perfectly -- accept all multicasts. */
		rx_mode = AcceptBroadcast | AcceptMulticast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0xffffffff;
	} else {
		struct dev_mc_list *mclist;
		rx_mode = AcceptBroadcast | AcceptMyPhys;
		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
		     i++, mclist = mclist->next) {
			int bit_nr = ether_crc(ETH_ALEN, mclist->dmi_addr) >> 26;
			mc_filter[bit_nr >> 5] |= 1 << (bit_nr & 31);
			rx_mode |= AcceptMulticast;
		}
	}

	spin_lock_irqsave(&tp->lock, flags);


	tp->rtl8168_rx_config = rtl_chip_info[tp->chipset].RCR_Cfg;
	tmp = tp->rtl8168_rx_config | rx_mode | (RTL_R32(RxConfig) & rtl_chip_info[tp->chipset].RxConfigMask);

	for (j = 0; j < 2; j++) {
		u32 mask = 0x000000ff;
		u32 tmp1 = 0;
		u32 tmp2 = 0;
		int x = 0;
		int y = 0;

		for (k = 0; k < 4; k++) {
			tmp1 = mc_filter[j] & mask;
			x = 32 - (8 + 16 * k);
			y = x - 2 * x;

			if (x > 0)
				tmp2 = tmp2 | (tmp1 << x);
			else
				tmp2 = tmp2 | (tmp1 >> y);

			mask = mask << 8;
		}
		mc_filter[j] = tmp2;
	}

	RTL_W32(RxConfig, tmp);
	RTL_W32(MAR0 + 0, mc_filter[1]);
	RTL_W32(MAR0 + 4, mc_filter[0]);

	spin_unlock_irqrestore(&tp->lock, flags);
}

/**
 *  rtl8168_get_stats - Get rtl8168 read/write statistics
 *  @dev: The Ethernet Device to get statistics for
 *
 *  Get TX/RX statistics for rtl8168
 */
static struct
net_device_stats *rtl8168_get_stats(struct net_device *dev)
{
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,22)
	struct rtl8168_private *tp = netdev_priv(dev);
#endif
//	unsigned long flags;

	if (netif_running(dev)) {
//		spin_lock_irqsave(&tp->lock, flags);
//		spin_unlock_irqrestore(&tp->lock, flags);
	}

	return &RTLDEV->stats;
}

#ifdef CONFIG_PM

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
static int
rtl8168_suspend(struct pci_dev *pdev, u32 state)
#else
static int
rtl8168_suspend(struct pci_dev *pdev, pm_message_t state)
#endif
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8168_private *tp = netdev_priv(dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	u32 pci_pm_state = pci_choose_state(pdev, state);
#endif

	if (!netif_running(dev))
		goto out;

	netif_stop_queue(dev);

	del_timer_sync(&tp->esd_timer);

	rtl8168_dsm(dev, DSM_NIC_GOTO_D3);

	netif_device_detach(dev);

	spin_lock_irq(&tp->lock);

	rtl8168_asic_down(dev);

	rtl8168_sleep_rx_enable(dev);

	spin_unlock_irq(&tp->lock);

	rtl8168_powerdown_pll(dev);

out:

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	pci_save_state(pdev, &pci_pm_state);
#else
	pci_save_state(pdev);
#endif
	pci_enable_wake(pdev, pci_choose_state(pdev, state), tp->wol_enabled);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int
rtl8168_resume(struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);
	struct rtl8168_private *tp = netdev_priv(dev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	u32 pci_pm_state = PCI_D0;
#endif

	pci_set_power_state(pdev, PCI_D0);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	pci_restore_state(pdev, &pci_pm_state);
#else
	pci_restore_state(pdev);
#endif
	pci_enable_wake(pdev, PCI_D0, 0);

	if (!netif_running(dev))
		goto out;

	rtl8168_rx_desc_offset0_init(tp, 1);

	rtl8168_dsm(dev, DSM_NIC_RESUME_D3);

	rtl8168_powerup_pll(dev);

	rtl8168_schedule_work(dev, rtl8168_reset_task);

	netif_device_attach(dev);

	mod_timer(&tp->esd_timer, jiffies + RTL8168_ESD_TIMEOUT);
out:
	return 0;
}

#endif /* CONFIG_PM */

static struct pci_driver rtl8168_pci_driver = {
	.name		= MODULENAME,
	.id_table	= rtl8168_pci_tbl,
	.probe		= rtl8168_init_one,
	.remove		= __devexit_p(rtl8168_remove_one),
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,11)
	.shutdown	= rtl8168_shutdown,
#endif
#ifdef CONFIG_PM
	.suspend	= rtl8168_suspend,
	.resume		= rtl8168_resume,
#endif
};

static int __init
rtl8168_init_module(void)
{
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,0)
	return pci_register_driver(&rtl8168_pci_driver);
#else
	return pci_module_init(&rtl8168_pci_driver);
#endif
}

static void __exit
rtl8168_cleanup_module(void)
{
	pci_unregister_driver(&rtl8168_pci_driver);
}

module_init(rtl8168_init_module);
module_exit(rtl8168_cleanup_module);

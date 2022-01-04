#ifndef MT_PCI_H
#define MT_PCI_H

// ===========================================
// LITERALS
// ===========================================

#ifndef PCI_VENDOR_ID_MELLANOX
#define PCI_VENDOR_ID_MELLANOX									0x15b3
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_TAVOR
#define PCI_DEVICE_ID_MELLANOX_TAVOR						0x5a44
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT
#define PCI_DEVICE_ID_MELLANOX_ARBEL_COMPAT		0x6278
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_ARBEL
#define PCI_DEVICE_ID_MELLANOX_ARBEL						0x6282
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI_OLD
#define PCI_DEVICE_ID_MELLANOX_SINAI_OLD				0x5e8c
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI
#define PCI_DEVICE_ID_MELLANOX_SINAI						0x6274
#endif

#ifndef PCI_VENDOR_ID_TOPSPIN
#define PCI_VENDOR_ID_TOPSPIN										0x1867
#endif

/* live fishes */
#ifndef PCI_DEVICE_ID_MELLANOX_TAVOR_BD
#define PCI_DEVICE_ID_MELLANOX_TAVOR_BD		0x5a45
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_ARBEL_BD
#define PCI_DEVICE_ID_MELLANOX_ARBEL_BD		0x6279
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI_OLD_BD
#define PCI_DEVICE_ID_MELLANOX_SINAI_OLD_BD	0x5e8d
#endif

#ifndef PCI_DEVICE_ID_MELLANOX_SINAI_BD
#define PCI_DEVICE_ID_MELLANOX_SINAI_BD		0x6275
#endif

// ===========================================
// TYPES
// ===========================================


// ===========================================
// MACROS/FUNCTIONS
// ===========================================

// get bar boundaries
#if 1
#define pci_resource_start(dev,bar_num)	((dev)->ext->bar[bar_num].phys)
#define pci_resource_len(dev,bar_num)	((dev)->ext->bar[bar_num].size)
#else
static inline 	uint64_t pci_resource_start(struct mthca_dev *dev, int bar_num) 
{
	return dev->ext->bar[bar_num].phys;
}
#endif


// i/o to registers

static inline u64 readq(const volatile void __iomem *addr)
{
	//TODO: write atomic implementation of _IO_READ_QWORD and change mthca_doorbell.h
	u64 val;
	READ_REGISTER_BUFFER_ULONG((PULONG)(addr), (PULONG)&val, 2 );
	return val;
}

static inline u32 readl(const volatile void __iomem *addr)
{
	return READ_REGISTER_ULONG((PULONG)(addr));
}

static inline u16 reads(const volatile void __iomem *addr)
{
	return READ_REGISTER_USHORT((PUSHORT)(addr));
}

static inline u8 readb(const volatile void __iomem *addr)
{
	return READ_REGISTER_UCHAR((PUCHAR)(addr));
}

#define __raw_readq		readq
#define __raw_readl		readl
#define __raw_reads		reads
#define __raw_readb		readb

static inline void writeq(unsigned __int64 val, volatile void __iomem *addr)
{
	//TODO: write atomic implementation of _IO_WRITE_QWORD and change mthca_doorbell.h
	WRITE_REGISTER_BUFFER_ULONG( (PULONG)(addr), (PULONG)&val, 2 );
}

static inline void writel(unsigned int val, volatile void __iomem *addr)
{
	WRITE_REGISTER_ULONG((PULONG)(addr),val);
}

static inline void writes(unsigned short val, volatile void __iomem *addr)
{
	WRITE_REGISTER_USHORT((PUSHORT)(addr),val);
}

static inline void writeb(unsigned char val, volatile void __iomem *addr)
{
	WRITE_REGISTER_UCHAR((PUCHAR)(addr),val);
}

#define __raw_writeq		writeq
#define __raw_writel		writel
#define __raw_writes		writes
#define __raw_writeb		writeb

#endif


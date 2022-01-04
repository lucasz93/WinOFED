#pragma once

// ===========================================
// LITERALS
// ===========================================


// ===========================================
// TYPES
// ===========================================


// ===========================================
// MACROS/FUNCTIONS
// ===========================================

#define PCI_DEVFN(slot, func)		((((slot) & 0x1f) << 3) | ((func) & 0x07))
#define PCI_SLOT(devfn)				(((devfn) >> 3) & 0x1f)
#define PCI_FUNC(devfn)				((devfn) & 0x07)

NTSTATUS pci_hca_reset( struct pci_dev *pdev);

/* use shim to implement that */
#define mlx4_reset(dev)		pci_hca_reset(dev->pdev)

// get bar boundaries
#define pci_resource_start(dev,bar_num)	((dev)->bar[bar_num >> 1].phys)
#define pci_resource_len(dev,bar_num)	((dev)->bar[bar_num >> 1].size)

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
#ifdef _WIN64
	WRITE_REGISTER_BUFFER_ULONG64( (PULONG64)addr, &val, 1 );
#else
	WRITE_REGISTER_BUFFER_ULONG( (PULONG)(addr), (PULONG)&val, 2 );
#endif
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

#if defined(_M_IX86) || defined(_M_AMD64)
#define __fast_writel(val,addr)			*(PULONG)(addr) = (ULONG)(val)
#else
#define __fast_writel(val,addr)			writel(val,addr)
#endif

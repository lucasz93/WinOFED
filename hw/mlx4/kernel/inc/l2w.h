#pragma once

#ifndef L2W_H
#define L2W_H

////////////////////////////////////////////////////////
//
// GENERAL INCLUDES
//
////////////////////////////////////////////////////////

// OS
#include <ntddk.h>
//#include <iointex.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#define NTSTRSAFE_LIB
#include <ntstrsafe.h>

#ifdef __cplusplus
extern "C"
{
#endif

// complib
#include <complib/cl_timer.h>
#include <complib/cl_qlist.h>

// mlx4
#include "vc.h"

////////////////////////////////////////////////////////
//
// LITERALS
//
////////////////////////////////////////////////////////

#define BITS_PER_LONG			32
#define N_BARS					3
#define HZ						1000000 /* 1 sec in usecs */
#define EOPNOTSUPP				95
#define ETH_LENGTH_OF_ADDRESS	6


////////////////////////////////////////////////////////
//
// SUBSTITUTIONS
//
////////////////////////////////////////////////////////

#define BUG_ON(exp)		ASSERT(!(exp)) /* in Linux follows here panic() !*/ 
#define WARN_ON(exp)	ASSERT(!(exp)) /* in Linux follows here panic() !*/ 

#pragma warning(disable : 4995)		// warning C4995: name was marked as #pragma deprecated (_snprintf)
#define snprintf		_snprintf
#define printk			cl_dbg_out
#define KERN_ERR		"err:"
#define KERN_WARNING	"warn:"
#define KERN_DEBUG		"dbg:"
#define BUG()

// memory barriers
#define wc_wmb KeMemoryBarrier

// x64 platform
#ifdef _WIN64

// x86_64
#ifdef _M_AMD64

#define wmb()	 	KeMemoryBarrierWithoutFence(); _mm_sfence()
#define rmb()	 	KeMemoryBarrierWithoutFence(); _mm_lfence()
#define mb()	 	KeMemoryBarrierWithoutFence(); _mm_mfence()
#define mmiowb() 

// Itanium and others
#else

#define wmb() 		KeMemoryBarrier()
#define rmb() 		KeMemoryBarrier()
#define mb() 		KeMemoryBarrier()
#define mmiowb() 	KeMemoryBarrier()

#endif

// x86 platform
#else

// x86
#ifdef _M_IX86

#define wmb() 		KeMemoryBarrier()
#define rmb() 		KeMemoryBarrier()
#define mb() 		KeMemoryBarrier()
#define mmiowb() 	KeMemoryBarrier()

// Itanium and others
#else

#define wmb() 		KeMemoryBarrier()
#define rmb() 		KeMemoryBarrier()
#define mb() 		KeMemoryBarrier()
#define mmiowb() 	KeMemoryBarrier()

#endif

#endif

// gcc compiler attributes
#define __devinit
#define __devinitdata
#define __init
#define __exit
#define __force
#define __iomem
#define __attribute_const__
#define likely(x)			(x)
#define unlikely(x)			(x)
#define __attribute__(a)
#define __bitwise

// container_of
#define container_of		CONTAINING_RECORD

// inline 
#define inline	__inline

// new Linux event mechanism
#define complete(a)					wake_up(a)

// convert
#define __constant_htons		CL_HTON16
#define __constant_cpu_to_be32	CL_HTON32

// various
#define __always_inline				inline

#if WINVER >= 0x602
#define num_possible_cpus() KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS)
#else 
#define num_possible_cpus() KeQueryActiveProcessorCount(NULL)
#endif

// ByteSwap
#define htons		cl_hton16
#define htonl		cl_hton32
#define htonll		cl_hton64

#define ntohs		cl_ntoh16
#define ntohl		cl_ntoh32
#define ntohll		cl_ntoh64


////////////////////////////////////////////////////////
//
// TYPES
//
////////////////////////////////////////////////////////

#define true (u8)1
#define false (u8)0

// basic types
typedef unsigned char			u8, __u8;
typedef unsigned short int	u16, __u16;
typedef unsigned int				u32, __u32;
typedef unsigned __int64		u64, __u64;
typedef char			s8, __s8;
typedef short int	s16, __s16;
typedef int				s32, __s32;
typedef __int64		s64, __s64;

#ifndef __cplusplus
typedef u8 bool;
#endif

// inherited
typedef u16  __le16;
typedef u16  __be16;
typedef u32  __le32;
typedef u32  __be32;
typedef u64  __le64;
typedef u64  __be64;
typedef u16  be16;
typedef u32  le32;
typedef u32  be32;
typedef u64  le64;
typedef u64  be64;
typedef u64 io_addr_t;
typedef io_addr_t resource_size_t;

// dummy function
typedef void (*MT_EMPTY_FUNC)();

// PCI BAR descriptor
typedef enum _hca_bar_type
{
	HCA_BAR_TYPE_HCR,
	HCA_BAR_TYPE_UAR,
	HCA_BAR_TYPE_DDR,
	HCA_BAR_TYPE_MAX

}	hca_bar_type_t;


typedef struct _hca_bar
{
	uint64_t			phys;
	void				*virt;
	SIZE_T				size;

}	hca_bar_t;

struct msix_saved_info {
	PVOID	vca;		/* MSI-X Vector Table card address */
	PVOID	mca;		/* MSI-X Mask Table card address */
	PVOID	vsa;		/* MSI-X Vector Table saved address */
	PVOID	msa;		/* MSI-X Mask Table saved address */
	ULONG	vsz;		/* MSI-X Vector Table size */
	ULONG	msz;		/* MSI-X Mask Table size */
	int		num;		/* number of supported MSI-X vectors */
	int		valid;		/* the structure is valid */
};

struct msix_map {
	KAFFINITY	cpu;		/* affinity of this MSI-X vector */
	int			eq_ix;		/* EQ index in the array of EQs */
	int			ref_cnt;	/* number of users */
};

typedef VOID (*ET_POST_EVENT)( IN const char * const  Caller, IN const char * const  Format, ...);

typedef struct _MLX4_ST_DEVICE *PMLX4_ST_DEVICE;

// interface structure between Upper and Low Layers of the driver
// This structure is a part of MLX4_BUS_IB_INTERFACE 
// Upon its changing you have to change MLX4_BUS_IB_INTERFACE_VERSION 
struct pci_dev
{
	// driver: OS/platform resources
	BUS_INTERFACE_STANDARD			bus_pci_ifc;
	PCI_COMMON_CONFIG				pci_cfg_space;
	struct msix_saved_info			msix_info;
	struct msix_map*				p_msix_map;
	uplink_info_t					uplink_info;
	// driver: card resources
	hca_bar_t						bar[N_BARS];
	CM_PARTIAL_RESOURCE_DESCRIPTOR	int_info;	/* HCA interrupt resources */
	// driver: various objects and info
	USHORT							ven_id;
	USHORT							dev_id;
	USHORT							sub_vendor_id;
	USHORT							sub_system_id;
	UCHAR							revision_id;
	UCHAR							partition_status;
	DMA_ADAPTER		*				p_dma_adapter;	/* HCA adapter object */
	DEVICE_OBJECT	*				p_self_do;		/* mlx4_bus's FDO */
	DEVICE_OBJECT	*				pdo;			/* mlx4_bus's PDO */
	PVOID							p_wdf_device;   /* wdf_device */
	LONG							ib_hca_created;
	// mlx4_ib: various objects and info	
	struct ib_device *				ib_dev;
	// mlx4_net: various objects and info	
	struct mlx4_dev *				dev;
	volatile long					dpc_lock;
	PUCHAR							vpd;
	int								vpd_size;
	WCHAR							location[64];
	int								pci_bus;
	int								pci_device;
	int								pci_func;
	USHORT							devfn;
	char							name[24];		/* mlx4_role_bus_func_dev */
	// statistics
	PMLX4_ST_DEVICE 				p_stat;
	struct mlx4_priv 				*priv;
//
// WDM interrupts
//
	// legacy
	PKINTERRUPT						int_obj;		/* HCA interrupt object */
	KSPIN_LOCK						isr_lock; 		/* lock for the ISR */

	// MSI-X interrupts
	int								n_msi_vectors_req;/* number of MSI vectors, requested in Registry (usually, taken from INF) */
	int								n_msi_vectors_sup;/* number of MSI vectors, supported by first device */
	int								n_msi_vectors_alloc;/* number of MSI vectors, allocated by PnP manager */
	int								n_msi_vectors;		/* number of MSI vectors; 0 - no MSI */
	ULONG							version;
	int 							legacy_connect;
	// others
	int 							is_reset_prohibited;
	boolean_t 						start_event_taken;
	
	USHORT							clp_ver;
    KEVENT 							remove_dev_lock;      /* lock remove_one process */
	ET_POST_EVENT					post_event;

};

/* DPC */
typedef void (*dpc_t)( struct _KDPC *, PVOID, PVOID, PVOID );

#ifdef SUPPORTED_ONLY_IN_LINUX
struct attribute {
	const char						*name;
	void							*owner;
	u32								mode;
};

struct device_attribute {
	struct attribute        attr;
	ssize_t (*show)(struct device *dev, struct device_attribute *attr, char *buf);
	ssize_t (*store)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
};
#endif

////////////////////////////////////////////////////////
//
// MACROS
//
////////////////////////////////////////////////////////

// conversions
#define swab32(a)			_byteswap_ulong((ULONG)(a))
#define cpu_to_be16(a)		_byteswap_ushort((USHORT)(a))
#define be16_to_cpu(a)		_byteswap_ushort((USHORT)(a))
#define cpu_to_be32(a)		_byteswap_ulong((ULONG)(a))
#define be32_to_cpu(a)		_byteswap_ulong((ULONG)(a))
#define cpu_to_be64(a)		_byteswap_uint64((UINT64)(a))
#define cpu_to_be24(dst, src) {(dst)[0] = (u8) (((src) >> 16) & 0xff); (dst)[1] = (u8) (((src) >> 8) & 0xff); (dst)[2] = (u8) ((src) & 0xff);}
#define be24_to_cpu(a)		(u32)((a)[0] << 16 | (a)[1] << 8 | (a)[2])
#define be64_to_cpu(a)		_byteswap_uint64((UINT64)(a))
#define be64_to_cpup(p)		_byteswap_uint64(*(PUINT64)(p))
#define be32_to_cpup(p)		_byteswap_ulong(*(PULONG)(p))
#define be16_to_cpup(p)		_byteswap_ushort(*(PUSHORT)(p))

// ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// ALIGN
#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))
#define PTR_ALIGN(size)	(((size) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))

// there is a bug in Microsoft compiler, that when _byteswap_uint64() gets an expression
// it executes the expression but doesn't swap tte dwords
// So, there's a workaround
#ifdef BYTESWAP_UINT64_BUG_FIXED
#define CPU_2_BE64_PREP		
#define CPU_2_BE64(x)			cl_hton64(x)
#else
#define CPU_2_BE64_PREP	unsigned __int64 __tmp__	
#define CPU_2_BE64(x)			( __tmp__ = x, cl_hton64(__tmp__) )
#endif

#define ERR_PTR(error)		((void*)(LONG_PTR)(error))
#define PTR_ERR(ptr)			((long)(LONG_PTR)(void*)(ptr))
#define ETH_ALEN    6

//TODO: there are 2 assumptions here:
// - pointer can't be too big (around -1)
// - error can't be bigger than 1000
#define IS_ERR(ptr)				((ULONG_PTR)ptr > (ULONG_PTR)-1000L)

#define BITS_TO_LONGS(bits) \
	 (((bits)+BITS_PER_LONG-1)/BITS_PER_LONG)

#ifndef ETIMEDOUT
#define ETIMEDOUT		(110)
#endif

#ifdef PAGE_ALIGN
#undef PAGE_ALIGN
#define PAGE_ALIGN(Va) ((u64)((ULONG_PTR)(Va) & ~(PAGE_SIZE - 1)))
#endif

#define NEXT_PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* typed minimum */
#define min_t(type,x,y)		((type)(x) < (type)(y) ? (type)(x) : (type)(y))
#define max_t(type,x,y)		((type)(x) > (type)(y) ? (type)(x) : (type)(y))

#define DIV_ROUND_UP(n,d) 	(((n) + (d) - 1) / (d))
#define roundup(x, y) 		((((x) + ((y) - 1)) / (y)) * (y))

#define EXPORT_SYMBOL(name)
#ifndef USE_WDM_INTERRUPTS
#define free_irq(pdev)
#endif

static inline NTSTATUS errno_to_ntstatus(int err)
{
#define MAP_ERR(err,ntstatus)	case err: status = ntstatus; break
	NTSTATUS status;

	if (!err) 
		return STATUS_SUCCESS;

	if (err < 0)
		err = -err;
	switch (err) {
		MAP_ERR( ENOENT, STATUS_NOT_FOUND );
		MAP_ERR( EAGAIN, STATUS_DEVICE_BUSY );
		MAP_ERR( ENOMEM, STATUS_NO_MEMORY );
		MAP_ERR( EACCES, STATUS_ACCESS_DENIED );
		MAP_ERR( EFAULT, STATUS_DRIVER_INTERNAL_ERROR );
		MAP_ERR( EBUSY, STATUS_INSUFFICIENT_RESOURCES );
		MAP_ERR( ENODEV, STATUS_NOT_SUPPORTED );
		MAP_ERR( EINVAL, STATUS_INVALID_PARAMETER );
		MAP_ERR( ENOSYS, STATUS_NOT_SUPPORTED );
		default:
			status = STATUS_UNSUCCESSFUL;
			break;
	}
	return status;
}


////////////////////////////////////////////////////////
//
// PROTOTYPES
//
////////////////////////////////////////////////////////

SIZE_T strlcpy(__out char *dest,__in const void *src,__in SIZE_T size);
int core_init();
void core_cleanup();
int l2w_init();
void l2w_cleanup();


////////////////////////////////////////////////////////
//
// SPECIFIC INCLUDES
//
////////////////////////////////////////////////////////

struct mlx4_dev;
struct mlx4_priv;

#include <l2w_atomic.h>
#include <l2w_bit.h>
#include <l2w_bitmap.h>
#include "l2w_debug.h"
#include <l2w_memory.h>
#include <l2w_umem.h>
#include <l2w_list.h>
#include <l2w_pci.h>
#include <l2w_pcipool.h>
#include "l2w_radix.h"
#include <l2w_spinlock.h>
#include <l2w_sync.h>
#include <l2w_time.h>
#include <l2w_network_headers.h>
#include <l2w_workqueue.h>
#include <l2w_sk_buff.h>
#include <l2w_debug.h>
#include <l2w_scsi.h>

#include "device.h"

#ifdef __cplusplus
}	// extern "C"
#endif
static inline int mlx4_is_barred(struct mlx4_dev *dev)
{
	return dev->flags &  MLX4_FLAG_RESET_DRIVER;
}

static inline int mlx4_is_in_reset(struct mlx4_dev *dev)
{
	return dev->flags & MLX4_FLAG_RESET_STARTED;
}

int parse_dev_location(
	const char *buffer,
	const char *format,
	int *bus, int *dev, int *func
);

#endif

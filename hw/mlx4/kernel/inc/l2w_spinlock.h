#pragma once

#include <complib/cl_spinlock.h>

#if 1

typedef cl_spinlock_t	spinlock_t;

static inline void spin_lock_init(
	IN		spinlock_t* const p_spinlock )
{
	cl_spinlock_init( p_spinlock );
}

#define spin_lock								cl_spinlock_acquire
#define spin_unlock								cl_spinlock_release

CL_INLINE void
spin_lock_dpc( 
	IN	cl_spinlock_t* const	p_spinlock )
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	KeAcquireSpinLockAtDpcLevel( &p_spinlock->lock );
}

CL_INLINE void
spin_unlock_dpc(
	IN	cl_spinlock_t* const	p_spinlock )
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	KeReleaseSpinLockFromDpcLevel( &p_spinlock->lock );
}

#else
typedef struct spinlock {
	KSPIN_LOCK			lock;
	KLOCK_QUEUE_HANDLE	lockh;
	KIRQL 				irql;
} spinlock_t;


static inline void spin_lock_init(
	IN		spinlock_t* const p_spinlock )
{ 
	KeInitializeSpinLock( &p_spinlock->lock ); 
}

static inline void
spin_lock( 
	IN		spinlock_t* const	l)
{
	KIRQL irql = KeGetCurrentIrql();

	ASSERT( l && irql <= DISPATCH_LEVEL );

	if (irql == DISPATCH_LEVEL)
		KeAcquireInStackQueuedSpinLockAtDpcLevel( &l->lock, &l->lockh );
	else
		KeAcquireInStackQueuedSpinLock( &l->lock, &l->lockh );
	l->irql = irql;
}

static inline void
spin_unlock(
	IN		spinlock_t* const	l)
{
	ASSERT( l && KeGetCurrentIrql() == DISPATCH_LEVEL );
	if (l->irql == DISPATCH_LEVEL)
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &l->lockh );
	else
		KeReleaseInStackQueuedSpinLock( &l->lockh );
}

/* to be used only at DPC level */
static inline void
spin_lock_dpc( 
	IN		spinlock_t* const	l)
{
	ASSERT( l && KeGetCurrentIrql() == DISPATCH_LEVEL );
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &l->lock, &l->lockh );
}

/* to be used only at DPC level */
static inline void
spin_unlock_dpc(
	IN		spinlock_t* const	l)
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &l->lockh );
}

static inline void
spin_lock_sync( 
	IN		spinlock_t* const	l )
{
	KLOCK_QUEUE_HANDLE lockh;
	ASSERT( l && KeGetCurrentIrql() <= DISPATCH_LEVEL );
	KeAcquireInStackQueuedSpinLock ( &l->lock, &lockh );
	KeReleaseInStackQueuedSpinLock( &lockh );
}

#endif

#define DEFINE_SPINLOCK(lock)		spinlock_t lock


#define spin_lock_irqsave(_lock_, flags) cl_spinlock_acquire((cl_spinlock_t *)(_lock_))
#define spin_unlock_irqrestore(_lock_, flags) cl_spinlock_release((cl_spinlock_t *)(_lock_))

static inline void
spin_lock_sync( 
	IN		spinlock_t* const	l )
{
	KLOCK_QUEUE_HANDLE lockh;
	ASSERT( l && KeGetCurrentIrql() <= DISPATCH_LEVEL );
	KeAcquireInStackQueuedSpinLock ( &l->lock, &lockh );
	KeReleaseInStackQueuedSpinLock( &lockh );
}

/* we are working from DPC level, so we can use usual spinlocks */
#define spin_lock_irq(_lock_) cl_spinlock_acquire((cl_spinlock_t *)(_lock_))
#define spin_unlock_irq(_lock_) cl_spinlock_release((cl_spinlock_t *)(_lock_))
#define spin_lock_nested(a,b)				spin_lock(a)

/* Windows doesn't support such kind of spinlocks so far, but may be tomorrow ... */
#define rwlock_init							spin_lock_init
#define read_lock_irqsave(lock, flags) 		cl_spinlock_acquire(lock)
#define read_unlock_irqrestore(lock, flags) cl_spinlock_release(lock)
#define write_lock_irq						spin_lock_irq
#define write_unlock_irq					spin_unlock_irq

// rw_lock
typedef spinlock_t		rwlock_t;


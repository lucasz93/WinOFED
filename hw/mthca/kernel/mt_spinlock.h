#ifndef MT_SPINLOCK_H
#define MT_SPINLOCK_H

typedef struct spinlock {
	KSPIN_LOCK			lock;

#ifdef SUPPORT_SPINLOCK_ISR	
	PKINTERRUPT  	p_int_obj;
	KIRQL				irql;
#endif
} spinlock_t;

typedef struct {
	KLOCK_QUEUE_HANDLE lockh;
	KIRQL 				irql;
} spinlockh_t;

#ifdef SUPPORT_SPINLOCK_ISR	

static inline void
spin_lock_setint( 
	IN		spinlock_t* const	l, 
	IN PKINTERRUPT  p_int_obj )
{
	MT_ASSERT( l );
	l->p_int_obj = p_int_obj;
}

static inline void spin_lock_irq_init(
	IN		spinlock_t* const l,
	IN 	PKINTERRUPT int_obj
	)
{ 
	KeInitializeSpinLock( &l->lock ); 
	l->p_int_obj = int_obj; 
}

static inline unsigned long
spin_lock_irq( 
	IN		spinlock_t* const	l)
{
	MT_ASSERT( l );
	MT_ASSERT( l->p_int_obj );
	return (unsigned long)(l->irql = KeAcquireInterruptSpinLock ( l->p_int_obj ));
}

static inline void
spin_unlock_irq( 
	IN		spinlock_t* const p_spinlock ) 
{
	MT_ASSERT( p_spinlock );
	MT_ASSERT( p_spinlock->p_int_obj );
	KeReleaseInterruptSpinLock ( p_spinlock->p_int_obj, p_spinlock->irql );
}

#endif

#define SPIN_LOCK_PREP(lh)		spinlockh_t lh

static inline void spin_lock_init(
	IN		spinlock_t* const p_spinlock )
{ 
	KeInitializeSpinLock( &p_spinlock->lock ); 
}

static inline void
spin_lock( 
	IN		spinlock_t* const	l,
	IN		spinlockh_t * const lh)
{
	KIRQL irql = KeGetCurrentIrql();

	MT_ASSERT( l || lh );
	ASSERT(irql <= DISPATCH_LEVEL);

	if (irql == DISPATCH_LEVEL)
		KeAcquireInStackQueuedSpinLockAtDpcLevel( &l->lock, &lh->lockh );
	else
		KeAcquireInStackQueuedSpinLock( &l->lock, &lh->lockh );
	lh->irql = irql;
}

static inline void
spin_unlock(
	IN		spinlockh_t * const lh)
{
	MT_ASSERT( lh );
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	if (lh->irql == DISPATCH_LEVEL)
		KeReleaseInStackQueuedSpinLockFromDpcLevel( &lh->lockh );
	else
		KeReleaseInStackQueuedSpinLock( &lh->lockh );
}

static inline void
spin_lock_sync( 
	IN		spinlock_t* const	l )
{
	KLOCK_QUEUE_HANDLE lockh;
	MT_ASSERT( l );
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeAcquireInStackQueuedSpinLock ( &l->lock, &lockh );
	KeReleaseInStackQueuedSpinLock( &lockh );
}

/* to be used only at DPC level */
static inline void
spin_lock_dpc( 
	IN		spinlock_t* const	l,
	IN		spinlockh_t * const lh)
{
	MT_ASSERT( l || lh );
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	KeAcquireInStackQueuedSpinLockAtDpcLevel( &l->lock, &lh->lockh );
}

/* to be used only at DPC level */
static inline void
spin_unlock_dpc(
	IN		spinlockh_t * const lh)
{
	ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
	KeReleaseInStackQueuedSpinLockFromDpcLevel( &lh->lockh );
}


/* we are working from DPC level, so we can use usual spinlocks */
#define spin_lock_irq						spin_lock
#define spin_unlock_irq 					spin_unlock

/* no diff in Windows */
#define spin_lock_irqsave 				spin_lock_irq
#define spin_unlock_irqrestore 			spin_unlock_irq

/* Windows doesn't support such kind of spinlocks so far, but may be tomorrow ... */
#define rwlock_init							spin_lock_init
#define read_lock_irqsave				spin_lock_irqsave
#define read_unlock_irqrestore			spin_unlock_irqrestore
#define write_lock_irq						spin_lock_irq
#define write_unlock_irq					spin_unlock_irq

#endif


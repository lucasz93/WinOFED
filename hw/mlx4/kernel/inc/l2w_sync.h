#pragma once

// literals
#ifndef LONG_MAX
#define LONG_MAX      2147483647L   /* maximum (signed) long value */
#endif

#ifndef ULONG_MAX
#define ULONG_MAX 4294967295UL
#endif

//
// mutex wrapper
//

struct mutex
{
	KMUTEX m;
};

#define DEFINE_MUTEX(a)		struct mutex a

static inline void mutex_init( struct mutex * mutex )
{
	KeInitializeMutex( &mutex->m, 0 );
}

static inline void mutex_lock( struct mutex * mutex )
{
	NTSTATUS	status;
	int			need_to_wait = 1;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	while (need_to_wait) {
		status = KeWaitForSingleObject( &mutex->m, Executive, KernelMode, FALSE,	NULL );
		if (status == STATUS_SUCCESS)
			break;
	}
}

static inline void mutex_unlock( struct mutex * mutex )
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeReleaseMutex( &mutex->m, FALSE );
}


//
// semaphore wrapper
//

struct semaphore
{
	KSEMAPHORE s;
};

static inline void sema_init(
	IN struct semaphore *sem,
	IN LONG  cnt)
{
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	KeInitializeSemaphore( &sem->s, cnt, cnt );
}

static inline void up( struct semaphore *sem )
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeReleaseSemaphore( &sem->s, 0, 1, FALSE );
}
static inline void down( struct semaphore *sem )
{
	NTSTATUS 		status;
	int need_to_wait = 1;
	
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	while (need_to_wait) {
		status = KeWaitForSingleObject( &sem->s, Executive, KernelMode, FALSE,  NULL );
		if (status == STATUS_SUCCESS)
			break;
	}
}


//
// completion wrapper
//

struct completion
{
	KEVENT		event;
	int			done;
};

static inline void init_completion( struct completion * compl )
{
	//TODO: ASSERT is temporary outcommented, because using of fast mutexes in CompLib
	// cause working on APC_LEVEL
	//ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);	 
	KeInitializeEvent( &compl->event, NotificationEvent , FALSE );
	compl->done = 0;
}

static inline int wait_for_completion_timeout( struct completion * compl, unsigned long timeout )
{
	LARGE_INTEGER interval;
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	interval.QuadPart = (-10)* (__int64)timeout;
	return (int)KeWaitForSingleObject( &compl->event, Executive, KernelMode, FALSE,  &interval );
}

static inline void wait_for_completion( struct completion * compl )
{
	NTSTATUS status;
	int need_to_wait = 1;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

	while (need_to_wait) {
		status = KeWaitForSingleObject( &compl->event, Executive, KernelMode, FALSE,  NULL );
		if (status == STATUS_SUCCESS)
		 break;
	}
}



static inline void complete( struct completion * compl )
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	compl->done++;
	KeSetEvent( &compl->event, 0, FALSE );
}

#ifdef USE_WDM_INTERRUPTS

//
// IRQ wrapper
//

void free_irq(struct mlx4_dev *dev);

int request_irq(
	IN		struct mlx4_dev *	dev,		
	IN		PKSERVICE_ROUTINE	isr,		/* ISR */
	IN		PVOID				isr_ctx,	/* ISR context */
	IN		PKMESSAGE_SERVICE_ROUTINE	misr,		/* Message ISR */
	OUT		PKINTERRUPT		*	int_obj		/* interrupt object */
	);

#endif

//
// various
//

// TODO: Is it enough to wait at DPC level ? 
// Maybe we need to use here KeSynchronizeExecution ?
static inline void synchronize_irq(unsigned int irq)
{
	UNUSED_PARAM(irq);
	KeFlushQueuedDpcs();
}




#ifndef MT_SYNC_H
#define MT_SYNC_H

// literals
#ifndef LONG_MAX
#define LONG_MAX      2147483647L   /* maximum (signed) long value */
#endif


// mutex wrapper

// suitable both for mutexes and semaphores
static inline void down(PRKMUTEX  p_mutex)
{
	NTSTATUS 		status;
	int need_to_wait = 1;
	
   ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
   while (need_to_wait) {
	  status = KeWaitForSingleObject( p_mutex, Executive, KernelMode, FALSE,  NULL );
	  if (status == STATUS_SUCCESS)
		break;
  }
}

// suitable both for mutexes and semaphores
static inline int down_interruptible(PRKMUTEX  p_mutex)
{
	NTSTATUS 		status;
	
	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	status = KeWaitForSingleObject( p_mutex, Executive, KernelMode, TRUE,  NULL );
	if (status == STATUS_SUCCESS)
		return 0;
	return -EINTR;
}

#define sem_down(ptr)								down((PRKMUTEX)(ptr))
#define sem_down_interruptible(ptr)		down_interruptible((PRKMUTEX)(ptr))

static inline void up(PRKMUTEX  p_mutex)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeReleaseMutex( p_mutex, FALSE );
}

static inline void sem_up(PRKSEMAPHORE  p_sem)
{
	ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeReleaseSemaphore( p_sem, 0, 1, FALSE );
}

static inline void sem_init(
	IN PRKSEMAPHORE  p_sem,
	IN LONG  cnt,
	IN LONG  limit)
{
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	KeInitializeSemaphore( p_sem, cnt, limit );
}


typedef struct wait_queue_head {
	KEVENT		event;	
} wait_queue_head_t;

static inline void wait_event(wait_queue_head_t *obj_p, int condition)
{
	NTSTATUS 		status;
	int need_to_wait = 1;
	MT_ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);
	if (condition) 
		return;
   while (need_to_wait) {
	  status = KeWaitForSingleObject( &obj_p->event, Executive, KernelMode, FALSE,  NULL );
	  if (status == STATUS_SUCCESS)
		break;
  }
}

static inline void wake_up(wait_queue_head_t *obj_p)
{
	MT_ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
	KeSetEvent( &obj_p->event, 0, FALSE );
}

static inline void init_waitqueue_head(wait_queue_head_t *obj_p)
{
	//TODO: ASSERT is temporary outcommented, because using of fast mutexes in CompLib
	// cause working on APC_LEVEL
	//ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);	 
	KeInitializeEvent(  &obj_p->event, NotificationEvent , FALSE );
}

static inline void free_irq(PKINTERRUPT int_obj)
{
	IoDisconnectInterrupt( int_obj );
}

int request_irq(
	IN 	CM_PARTIAL_RESOURCE_DESCRIPTOR	*int_info,	/* interrupt resources */
	IN		KSPIN_LOCK  	*isr_lock,		/* spin lcok for ISR */			
	IN		PKSERVICE_ROUTINE isr,		/* ISR */
	IN		void *isr_ctx,						/* ISR context */
	OUT	PKINTERRUPT *int_obj			/* interrupt object */
	);

		
#endif

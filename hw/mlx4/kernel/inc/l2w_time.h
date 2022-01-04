#pragma once

#include <complib/cl_timer.h>

// returns current time in msecs (u64)
#define jiffies						get_tickcount_in_ms()

// jiffies is measured in msecs 
#define jiffies_to_usecs(msecs)		((msecs)*1000)


#define time_after(a,b) 			((__int64)(b) - (__int64)(a) < 0)
#define time_before(a,b)			time_after(b,a)

#define time_after_eq(a,b)			((__int64)(a) - (__int64)(b) >= 0)
#define time_before_eq(a,b)			time_after_eq(b,a)

extern u32 g_time_increment;
extern LARGE_INTEGER g_cmd_interval;
#define cond_resched()	KeDelayExecutionThread( KernelMode, FALSE, &g_cmd_interval )

uint64_t get_tickcount_in_ms(void);

/*
*   Timer
*/

#define timer_list _cl_timer

static inline void setup_timer(struct timer_list *timer, void (*function)(void*), void* context)
{
    cl_timer_init(timer, function, context);
}


static inline void del_timer_sync(struct timer_list *timer)
{
    if(timer->pfn_callback)
    {
        cl_timer_destroy(timer);
    }
}

static inline void del_timer(struct timer_list * timer)
{
    if(timer->pfn_callback)
    {
        cl_timer_stop(timer);
    }
}

static inline void msleep(unsigned int msecs)
{
    LARGE_INTEGER interval = {0};

    interval.QuadPart = 10000 * msecs; /* msecs -> 100 nsecs */
    KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

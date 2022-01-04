#pragma once

#include <complib/cl_thread.h>
#include <complib/cl_event.h>
#include "l2w.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define NAME_LENGTH 255

struct workqueue_struct
{
    char name[NAME_LENGTH];
    cl_thread_t thread;
    struct list_head works;
    spinlock_t lock;
    cl_event_t work_event;
    int terminate_flag;
    struct work_struct *current_work;
};

struct work_struct;

typedef void (*work_func_t)(struct work_struct *work);

struct work_struct
{
    struct list_head list;
    work_func_t func;

    struct workqueue_struct *wq;
};

struct delayed_work
{
    struct work_struct work;
    struct timer_list timer;
};

/* init_work_queues - init provider (must for delayed work) */
int init_workqueues();
void shutdown_workqueues();

static void workqueue_do_work(struct workqueue_struct *queue)
{
    struct work_struct *work = NULL;

    spin_lock(&queue->lock);
    while(! list_empty(&queue->works))
    {
        work = container_of(queue->works.Flink, struct work_struct, list);
        list_del(&work->list);
        queue->current_work = work;
        work->wq = NULL;
        spin_unlock(&queue->lock);
        work->func(work);
        spin_lock(&queue->lock);
        queue->current_work = NULL;
    }
    spin_unlock(&queue->lock);
}

static void workqueue_func(void *context)
{
    struct workqueue_struct *queue = (struct workqueue_struct *) context;

    while(! queue->terminate_flag)
    {
        cl_event_wait_on(&queue->work_event, EVENT_NO_TIMEOUT, FALSE);
        workqueue_do_work(queue);
    }
}

static inline struct workqueue_struct *create_singlethread_workqueue(const char *name)
{
    struct workqueue_struct *queue = NULL;
    cl_status_t status;

    queue = (struct workqueue_struct *) kmalloc(sizeof(struct workqueue_struct), GFP_KERNEL);
    if(queue == NULL)
    {
        return NULL;
    }
    memset(queue, 0, sizeof(struct workqueue_struct));

#ifdef NTDDI_WIN8   
    strncpy_s(queue->name, NAME_LENGTH, name, NAME_LENGTH);
#else
    strncpy(queue->name, name, NAME_LENGTH);
#endif

    INIT_LIST_HEAD(&queue->works);
    spin_lock_init(&queue->lock);
    cl_event_init(&queue->work_event, FALSE);
    
    status = cl_thread_init(&queue->thread, workqueue_func, queue, name);

    if(status != CL_SUCCESS)
    {
        kfree(queue);
        return NULL;
    }
    return queue;
}

static inline void flush_workqueue(struct workqueue_struct *queue)
{
    workqueue_do_work(queue);
}

static inline void destroy_workqueue(struct workqueue_struct *queue)
{
    // set the exit flag
    queue->terminate_flag = TRUE;
    cl_event_signal(&queue->work_event);

    // wait for thread to exit
    cl_thread_destroy(&queue->thread);

    cl_event_destroy(&queue->work_event);
    
    kfree(queue);
}

#define INIT_WORK(_work, _func) { (_work)->func = (_func); INIT_LIST_HEAD(&(_work)->list); }

static inline int queue_work(struct workqueue_struct *queue, 
               struct work_struct *work)
{
    if(queue == NULL || work == NULL)
    {
        return -1;
    }
    
    spin_lock(&queue->lock);
    list_add_tail(&work->list, &queue->works);
    work->wq = queue;
    spin_unlock(&queue->lock);
    cl_event_signal(&queue->work_event);
    return 0;
}

int cancel_work_sync(struct work_struct *work);

#define INIT_DELAYED_WORK(_delayed_work, func)  { INIT_WORK(&(_delayed_work)->work, func); }

int schedule_delayed_work(struct delayed_work *work, unsigned long delay);

/* Reliably kill delayed work */
void cancel_delayed_work_sync(struct delayed_work *work);

#ifdef __cplusplus
}   // extern "C"
#endif


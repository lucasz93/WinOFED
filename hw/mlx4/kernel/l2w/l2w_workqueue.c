#include "l2w_precomp.h"

static struct workqueue_struct *delayed_wq = NULL;

int init_workqueues()
{
    delayed_wq = create_singlethread_workqueue("DELAYED_WQ");

    if(delayed_wq == NULL)
    {
        return -1;
    }
    return 0;
}

void shutdown_workqueues()
{
    flush_workqueue(delayed_wq);
    destroy_workqueue(delayed_wq);
}

#pragma warning(disable:4505) //unreferenced local function
static void queue_delayed_work_timer(void* context)
{
    struct delayed_work *work = (struct delayed_work *) context;

    queue_work(delayed_wq, &work->work);
}



int cancel_work_sync(struct work_struct *work)
{
    struct workqueue_struct *wq = NULL;
    int pending = 0;

    if(work == NULL)
    {
        return 0;
    }

    if(work->func == NULL)
    {// work was not initialized
        return 0;
    }

    wq = work->wq;

    if(wq == NULL)
    {
        return 0;
    }
    
    spin_lock(&wq->lock);
    if(wq->current_work == work)
    {// work is running - wait for completion
        while(wq->current_work == work)
        {
            spin_unlock(&wq->lock);
            msleep(10);
            spin_lock(&wq->lock);
        }
        spin_unlock(&wq->lock);
    }
    else
    {// work is pending in the queue
        if(work->wq != NULL)
        {// work is queued, and not just initialized
            list_del(&work->list);
            pending = 1;
        }
        spin_unlock(&wq->lock);
    }
    return pending;
}

void cancel_delayed_work_sync(struct delayed_work *work)
{
    if(work->timer.pfn_callback != NULL)
    {// timer was set
        del_timer_sync(&work->timer);
    }
    
    cancel_work_sync(&work->work);
}


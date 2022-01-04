/*
 * Copyright (c) 2009 Intel Corporation. All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <ntddk.h>
#include "work_queue.h"


typedef struct _WORK_QUEUE_TASK
{
	WORK_QUEUE			*pWorkQueue;
	PIO_WORKITEM		pWorkItem;
	int					Next;
	int					Index;

}	WORK_QUEUE_TASK;


NTSTATUS WorkQueueInit(WORK_QUEUE *pWorkQueue, PDEVICE_OBJECT Device, int TaskCount)
{
	WORK_QUEUE_TASK *task = NULL;
	int i;

	if (TaskCount == 0) {
#if WINVER >= 0x602
		TaskCount = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
#else 
		TaskCount = KeQueryActiveProcessorCount(NULL);
#endif
	}

	KeInitializeSpinLock(&pWorkQueue->Lock);
	InitializeListHead(&pWorkQueue->List);
	pWorkQueue->TaskCount = TaskCount;
	pWorkQueue->TaskArray = (WORK_QUEUE_TASK *)ExAllocatePoolWithTag(NonPagedPool,
												  sizeof(WORK_QUEUE_TASK) * (TaskCount + 1),
												  'ktqw');
	if (pWorkQueue->TaskArray == NULL) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for (i = 0; i <= TaskCount; i++) {
		task = &pWorkQueue->TaskArray[i];
		task->pWorkQueue = pWorkQueue;
		task->Index = i;
		task->Next = i + 1;
		if (i > 0) {
			task->pWorkItem = IoAllocateWorkItem(Device);
			if (task->pWorkItem == NULL) {
				goto err;
			}
		}
	}

    #pragma prefast(suppress:6011, "task can't be NULL")
    task->Next = 0;
	return STATUS_SUCCESS;

err:
	while (--i > 0) {
		IoFreeWorkItem(pWorkQueue->TaskArray[i].pWorkItem);
	}
	ExFreePoolWithTag(pWorkQueue->TaskArray, 'ktqw');
	return STATUS_INSUFFICIENT_RESOURCES;
}

void WorkQueueDestroy(WORK_QUEUE *pWorkQueue)
{
	while (pWorkQueue->TaskCount > 0) {
		IoFreeWorkItem(pWorkQueue->TaskArray[pWorkQueue->TaskCount--].pWorkItem);
	}
	ExFreePool(pWorkQueue->TaskArray);
}

static VOID WorkQueueHandler(PDEVICE_OBJECT pDevice, void *Context)
{
	WORK_QUEUE *wq;
	WORK_QUEUE_TASK *task = (WORK_QUEUE_TASK *) Context;
	WORK_ENTRY *work;
	LIST_ENTRY *entry;
	KLOCK_QUEUE_HANDLE lockqueue;
	UNREFERENCED_PARAMETER(pDevice);

	wq = task->pWorkQueue;
	KeAcquireInStackQueuedSpinLock(&wq->Lock, &lockqueue);

	if (!IsListEmpty(&wq->List)) {
		entry = RemoveHeadList(&wq->List);
		KeReleaseInStackQueuedSpinLock(&lockqueue);

		work = CONTAINING_RECORD(entry, WORK_ENTRY, Entry);
		work->WorkHandler(work);

		KeAcquireInStackQueuedSpinLock(&wq->Lock, &lockqueue);
		if (!IsListEmpty(&wq->List)) {
			KeReleaseInStackQueuedSpinLock(&lockqueue);

            #pragma prefast(suppress:28155, "WorkQueueHandler is of correct type ")
			IoQueueWorkItem(task->pWorkItem, WorkQueueHandler, DelayedWorkQueue, task);
			return;
		}
	}

	task->Next = wq->TaskArray[0].Next;
	wq->TaskArray[0].Next = task->Index;
	KeReleaseInStackQueuedSpinLock(&lockqueue);
}

void WorkQueueInsert(WORK_QUEUE *pWorkQueue, WORK_ENTRY *pWork)
{
	WORK_QUEUE_TASK *task;
	KLOCK_QUEUE_HANDLE lockqueue;

	KeAcquireInStackQueuedSpinLock(&pWorkQueue->Lock, &lockqueue);
	InsertHeadList(&pWorkQueue->List, &pWork->Entry);
	task = &pWorkQueue->TaskArray[pWorkQueue->TaskArray[0].Next];
	pWorkQueue->TaskArray[0].Next = task->Next;
	KeReleaseInStackQueuedSpinLock(&lockqueue);

	if (task->Index != 0) {

        #pragma prefast(suppress:28155, "WorkQueueHandler is of correct type ")
		IoQueueWorkItem(task->pWorkItem, WorkQueueHandler, DelayedWorkQueue, task);
	}
}

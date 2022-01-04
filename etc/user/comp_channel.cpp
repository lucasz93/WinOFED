/*
 * Copyright (c) 2008, 2009 Intel Corporation.  All rights reserved.
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

#include <comp_channel.h>
#include <process.h>

static void CompChannelQueue(COMP_CHANNEL *pChannel, COMP_ENTRY *pEntry);


/*
 * Completion manager
 */

static unsigned __stdcall CompThreadPoll(void *Context)
{
	COMP_MANAGER *mgr = (COMP_MANAGER *) Context;
	COMP_ENTRY *entry;
	OVERLAPPED *overlap;
	DWORD bytes;
	ULONG_PTR key;

	SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	while (mgr->Run) {
		GetQueuedCompletionStatus(mgr->CompQueue, &bytes, &key,
								  &overlap, INFINITE);
		entry = CONTAINING_RECORD(overlap, COMP_ENTRY, Overlap);
		if (entry->Channel != NULL) {
			CompChannelQueue(entry->Channel, entry);
		}
	}

	_endthreadex(0);
	return 0;
}

DWORD CompManagerOpen(COMP_MANAGER *pMgr)
{
	DWORD ret;

	pMgr->CompQueue = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, -1);
	if (pMgr->CompQueue == NULL) {
		return GetLastError();
	}

	pMgr->Run = TRUE;
	pMgr->Thread = (HANDLE) _beginthreadex(NULL, 0, CompThreadPoll, pMgr, 0, NULL);
	if (pMgr->Thread == NULL) {
		ret = GetLastError();
		goto err;
	}
	return 0;

err:
	CloseHandle(pMgr->CompQueue);
	return ret;
}

void CompManagerClose(COMP_MANAGER *pMgr)
{
	COMP_CHANNEL *channel;
	COMP_ENTRY entry;

	pMgr->Run = FALSE;
	CompEntryInit(NULL, &entry);
	PostQueuedCompletionStatus(pMgr->CompQueue, 0, (ULONG_PTR) pMgr, &entry.Overlap);
	WaitForSingleObject(pMgr->Thread, INFINITE);
	CloseHandle(pMgr->Thread);

	CloseHandle(pMgr->CompQueue);
}

DWORD CompManagerMonitor(COMP_MANAGER *pMgr, HANDLE hFile, ULONG_PTR Key)
{
	HANDLE cq;

	cq = CreateIoCompletionPort(hFile, pMgr->CompQueue, Key, 0);
	return (cq == NULL) ? GetLastError() : 0;
}


/*
 * Completion channel sets
 */

DWORD CompSetInit(COMP_SET *pSet)
{
	pSet->Head = NULL;
	pSet->TailPtr = &pSet->Head;

	pSet->Event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (pSet->Event == NULL) {
		return GetLastError();
	}

	return 0;
}

void CompSetCleanup(COMP_SET *pSet)
{
	CloseHandle(pSet->Event);
}

void CompSetZero(COMP_SET *pSet)
{
	pSet->Head = NULL;
	pSet->TailPtr = &pSet->Head;
	ResetEvent(pSet->Event);
}

void CompSetAdd(COMP_CHANNEL *pChannel, COMP_SET *pSet)
{
	*pSet->TailPtr = pChannel;
	pSet->TailPtr = &pChannel->Next;

	EnterCriticalSection(&pChannel->Lock);
	pChannel->Set = pSet;
	if (pChannel->Head != NULL) {
		SetEvent(pSet->Event);
	}
	LeaveCriticalSection(&pChannel->Lock);
}

DWORD CompSetPoll(COMP_SET *pSet, DWORD Milliseconds)
{
	DLIST_ENTRY *entry;
	COMP_CHANNEL *channel;
	DWORD ret, cnt = 0;

	*pSet->TailPtr = NULL;
	ret = WaitForSingleObject(pSet->Event, Milliseconds);
	if (ret == WAIT_TIMEOUT) {
		ret = 0;
	}

	for (channel = pSet->Head; channel != NULL; channel = channel->Next) {
		EnterCriticalSection(&channel->Lock);
		channel->Set = NULL;
		cnt += (channel->Head != NULL);
		LeaveCriticalSection(&channel->Lock);
	}

	return cnt ? cnt : ret;
}

void CompSetCancel(COMP_SET *pSet)
{
	SetEvent(pSet->Event);
}


/*
 * Completion channel
 */

DWORD CompChannelInit(COMP_MANAGER *pMgr, COMP_CHANNEL *pChannel, DWORD Milliseconds)
{
	pChannel->Manager = pMgr;
	pChannel->Next = NULL;
	pChannel->Set = NULL;
	pChannel->Head = NULL;
	pChannel->TailPtr = &pChannel->Head;
	pChannel->Milliseconds = Milliseconds;

	pChannel->Event = CreateEvent(NULL, TRUE, TRUE, NULL);
	if (pChannel->Event == NULL) {
		return GetLastError();
	}

	InitializeCriticalSection(&pChannel->Lock);
	CompEntryInit(pChannel, &pChannel->Entry);
	return 0;
}

void CompChannelCleanup(COMP_CHANNEL *pChannel)
{
	CloseHandle(pChannel->Event);
	DeleteCriticalSection(&pChannel->Lock);	
}

static void CompChannelInsertTail(COMP_CHANNEL *pChannel, COMP_ENTRY *pEntry)
{
	*pChannel->TailPtr = pEntry;
	pChannel->TailPtr = &pEntry->Next;
	pEntry->Next = NULL;
}

static COMP_ENTRY *CompChannelRemoveHead(COMP_CHANNEL *pChannel)
{
	COMP_ENTRY *entry;

	entry = pChannel->Head;
	pChannel->Head = entry->Next;
	if (pChannel->Head == NULL) {
		pChannel->TailPtr = &pChannel->Head;
	}
	return entry;
}

static COMP_ENTRY *CompChannelFindRemove(COMP_CHANNEL *pChannel, COMP_ENTRY *pEntry)
{
	COMP_ENTRY **entry_ptr, *entry;

	EnterCriticalSection(&pChannel->Lock);
	entry_ptr = &pChannel->Head;
	while (*entry_ptr && *entry_ptr != pEntry) {
		entry_ptr = &(*entry_ptr)->Next;
	}

	entry = *entry_ptr;
	if (entry != NULL) {
		*entry_ptr = pEntry->Next;
		if (pChannel->TailPtr == &pEntry->Next) {
			pChannel->TailPtr = entry_ptr;
		}
		InterlockedExchange(&pEntry->Busy, 0);
	}
	LeaveCriticalSection(&pChannel->Lock);
	return entry;
}

static void CompChannelQueue(COMP_CHANNEL *pChannel, COMP_ENTRY *pEntry)
{
	pEntry->Next = NULL;
	EnterCriticalSection(&pChannel->Lock);
	CompChannelInsertTail(pChannel, pEntry);
	SetEvent(pChannel->Event);
	if (pChannel->Set != NULL) {
		SetEvent(pChannel->Set->Event);
	}
	LeaveCriticalSection(&pChannel->Lock);
}

DWORD CompChannelPoll(COMP_CHANNEL *pChannel, COMP_ENTRY **ppEntry)
{
	COMP_ENTRY *entry;
	DWORD ret;

	EnterCriticalSection(&pChannel->Lock);
	while (pChannel->Head == NULL) {
		ResetEvent(pChannel->Event);
		LeaveCriticalSection(&pChannel->Lock);

		ret = WaitForSingleObject(pChannel->Event, pChannel->Milliseconds);
		if (ret) {
			return ret;
		}

		EnterCriticalSection(&pChannel->Lock);
	}
	entry = CompChannelRemoveHead(pChannel);
	LeaveCriticalSection(&pChannel->Lock);

	InterlockedExchange(&entry->Busy, 0);
	*ppEntry = entry;
	ret = (entry == &pChannel->Entry) ? ERROR_CANCELLED : 0;

	return ret;
}

void CompChannelCancel(COMP_CHANNEL *pChannel)
{
	if (InterlockedCompareExchange(&pChannel->Entry.Busy, 1, 0) == 0) {
		PostQueuedCompletionStatus(pChannel->Manager->CompQueue, 0,
								   (ULONG_PTR) pChannel, &pChannel->Entry.Overlap);
	}
}


/*
 * Completion entry
 */

void CompEntryInit(COMP_CHANNEL *pChannel, COMP_ENTRY *pEntry)
{
	RtlZeroMemory(pEntry, sizeof *pEntry);
	pEntry->Channel = pChannel;
}

DWORD CompEntryPost(COMP_ENTRY *pEntry)
{
	if (InterlockedCompareExchange(&pEntry->Busy, 1, 0) == 0) {
		if (!PostQueuedCompletionStatus(pEntry->Channel->Manager->CompQueue,
										0, 0, &pEntry->Overlap)) {
			InterlockedExchange(&pEntry->Busy, 0);
			return GetLastError();
		}
	}
	return 0;
}

COMP_ENTRY *CompEntryCancel(COMP_ENTRY *pEntry)
{
	COMP_ENTRY *entry = NULL;

	while (pEntry->Busy) {
		Sleep(0);
		entry = CompChannelFindRemove(pEntry->Channel, pEntry);
	}
	return entry;
}

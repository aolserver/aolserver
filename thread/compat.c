/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is AOLserver Code and related documentation
 * distributed by AOL.
 * 
 * The Initial Developer of the Original Code is America Online,
 * Inc. Portions created by AOL are Copyright (C) 1999 America Online,
 * Inc. All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above.  If you wish
 * to allow use of your version of this file only under the terms of the
 * GPL and not to allow others to use your version of this file under the
 * License, indicate your decision by deleting the provisions above and
 * replace them with the notice and other provisions required by the GPL.
 * If you do not delete the provisions above, a recipient may use your
 * version of this file under either the License or the GPL.
 */


/* 
 * compat.c --
 *
 *	AOLserver 2.x nsthread compatibility routines.  Basically, the new
 *	routines abort the server instead of returning an error result code.
 *	In addition, the relative time based Ns_Event functions have been
 *	replaced with proper, absolute time based Ns_Cond objects.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/compat.c,v 1.2 2000/05/02 14:39:33 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

int
Ns_InitializeMutex(Ns_Mutex *mutexPtr)
{
    Ns_MutexInit(mutexPtr);

    return NS_OK;
}

int
Ns_DestroyMutex(Ns_Mutex *mutexPtr)
{
    Ns_MutexDestroy(mutexPtr);

    return NS_OK;
}

int
Ns_LockMutex(Ns_Mutex *mutexPtr)
{
    Ns_MutexLock(mutexPtr);

    return NS_OK;
}

int
Ns_UnlockMutex(Ns_Mutex *mutexPtr)
{
    Ns_MutexUnlock(mutexPtr);

    return NS_OK;
}


int
Ns_InitializeCriticalSection(Ns_CriticalSection *cs)
{
    Ns_CsInit((Ns_Cs *) cs);

    return NS_OK;
}

int
Ns_DestroyCriticalSection(Ns_CriticalSection *cs)
{
    Ns_CsDestroy((Ns_Cs *) cs);

    return NS_OK;
}

int
Ns_EnterCriticalSection(Ns_CriticalSection *cs)
{
    Ns_CsEnter((Ns_Cs *) cs);

    return NS_OK;
}

int
Ns_LeaveCriticalSection(Ns_CriticalSection *cs)
{
    Ns_CsLeave((Ns_Cs *) cs);

    return NS_OK;
}


int
Ns_InitializeEvent(Ns_Event *event)
{
    Ns_CondInit((Ns_Cond *) event);

    return NS_OK;
}

int
Ns_DestroyEvent(Ns_Event *event)
{
    Ns_CondDestroy((Ns_Cond *) event);

    return NS_OK;
}

int
Ns_SetEvent(Ns_Event *event)
{
    Ns_CondSignal((Ns_Cond *) event);

    return NS_OK;
}

int
Ns_BroadcastEvent(Ns_Event *event)
{
    Ns_CondBroadcast((Ns_Cond *) event);

    return NS_OK;
}

int
Ns_WaitForEvent(Ns_Event *event, Ns_Mutex *lock)
{
    return Ns_CondTimedWait((Ns_Cond *) event, lock, NULL);
}

int
Ns_TimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock, int timeout)
{
    return Ns_UTimedWaitForEvent(event, lock, timeout, 0);
}

int
Ns_AbsTimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock, time_t abstime)
{
    Ns_Time wait;

    wait.sec = abstime;
    wait.usec = 0;

    return Ns_CondTimedWait((Ns_Cond *) event, lock, &wait);
}

int
Ns_UTimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock, 
		      int seconds, int microseconds)
{
    Ns_Time to, *timePtr;

    if (seconds <= 0 && microseconds <= 0) {
	timePtr = NULL;
    } else {
    	Ns_GetTime(&to);
    	Ns_IncrTime(&to, seconds, microseconds);
	timePtr = &to;
    }

    return Ns_CondTimedWait((Ns_Cond *) event, lock, timePtr);
}

int
Ns_InitializeRWLock(Ns_RWLock *lock)
{
    Ns_RWLockInit(lock);

    return NS_OK;
}

int 
Ns_DestroyRWLock(Ns_RWLock *lock)
{
    Ns_RWLockDestroy(lock);

    return NS_OK;
}

int 
Ns_ReadLockRWLock(Ns_RWLock *lock)
{
    Ns_RWLockRdLock(lock);

    return NS_OK;
}


int 
Ns_ReadUnlockRWLock(Ns_RWLock *lock)
{
    Ns_RWLockUnlock(lock);

    return NS_OK;
}

int 
Ns_WriteLockRWLock(Ns_RWLock *lock)
{
    Ns_RWLockWrLock(lock);

    return NS_OK;
}

int 
Ns_WriteUnlockRWLock(Ns_RWLock *lock)
{
    Ns_RWLockUnlock(lock);

    return NS_OK;
}


int
Ns_InitializeSemaphore(Ns_Semaphore *sema, int initCount)
{
    Ns_SemaInit((Ns_Sema *) sema, initCount);

    return NS_OK;
}

int
Ns_DestroySemaphore(Ns_Semaphore *sema)
{
    Ns_SemaDestroy((Ns_Sema *) sema);

    return NS_OK;
}

int
Ns_WaitForSemaphore(Ns_Semaphore *sema)
{
    Ns_SemaWait((Ns_Sema *) sema);

    return NS_OK;
}

int
Ns_ReleaseSemaphore(Ns_Semaphore *sema, int count)
{
    Ns_SemaPost((Ns_Sema *) sema, count);

    return NS_OK;
}


int
Ns_AllocThreadLocalStorage(Ns_ThreadLocalStorage *tls, Ns_TlsCleanup *cleanup)
{
    Ns_TlsAlloc((Ns_Tls *) tls, cleanup);

    return NS_OK;
}

int
Ns_SetThreadLocalStorage(Ns_ThreadLocalStorage *tls, void *p)
{
    Ns_TlsSet((Ns_Tls *) tls, p);

    return NS_OK;
}

int
Ns_GetThreadLocalStorage(Ns_ThreadLocalStorage *tls, void **p)
{
    *p = Ns_TlsGet((Ns_Tls *) tls);

    return NS_OK;
}


int
Ns_WaitForThread(Ns_Thread *thrPtr)
{
    Ns_ThreadJoin(thrPtr, NULL);

    return NS_OK;
}


int
Ns_WaitThread(Ns_Thread *thrPtr, int *exitCodePtr)
{
    void *arg;

    Ns_ThreadJoin(thrPtr, &arg);
    if (exitCodePtr != NULL) {
	*exitCodePtr = (int) arg;
    }
    return NS_OK;
}


void
Ns_ExitThread(int exitCode)
{
    Ns_ThreadExit((void *) exitCode);
}

int
Ns_BeginDetachedThread(Ns_ThreadProc *proc, void *arg)
{
    Ns_ThreadCreate(proc, arg, 0, NULL);

    return NS_OK;
}

int
Ns_BeginThread(Ns_ThreadProc *proc, void *arg, Ns_Thread *thrPtr)
{
    Ns_Thread thr;

    Ns_ThreadCreate(proc, arg, 0, thrPtr ? thrPtr : &thr);

    return NS_OK;
}

int
Ns_GetThreadId(void)
{
    return Ns_ThreadId();
}

void
Ns_GetThread(Ns_Thread *threadPtr)
{
    Ns_ThreadSelf(threadPtr);
}

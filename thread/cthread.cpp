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
 * cthread.c --
 *
 *	Interface routines for nsthreads using Mach cthreads
 */


#include <mach/cthreads.h>
#include "thread.h"


/*
 *----------------------------------------------------------------------
 *
 * NsThreadLibName --
 *
 *	Return the string name of the thread library.
 *
 * Results:
 *	Pointer to static string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
NsThreadLibName(void)
{
    return "cthread";
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexInit --
 *
 *	Cthread mutex initialization
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexInit(void **lockPtr)
{
    mutex_t         mPtr;

    mPtr = ns_malloc(sizeof(struct mutex));
    if (mPtr == NULL) {
    	NsThreadFatal("Ns_MutexInit", "mutex_alloc", 0);
    } else {
	mutex_init(mPtr);
    }
    *lockPtr = (void *) mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexDestroy --
 *
 *	Cthread mutex destroy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexDestroy(void **lockPtr)
{
    mutex_clear((mutex_t) *lockPtr);
    ns_free((void *)*lockPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexLock --
 *
 *	Cthread mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may be suspended by the cthread library if the lock is
 *	already held.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexLock(void **lockPtr)
{
    mutex_lock((mutex_t) *lockPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexTryLock --
 *
 *	Cthread mutex non-blocking lock.
 *
 * Results:
 *	NS_OK if locked, NS_TIMEOUT if lock already held.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
NsMutexTryLock(void **lockPtr)
{
    if (mutex_try_lock((mutex_t) *lockPtr)) {
	return NS_OK;
    } else {
	return NS_TIMEOUT;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexUnlock --
 *
 *	Cthread mutex unlock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Other waiting thread, if any, is resumed.
 *
 *----------------------------------------------------------------------
 */
void
NsMutexUnlock(void **lockPtr)
{
    mutex_unlock((mutex_t) *lockPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	cthread condition variable initialization.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondInit(Ns_Cond *condPtr)
{
    condition_t     cPtr;

    cPtr = ns_malloc(sizeof(struct condition));
    if (cPtr == NULL) {
	NsThreadFatal("Ns_CondInit", "condition_alloc", 0);
    } else {
	condition_init(cPtr);
    }
    *condPtr = (Ns_Cond) cPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	cthread condition destroy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondDestroy(Ns_Cond *condPtr)
{
    if (*condPtr != NULL) {
    	int             err;

    	condition_clear((condition_t) *condPtr);
	ns_free((void *)*condPtr);
        *condPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *	cthread condition signal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A single waiting thread, if any, is resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *condPtr)
{
    condition_t cPtr = GETCOND(condPtr);

    condition_signal(cPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Pthread condition broadcast.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All waiting threads, if any, are resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *condPtr)
{
    condition_t cPtr = GETCOND(condPtr);

    condition_broadcast(cPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *	cthread indefinite condition wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread will be suspended.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr)
{
    condition_t cPtr = GETCOND(condPtr);
    Mutex *mPtr = GETMUTEX(mutexPtr);
    Thread *oPtr;

    oPtr = mPtr->ownerPtr;
    mPtr->ownerPtr = NULL;
    condition_wait(cPtr, (mutex_t) mPtr->lock);
    mPtr->ownerPtr = oPtr;
    ++mPtr->nlock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *	cthread absolute time wait.  Note that the condition_wait_timeout
 *	call isn't strictly documented in Mac OS X Server, but it seems to
 *	work, and emulating it would be a real pain. The condition_wait_timeout
 *	call requires an relative time in milliseconds, while we're
 *	passed an absolute time in microseconds, so we have to convert.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread will be suspended until a signal or timeout occurs.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr, Ns_Time *timePtr)
{
    unsigned int    millisecs;
    condition_t     cond;
    int             wait_result;
    Ns_Time         now;
    Ns_Time         timeout_interval;
    Thread         *oPtr;
    Mutex          *mPtr;

    if (timePtr == NULL) {
	Ns_CondWait(condPtr, mutexPtr);
    } else {
	cond = GETCOND(condPtr);
	Ns_GetTime(&now);
	if (timePtr->sec > now.sec) {
	    Ns_DiffTime(timePtr, &now, &timeout_interval);
	    millisecs = timeout_interval.sec * 1000;
	    millisecs += timeout_interval.usec / 1000;
	} else if (timePtr->sec == now.sec) {
	    if (timePtr->usec > now.usec) {
		millisecs = (timePtr->usec - now.sec) / 1000;
	    } else {
		millisecs = 0;
	    }
	} else {
	    millisecs = 0;
	}
	if (millisecs > 0) {
    	    mPtr = GETMUTEX(mutexPtr);
    	    oPtr = mPtr->ownerPtr;
    	    mPtr->ownerPtr = NULL;
	    wait_result = condition_wait_timeout(cond, (mutex_t) mPtr->lock,
			millisecs);
    	    mPtr->ownerPtr = oPtr;
    	    ++mPtr->nlock;
	    if (wait_result == 0) {
		return NS_TIMEOUT;
	    }
	}
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsSetThread --
 *
 *	cthread routine to set this thread's nsthread data structure.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

void
NsSetThread(Thread *thisPtr)
{
    cthread_t tid;

    tid = cthread_self();
    thisPtr->tid = (int) tid;
    cthread_set_data(cthread_self(), (any_t) thisPtr);
    NsSetThread2(thisPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread --
 *
 *	cthread routine to return this thread's nsthread data structure.
 *
 * Results:
 *	Pointer to per-thread data structure.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Thread      *
NsGetThread(void)
{
    Thread *thisPtr;

    thisPtr = (Thread *) cthread_data(cthread_self());
    return (thisPtr ? thisPtr : NsGetThread2());
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadCreate --
 *
 *	cthread thread creation.  All nsthreads are created as detached
 *	cthreads as the nsthread library handles joining itself in an
 *	interface independent manner.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread is created by the cthread library.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    cthread_t thr;
    int     err;

    thr = cthread_fork((cthread_fn_t) NsThreadMain, (any_t)thrPtr);
    cthread_detach(thr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *	cthread thread exit routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	cthread is destroyed.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadExit(void)
{
    cthread_exit(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *	Yield the cpu to another thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thead may be suspended by operating system.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadYield(void)
{
    cthread_yield();
}


/*
 *----------------------------------------------------------------------
 *
 * ns_sigmask --
 *
 *	Set the thread's signal mask.  Mach doesn't implement signal
 *	masks this way, so we just use sigsetprocmask and hope for
 *	the best.
 *
 * Results:
 *	0 on success, otherwise an error code.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */
int
ns_sigmask(int how, sigset_t * set, sigset_t * oset)
{
    return sigprocmask(how, set, oset);
}


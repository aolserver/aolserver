/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
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
 * mutex.c --
 *
 *	Mutex routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/mutex.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

static Mutex list;
int nsThreadMutexMeter = 0;


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexInit --
 *
 *	Mutex initialization.  Note this routines isn't normally
 *	called directly as mutexes are now self-initializing when first
 *	locked.
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
Ns_MutexInit2(Ns_Mutex *mutexPtr, char *prefix)
{
    Mutex	   *mPtr;
    int		    next;
    char	    buf[10];

    mPtr = ns_calloc(1, sizeof(Mutex));
    NsMutexInit(&mPtr->lock);
    if (list.lock == NULL) {
	NsMutexInit(&list.lock);
    }
    NsMutexLock(&list.lock);
    mPtr->nextPtr = list.nextPtr;
    list.nextPtr = mPtr;
    next = mPtr->id = list.id++;
    NsMutexUnlock(&list.lock);
    *mutexPtr = (Ns_Mutex) mPtr;
    sprintf(buf, "%d", next);
    Ns_MutexSetName2(mutexPtr, prefix ? prefix : "m", buf);
}

void
Ns_MutexInit(Ns_Mutex *mutexPtr)
{
    Ns_MutexInit2(mutexPtr, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexSetName --
 *
 *	Update the string name of a mutex.
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
Ns_MutexSetName2(Ns_Mutex *mutexPtr, char *prefix, char *name)
{
    Mutex *mPtr = GETMUTEX(mutexPtr);
    int plen, nlen;
    char *p;

    plen = strlen(prefix);
    if (plen > NS_THREAD_NAMESIZE) {
	plen = NS_THREAD_NAMESIZE;
	nlen = 0;
    } else {
    	nlen = name ? strlen(name) : 0;
	if ((nlen + plen + 1) > NS_THREAD_NAMESIZE) {
	    nlen = NS_THREAD_NAMESIZE - plen - 1;
	}
    }
    NsMutexLock(&list.lock);
    p = strncpy(mPtr->name, prefix, plen) + plen;
    if (nlen > 0) {
	*p++ = ':';
	p = strncpy(p, name, nlen) + nlen;
    }
    *p = '\0';
    NsMutexUnlock(&list.lock);
}


void
Ns_MutexSetName(Ns_Mutex *mutexPtr, char *name)
{
    Ns_MutexSetName2(mutexPtr, name, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexDestroy --
 *
 *	Mutex destroy.  Note this routine is almost never used
 *	as mutexes normally exists in memory until the process exits.
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
Ns_MutexDestroy(Ns_Mutex *mutexPtr)
{
    Mutex	*mPtr = (Mutex *) *mutexPtr;
    Mutex      **mPtrPtr;

    if (mPtr != NULL) {
	NsMutexDestroy(&mPtr->lock);
	NsMutexLock(&list.lock);
	mPtrPtr = &list.nextPtr; 
	while ((*mPtrPtr) != mPtr) {
	    mPtrPtr = &(*mPtrPtr)->nextPtr;
        }
	*mPtrPtr = mPtr->nextPtr;
	NsMutexUnlock(&list.lock);
    	ns_free(mPtr);
    	*mutexPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexLock --
 *
 *	Lock a mutex, tracking the # of locks and the # of locks which
 *	aren't aquired immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may be suspended if the lock is already held.
 *
 *----------------------------------------------------------------------
 */

void
Ns_MutexLock(Ns_Mutex *mutexPtr)
{
    Mutex *mPtr = GETMUTEX(mutexPtr);
    Thread *thisPtr;

    if (!nsThreadMutexMeter) {
	NsMutexLock(&mPtr->lock);
    } else {
	thisPtr = NsGetThread();
    	if (NsMutexTryLock(&mPtr->lock) != NS_OK) {
	    NsMutexLock(&mPtr->lock);
	    ++mPtr->nbusy;
    	}
    	mPtr->ownerPtr = thisPtr;
    }
    ++mPtr->nlock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexTryLock --
 *
 *	Attempt to lock a mutex.
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
Ns_MutexTryLock(Ns_Mutex *mutexPtr)
{
    Mutex *mPtr = GETMUTEX(mutexPtr);
    Thread *thisPtr;
    int status;

    if (!nsThreadMutexMeter) {
	status = NsMutexTryLock(&mPtr->lock);
    } else {
    	thisPtr = NsGetThread();
    	status = NsMutexTryLock(&mPtr->lock);
	if (status == NS_OK) {
	    mPtr->ownerPtr = thisPtr;
	}
    }
    if (status == NS_OK) {
	++mPtr->nlock;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexUnlock --
 *
 *	Unlock a mutex.
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
Ns_MutexUnlock(Ns_Mutex *mutexPtr)
{
    Mutex	*mPtr = (Mutex *) *mutexPtr;

    mPtr->ownerPtr = NULL;
    NsMutexUnlock(&mPtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexEnum --
 *
 *	Invoke given callback on all current mutexes. 
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
Ns_MutexEnum(Ns_MutexInfoProc *proc, void *arg)
{
    Mutex *mPtr;
    Ns_MutexInfo info;

    NsMutexLock(&list.lock);
    mPtr = list.nextPtr;
    while (mPtr != NULL) {
	info.mutexPtr = (Ns_Mutex *) mPtr;
	info.id = mPtr->id;
	info.name = mPtr->name;
	/* NB: Potential dirty reads. */
	info.nlock = mPtr->nlock;
	info.nbusy = mPtr->nbusy;
	info.owner = mPtr->ownerPtr ? mPtr->ownerPtr->name : NULL;
	(*proc)(&info, arg);
	mPtr = mPtr->nextPtr;
    }
    NsMutexUnlock(&list.lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetMutex --
 *
 *	Cast an Ns_Mutex to a Mutex, initializing it if needed.
 *
 * Results:
 *	Void pointer to mutex object.
 *
 * Side effects:
 *	Mutex initialized the first time.
 *
 *----------------------------------------------------------------------
 */

Mutex *
NsGetMutex(Ns_Mutex *mutexPtr)
{
    Ns_MasterLock();
    if (*mutexPtr == NULL) {
	Ns_MutexInit(mutexPtr);
    }
    Ns_MasterUnlock();
    return (Mutex *) *mutexPtr;
}

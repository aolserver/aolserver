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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/mutex.c,v 1.5 2000/11/09 00:33:54 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

static Mutex *firstMutexPtr;
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
Ns_MutexInit(Ns_Mutex *mutexPtr)
{
    Ns_MutexInit2(mutexPtr, NULL);
}

void
Ns_MutexInit2(Ns_Mutex *mutexPtr, char *prefix)
{
    Mutex	   *mPtr;
    int		    next;
    char	    buf[10];
    static int	    nextid;

    mPtr = NsAlloc(sizeof(Mutex));
    mPtr->lock = NsLockAlloc();
    Ns_MasterLock();
    mPtr->nextPtr = firstMutexPtr;
    firstMutexPtr = mPtr;
    next = mPtr->id = nextid++;
    *mutexPtr = (Ns_Mutex) mPtr;
    sprintf(buf, "%d", next);
    Ns_MutexSetName2(mutexPtr, prefix ? prefix : "m", buf);
    Ns_MasterUnlock();
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
Ns_MutexSetName(Ns_Mutex *mutexPtr, char *name)
{
    Ns_MutexSetName2(mutexPtr, name, NULL);
}

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
    Ns_MasterLock();
    p = strncpy(mPtr->name, prefix, plen) + plen;
    if (nlen > 0) {
	*p++ = ':';
	p = strncpy(p, name, nlen) + nlen;
    }
    *p = '\0';
    Ns_MasterUnlock();
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
	NsLockFree(mPtr->lock);
	Ns_MasterLock();
	mPtrPtr = &firstMutexPtr;
	while ((*mPtrPtr) != mPtr) {
	    mPtrPtr = &(*mPtrPtr)->nextPtr;
        }
	*mPtrPtr = mPtr->nextPtr;
	Ns_MasterUnlock();
    	NsFree(mPtr);
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
	NsLockSet(mPtr->lock);
    } else {
	thisPtr = NsGetThread();
    	if (!NsLockTry(mPtr->lock)) {
	    NsLockSet(mPtr->lock);
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
    int locked;

    if (!nsThreadMutexMeter) {
	locked = NsLockTry(mPtr->lock);
    } else {
    	thisPtr = NsGetThread();
    	locked = NsLockTry(mPtr->lock);
	if (locked) {
	    mPtr->ownerPtr = thisPtr;
	}
    }
    if (locked) {
	++mPtr->nlock;
	return NS_OK;
    }
    return NS_TIMEOUT;
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
    NsLockUnset(mPtr->lock);
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

    Ns_MasterLock();
    mPtr = firstMutexPtr;
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
    Ns_MasterUnlock();
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

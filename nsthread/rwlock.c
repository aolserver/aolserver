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
 * rwlock.c --
 *
 *	Routines for read/write locks.  Read/write locks differ from a mutex
 *	in that multiple threads can aquire the read lock until a single
 *	thread aquires a write lock.  This code is adapted from that in 
 *	Steven's Unix Network Programming, Volume 3. 
 *
 *	Note:  Read/write locks are not often a good idea.  The reason
 *	is, like critical sections, the number of actual lock operations
 *	is doubled which makes them more expensive to use.  Cases where the
 *	overhead are justified are then often subject to read locks being 
 *	held longer than writer threads can wait and/or writer threads holding
 *	the lock so long that many reader threads back up.  In these cases,
 *	specific reference counting techniques (e.g., the management of
 *	the Req structures in op.c) normally work better.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/rwlock.c,v 1.2 2002/06/12 11:30:44 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following structure defines a read/write lock including a mutex
 * to protect access to the structure and condition variables for waiting
 * reader and writer threads.
 */

typedef struct RwLock {
    Ns_Mutex  mutex;    /* Mutex guarding lock structure. */
    Ns_Cond   rcond;    /* Condition variable for waiting readers. */
    Ns_Cond   wcond;    /* condition variable for waiting writers. */
    int       nreaders; /* Number of readers waiting for lock. */
    int       nwriters; /* Number of writers waiting for lock. */
    int       lockcnt;  /* Lock count, > 0 indicates # of shared
			 * readers, -1 indicates exclusive writer. */
} RwLock;

static RwLock *GetRwLock(Ns_RWLock *rwPtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockInit --
 *
 *	Initialize a read/write lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Lock memory is allocated from the heap and initialized.
 *
 *----------------------------------------------------------------------
 */
	
void
Ns_RWLockInit(Ns_RWLock *rwPtr)
{
    RwLock *lockPtr;
    static unsigned int nextid = 0;
    
    lockPtr = ns_calloc(1, sizeof(RwLock));
    NsMutexInitNext(&lockPtr->mutex, "rw", &nextid);
    Ns_CondInit(&lockPtr->rcond);
    Ns_CondInit(&lockPtr->wcond);
    lockPtr->nreaders = 0;
    lockPtr->nwriters = 0;
    lockPtr->lockcnt = 0;
    *rwPtr = (Ns_RWLock) lockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockDestroy --
 *
 *	Destory a read/write lock if it was previously initialized.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Read/write lock objects are destroy and the lock memory is
 *	returned to the heap.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockDestroy(Ns_RWLock *rwPtr)
{
    RwLock *lockPtr = (RwLock *) *rwPtr;

    if (lockPtr != NULL) {
    	Ns_MutexDestroy(&lockPtr->mutex);
    	Ns_CondDestroy(&lockPtr->rcond);
    	Ns_CondDestroy(&lockPtr->wcond);
    	ns_free(lockPtr);
    	*rwPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockRdLock --
 *
 *	Aquire a read lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may wait on a condition variable if the read/write lock
 *	currently has a write lock.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockRdLock(Ns_RWLock *rwPtr)
{
    RwLock *lockPtr = GetRwLock(rwPtr);

    Ns_MutexLock(&lockPtr->mutex);

    /*
     * Wait on the read condition while the lock is write-locked or
     * some other thread is waiting for a write lock.
     */

    while (lockPtr->lockcnt < 0 || lockPtr->nwriters > 0) {
	lockPtr->nreaders++;
	Ns_CondWait(&lockPtr->rcond, &lockPtr->mutex);
	lockPtr->nreaders--;
    }

    lockPtr->lockcnt++;
    Ns_MutexUnlock(&lockPtr->mutex);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockWrLock --
 *
 *	Aquire a write lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may wait on the write condition if other threads either
 *	have the lock read or write locked.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockWrLock(Ns_RWLock *rwPtr)
{
    RwLock *lockPtr = GetRwLock(rwPtr);

    Ns_MutexLock(&lockPtr->mutex);
    while (lockPtr->lockcnt != 0) {
	lockPtr->nwriters++;
	Ns_CondWait(&lockPtr->wcond, &lockPtr->mutex);
	lockPtr->nwriters--;
    }
    lockPtr->lockcnt = -1;
    Ns_MutexUnlock(&lockPtr->mutex);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockUnlock --
 *
 *	Unlock a read/write lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Read or write condition may be signaled.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockUnlock(Ns_RWLock *rwPtr)
{
    RwLock *lockPtr = (RwLock *) *rwPtr;

    Ns_MutexLock(&lockPtr->mutex);
    if (--lockPtr->lockcnt < 0) {
	lockPtr->lockcnt = 0;
    }
    if (lockPtr->nwriters) {
	Ns_CondSignal(&lockPtr->wcond);
    } else if (lockPtr->nreaders) {
	Ns_CondBroadcast(&lockPtr->rcond);
    }
    Ns_MutexUnlock (&lockPtr->mutex);
}


/*
 *----------------------------------------------------------------------
 *
 * GetRwLock --
 *
 *	Return the read/write lock structure, initializing it if needed.
 *
 * Results:
 *	Pointer to lock.
 *
 * Side effects:
 *	Lock may be initialized.
 *
 *----------------------------------------------------------------------
 */

static RwLock *
GetRwLock(Ns_RWLock *rwPtr)
{
    if (*rwPtr == NULL) {
	Ns_MasterLock();
	if (*rwPtr == NULL) {
	    Ns_RWLockInit(rwPtr);
	}
	Ns_MasterUnlock();
    }
    return (RwLock *) *rwPtr;
}

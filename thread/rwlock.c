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
 *	Note:  Read/write locks are almost always a bad idea.  The reason
 *	is that, like critical sections, the number of actual lock operations
 *	is doubled for read/write locks and there are very few cases in
 *	practice where the extra overhead is justified.  For example, if you
 *	think it's a good idea to have a read/write lock around some large
 *	in-memory data structure you're probably wrong:  The overhead of
 *	the read/write lock operations will likely drawf the time spent
 *	grunging around in the data structure.  On a multi-processor machine
 *	this can actually cause a severe performance degradation, locking
 *	up the process under load. As a rule, if you think you need a
 *	read/write lock you probably need to rethink your problem.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/rwlock.c,v 1.4 2000/11/06 17:53:50 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following structure defines a read/write lock including a mutex
 * to protect access to the structure and condition variables for threads
 * waiting for read and threads waiting for write. 
 */

typedef struct Lock {
    Ns_Mutex  mutex;       /* Mutex guarding lock structure. */
    Ns_Cond   rdCond;      /* Condition variable for waiting readers. */
    Ns_Cond   wrCond;      /* condition variable for waiting writers. */
    int       nReaders;	   /* Number of readers waiting for lock. */
    int       nWriters;    /* Number of writers waiting for lock. */
    int       lockCount;   /* Lock ref count, > 0 indicates # of shared
			    * readers, -1 indicates exclusive writer. */
} Lock;


/*
 *----------------------------------------------------------------------
 *
 * GetLock --
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

static Lock *
GetLock(Ns_RWLock *lockPtr)
{
    if (*lockPtr == NULL) {
	Ns_MasterLock();
	if (*lockPtr == NULL) {
	    Ns_RWLockInit(lockPtr);
	}
	Ns_MasterUnlock();
    }

    return (Lock *) *lockPtr;
}


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
Ns_RWLockInit(Ns_RWLock *lockPtr)
{
    Lock *lPtr;
    
    lPtr = NsAlloc(sizeof(Lock));
    Ns_MutexInit2(&lPtr->mutex, "nsthread:rwlock");
    Ns_CondInit(&lPtr->rdCond);
    Ns_CondInit(&lPtr->wrCond);
    lPtr->nReaders = 0;
    lPtr->nWriters = 0;
    lPtr->lockCount = 0;
    *lockPtr = (Ns_RWLock) lPtr;
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
Ns_RWLockDestroy(Ns_RWLock *lockPtr)
{
    if (*lockPtr != NULL) {
    	Lock *lPtr = (Lock *) *lockPtr;

    	Ns_MutexDestroy(&lPtr->mutex);
    	Ns_CondDestroy(&lPtr->rdCond);
    	Ns_CondDestroy(&lPtr->wrCond);
    	NsFree(lPtr);
    	*lockPtr = NULL;
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
Ns_RWLockRdLock(Ns_RWLock *lockPtr)
{
    Lock *lPtr = GetLock(lockPtr);

    Ns_MutexLock(&lPtr->mutex);

    /*
     * Wait on the read condition while the lock is write-locked or
     * some other thread is waiting for a write lock.
     */

    while (lPtr->lockCount < 0 || lPtr->nWriters > 0) {
	lPtr->nReaders++;
	Ns_CondWait(&lPtr->rdCond, &lPtr->mutex);
	lPtr->nReaders--;
    }

    lPtr->lockCount++;
    Ns_MutexUnlock(&lPtr->mutex);
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
Ns_RWLockWrLock(Ns_RWLock *lockPtr)
{
    Lock *lPtr = GetLock(lockPtr);

    Ns_MutexLock(&lPtr->mutex);
    while (lPtr->lockCount != 0) {
	lPtr->nWriters++;
	Ns_CondWait(&lPtr->wrCond, &lPtr->mutex);
	lPtr->nWriters--;
    }
    lPtr->lockCount = -1;
    Ns_MutexUnlock(&lPtr->mutex);
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
Ns_RWLockUnlock(Ns_RWLock *lockPtr)
{
    Lock *lPtr = (Lock *) *lockPtr;

    Ns_MutexLock(&lPtr->mutex);
    if (--lPtr->lockCount < 0) {
	lPtr->lockCount = 0;
    }
    if (lPtr->nWriters) {
	Ns_CondSignal(&lPtr->wrCond);
    } else if (lPtr->nReaders) {
	Ns_CondBroadcast(&lPtr->rdCond);
    }
    Ns_MutexUnlock (&lPtr->mutex);
}

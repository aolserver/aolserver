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
 * lock.c --
 *
 *	Mutex, critical section, and reader/writer lock code.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/locks.c,v 1.1 2001/11/05 20:26:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a lock which can
 * be an rwlock, critical section (recursive lock),
 * or ordinary mutex.  All locks include a string
 * name and lock and busy counters.
 */

typedef struct Lock {
    /* NB: Must be first for Ns_Cond code. */
    union {
    	pthread_mutex_t  mutex;
	pthread_rwlock_t rwlock;
    } l;
    struct Lock    *nextPtr;
    int		     id;
    unsigned long    nlock;
    unsigned long    nbusy;
    char	     name[NS_THREAD_NAMESIZE+1];
} Lock;

#define GETLOCK(m)  	(*(m)?((Lock *)*(m)):GetLock((m)))
static Lock *GetLock(Ns_Mutex *mutexPtr);
static Lock *LockAlloc(char *prefix);
static void LockFree(Lock *lockPtr);

static Lock *firstLockPtr;


/*
 *----------------------------------------------------------------------
 
 * Ns_MutexInit --
 *
 *	Mutex initialization, often called the first time a mutex
 *	is locked.
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
    Lock *lockPtr;
    int err;
    
    lockPtr = LockAlloc("mu");
    err = pthread_mutex_init(&lockPtr->l.mutex, NULL);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexInit", "pthread_mutex_init", err);
    }
    *mutexPtr = (Ns_Mutex) lockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexSetName, Ns_MutexSetName2 --
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
    Lock *lockPtr = GETLOCK(mutexPtr);
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
    p = strncpy(lockPtr->name, prefix, plen) + plen;
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
 *	Mutex destroy.  Note this routine is not used very often
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
    Lock	*lockPtr = (Lock *) *mutexPtr;
    int     	 err;

    if (lockPtr != NULL) {
	err = pthread_mutex_destroy(&lockPtr->l.mutex);
	if (err != 0) {
	    NsThreadFatal("Ns_MutexDestroy", "ptread_mutex_destroy", err);
	}
	LockFree(lockPtr);
	*mutexPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexLock --
 *
 *	Lock a mutex, tracking the number of locks and the number of
 *	which were not aquired immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may be suspended if the lock is held.
 *
 *----------------------------------------------------------------------
 */

void
Ns_MutexLock(Ns_Mutex *mutexPtr)
{
    Lock *lockPtr = GETLOCK(mutexPtr);
    int err;
    
    err = pthread_mutex_trylock(&lockPtr->l.mutex);
    if (err == EBUSY) {
    	err = pthread_mutex_lock(&lockPtr->l.mutex);
	if (err != 0) {
	    NsThreadFatal("Ns_MutexLock", "ptread_mutex_lock", err);
	}
	++lockPtr->nbusy;
    } else if (err != 0) {
	NsThreadFatal("Ns_MutexLock", "ptread_mutex_trylock", err);
    }
    ++lockPtr->nlock;
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
    Lock *lockPtr = GETLOCK(mutexPtr);
    int err;

    err = pthread_mutex_trylock(&lockPtr->l.mutex);
    if (err == EBUSY) {
    	return NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_MutexLock", "ptread_mutex_trylock", err);
    }
    ++lockPtr->nlock;
    return NS_OK;
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
    Lock *lockPtr = (Lock *) *mutexPtr;
    int     	 err;
    
    err = pthread_mutex_unlock(&lockPtr->l.mutex);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexUnlock", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsInit --
 *
 *	Critical section initialization, often called the first time
 *  	a critical section is entered.  A critical section is simply
 *  	a recursive mutex useful like the master lock.
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
Ns_CsInit(Ns_Cs *csPtr)
{
    Lock *lockPtr;
    pthread_mutexattr_t attr;
    int err;
    
    lockPtr = LockAlloc("cs");
    err = pthread_mutexattr_init(&attr);
    if (err != 0) {
	NsThreadFatal("Ns_CsInit", "pthread_mutexattr_init", err);
    }
    err = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (err != 0) {
	NsThreadFatal("Ns_CsInit", "pthread_mutexattr_settype", err);
    }
    err = pthread_mutex_init(&lockPtr->l.mutex, &attr);
    if (err != 0) {
	NsThreadFatal("Ns_CsInit", "pthread_mutex_init", err);
    }
    err = pthread_mutexattr_destroy(&attr);
    if (err != 0) {
	NsThreadFatal("Ns_CsInit", "pthread_mutexattr_destroy", err);
    }
    *csPtr = (Ns_Cs) lockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsDestroy --
 *
 *	Destroy a critical section, rarely used as critical sections
 *  	normally last for the lifetime of the process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsDestroy(Ns_Cs *csPtr)
{
    Lock	*lockPtr = (Lock *) *csPtr;
    int     	 err;

    if (lockPtr != NULL) {
	err = pthread_mutex_destroy(&lockPtr->l.mutex);
	if (err != 0) {
	    NsThreadFatal("Ns_CsDestroy", "ptread_mutex_destroy", err);
	}
	LockFree(lockPtr);
	*csPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsEnter --
 *
 *	Enter a critical section.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May wait if some other thread has entered one or more times.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsEnter(Ns_Cs *csPtr)
{
    Lock *lockPtr;
    int err;

    if (*csPtr == NULL) {
    	Ns_MasterLock();
	if (*csPtr == NULL) {
	    Ns_CsInit(csPtr);
	}
	Ns_MasterUnlock();
    }

    lockPtr = (Lock *) *csPtr;
    err = pthread_mutex_trylock(&lockPtr->l.mutex);
    if (err == EBUSY) {
    	err = pthread_mutex_lock(&lockPtr->l.mutex);
	if (err != 0) {
	    NsThreadFatal("Ns_CsEnter", "ptread_mutex_lock", err);
	}
	++lockPtr->nbusy;
    } else if (err != 0) {
	NsThreadFatal("Ns_CsEnter", "ptread_mutex_trylock", err);
    }
    ++lockPtr->nlock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsLeave --
 *
 *	Leave a critical section.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May wake up some other thread on the last exit.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsLeave(Ns_Cs *csPtr)
{
    Lock *lockPtr = (Lock *) *csPtr;
    int     	 err;
    
    err = pthread_mutex_unlock(&lockPtr->l.mutex);
    if (err != 0) {
    	NsThreadFatal("Ns_CsLeave", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockInit --
 *
 *	Initialize a reader-write lock, often called the first time
 *  	a lock is read or write locked the first time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockInit(Ns_RWLock *rwPtr)
{
    Lock *lockPtr;
    int err;
    
    lockPtr = LockAlloc("rw");
    err = pthread_rwlock_init(&lockPtr->l.rwlock, NULL);
    if (err != 0) {
	NsThreadFatal("Ns_RWLockInit", "pthread_rwlock_init", err);
    }
    *rwPtr = (Ns_RWLock) lockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RWLockDestroy --
 *
 *	Destory a reader-writer lock, rarely used as locks normally
 *  	exist throughout the lifetime of the server.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockDestroy(Ns_RWLock *rwPtr)
{
    Lock	*lockPtr = (Lock *) *rwPtr;
    int err;

    if (lockPtr != NULL) {
	err = pthread_rwlock_destroy(&lockPtr->l.rwlock);
	if (err != 0) {
	    NsThreadFatal("Ns_RWlockDestroy", "ptread_rwlock_destroy", err);
	}
	LockFree(lockPtr);
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
 *	Thread may wait if the lock currently has a writer.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockRdLock(Ns_RWLock *rwPtr)
{
    Lock *lockPtr;
    int err;

    if (*rwPtr == NULL) {
    	Ns_MasterLock();
	if (*rwPtr == NULL) {
	    Ns_RWLockInit(rwPtr);
	}
	Ns_MasterUnlock();
    }
    lockPtr = (Lock *) *rwPtr;

    err = pthread_rwlock_rdlock(&lockPtr->l.rwlock);
    if (err != 0) {
	NsThreadFatal("Ns_RWLockRdLock", "pthread_rwlock_rdlock", err);
    }
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
 *	Thread may wait until all readers have released the lock.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockWrLock(Ns_RWLock *rwPtr)
{
    Lock *lockPtr;
    int err;

    if (*rwPtr == NULL) {
    	Ns_MasterLock();
	if (*rwPtr == NULL) {
	    Ns_RWLockInit(rwPtr);
	}
	Ns_MasterUnlock();
    }
    lockPtr = (Lock *) *rwPtr;

    err = pthread_rwlock_trywrlock(&lockPtr->l.rwlock);
    if (err == EBUSY || err == EWOULDBLOCK) {
        err = pthread_rwlock_wrlock(&lockPtr->l.rwlock);
    	if (err != 0) {
	    NsThreadFatal("Ns_RWLockWrLock", "pthread_rwlock_wrlock", err);
    	}
	++lockPtr->nbusy;
    } else if (err != 0) {
	NsThreadFatal("Ns_RWLockWrLock", "pthread_rwlock_trywrlock", err);
    }
    ++lockPtr->nlock;
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
 *  	Single writer or multiple readers may enter.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RWLockUnlock(Ns_RWLock *rwPtr)
{
    Lock *lockPtr = (Lock *) *rwPtr;
    int err;

    err = pthread_rwlock_unlock(&lockPtr->l.rwlock);
    if (err != 0) {
	NsThreadFatal("Ns_RWLockUnlock", "pthread_rwlock_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockInfo --
 *
 *	Append info on each lock.
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
NsLockInfo(Tcl_DString *dsPtr)
{
    Lock *lockPtr;
    char buf[100];

    Ns_MasterLock();
    lockPtr = firstLockPtr;
    while (lockPtr != NULL) {
	Tcl_DStringStartSublist(dsPtr);
	Tcl_DStringAppendElement(dsPtr, lockPtr->name);
	Tcl_DStringAppendElement(dsPtr, "");
	sprintf(buf, " %d %lu %lu", lockPtr->id, lockPtr->nlock, lockPtr->nbusy);
	Tcl_DStringAppend(dsPtr, buf, -1);
	Tcl_DStringEndSublist(dsPtr);
	lockPtr = lockPtr->nextPtr;
    }
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * GetMutex --
 *
 *	Cast an Ns_Mutex to a mutex Lock, initializing if needed.
 *
 * Results:
 *	Pointer to Lock.
 *
 * Side effects:
 *	Lock is initialized the first time.
 *
 *----------------------------------------------------------------------
 */

static Lock *
GetLock(Ns_Mutex *mutexPtr)
{
    Ns_MasterLock();
    if (*mutexPtr == NULL) {
	Ns_MutexInit(mutexPtr);
    }
    Ns_MasterUnlock();
    return (Lock *) *mutexPtr;
}

static Lock *
LockAlloc(char *prefix)
{
    Lock	   *lockPtr;
    static int	    nextid;

    lockPtr = ns_calloc(1, sizeof(Lock));
    Ns_MasterLock();
    lockPtr->nextPtr = firstLockPtr;
    firstLockPtr = lockPtr;
    lockPtr->id = nextid++;
    sprintf(lockPtr->name, "%s(%d)", prefix, lockPtr->id);
    Ns_MasterUnlock();
    return lockPtr;
}

static void
LockFree(Lock *lockPtr)
{
    Lock       **lockPtrPtr;

    Ns_MasterLock();
    lockPtrPtr = &firstLockPtr;
    while ((*lockPtrPtr) != lockPtr) {
	lockPtrPtr = &(*lockPtrPtr)->nextPtr;
    }
    *lockPtrPtr = lockPtr->nextPtr;
    Ns_MasterUnlock();
    ns_free(lockPtr);
}

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
 *	Mutex locks with metering.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/mutex.c,v 1.1 2002/02/16 00:12:01 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a mutex with
 * string name and lock and busy counters.
 */

typedef struct Lock {
    pthread_mutex_t  mutex;
    struct Lock     *nextPtr;
    unsigned int     id;
    unsigned long    nlock;
    unsigned long    nbusy;
    char	     name[NS_THREAD_NAMESIZE+1];
} Lock;

#define GETLOCK(m)  	(*(m)?((Lock *)*(m)):GetLock((m)))
static Lock *GetLock(Ns_Mutex *mutexPtr);
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
    static unsigned int nextid;

    lockPtr = ns_calloc(1, sizeof(Lock));
    Ns_MasterLock();
    lockPtr->nextPtr = firstLockPtr;
    firstLockPtr = lockPtr;
    lockPtr->id = nextid++;
    sprintf(lockPtr->name, "mu%d", lockPtr->id);
    Ns_MasterUnlock();
    err = pthread_mutex_init(&lockPtr->mutex, NULL);
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
    Lock       **lockPtrPtr;
    Lock	*lockPtr = (Lock *) *mutexPtr;
    int     	 err;

    if (lockPtr != NULL) {
	err = pthread_mutex_destroy(&lockPtr->mutex);
	if (err != 0) {
	    NsThreadFatal("Ns_MutexDestroy", "ptread_mutex_destroy", err);
	}
    	Ns_MasterLock();
    	lockPtrPtr = &firstLockPtr;
    	while ((*lockPtrPtr) != lockPtr) {
	    lockPtrPtr = &(*lockPtrPtr)->nextPtr;
    	}
    	*lockPtrPtr = lockPtr->nextPtr;
    	Ns_MasterUnlock();
    	ns_free(lockPtr);
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
    
    err = pthread_mutex_trylock(&lockPtr->mutex);
    if (err == EBUSY) {
    	err = pthread_mutex_lock(&lockPtr->mutex);
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

    err = pthread_mutex_trylock(&lockPtr->mutex);
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
    
    err = pthread_mutex_unlock(&lockPtr->mutex);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexUnlock", "pthread_mutex_unlock", err);
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
 * GetLock --
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

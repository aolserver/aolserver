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
 * tclNsThread.cpp --
 *
 *	This file implements the nsthread-based thread support.
 *
 * Copyright (c) 1999 AOL, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS:  @(#) tclNsThread.c 1.18 98/02/19 14:24:12
 */

#include "thread.h"
#define USE_TCL8X
#define BUILD_tcl
#include "tcl.h"

typedef void (Tcl_ThreadCreateProc) _ANSI_ARGS_((ClientData clientData));

/*
 * These are for the critical sections inside this file.
 */

#define MASTER_LOCK	Ns_MasterLock()
#define MASTER_UNLOCK	Ns_MasterUnlock()


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadCreate --
 *
 *	This procedure creates a new thread.
 *
 * Results:
 *	TCL_OK if the thread could be created.  The thread ID is
 *	returned in a parameter.
 *
 * Side effects:
 *	A new thread is created.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT int
TclpThreadCreate(idPtr, proc, clientData)
    Tcl_ThreadId *idPtr;		/* Return, the ID of the thread */
    Tcl_ThreadCreateProc proc;		/* Main() function of the thread */
    ClientData clientData;		/* The one argument to Main() */
{
    Ns_Thread tid;

    Ns_ThreadCreate(proc, clientData, 0, &tid);
    *idPtr = (Tcl_ThreadId) tid;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpThreadExit --
 *
 *	This procedure terminates the current thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	This procedure terminates the current thread.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpThreadExit(status)
    int status;
{
    Ns_ThreadExit((void *) status);
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetCurrentThread --
 *
 *	This procedure returns the ID of the currently running thread.
 *
 * Results:
 *	A thread ID.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT Tcl_ThreadId
Tcl_GetCurrentThread()
{
    Ns_Thread tid;

    Ns_ThreadSelf(&tid);
    return (Tcl_ThreadId) tid;
}


/*
 *----------------------------------------------------------------------
 *
 * TclpInitLock
 *
 *	This procedure is used to grab a lock that serializes initialization
 *	and finalization of Tcl.  On some platforms this may also initialize
 *	the mutex used to serialize creation of more mutexes and thread
 *	local storage keys.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquire the initialization mutex.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpInitLock()
{
    MASTER_LOCK;
}


/*
 *----------------------------------------------------------------------
 *
 * TclpInitUnlock
 *
 *	This procedure is used to release a lock that serializes initialization
 *	and finalization of Tcl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Release the initialization mutex.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpInitUnlock()
{
    MASTER_UNLOCK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpMasterLock
 *
 *	This procedure is used to grab a lock that serializes creation
 *	and finalization of serialization objects.  This interface is
 *	only needed in finalization; it is hidden during
 *	creation of the objects.
 *
 *	This lock must be different than the initLock because the
 *	initLock is held during creation of syncronization objects.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Acquire the master mutex.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpMasterLock()
{
    MASTER_LOCK;
}


/*
 *----------------------------------------------------------------------
 *
 * TclpMasterUnlock
 *
 *	This procedure is used to release a lock that serializes creation
 *	and finalization of synchronization objects.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Release the master mutex.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpMasterUnlock()
{
    MASTER_UNLOCK;
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetAllocMutex
 *
 *	This procedure returns a pointer to a statically initialized
 *	mutex for use by the memory allocator.  The alloctor must
 *	use this lock, because all other locks are allocated...
 *
 * Results:
 *	A pointer to a mutex that is suitable for passing to
 *	Tcl_MutexLock and Tcl_MutexUnlock.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT Tcl_Mutex *
Tcl_GetAllocMutex()
{
    static Ns_Mutex allocLock;
    return (Tcl_Mutex *)&allocLock;
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_MutexLock --
 *
 *	This procedure is invoked to lock a mutex.  This procedure
 *	handles initializing the mutex, if necessary.  The caller
 *	can rely on the fact that Tcl_Mutex is an opaque pointer.
 *	This routine will change that pointer from NULL after first use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May block the current thread.  The mutex is aquired when
 *	this returns.  Will allocate memory for a Ns_Mutex
 *	and initialize this the first time this Tcl_Mutex is used.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
Tcl_MutexLock(mutexPtr)
    Tcl_Mutex *mutexPtr;	/* Really (Ns_Mutex **) */
{
    Ns_Mutex *nsmutexPtr = (Ns_Mutex *) mutexPtr;

    if (*nsmutexPtr == NULL) {
	MASTER_LOCK;
	if (*nsmutexPtr == NULL) {
	    static int next;
	    char buf[20];

	    sprintf(buf, "tcl:%d", next++);
	    Ns_MutexInit(nsmutexPtr);
	    Ns_MutexSetName(nsmutexPtr, buf);
	}
	MASTER_UNLOCK;
    }
    Ns_MutexLock(nsmutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpMutexUnlock --
 *
 *	This procedure is invoked to unlock a mutex.  The mutex must
 *	have been locked by Tcl_MutexLock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mutex is released when this returns.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
Tcl_MutexUnlock(mutexPtr)
    Tcl_Mutex *mutexPtr;	/* Really (Ns_Mutex **) */
{
    Ns_MutexUnlock((Ns_Mutex *) mutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeMutex --
 *
 *	This procedure is invoked to clean up one mutex.  This is only
 *	safe to call at the end of time.
 *
 *	This assumes the Master Lock is held.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The mutex list is deallocated.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpFinalizeMutex(mutexPtr)
    Tcl_Mutex *mutexPtr;
{
    Ns_MutexDestroy((Ns_Mutex *) mutexPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeyInit --
 *
 *	This procedure initializes a thread specific data block key.
 *	Each thread has table of pointers to thread specific data.
 *	all threads agree on which table entry is used by each module.
 *	this is remembered in a "data key", that is just an index into
 *	this table.  To allow self initialization, the interface
 *	passes a pointer to this key and the first thread to use
 *	the key fills in the pointer to the key.  The key should be
 *	a process-wide static.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will allocate memory the first time this process calls for
 *	this key.  In this case it modifies its argument
 *	to hold the pointer to information about the key.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpThreadDataKeyInit(keyPtr)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk */
{
    Ns_Tls *pkeyPtr;

    MASTER_LOCK;
    if (*keyPtr == NULL) {
	pkeyPtr = (Ns_Tls *)ns_malloc(sizeof(Ns_Tls));
	Ns_TlsAlloc(pkeyPtr, NULL);
	*keyPtr = (Tcl_ThreadDataKey)pkeyPtr;
    }
    MASTER_UNLOCK;
}

/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeyGet --
 *
 *	This procedure returns a pointer to a block of thread local storage.
 *
 * Results:
 *	A thread-specific pointer to the data structure, or NULL
 *	if the memory has not been assigned to this key for this thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT VOID *
TclpThreadDataKeyGet(keyPtr)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk */
{
    Ns_Tls *pkeyPtr = *(Ns_Tls **)keyPtr;
    if (pkeyPtr == NULL) {
	return NULL;
    } else {
	return (VOID *)Ns_TlsGet(pkeyPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TclpThreadDataKeySet --
 *
 *	This procedure sets the pointer to a block of thread local storage.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets up the thread so future calls to TclpThreadDataKeyGet with
 *	this key will return the data pointer.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpThreadDataKeySet(keyPtr, data)
    Tcl_ThreadDataKey *keyPtr;	/* Identifier for the data chunk */
    VOID *data;			/* Thread local storage */
{
    Ns_Tls *pkeyPtr = *(Ns_Tls **)keyPtr;
    Ns_TlsSet(pkeyPtr, data);
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeThreadData --
 *
 *	This procedure cleans up the thread-local storage.  This is
 *	called once for each thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees up all thread local storage.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpFinalizeThreadData(keyPtr)
    Tcl_ThreadDataKey *keyPtr;
{
    VOID *result;
    Ns_Tls *pkeyPtr;

    if (*keyPtr != NULL) {
	pkeyPtr = *(Ns_Tls **)keyPtr;
	result = (VOID *)Ns_TlsGet(pkeyPtr);
	if (result != NULL) {
	    ns_free((char *)result);
	    Ns_TlsSet(pkeyPtr, (void *)NULL);
	}
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeThreadDataKey --
 *
 *	This procedure is invoked to clean up one key.  This is a
 *	process-wide storage identifier.  The thread finalization code
 *	cleans up the thread local storage itself.
 *
 *	This assumes the master lock is held.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The key is deallocated.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpFinalizeThreadDataKey(keyPtr)
    Tcl_ThreadDataKey *keyPtr;
{
    Ns_Tls *pkeyPtr;
    if (*keyPtr != NULL) {
	pkeyPtr = *(Ns_Tls **)keyPtr;
	ns_free((char *)pkeyPtr);
	*keyPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_ConditionWait --
 *
 *	This procedure is invoked to wait on a condition variable.
 *	The mutex is automically released as part of the wait, and
 *	automatically grabbed when the condition is signaled.
 *
 *	The mutex must be held when this procedure is called.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May block the current thread.  The mutex is aquired when
 *	this returns.  Will allocate memory for a Ns_Mutex
 *	and initialize this the first time this Tcl_Mutex is used.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
Tcl_ConditionWait(condPtr, mutexPtr, timePtr)
    Tcl_Condition *condPtr;	/* Really (Ns_Cond *) */
    Tcl_Mutex *mutexPtr;	/* Really (Ns_Mutex *) */
    Tcl_Time *timePtr;		/* Timeout on waiting period */
{
    Ns_Time timeout;
    
    if (timePtr != NULL) {
	/*
	 * Convert from the Tcl API relative timeout to the
	 * NS API absolute timeout.
	 */

	Ns_GetTime(&timeout);
	Ns_IncrTime(&timeout, timePtr->sec, timePtr->usec);
	timePtr = (Tcl_Time *) &timeout;
    }
    Ns_CondTimedWait((Ns_Cond *) condPtr, (Ns_Mutex *) mutexPtr,
	(Ns_Time *) timePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_ConditionNotify --
 *
 *	This procedure is invoked to signal a condition variable.
 *
 *	The mutex must be held during this call to avoid races,
 *	but this interface does not enforce that.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May unblock another thread.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
Tcl_ConditionNotify(condPtr)
    Tcl_Condition *condPtr;
{
    Ns_CondBroadcast((Ns_Cond *) condPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpFinalizeCondition --
 *
 *	This procedure is invoked to clean up a condition variable.
 *	This is only safe to call at the end of time.
 *
 *	This assumes the Master Lock is held.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The condition variable is deallocated.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpFinalizeCondition(condPtr)
    Tcl_Condition *condPtr;
{
    Ns_CondDestroy((Ns_Cond *) condPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpAlloc --
 *
 *	Wraps malloc. 
 *
 * Results:
 *	See ns_malloc 
 *
 * Side effects:
 *	See ns_malloc 
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT char *
TclpAlloc(unsigned int nbytes)
{
    return (char *) ns_malloc(nbytes);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpFree --
 *
 *	Wraps free. 
 *
 * Results:
 *	See ns_free 
 *
 * Side effects:
 *	See ns_free 
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT void
TclpFree(char *cp)
{
    ns_free(cp);
}


/*
 *----------------------------------------------------------------------
 *
 * TclpRealloc --
 *
 *	Wraps realloc. 
 *
 * Results:
 *	See ns_realloc 
 *
 * Side effects:
 *	See ns_realloc 
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT char *
TclpRealloc(char *cp, unsigned int nbytes)
{
    return ns_realloc(cp, nbytes);
}

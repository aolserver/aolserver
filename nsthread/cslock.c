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
 * cslock.c --
 *
 *	Support for critical sections.  Critical sections differ
 *	from mutexes in that a critical section can be repeatedly
 *	locked by the same thread as long as each lock is matched with
 * 	a cooresponding unlock.  Critical sections are used in cases
 *	where the lock could be called recursively, e.g., for the
 *	Ns_MasterLock.
 *
 *	Note:  Critical sections are almost always a bad idea.  You'll
 *	see below that the number of actual lock and unlock operations are
 *	doubled and threads can end up in condition waits instead of spin
 *	locks.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/cslock.c,v 1.2 2002/06/12 11:30:44 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following structure defines a critcal section including a mutex,
 * thread id of the owner, and a condition variable for waiting threads.
 */

typedef struct CsLock {
    Ns_Mutex        mutex;
    Ns_Cond         cond;
    int		    tid;
    int             count;
} CsLock;


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsInit --
 *
 *	Initialize a critical section object.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A critical section object is allocated from the heap.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsInit(Ns_Cs *csPtr)
{
    CsLock     *lockPtr;
    static unsigned int nextid = 0;

    lockPtr = ns_malloc(sizeof(CsLock));
    NsMutexInitNext(&lockPtr->mutex, "cs", &nextid);
    Ns_CondInit(&lockPtr->cond);
    lockPtr->count = 0;
    *csPtr = (Ns_Cs) lockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsDestroy --
 *
 *	Destroy a critical section object.  Note that you would almost
 *	never need to call this function as synchronization objects are
 *	typically created at startup and exist until the server exits.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The underly objects in the critical section are destroy and
 *	the critical section memory returned to the heap.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsDestroy(Ns_Cs *csPtr)
{
    CsLock *lockPtr = (CsLock *) *csPtr;

    /*
     * Destroy the condition only if it's not null, i.e., initialized
     * by the first use.
     */

    if (lockPtr != NULL) {
    	Ns_MutexDestroy(&lockPtr->mutex);
    	Ns_CondDestroy(&lockPtr->cond);
    	lockPtr->count = 0;
    	ns_free(lockPtr);
    	*csPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsEnter --
 *
 *	Lock a critical section object, initializing it first if needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may wait on the critical section condition variable if
 *	the critical section is already owned by another thread.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsEnter(Ns_Cs *csPtr)
{
    CsLock	*lockPtr;
    int		 tid = Ns_ThreadId();

    /*
     * Initialize the critical section if it has never been used before.
     */

    if (*csPtr == NULL) {
    	Ns_MasterLock();
	if (*csPtr == NULL) {
	    Ns_CsInit(csPtr);
	}
	Ns_MasterUnlock();
    }
    lockPtr = (CsLock *) *csPtr;

    /*
     * Wait on the condition if the critical section is owned by another
     * thread.
     */
    Ns_MutexLock(&lockPtr->mutex);
    while (lockPtr->count > 0 && lockPtr->tid != tid) {
        Ns_CondWait(&lockPtr->cond, &lockPtr->mutex);
    }
    lockPtr->tid = tid;
    lockPtr->count++;
    Ns_MutexUnlock(&lockPtr->mutex);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsLeave --
 *
 *	Unlock a critical section once.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Condition is signaled if this is the final unlock of the critical
 *	section.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CsLeave(Ns_Cs *csPtr)
{
    CsLock *lockPtr = (CsLock *) *csPtr;

    Ns_MutexLock(&lockPtr->mutex);
    if (--lockPtr->count == 0) {
        Ns_CondSignal(&lockPtr->cond);
    }
    Ns_MutexUnlock(&lockPtr->mutex);
}

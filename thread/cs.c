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
 * cs.c --
 *
 *	Support for nsthread critical sections.  Critical sections differ
 *	from mutex objects in that a critical section can be repeatedly
 *	locked by the same thread as long as each lock is matched with
 * 	a cooresponding unlock.  Critical sections are used cases where
 *	the lock could be called recursively.
 *
 *	Note:  Critical sections are almost always a bad idea.  You'll
 *	see below that the number of actual lock and unlock operations are
 *	doubled and threads can end up in condition waits instead of spin
 *	locks.  As a rule, if you think you need a critical section you
 * 	probably haven't solved your problem correctly.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/cs.c,v 1.2 2000/05/02 14:39:33 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following structure defines a critcal section object which includes
 * a lock, a pointer to the current thread which owns the critical section,
 * and a condiation variable for waiting threads.
 */

typedef struct {
    Ns_Mutex        lock;
    Ns_Cond         cond;
    Thread	   *ownerPtr;
    int             count;
} Cs;


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
    Cs       *cPtr;

    cPtr = ns_malloc(sizeof(Cs));
    Ns_MutexInit2(&cPtr->lock, "nsthread:cs");
    Ns_CondInit(&cPtr->cond);
    cPtr->count = 0;
    cPtr->ownerPtr = NULL;
    *csPtr = (Ns_Cs) cPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CsDestroy --
 *
 *	Destroy a critical section object.  Note that you would almost
 *	never need to call this function as synchronization objects are
 *	typically created at startup and exist until the server exists.
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

    /*
     * Destroy the condition only if it's not null, i.e., initialized
     * by the first use.
     */

    if (*csPtr != NULL) {
    	Cs       *cPtr = (Cs *) *csPtr;

    	Ns_MutexDestroy(&cPtr->lock);
    	Ns_CondDestroy(&cPtr->cond);
    	cPtr->ownerPtr = NULL;
    	cPtr->count = 0;
    	ns_free(cPtr);
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
    Cs		*cPtr;
    Thread    *thisPtr = NsGetThread();

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

    cPtr = (Cs *) *csPtr;
    Ns_MutexLock(&cPtr->lock);

    /*
     * Wait on the condition if the critical section is owned by another
     * thread.
     */
    while (cPtr->count > 0 && cPtr->ownerPtr != thisPtr) {
        Ns_CondWait(&cPtr->cond, &cPtr->lock);
    }

    cPtr->ownerPtr = thisPtr;
    cPtr->count++;
    Ns_MutexUnlock(&cPtr->lock);
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
    Cs       *cPtr = (Cs *) *csPtr;
    Thread *thisPtr = NsGetThread();

    Ns_MutexLock(&cPtr->lock);
    if (cPtr->ownerPtr == thisPtr && --cPtr->count == 0) {
        cPtr->ownerPtr = NULL;
        Ns_CondSignal(&cPtr->cond);
    }
    Ns_MutexUnlock(&cPtr->lock);
}

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
 * sema.c --
 *
 *	Couting semaphore routines.  Semaphores differ from ordinary mutex 
 *	locks in that they maintain a count instead of a simple locked/unlocked
 *	state.  Threads block if the semaphore count is less than one.
 *
 *	Note:  In general, cleaner and more flexible code can be implemented
 *	with condition variables.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/sema.c,v 1.1 2001/11/05 20:26:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include <semaphore.h>


/*
 *----------------------------------------------------------------------
 *
 * Ns_SemaInit --
 *
 *	Initialize a semaphore.   Note that because semaphores are
 *	initialized with a starting count they cannot be automatically
 *	created on first use as with other synchronization objects.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is allocated and initialized from the heap.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SemaInit(Ns_Sema *semaPtr, int initCount)
{
    sem_t *sPtr;

    sPtr = ns_malloc(sizeof(sem_t));
    if (sem_init(sPtr, 0, 0) != 0) {
	NsThreadFatal("Ns_SemaInit", "sem_init", errno);
    }
    *semaPtr = (Ns_Sema) sPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SemaDestroy --
 *
 *	Destroy a semaphore.  This routine should almost never be used
 *	as synchronization objects are normally created at process startup
 *	and exist entirely in process memory until the process exits.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Memory is returned to the heap.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SemaDestroy(Ns_Sema *semaPtr)
{
    if (*semaPtr != NULL) {
    	sem_t *sPtr = (sem_t *) *semaPtr;

	if (sem_destroy(sPtr) != 0) {
	    NsThreadFatal("Ns_SemaDestroy", "sem_destroy", errno);
	}
	ns_free(sPtr);
    	*semaPtr = NULL;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SemaWait --
 *
 *	Wait for a semaphore count to be greater than zero.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Calling thread may wait on the condition.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SemaWait(Ns_Sema *semaPtr)
{
    sem_t *sPtr = (sem_t *) *semaPtr;

    if (sem_wait(sPtr) != 0) {
	NsThreadFatal("Ns_SemaWait", "sem_wait", errno);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_SemaPost --
 *
 *	Increment a semaphore count, releasing waiting threads if needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Threads waiting on the condition, if any, may be resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SemaPost(Ns_Sema *semaPtr, int count)
{
    sem_t *sPtr = (sem_t *) *semaPtr;

    while (--count >= 0) {
        if (sem_post(sPtr) != 0) {
	    NsThreadFatal("Ns_SemaPost", "sem_post", errno);
        }
   }
}

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
 * cond.c --
 *
 *	Condition variable routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/cond.c,v 1.1 2001/11/05 20:31:32 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define GETCOND(c)	(*(c)?((pthread_cond_t *)*(c)):GetCond((c)))
static pthread_cond_t *GetCond(Ns_Cond *condPtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Pthread condition variable initialization.  Note this routine
 *	isn't used directly very often as static condition variables 
 *	are now self initialized when first used.
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
Ns_CondInit(Ns_Cond *condPtr)
{
    static char *func = "Ns_CondInit";
    pthread_cond_t *cond;
    int             err;

    cond = ns_malloc(sizeof(pthread_cond_t));
    err = pthread_cond_init(cond, NULL);
    if (err != 0) {
    	NsThreadFatal(func, "pthread_cond_init", err);
    }
    *condPtr = (Ns_Cond) cond;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	Pthread condition destroy.  Note this routine is almost never
 *	used as condition variables normally exist in memory until
 *	the process exits.
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
Ns_CondDestroy(Ns_Cond *condPtr)
{
    pthread_cond_t *cond = (pthread_cond_t *) *condPtr;
    int             err;

    if (cond != NULL) {
    	err = pthread_cond_destroy(cond);
    	if (err != 0) {
    	    NsThreadFatal("Ns_CondDestroy", "pthread_cond_destroy", err);
    	}
    	ns_free(cond);
    	*condPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *	Pthread condition signal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_signal.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *condPtr)
{
    pthread_cond_t *cond = GETCOND(condPtr);
    int             err;

    err = pthread_cond_signal(cond);
    if (err != 0) {
        NsThreadFatal("Ns_CondSignal", "pthread_cond_signal", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Pthread condition broadcast.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_broadcast.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *condPtr)
{
    pthread_cond_t *cond = GETCOND(condPtr);
    int             err;

    err = pthread_cond_broadcast(cond);
    if (err != 0) {
        NsThreadFatal("Ns_CondBroadcast", "pthread_cond_broadcast", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *	Pthread indefinite condition wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_wait.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr)
{
    pthread_cond_t  *cond = GETCOND(condPtr);
    pthread_mutex_t *lock = (pthread_mutex_t *) *mutexPtr;
    int              err;

    err = pthread_cond_wait(cond, lock);
#ifdef HAVE_ETIME_BUG
    /*
     * On Solaris, we have noticed that when the condition and/or
     * mutex are process-shared instead of process-private that
     * pthread_cond_wait may incorrectly return ETIME.  Because
     * we're not sure why ETIME is being returned (perhaps it's
     * from an underlying _lwp_cond_timedwait???), we allow
     * the condition to return.  This should be o.k. because
     * properly written condition code must be in a while
     * loop capable of handling spurious wakeups.
     */

    if (err == ETIME) {
	err = 0;
    }
#endif
    if (err != 0) {
	NsThreadFatal("Ns_CondWait", "pthread_cond_wait", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *	Pthread absolute time wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_timewait.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr, Ns_Time *timePtr)
{
    pthread_cond_t  *cond = GETCOND(condPtr);
    pthread_mutex_t *lock = (pthread_mutex_t *) *mutexPtr;
    int              err, status;
    struct timespec  ts;

    if (timePtr == NULL) {
	Ns_CondWait(condPtr, mutexPtr);
	return NS_OK;
    }

    /*
     * Convert the microsecond-based Ns_Time to a nanosecond-based
     * struct timespec.
     */

    ts.tv_sec = timePtr->sec;
    ts.tv_nsec = timePtr->usec * 1000;

    /*
     * As documented on Linux, pthread_cond_timedwait may return
     * EINTR if a signal arrives.  We have noticed that 
     * EINTR can be returned on Solaris as well although this
     * is not documented (perhaps, as above, it's possible it
     * bubbles up from _lwp_cond_timedwait???).  Anyway, unlike
     * the ETIME case above, we'll assume the wakeup is truely
     * spurious and simply restart the wait knowing that the
     * ts structure has not been modified.
     */

    do {
    	err = pthread_cond_timedwait(cond, lock, &ts);
    } while (err == EINTR);

#ifdef HAVE_ETIME_BUG

    /*
     * See comments above and note that here ETIME is still considered
     * a spurious wakeup, not an indication of timeout because we're
     * not making any assumptions about the nature or this bug.
     * While we're less certain, this should still be ok as properly
     * written condition code should tolerate the wakeup.
     */

    if (err == ETIME) {
	err = 0;
    }
#endif

    if (err == ETIMEDOUT) {
	status = NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_timedwait", err);
    } else {
	status = NS_OK;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCond --
 *
 *	Cast an Ns_Cond to pthread_cond_t, initializing if needed.
 *
 * Results:
 *	Pointer to pthread_cond_t.
 *
 * Side effects:
 *	Ns_Cond is initialized the first time.
 *
 *----------------------------------------------------------------------
 */

static pthread_cond_t *
GetCond(Ns_Cond *condPtr)
{
    Ns_MasterLock();
    if (*condPtr == NULL) {
	Ns_CondInit(condPtr);
    }
    Ns_MasterUnlock();
    return (pthread_cond_t *) *condPtr;
}

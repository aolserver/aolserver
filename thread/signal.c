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
 * signal.c --
 *
 *	Routines for signal handling.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/signal.c,v 1.1.1.1 2000/05/02 13:48:40 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#ifdef __hp10
#include <pthread.h>
#endif
#include "thread.h"

#ifdef __APPLE__
int sigwait(const sigset_t *set, int *sig);
#endif


/*
 *----------------------------------------------------------------------
 *
 * ns_sigwait --
 *
 *	Posix style sigwait().
 *
 * Results:
 *	0 on success, otherwise an error code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
ns_sigwait(sigset_t * set, int *sig)
{
    int ret;

#if defined(__unixware) || defined(__hp10)
    /* POSIX.1c Draft 6. */
    ret = sigwait(set);
    if (ret < 0) {
	ret = errno;
    } else { 
    	*sig = ret;
	ret = 0;
    }

#elif defined(__FreeBSD__)
    ret = sigwait(set, sig);
    if (ret != 0) {
	ret = errno;
    }
#else
    /* POSIX.1c */ 
    ret = sigwait(set, sig);
#endif

    return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_signal --
 *
 *	Install a process-wide signal handler.  Note that the handler
 *	is shared among all threads (although the signal mask is
 *	per-thread).
 *
 * Results:
 *	0 on success, -1 on error with specific error code set in errno.
 *
 * Side effects:
 *	Handler will be called when signal is received in this thread.
 *
 *----------------------------------------------------------------------
 */

int
ns_signal(int sig, void (*proc) (int))
{
    struct sigaction sa;

    sa.sa_flags = 0;
    sa.sa_handler = (void (*)(int)) proc;
    sigemptyset(&sa.sa_mask);

    return sigaction(sig, &sa, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * sigwait --
 *
 *	sigwait for Apple OS/X.  Modified from sigwait in the Linux
 *	pthread library.
 *
 * Results:
 *	Signal received.
 *
 * Side effects:
 *	Thread suspends until signal arrived.
 *
 *----------------------------------------------------------------------
 */
 
#ifdef __APPLE__

static void
sigwaithandler(int sig)
{
    volatile Thread *thisPtr = NsGetThread();

    thisPtr->exitarg = (void *) sig;
}

int
sigwait(const sigset_t *set, int *sig)
{
    sigset_t        mask;
    int             s;
    struct sigaction action, saved_signals[NSIG];
    volatile Thread *thisPtr = NsGetThread();

    sigfillset(&mask);
    for (s = 0; s < NSIG; s++) {
	if (sigismember(set, s)) {
	    sigdelset(&mask, s);
	    action.sa_handler = sigwaithandler;
	    sigfillset(&action.sa_mask);
	    action.sa_flags = 0;
	    sigaction(s, &action, &(saved_signals[s]));
	}
    }
    sigsuspend(&mask);
    for (s = 0; s < NSIG; s++) {
	if (sigismember(set, s)) {
	    sigaction(s, &(saved_signals[s]), NULL);
	}
    }
    *sig = (int) thisPtr->exitarg;
    thisPtr->exitarg = NULL;
    return 0;
}

#endif

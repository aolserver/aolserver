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
 * signal.c --
 *
 *	Routines for signal handling.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/signal.c,v 1.1 2002/06/10 22:30:24 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"


/*
 *----------------------------------------------------------------------
 *
 * ns_sigmask --
 *
 *	Set the thread's signal mask.
 *
 * Results:
 *	0 on success, otherwise an error code.
 *
 * Side effects:
 *	See pthread_sigmask.
 *
 *----------------------------------------------------------------------
 */

int
ns_sigmask(int how, sigset_t *set, sigset_t *oset)
{
    return pthread_sigmask(how, set, oset);
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
    return sigwait(set, sig);
}

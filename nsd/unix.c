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
 * unix.c --
 *
 *	Unix specific routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/unix.c,v 1.8 2001/01/16 18:13:49 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int Kill(int pid, int sig);
static int Wait(int pid, int seconds);
static int debugMode;


/*
 *----------------------------------------------------------------------
 *
 * NsBlockSignals --
 *
 *	Block signals at startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Signals will be pending until NsHandleSignals.
 *
 *----------------------------------------------------------------------
 */

void
NsBlockSignals(int debug)
{
    sigset_t set;
    int i;

    /*
     * Block SIGHUP, SIGPIPE, SIGTERM, and SIGINT. This mask is
     * inherited by all subsequent threads so that only this
     * thread will catch the signals in the sigwait() loop below.
     * Unfortunately this makes it impossible to kill the
     * server with a signal other than SIGKILL until startup
     * is complete.
     */

    debugMode = debug;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, NS_SIGHUP);
    if (!debugMode) {
        /* NB: Don't block SIGINT in debug mode for Solaris dbx. */
        sigaddset(&set, SIGINT);
    }
    ns_sigmask(SIG_BLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 * NsRestoreSignals --
 *
 *	Restore all signals to their default value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      A new thread will be created.
 *
 *----------------------------------------------------------------------
 */

void
NsRestoreSignals(void)
{
    sigset_t        set;
    int             sig;

    for (sig = 1; sig < NSIG; ++sig) {
        ns_signal(sig, (void (*)(int)) SIG_DFL);
    }
    sigfillset(&set);
    ns_sigmask(SIG_UNBLOCK, &set, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsHandleSignals --
 *
 *	Loop forever processing signals until a term signal
 *  	is received.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	HUP and/or Tcl init callbacks may be called.
 *
 *----------------------------------------------------------------------
 */

static int trigger[2];

static void Wakeup(int sig)
{
    unsigned char c = (unsigned char) sig;

    if (write(trigger[1], &c, 1) != 1) {
	Ns_Fatal("signal: wakeup trigger write() failed: %s",
	    strerror(errno));
    }
}

void
NsHandleSignals(void)
{
    sigset_t set;
    int err, sig, done;
    fd_set rset;
    unsigned char c;
    
    /*
     * Create the trigger pipe.
     */

    if (ns_pipe(trigger) != 0) {
	Ns_Fatal("signal: pipe() failed: %s", strerror(errno));
    }

    /*
     * Install trigger wakeup handler and unblock signals.
     */

    sigemptyset(&set);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    ns_signal(SIGHUP, Wakeup);
    ns_signal(SIGTERM, Wakeup);
    if (!debugMode) {
        sigaddset(&set, SIGINT);
	ns_signal(SIGINT, Wakeup);
    }
    ns_sigmask(SIG_UNBLOCK, &set, NULL);

    /*
     * Wait endlessly for trigger wakeups.
     */

    done = 0;
    do {
	do {
	    FD_ZERO(&rset);
	    FD_SET(trigger[0], &rset);
	    err = select(trigger[0]+1, &rset, NULL, NULL, NULL);
	} while (err < 0 && errno == EINTR);
	if (err < 0) {
	    Ns_Fatal("signal: select() failed: %s", strerror(errno));
	}
	if (!FD_ISSET(trigger[0], &rset)) {
	    continue;
	}
	if (read(trigger[0], &c, 1) != 1) {
	    Ns_Fatal("signal: wakupe trigger read() failed: %s",
		strerror(errno));
	}
	sig = (int) c;
	switch (sig) {
	case SIGHUP:
	    NsRunSignalProcs();
	    break;
	case NS_SIGTCL:
	    NsTclRunInits();
	    break;
	case SIGTERM:
	case SIGINT:
	    done = 1;
	    break;
	default:
	    Ns_Fatal("signal: unexpected wakeup signal: %d", sig);
	    break;
	}
    } while (!done);

    /*
     * Restore the default signal handlers and exit.
     */

    ns_signal(SIGHUP, SIG_DFL);
    ns_signal(SIGTERM, SIG_DFL);
    if (!debugMode) {
	ns_signal(SIGINT, SIG_DFL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsSendSignal --
 *
 *	Send an NS_SIG signal to the main thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Main thread in NsHandleSignals will wakeup.
 *
 *----------------------------------------------------------------------
 */

void
NsSendSignal(int sig)
{
    if (sig == NS_SIGTCL) {
	Wakeup(NS_SIGTCL);
    } else if (kill(Ns_InfoPid(),  sig) != 0) {
    	Ns_Fatal("unix: kill() failed: '%s'", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 * ns_sockpair, ns_pipe --
 *
 *      Create a pipe/socketpair with fd's set close on exec.
 *
 * Results:
 *      0 if ok, -1 otherwise.
 *
 * Side effects:
 *      Updates given fd array.
 *
 *----------------------------------------------------------------------
 */

static int
Pipe(int *fds, int sockpair)
{
    int err;

    if (sockpair) {
    	err = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    } else {
    	err = pipe(fds);
    }
    if (!err) {
	fcntl(fds[0], F_SETFD, 1);
	fcntl(fds[1], F_SETFD, 1);
    }
    return err;
}

int
ns_sockpair(SOCKET *socks)
{
    return Pipe(socks, 1);
}

int
ns_pipe(int *fds)
{
    return Pipe(fds, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsKillPid --
 *
 *	Kill a process and wait for exit.
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
NsKillPid(int pid)
{
    int timeout, err;
    
    if (!Kill(pid, SIGTERM)) {
	Ns_Log(Warning, "unix: failed to kill process %d: "
	       "pid %d does not exist", pid, pid);
    } else if (!Wait(pid, 10)) {
    	Ns_Log(Warning, "unix: "
	       "attempting again to kill process %d after waiting 10 seconds",pid);
	if (Kill(pid, SIGKILL) && !Wait(pid, 5)) {
	    Ns_Fatal("unix: failed to kill process %d: '%s'",pid,strerror(errno));
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Kill --
 *
 *	Send a signal, tolerating only a "no such process" error.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Kill(int pid, int sig)
{
    int err;
    
    err = kill(pid, sig);
    if (err != 0 && errno != ESRCH) {
    	Ns_Fatal("unix: kill(%d, %d) failed: '%s'", pid, sig, strerror(errno));
    }
    return (err == 0 ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Wait --
 *
 *	Wait for a process to die by polling for existance and sleeping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Wait(int pid, int seconds)
{
    int alive;
    
    while ((alive = Kill(pid, 0)) && seconds-- >= 0) {
	Ns_Log(Notice, "unix: waiting for killed process %d to die...", pid);
    	sleep(1);
    }
    return (alive ? 0 : 1);
}

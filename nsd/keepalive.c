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
 * keepalive.c --
 *
 *	Routines for monitoring keep-alive sockets.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/keepalive.c,v 1.2 2000/05/02 14:39:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The Keep structure maintains state for a socket in
 * the HTTP connection keep-alive state.
 */

typedef struct Keep {
    struct Keep *nextPtr;   /* Next is list of Keep structures.             */
    SOCKET       sock;      /* Underlying socket descriptor.                */
    time_t       timeout;   /* Timeout by which socket must become readable */
    Driver      *drvPtr;    /* Original accepting driver.                   */
    void        *drvData;   /* Pointer to driver connection data.           */
} Keep;

static Keep *keepBufPtr;
static Keep *firstFreeKeepPtr;	/* Free keepalive's, allocated at startup. */
static Keep *firstWaitKeepPtr;	/* New keepalive's to be monitored. */

/*
 * Maximum number of seconds for a socket to be in the keep-alive state
 * (used to set the timeout_t member of the Keep structure).
 */

static SOCKET trigPipe[2]; /* Trigger for waking up select. */
static int shutdownPending; /* Flag to indicate server shutdown. */
static int running;
static Ns_Thread keepThread;
static Ns_Mutex lock;	/* Lock around access to the above lists. */
static Ns_Cond cond;	/* Condition for shutdown signalling. */

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc KeepThread;	/* Main connection accepting thread. */
static void KeepTrigger(void);
static void KeepClose(Keep *keepPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsKeepAlive --
 *
 *	Put the conn onto the keepalive list if possible.
 *
 * Results:
 *	1 if connection kept alive, 0 otherwise.
 *
 * Side effects:
 *	The keepalive free and wait lists will be updated,
 *      the accept thread may be triggered.
 *
 *----------------------------------------------------------------------
 */

int
NsKeepAlive(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    void *drvData;
    Keep *keepPtr;
    SOCKET sock;
    time_t timeout;
    int trigger;
    
    if (connPtr->keepAlive != NS_TRUE ||
	connPtr->drvPtr->detachProc == NULL ||
	connPtr->drvPtr->sockProc == NULL ||
	(sock = ((*connPtr->drvPtr->sockProc)(connPtr->drvData))) < 0) {
    	return 0;
    }
    drvData = (*connPtr->drvPtr->detachProc)(connPtr->drvData);
	
    /*
     * Queue the new socket on the waiting list and wakeup the
     * the keep-alive thread if necessary.
     */

    trigger = 0;
    timeout = time(NULL) + nsconf.keepalive.timeout;
    keepPtr = NULL;
    Ns_MutexLock(&lock);
    if (!shutdownPending && firstFreeKeepPtr != NULL) {
	keepPtr = firstFreeKeepPtr;
    	firstFreeKeepPtr = keepPtr->nextPtr;
	keepPtr->nextPtr = firstWaitKeepPtr;
	firstWaitKeepPtr = keepPtr;
	keepPtr->drvPtr = connPtr->drvPtr;
	keepPtr->drvData = drvData;
	keepPtr->timeout = timeout;
	keepPtr->sock = sock;
	if (!running) {
    	    if (ns_sockpair(trigPipe) != 0) {
		Ns_Fatal("ns_sockpair() failed: %s", ns_sockstrerror(ns_sockerrno));
    	    }
	    Ns_ThreadCreate(KeepThread, NULL, 0, &keepThread);
	    running = 1;
	} else if (keepPtr->nextPtr == NULL) {
	    trigger = 1;
	}
    }
    Ns_MutexUnlock(&lock);
    if (keepPtr == NULL) {
    	return 0;
    } else if (trigger) {
	KeepTrigger();
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartKeepAlive --
 *
 *	Configure and then start the KeepThread if necessary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	KeepThread may be created.
 *
 *----------------------------------------------------------------------
 */

void
NsStartKeepAlive(void)
{
    int n;

    Ns_MutexSetName2(&lock, "nsd", "keepalive");
    if (nsconf.keepalive.enabled) {
	/*
	 * Adjust the keep-alive value, allocated the keep-alive
	 * structures, and create the KeepThread.
	 */

	n = FD_SETSIZE - 256;
	if (nsconf.keepalive.maxkeep > n) {
	    Ns_Log(Warning,
		      "%d max keepalive adjusted to %d (FD_SETSIZE - 256)",
		      nsconf.keepalive.maxkeep, n);
	    nsconf.keepalive.maxkeep = n;
	}
	keepBufPtr = ns_malloc(sizeof(Keep) * nsconf.keepalive.maxkeep);
	for (n = 0; n < nsconf.keepalive.maxkeep - 1; ++n) {
	    keepBufPtr[n].nextPtr = &keepBufPtr[n+1];
	}
	keepBufPtr[n].nextPtr = NULL;
	firstFreeKeepPtr = &keepBufPtr[0];
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopKeepAlive --
 *
 *	Set the shutdownPending flag and trigger the KeepThread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	KeepThread will close remaining keep-alive sockets and exit.
 *
 *----------------------------------------------------------------------
 */

void
NsStartKeepAliveShutdown(void)
{
    Ns_MutexLock(&lock);
    if (running) {
	shutdownPending = 1;
	KeepTrigger();
    }
    Ns_MutexUnlock(&lock);
}

void
NsWaitKeepAliveShutdown(Ns_Time *toPtr)
{
    int status;
    
    status = NS_OK;
    Ns_MutexLock(&lock);
    while (status == NS_OK && running) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "timeout waiting for keep-alive thread!");
    } else if (keepThread != NULL) {
	Ns_ThreadJoin(&keepThread, NULL);
	keepThread = NULL;
    	ns_sockclose(trigPipe[0]);
    	ns_sockclose(trigPipe[1]);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * KeepThread --
 *
 *	Main listening port service thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connections are accepted on the configured ports and placed
 *	on the run queue to be serviced.
 *
 *----------------------------------------------------------------------
 */

static void
KeepThread(void *ignored)
{
    fd_set set;
    char c;
    SOCKET max;
    int n;
    struct timeval tv, *tvPtr;
    Keep *activePtr, *freePtr, *keepPtr, *nextPtr;
    time_t  now, timeout;
    
    Ns_ThreadSetName("-keepalive-");
    Ns_Log(Notice, "starting");

    /*
     * Lock the mutex and loop forever waiting for readability of
     * listening ports and, if enabled, keep-alive sockets.  The
     * mutex is released during the select() call.  Note that
     * if select() is triggered for shutdown, one last pass is made
     * through the ports and keep-alive sockets to service any
     * recently arrived connections.
     */

    activePtr = freePtr = NULL;
    Ns_MutexLock(&lock);
    while (1) {

	/*
	 * Move any new keep-alive sockets to the active list.
	 */

	while (firstWaitKeepPtr != NULL) {
	    keepPtr = firstWaitKeepPtr;
	    firstWaitKeepPtr = keepPtr->nextPtr;
	    keepPtr->nextPtr = activePtr;
	    activePtr = keepPtr;
	    ++nsconf.keepalive.npending;
	}

	/*
	 * Unlock the mutex and select for readable sockets.
	 */

	if (shutdownPending) {
	    break;
	}
	Ns_MutexUnlock(&lock);

	do {
	    FD_ZERO(&set);
	    FD_SET(trigPipe[0], &set);
	    max = trigPipe[0];
	    if (activePtr == NULL) {
	        tvPtr = NULL;
	    } else {
	        time(&now);
	        timeout = now + nsconf.keepalive.timeout;
	        keepPtr = activePtr;
	        while (keepPtr != NULL) {
		    if (max < keepPtr->sock) {
		    	max = keepPtr->sock;
		    }
		    FD_SET(keepPtr->sock, &set);
		    if (timeout > keepPtr->timeout) {
		    	timeout = keepPtr->timeout;
		    }
		    keepPtr = keepPtr->nextPtr;
	    	}
	    	tv.tv_usec = 0;
	    	tv.tv_sec = timeout - now;
	    	if (tv.tv_sec < 0) {
		    tv.tv_sec = 0;
	    	}
	    	tvPtr = &tv;
	    }
	    n = select(max+1, &set, NULL, NULL, tvPtr);
	} while (n < 0 && ns_sockerrno == EINTR);
	if (n < 0) {
	    Ns_Fatal("select() failed: %s", ns_sockstrerror(ns_sockerrno));
	}
	if (FD_ISSET(trigPipe[0], &set) && recv(trigPipe[0], &c, 1, 0) != 1) {
	    Ns_Fatal("trigger recv() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}

    	/*
	 * Check for readablility of all active sockets.  Readable
	 * sockets which actually have data pending are re-queued
	 * otherwise they're closed along with non-readable sockets which
	 * have timed out.
	 */

	time(&now);
	keepPtr = activePtr;
	activePtr = NULL;
	while (keepPtr != NULL) {
	    nextPtr = keepPtr->nextPtr;
	    if (FD_ISSET(keepPtr->sock, &set)) {

		/*
		 * Queue readable sockets, closing them directly
		 * if there are no bytes to read or the queue
		 * is full.
		 */

		if (ns_sockioctl(keepPtr->sock, FIONREAD, &n) != 0
		    || n == 0
		    || Ns_QueueConn(keepPtr->drvPtr,
				    keepPtr->drvData) != NS_OK) {
		    KeepClose(keepPtr);
		}
	    } else if (keepPtr->timeout <= now) {

		/*
		 * Close directly sockets not queued and now
		 * beyond the alloated timeout.
		 */

		KeepClose(keepPtr);
	    } else {

		/*
		 * Return non-readable and not yet timed out
		 * sockets back on the active list.
		 */

		keepPtr->nextPtr = activePtr;
		activePtr = keepPtr;
		keepPtr = NULL;
	    }

	    /*
	     * Move queued, failed, or timed out sockets
	     * to the temporary free list.
	     */

	    if (keepPtr != NULL) {
	        keepPtr->nextPtr = freePtr;
		freePtr = keepPtr;
	    }

	    keepPtr = nextPtr;
	}
	Ns_MutexLock(&lock);

	/*
	 * Move free sockets from the temporary to the shared
	 * free list.
	 */

	while ((keepPtr = freePtr) != NULL) {
	    freePtr = keepPtr->nextPtr;
	    keepPtr->nextPtr = firstFreeKeepPtr;
	    firstFreeKeepPtr = keepPtr;
	    --nsconf.keepalive.npending;
	}
    }

    /*
     * Close any remaining sockets, cleanup the keep-alive
     * system, and signal shutdown complete.
     */

    Ns_Log(Notice, "shutdown pending");
    Ns_MutexUnlock(&lock);
    while ((keepPtr = activePtr) != NULL) {
	activePtr = keepPtr->nextPtr;
	KeepClose(keepPtr);
    }
    ns_free(keepBufPtr);

    Ns_Log(Notice, "shutdown complete");
    Ns_MutexLock(&lock);
    running = 0;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * KeepTrigger --
 *
 *	Wakeup the keepalive thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	KeepThread will return from select() if blocked.
 *
 *----------------------------------------------------------------------
 */

static void
KeepTrigger(void)
{
    if (send(trigPipe[1], "", 1, 0) != 1) {
	Ns_Fatal("trigger send() failed: %s", ns_sockstrerror(ns_sockerrno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * KeepClose --
 *
 *	Call the driver close routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
KeepClose(Keep *keepPtr)
{
    (void) (*keepPtr->drvPtr->closeProc)(keepPtr->drvData);
}

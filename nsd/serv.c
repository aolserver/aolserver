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
 * serv.c --
 *
 *	Routines for the core server connection threads.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/serv.c,v 1.11 2000/11/03 00:18:02 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The connection structures are all allocated at startup and maintained
 * in three lists:  The active list of connections currently being serviced
 * by the ConnThread's (maintained for the benefit of the ns_server
 * command below), the queue of connections waiting for service by
 * a connection thread, or the free stack of available connection structures
 * popped by Ns_QueueConn when new connections are queued by the driver
 * or keep-alive threads.
 */

static Conn *firstFreeConnPtr;
static Conn *firstWaitConnPtr;
static Conn *lastWaitConnPtr;
static Conn *firstActiveConnPtr;
static Conn *lastActiveConnPtr;
static Conn *connBufPtr;

/*
 * The following int is used to identify connections with a unique id.
 */

static unsigned int nextConnId;

/*
 * The current number of connections waiting for service.
 */

static int waitingConns;

/*
 * Counters to maintain the state of the ConnThread system.  Min and max
 * threads are determined at startup and then Ns_QueueConn ensure the
 * current number of threads remains within that range with individual
 * ConnThread's waiting no more than threadtimeout for a connection to
 * arrive.  The number of idle threads is maintained for the benefit of
 * the ns_server command.
 */

static int currentThreads;
static int idleThreads;
static int shutdownPending;
static Ns_Thread lastThread;
static Ns_Tls conntls;
static Ns_Mutex lock;	/* Lock around access to the above lists. */
static Ns_Cond cond;	/* Condition to signal connection threads when
			 * new connections arrive or shutdown. */

/*
 * The following structure, cache, and functions are used to
 * maintain URL timing stats.
 */

typedef struct Stats {
    unsigned int nconns;
    Ns_Time waitTime;
    Ns_Time openTime;
    Ns_Time closedTime;
} Stats;

static Stats globalStats;
static Ns_Cache *statsCache;
static void IncrStats(Stats *, Ns_Time *wPtr, Ns_Time *oPtr, Ns_Time *cPtr);
static char *SprintfStats(Stats *statsPtr, char *buf);
static int UrlStats(Tcl_Interp *interp, char *pattern);

/*
 * Local functions defined in this file
 */

static Ns_Tls *GetConnTls(void);
static void ConnRun(Conn *connPtr);	/* Connection run routine. */
static void ParseAuth(Conn *connPtr, char *auth);
static void CreateConnThread(void);
static void JoinConnThread(Ns_Thread *threadPtr);
static void AppendConn(Tcl_DString *dsPtr, Conn *connPtr,
	char *state, time_t now);
static void AppendConnList(Tcl_DString *dsPtr, Conn *firstPtr,
    	char *state);


/*
 *----------------------------------------------------------------------
 *
 * Ns_QueueConn --
 *
 *	Append a connection to the run queue.
 *
 * Results:
 *	NS_OK or NS_ERROR if no more connections are available.
 *
 * Side effects:
 *	Conneciton will run shortly.
 *
 *----------------------------------------------------------------------
 */

int
Ns_QueueConn(void *drvPtr, void *drvData)
{
    Conn *connPtr;
    Ns_Time now;
    int create, status;
    static int ndropped;
    
    create = 0;
    if (nsconf.serv.stats) {
    	Ns_GetTime(&now);
    } else {
	now.sec = time(NULL);
	now.usec = 0;
    }

    /*
     * Allocate a free connection structure.
     */

    Ns_MutexLock(&lock);
    if (shutdownPending) {
	status = NS_SHUTDOWN;
    } else if (firstFreeConnPtr == NULL) {
	if (nsconf.serv.maxdropped > 0 && ++ndropped > nsconf.serv.maxdropped) {
	    Ns_Log(Error, "serv: shutting down: %d dropped connections",
		   nsconf.serv.maxdropped);
	    NsSendSignal(NS_SIGTERM);
	    nsconf.serv.maxdropped = 0;
	}
	status = NS_ERROR;
    } else {
	status = NS_OK;
	connPtr = firstFreeConnPtr;
	firstFreeConnPtr = connPtr->nextPtr;
	ndropped = 0;

	/*
	 * Initialize the structure and place it at the end 
	 * of the wait queue. 
	 */

	memset(connPtr, 0, sizeof(Conn));
	connPtr->id = nextConnId++;
	connPtr->startTime = now.sec;
	connPtr->tqueue = now;
	connPtr->drvPtr = drvPtr;
	connPtr->drvData = drvData;
	if (firstWaitConnPtr == NULL) {
	    firstWaitConnPtr = connPtr;
	} else {
	    lastWaitConnPtr->nextPtr = connPtr;
	}
	lastWaitConnPtr = connPtr;

	/*
	 * Create or signal a connection thread if necessary.
	 */

	if (idleThreads == 0 && currentThreads < nsconf.serv.maxthreads) {
	    ++idleThreads;
	    ++currentThreads;
	    create = 1;
	}
	++waitingConns;
	Ns_CondSignal(&cond);
    }
    Ns_MutexUnlock(&lock);
    if (create) {
    	CreateConnThread();
    }

    /*
     * Yield so a thread can grab the new connection, perhaps
     * avoiding a startup storm.
     */

    Ns_ThreadYield();

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetConn, Ns_SetConn --
 *
 *	Get/Set the current connection for this thread.
 *
 * Results:
 *	Pointer to Ns_Conn or NULL if no active connection.
 *
 * Side effects:
 *	None.
 *
 *--------------------------------------------------------------------
 */

Ns_Conn *
Ns_GetConn(void)
{
    return Ns_TlsGet(GetConnTls());
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetBuf --
 *
 *	Return a big buffer suitable for I/O.
 *
 * Results:
 *	Pointer to TLS allocated buffer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsGetBuf(char **bufPtr, int *sizePtr)
{
    char *buf;
    static Ns_Tls tls;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, ns_free);
	}
	Ns_MasterUnlock();
    }
    buf = Ns_TlsGet(&tls);
    if (buf == NULL) {
	buf = ns_malloc(nsconf.bufsize);
	Ns_TlsSet(&tls, buf);
    } 
    *bufPtr = buf;
    *sizePtr = nsconf.bufsize;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclServerCmd --
 *
 *	Implement the ns_server Tcl command to return simple statistics
 *	about the running server.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclServerCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[100];
    int  status;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ?args?\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "urlstats")) {
	return UrlStats(interp, argv[2]);
    }
    status = TCL_OK;
    Ns_MutexLock(&lock);
    if (STREQ(argv[1], "waiting")) {
        sprintf(interp->result, "%d", waitingConns);
    } else if (STREQ(argv[1], "keepalive")) {
        sprintf(interp->result, "%d", nsconf.keepalive.npending);
    } else if (STREQ(argv[1], "stats")) {
	SprintfStats(&globalStats, interp->result);
    } else if (STREQ(argv[1], "connections")) {
        sprintf(interp->result, "%d", nextConnId);
    } else if (STREQ(argv[1], "threads")) {
        sprintf(buf, "min %d", nsconf.serv.minthreads);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "max %d", nsconf.serv.maxthreads);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "current %d", currentThreads);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "idle %d", idleThreads);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "stopping 0");
        Tcl_AppendElement(interp, buf);
    } else if (STREQ(argv[1], "active") ||
    	       STREQ(argv[1], "queued") ||
	       STREQ(argv[1], "all")) {
        Tcl_DString     ds;

    	Tcl_DStringInit(&ds);
    	if (argv[1][0] == 'a') {
	    AppendConnList(&ds, firstActiveConnPtr, "running");
	}
	if (argv[1][1] != 'c') {
	    AppendConnList(&ds, firstWaitConnPtr, "queued");
	}
        Tcl_DStringResult(interp, &ds);
    } else {
        Tcl_AppendResult(interp, "unknown command \"",
            argv[1], "\": should be "
            "active, "
            "waiting, "
            "connections, "
            "stats, "
            "urlstats, "
            "or threads", NULL);
        status = TCL_ERROR;
    }
    Ns_MutexUnlock(&lock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartServer --
 *
 *	Start the core connection thread interface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Minimum connection threads may be created.
 *
 *----------------------------------------------------------------------
 */

void
NsStartServer(void)
{
    int n;

    Ns_MutexSetName2(&lock, "nsd", "connqueue");
    if (nsconf.serv.stats & STATS_PERURL) {
	statsCache = Ns_CacheCreateSz("urlstats", TCL_STRING_KEYS, nsconf.serv.maxurlstats, ns_free);
    }

    /*
     * Pre-allocate all available connection structures to avoid having
     * to repeatedly allocate and free them at run time and to ensure there
     * is a per-set maximum number of simultaneous connections to handle
     * before Ns_QueueConn begins to return NS_ERROR.
     */

    connBufPtr = ns_malloc(sizeof(Conn) * nsconf.serv.maxconns);
    for (n = 0; n < nsconf.serv.maxconns - 1; ++n) {
	connBufPtr[n].nextPtr = &connBufPtr[n+1];
    }
    connBufPtr[n].nextPtr = NULL;
    firstFreeConnPtr = &connBufPtr[0];

    /*
     * Determine the minimum and maximum number of threads, adjusting the
     * values as needed.  The threadtimeout value is the maximum number of
     * seconds a thread will wait for a connection before exiting if the
     * current number of threads is above the minimum.
     */

    if (nsconf.serv.maxthreads > nsconf.serv.maxconns) {
	Ns_Log(Warning, "serv: cannot have more maxthreads than maxconns: "
	       "%d max threads adjusted down to %d max connections",
	       nsconf.serv.maxthreads, nsconf.serv.maxconns);
	nsconf.serv.maxthreads = nsconf.serv.maxconns;
    }
    if (nsconf.serv.minthreads > nsconf.serv.maxthreads) {
	Ns_Log(Warning, "serv: cannot have more minthreads than maxthreads: "
	       "%d min threads adjusted down to %d max threads",
	       nsconf.serv.minthreads, nsconf.serv.maxthreads);
	nsconf.serv.minthreads = nsconf.serv.maxthreads;
    }
    
    /*
     * Finally, create the accept thread which will begin servicing client
     * connections and the minimum number of connection threads.
     */

    currentThreads = idleThreads = nsconf.serv.minthreads;
    for (n = 0; n < nsconf.serv.minthreads; ++n) {
    	CreateConnThread();
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopServer --
 *
 *	Signal and wait for connection threads to exit.
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
NsStopServer(Ns_Time *toPtr)
{
    Ns_Thread joinThread;
    int status;
    
    Ns_Log(Notice, "serv: stopping connection threads");

    status = NS_OK;
    Ns_MutexLock(&lock);
    shutdownPending = 1;
    Ns_CondBroadcast(&cond);
    while (status == NS_OK &&
	   (firstWaitConnPtr != NULL || currentThreads > 0)) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    joinThread = lastThread;
    lastThread = NULL;
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "serv: timeout waiting for connection thread exit");
    } else {
	Ns_Log(Notice, "serv: connection threads stopped");
	if (joinThread != NULL) {
	    JoinConnThread(&joinThread);
	}
	ns_free(connBufPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnArgProc --
 *
 *	Ns_GetProcInfo callback for a running conn thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See AppendConn.
 *
 *----------------------------------------------------------------------
 */

void
NsConnArgProc(Tcl_DString *dsPtr, void *arg)
{
    Conn *connPtr;
    
    /*
     * A race condition here causes problems occasionally.
     */
    if (arg != NULL) {
	connPtr = *((Conn **) arg);
    	AppendConn(dsPtr, connPtr, "running", time(NULL));
    } else {
    	Tcl_DStringAppendElement(dsPtr, "");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnThread --
 *
 *	Main connection service thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connections are removed from the waiting queue and serviced.
 *
 *----------------------------------------------------------------------
 */

void
NsConnThread(void *arg)
{
    Conn            *connPtr, **connPtrPtr;
    Ns_Time          wait, ewait, eopen, eclosed, now, *timePtr;
    static unsigned  next = 0;
    char             thrname[32];
    int              new, status;
    char            *p;
    Ns_Set	    *headers, *outputheaders;
    Ns_Thread	     joinThread;
    Stats	    *statsPtr;
    Ns_Entry	    *entry;
    
    connPtrPtr = (Conn **) arg;
    headers = Ns_SetCreate(NULL);
    outputheaders = Ns_SetCreate(NULL);

    Ns_MutexLock(&lock);
    sprintf(thrname, "-conn%d-", next++);
    Ns_ThreadSetName(thrname);
    Ns_Log(Debug, "serv: starting: waiting for connections");

    while (1) {

	/*
	 * Wait for a connection to arrive, exiting if one doesn't
	 * arrive in the configured timeout period.
	 */

	if (currentThreads <= nsconf.serv.minthreads) {
	    timePtr = NULL;
	} else {
	    Ns_GetTime(&wait);
	    Ns_IncrTime(&wait, nsconf.serv.threadtimeout, 0);
	    timePtr = &wait;
	}

	status = NS_OK;
    	while (!shutdownPending && status == NS_OK && firstWaitConnPtr == NULL) {
	    status = Ns_CondTimedWait(&cond, &lock, timePtr);
	}
	if (firstWaitConnPtr == NULL) {
	    break;
	}

	/*
	 * Pull the first connection of the waiting list.
	 */

    	connPtr = firstWaitConnPtr;
    	firstWaitConnPtr = connPtr->nextPtr; 
    	if (lastWaitConnPtr == connPtr) {
	    lastWaitConnPtr = NULL;
    	}
	connPtr->nextPtr = NULL;
	connPtr->prevPtr = lastActiveConnPtr;
	if (lastActiveConnPtr != NULL) {
	    lastActiveConnPtr->nextPtr = connPtr;
	}
	lastActiveConnPtr = connPtr;
	if (firstActiveConnPtr == NULL) {
	    firstActiveConnPtr = connPtr;
	}
	idleThreads--;
	waitingConns--;
	*connPtrPtr = connPtr;
    	Ns_MutexUnlock(&lock);
	
	/*
	 * Re-initialize and run the connection.
	 */

	if (nsconf.serv.stats) {
	    Ns_GetTime(&connPtr->tstart);
	}
	connPtr->headers = headers;
	connPtr->outputheaders = outputheaders;
	Ns_TlsSet(GetConnTls(), connPtr);
	ConnRun(connPtr);
        Ns_TlsSet(GetConnTls(), connPtr);
	if (nsconf.serv.stats) {
	    Ns_GetTime(&now);
	    Ns_DiffTime(&connPtr->tstart, &connPtr->tqueue, &ewait);
	    Ns_DiffTime(&connPtr->tclose, &connPtr->tstart, &eopen);
	    Ns_DiffTime(&now, &connPtr->tclose, &eclosed);
	    if ((nsconf.serv.stats & STATS_PERURL) && connPtr->request && connPtr->request->url) {
		Ns_CacheLock(statsCache);
	    	entry = Ns_CacheCreateEntry(statsCache, connPtr->request->url, &new);
	    	if (!new) {
			statsPtr = Ns_CacheGetValue(entry);
	    	} else {
			statsPtr = ns_calloc(1, sizeof(Stats));
			Ns_CacheSetValueSz(entry, statsPtr, 1);
	    	}
		IncrStats(statsPtr, &ewait, &eopen, &eclosed);
	    	Ns_CacheUnlock(statsCache);
	    }
	}

	/*
	 * Perform various garbage collection tasks.  Note
	 * the order is significant:  The driver freeProc could
	 * possibly use Tcl and Tcl deallocate callbacks
	 * could possibly access header and/or request data.
	 */

	if (connPtr->interp != NULL) {
            Ns_TclDeAllocateInterp(NULL);
	}
	if (connPtr->request != NULL) {
            Ns_RequestFree(connPtr->request);
	}
        if (connPtr->authUser != NULL) {
	    ns_free(connPtr->authUser);
	}
        if (connPtr->authPasswd != NULL) {
            ns_free(connPtr->authPasswd);
	}
	if (connPtr->query != NULL) {
	    Ns_SetFree(connPtr->query);
	}

	/*
	 * NB: Headers and/or output headers could have been
	 * modified by the connection and/or cleanups.
	 */

	headers = connPtr->headers;
	outputheaders = connPtr->outputheaders;
        Ns_SetTrunc(headers, 0);
        Ns_SetTrunc(outputheaders, 0);

	/*
	 * Remove from the active list and push on the free list.
	 */

	Ns_MutexLock(&lock);
	if (nsconf.serv.stats & STATS_GLOBAL) {
	    IncrStats(&globalStats, &ewait, &eopen, &eclosed);
	}
	*connPtrPtr = NULL;
	if (connPtr->prevPtr != NULL) {
	    connPtr->prevPtr->nextPtr = connPtr->nextPtr;
	} else {
	    firstActiveConnPtr = connPtr->nextPtr;
	}
	if (connPtr->nextPtr != NULL) {
	    connPtr->nextPtr->prevPtr = connPtr->prevPtr;
	} else {
	    lastActiveConnPtr = connPtr->prevPtr;
	}
	idleThreads++;
	connPtr->nextPtr = firstFreeConnPtr;
	firstFreeConnPtr = connPtr;
	if (connPtr->nextPtr == NULL) {
	    /*
	     * If this thread just free'd up the busy server,
	     * run the ready procs to signal other subsystems.
	     */

	    Ns_MutexUnlock(&lock);
	    NsRunAtReadyProcs();
	    Ns_MutexLock(&lock);
	}
    }

    idleThreads--;
    currentThreads--;
    if (currentThreads == 0) {
    	Ns_CondBroadcast(&cond);
    }
    if (shutdownPending) {
	p = "shutdown pending";
    } else {
	p = "no waiting connections";
    }
    joinThread = lastThread;
    Ns_ThreadSelf(&lastThread);
    Ns_MutexUnlock(&lock);
    Ns_SetFree(headers);
    Ns_SetFree(outputheaders);
    if (joinThread != NULL) {
	JoinConnThread(&joinThread);
    }
    Ns_Log(Debug, "serv: exiting: %s", p);
    Ns_ThreadExit(connPtrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * ConnRun --
 *
 *	Run a valid connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connection request is read and parsed and the cooresponding
 *	service routine is called.
 *
 *----------------------------------------------------------------------
 */

static void
ConnRun(Conn *connPtr)
{
    Ns_Conn 	   *conn = (Ns_Conn *) connPtr;
    Ns_DString     ds;
    int            n, status;
    
    Ns_DStringInit(&ds);

    /*
     * Initialize the connection.
     */

    if (Ns_ConnInit(conn) != NS_OK) {
	goto done;
    }

    /*
     * Read and parse the HTTP request line.
     */

    if (Ns_ConnReadLine(conn, &ds, &n) != NS_OK ||
    	(connPtr->request = Ns_ParseRequest(ds.string)) == NULL) {
        (void) Ns_ReturnBadRequest(conn, "Invalid HTTP request");
        goto done;
    }

    /*
     * Read the headers into the connection header set
     * and parse the content-length and authorization
     * headers.
     */

    if (connPtr->request->version < 1.0) {
	conn->flags |= NS_CONN_SKIPHDRS;
    } else {
    	char *p;

        if (Ns_ConnReadHeaders(conn, connPtr->headers, &n) != NS_OK) {
            Ns_ReturnBadRequest(conn, "Invalid HTTP headers");
            goto done;
        }
        p = Ns_SetIGet(connPtr->headers, "Content-Length");
        if (p != NULL) {
            connPtr->contentLength = atoi(p);
        }
        p = Ns_SetIGet(connPtr->headers, "Authorization");
	if (p != NULL) {
	    ParseAuth(connPtr, p);
	}
    }
    if (conn->request->method && STREQ(conn->request->method, "HEAD")) {
	conn->flags |= NS_CONN_SKIPBODY;
    }

    /*
     * Check if this is a proxy request
     */

    if (connPtr->request->protocol != NULL && connPtr->request->host != NULL) {
	status = NsConnRunProxyRequest((Ns_Conn *) connPtr);
	goto done;
    }

    /*
     * Run the pre-authorization filters and, if ok, 
     * authorize and run the request procedure.
     */
     
    status = NsRunFilters(conn, NS_FILTER_PRE_AUTH);
    if (status != NS_OK) {
	goto done;
    }

    status = Ns_RequestAuthorize(nsServer,
		connPtr->request->method, connPtr->request->url, 
		connPtr->authUser, connPtr->authPasswd, 
		Ns_ConnPeer(conn));

    switch (status) {
    case NS_OK:
	status = NsRunFilters(conn, NS_FILTER_POST_AUTH);
	if (status == NS_OK) {
	    status = Ns_ConnRunRequest(conn);
	}
	break;

    case NS_FORBIDDEN:
	if (Ns_ConnFlushContent(conn) == NS_OK) {
	    Ns_ReturnForbidden(conn);
	}
	break;

    case NS_UNAUTHORIZED:
	if (Ns_ConnFlushContent(conn) == NS_OK) {
	    Ns_ReturnUnauthorized(conn);
	}
	break;

    case NS_ERROR:
    default:
	Ns_ReturnInternalError(conn);
	break;
    }

 done:
    Ns_ConnClose(conn);
    if (status == NS_OK || status == NS_FILTER_RETURN) {
	status = NsRunFilters(conn, NS_FILTER_TRACE);
	if (status == NS_OK) {
	    (void) NsRunFilters(conn, NS_FILTER_VOID_TRACE);
	    NsRunTraces(conn);
	}
    }
    NsRunCleanups(conn);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseAuth --
 *
 *	Parse an HTTP authorization string.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May set the authPasswd and authUser connection pointers.
 *
 *----------------------------------------------------------------------
 */

static void
ParseAuth(Conn *connPtr, char *auth)
{
    register char *p, *q;
    char           buf[0x100];
    int            n;
    char    	   save;
    
    p = auth;
    while (*p != '\0' && !isspace(UCHAR(*p))) {
        ++p;
    }
    if (*p != '\0') {
    	save = *p;
	*p = '\0';
        if (strcasecmp(auth, "Basic") == 0) {
    	    q = p + 1;
            while (*q != '\0' && isspace(UCHAR(*q))) {
                ++q;
            }
            n = Ns_HtuuDecode(q, (unsigned char *) buf, sizeof(buf));
            buf[n] = '\0';
            q = strchr(buf, ':');
            if (q != NULL) {
                *q++ = '\0';
                connPtr->authPasswd = ns_strdup(q);
            }
            connPtr->authUser = ns_strdup(buf);
        }
	*p = save;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CreateConnThread --
 *
 *	Create a connection thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New thread.
 *
 *----------------------------------------------------------------------
 */

static void
CreateConnThread(void)
{
    Ns_Thread thread;
    Conn **connPtrPtr;

    connPtrPtr = ns_malloc(sizeof(Conn *));
    *connPtrPtr = NULL;
    Ns_ThreadCreate(NsConnThread, connPtrPtr, 0, &thread);
}


/*
 *----------------------------------------------------------------------
 *
 * JoinConnThread --
 *
 *	Join a connection thread, freeing the threads connPtrPtr.
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
JoinConnThread(Ns_Thread *threadPtr)
{
    Conn **connPtrPtr;

    Ns_ThreadJoin(threadPtr, (void **) &connPtrPtr);
    ns_free(connPtrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendConn --
 *
 *	Append connection data to a dstring.
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
AppendConn(Tcl_DString *dsPtr, Conn *connPtr, char *state, time_t now)
{
    char buf[100];
    char  *p;

    time(&now);
    Tcl_DStringStartSublist(dsPtr);

    /*
     * An annoying race condition can be lethal here.
     */
    if ( connPtr != NULL ) {
	
	sprintf(buf, "%d", connPtr->id);
	Tcl_DStringAppendElement(dsPtr, buf);
	Tcl_DStringAppendElement(dsPtr, Ns_ConnPeer((Ns_Conn *) connPtr));
	Tcl_DStringAppendElement(dsPtr, state);
	
	/*
	 * Carefully copy the bytes to avoid chasing a pointer
	 * which may be changing in the connection thread.  This
	 * is not entirely safe but acceptible for a seldom-used
	 * admin command.
	 */
	
	p = (connPtr->request && connPtr->request->method) ?
	    connPtr->request->method : "?";
	Tcl_DStringAppendElement(dsPtr, strncpy(buf, p, sizeof(buf)));
	p = (connPtr->request && connPtr->request->url) ?
	    connPtr->request->url : "?";
	Tcl_DStringAppendElement(dsPtr, strncpy(buf, p, sizeof(buf)));
	sprintf(buf, "%d", (int) difftime(now, connPtr->startTime));
	Tcl_DStringAppendElement(dsPtr, buf);
	sprintf(buf, "%d", connPtr->nContentSent);
	Tcl_DStringAppendElement(dsPtr, buf);
    }
    Tcl_DStringEndSublist(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendConnList --
 *
 *	Append list of connection data to a dstring.
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
AppendConnList(Tcl_DString *dsPtr, Conn *firstPtr, char *state)
{
    time_t now;
    
    time(&now);
    while (firstPtr != NULL) {
	AppendConn(dsPtr, firstPtr, state, now);
	firstPtr = firstPtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetConnTls --
 *
 *	Return Ns_Tls for the current connection.
 *
 * Results:
 *	Pointer to statis Ns_Tls.
 *
 * Side effects:
 *	Tls is initialized on first call.
 *
 *----------------------------------------------------------------------
 */

static Ns_Tls *
GetConnTls(void)
{
    static Ns_Tls tls;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, NULL);
	}
	Ns_MasterUnlock();
    }
    return &tls;
}


/*
 *----------------------------------------------------------------------
 *
 * IncrStats --
 *
 *	Increment a stats structure with latest timing data.
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
IncrStats(Stats *statsPtr, Ns_Time *wPtr, Ns_Time *oPtr, Ns_Time *cPtr)
{
    ++statsPtr->nconns;
    Ns_IncrTime(&statsPtr->waitTime, wPtr->sec, wPtr->usec);
    Ns_IncrTime(&statsPtr->openTime, oPtr->sec, oPtr->usec);
    Ns_IncrTime(&statsPtr->closedTime, cPtr->sec, cPtr->usec);
}


/*
 *----------------------------------------------------------------------
 *
 * UrlStats --
 *
 *	Helper function to format URL stats for NsTclServerCmd.
 *
 * Results:
 *	TCL_OK.
 *
 * Side effects:
 *	Builds up stats results in interp.
 *
 *----------------------------------------------------------------------
 */

static char *
SprintfStats(Stats *statsPtr, char *buf)
{
    sprintf(buf, "%u %d %ld %d %ld %d %ld",
	statsPtr->nconns,
	(int) statsPtr->waitTime.sec, statsPtr->waitTime.usec,
	(int) statsPtr->openTime.sec, statsPtr->openTime.usec,
	(int) statsPtr->closedTime.sec, statsPtr->closedTime.usec);
    return buf;
}

static int
UrlStats(Tcl_Interp *interp, char *pattern)
{
    Ns_CacheSearch search;
    Ns_Entry *entry;
    Tcl_DString ds;
    char *url, buf[100];

    if (statsCache != NULL) {
	Tcl_DStringInit(&ds);
	Ns_CacheLock(statsCache);
	entry = Ns_CacheFirstEntry(statsCache, &search);
	while (entry != NULL) {
	    url = Ns_CacheKey(entry);
	    if (pattern == NULL || Tcl_StringMatch(url, pattern)) {
		Tcl_DStringStartSublist(&ds);
		Tcl_DStringAppendElement(&ds, url);
		Tcl_DStringAppendElement(&ds, SprintfStats(Ns_CacheGetValue(entry), buf));
		Tcl_DStringEndSublist(&ds);
	    }
	    entry = Ns_CacheNextEntry(&search);
	}
	Ns_CacheUnlock(statsCache);
	Tcl_DStringResult(interp, &ds);
    }
    return TCL_OK;
}

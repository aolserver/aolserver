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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/queue.c,v 1.3 2001/03/14 02:01:58 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is allocated for each new thread.  The
 * connPtr arg is used for the proc arg callback to list conn
 * info for running threads.
 */

typedef struct {
    NsServer *servPtr;
    Conn *connPtr;
} Arg;

/*
 * Local functions defined in this file
 */

static void ConnRun(Conn *connPtr);	/* Connection run routine. */
static void ParseAuth(Conn *connPtr, char *auth);
static void CreateConnThread(NsServer *servPtr);
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
Ns_QueueConn(Ns_Driver driver, void *drvData)
{
    NsServer *servPtr;
    Conn *connPtr;
    int create, status;

    servPtr = NsGetServer(Ns_GetDriverServer(driver));
    create = 0;

    /*
     * Allocate a free connection structure.
     */

    Ns_MutexLock(&servPtr->queue.lock);
    if (servPtr->queue.shutdown) {
	status = NS_SHUTDOWN;
    } else if (servPtr->queue.freePtr == NULL) {
	status = NS_ERROR;
    } else {
	status = NS_OK;
	connPtr = servPtr->queue.freePtr;
	servPtr->queue.freePtr = connPtr->nextPtr;

	/*
	 * Initialize the structure and place it at the end 
	 * of the wait queue. 
	 */

	memset(connPtr, 0, sizeof(Conn));
	connPtr->id = servPtr->queue.nextid++;
	connPtr->startTime = time(NULL);
	connPtr->drvPtr = (Driver *) driver;
	connPtr->drvData = drvData;
	connPtr->servPtr = servPtr;
	if (servPtr->queue.wait.firstPtr == NULL) {
	    servPtr->queue.wait.firstPtr = connPtr;
	} else {
	    servPtr->queue.wait.lastPtr->nextPtr = connPtr;
	}
	servPtr->queue.wait.lastPtr = connPtr;

	/*
	 * Create or signal a connection thread if necessary.
	 */

	if (servPtr->threads.idle == 0 && servPtr->threads.current < servPtr->threads.max) {
	    ++servPtr->threads.idle;
	    ++servPtr->threads.current;
	    create = 1;
	}
	++servPtr->queue.wait.num;
	Ns_CondSignal(&servPtr->queue.cond);
    }
    Ns_MutexUnlock(&servPtr->queue.lock);
    if (create) {
    	CreateConnThread(servPtr);
    }
    if (servPtr->queue.yield) {
        Ns_ThreadYield();
    }
    return status;
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
NsTclServerCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    char buf[100];
    int  status;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ?args?\"", NULL);
        return TCL_ERROR;
    }
    status = TCL_OK;
    Ns_MutexLock(&servPtr->queue.lock);
    if (STREQ(argv[1], "waiting")) {
        sprintf(interp->result, "%d", servPtr->queue.wait.num);
    } else if (STREQ(argv[1], "keepalive")) {
        sprintf(interp->result, "%d", nsconf.keepalive.npending);
    } else if (STREQ(argv[1], "connections")) {
        sprintf(interp->result, "%d", servPtr->queue.nextid);
    } else if (STREQ(argv[1], "threads")) {
        sprintf(buf, "min %d", servPtr->threads.min);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "max %d", servPtr->threads.max);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "current %d", servPtr->threads.current);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "idle %d", servPtr->threads.idle);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "stopping 0");
        Tcl_AppendElement(interp, buf);
    } else if (STREQ(argv[1], "active") ||
    	       STREQ(argv[1], "queued") ||
	       STREQ(argv[1], "all")) {
        Tcl_DString     ds;

    	Tcl_DStringInit(&ds);
    	if (argv[1][0] == 'a') {
	    AppendConnList(&ds, servPtr->queue.active.firstPtr, "running");
	}
	if (argv[1][1] != 'c') {
	    AppendConnList(&ds, servPtr->queue.wait.firstPtr, "queued");
	}
        Tcl_DStringResult(interp, &ds);
    } else {
        Tcl_AppendResult(interp, "unknown command \"",
            argv[1], "\": should be "
            "active, "
            "waiting, "
            "connections, "
            "or threads", NULL);
        status = TCL_ERROR;
    }
    Ns_MutexUnlock(&servPtr->queue.lock);
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
NsStartServer(NsServer *servPtr)
{
    int n;

    servPtr->threads.current = servPtr->threads.idle = servPtr->threads.min;
    for (n = 0; n < servPtr->threads.min; ++n) {
    	CreateConnThread(servPtr);
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
NsStopServer(NsServer *servPtr)
{
    Ns_Log(Notice, "serv: stopping server: %s", servPtr->server);
    Ns_MutexLock(&servPtr->queue.lock);
    servPtr->queue.shutdown = 1;
    Ns_CondBroadcast(&servPtr->queue.cond);
    Ns_MutexUnlock(&servPtr->queue.lock);
}

void
NsWaitServer(NsServer *servPtr, Ns_Time *toPtr)
{
    Ns_Thread joinThread;
    int status;
    
    status = NS_OK;
    Ns_MutexLock(&servPtr->queue.lock);
    while (status == NS_OK &&
	   (servPtr->queue.wait.firstPtr != NULL || servPtr->threads.current > 0)) {
	status = Ns_CondTimedWait(&servPtr->queue.cond, &servPtr->queue.lock, toPtr);
    }
    joinThread = servPtr->threads.last;
    servPtr->threads.last = NULL;
    Ns_MutexUnlock(&servPtr->queue.lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "serv: timeout waiting for connection thread exit");
    } else {
	Ns_Log(Notice, "serv: connection threads stopped");
	if (joinThread != NULL) {
	    JoinConnThread(&joinThread);
	}
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
    Arg *argPtr = arg;
    
    /*
     * A race condition here causes problems occasionally.
     */

    if (arg != NULL) {
    	AppendConn(dsPtr, argPtr->connPtr, "running", time(NULL));
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
    Arg    	    *argPtr = arg;
    NsServer	    *servPtr = argPtr->servPtr;
    Conn            *connPtr;
    Ns_Time          wait, *timePtr;
    unsigned int     id;
    char             thrname[32];
    int              status;
    char            *p;
    Ns_Set	    *headers, *outputheaders;
    Ns_Thread	     joinThread;
    
    /*
     * Set the conn thread name.
     */

    Ns_MutexLock(&servPtr->queue.lock);
    id = servPtr->threads.nextid++;
    Ns_MutexUnlock(&servPtr->queue.lock);
    sprintf(thrname, "-conn%d-", id); 
    Ns_ThreadSetName(thrname);

    /*
     * Pre-allocate some sets and warm up Tcl. 
     */

    headers = Ns_SetCreate(NULL);
    outputheaders = Ns_SetCreate(NULL);

    /*
     * Signal that conn threads appear warmed up if necessary and
     * start handling connections.
     */

    Ns_MutexLock(&servPtr->queue.lock);
    while (1) {

	/*
	 * Wait for a connection to arrive, exiting if one doesn't
	 * arrive in the configured timeout period.
	 */

	if (servPtr->threads.current <= servPtr->threads.min) {
	    timePtr = NULL;
	} else {
	    Ns_GetTime(&wait);
	    Ns_IncrTime(&wait, servPtr->threads.timeout, 0);
	    timePtr = &wait;
	}

	status = NS_OK;
    	while (!servPtr->queue.shutdown
		&& status == NS_OK
		&& servPtr->queue.wait.firstPtr == NULL) {
	    status = Ns_CondTimedWait(&servPtr->queue.cond, &servPtr->queue.lock, timePtr);
	}
	if (servPtr->queue.wait.firstPtr == NULL) {
	    break;
	}

	/*
	 * Pull the first connection of the waiting list.
	 */

    	connPtr = servPtr->queue.wait.firstPtr;
    	servPtr->queue.wait.firstPtr = connPtr->nextPtr; 
    	if (servPtr->queue.wait.lastPtr == connPtr) {
	    servPtr->queue.wait.lastPtr = NULL;
    	}
	connPtr->nextPtr = NULL;
	connPtr->prevPtr = servPtr->queue.active.lastPtr;
	if (servPtr->queue.active.lastPtr != NULL) {
	    servPtr->queue.active.lastPtr->nextPtr = connPtr;
	}
	servPtr->queue.active.lastPtr = connPtr;
	if (servPtr->queue.active.firstPtr == NULL) {
	    servPtr->queue.active.firstPtr = connPtr;
	}
	servPtr->threads.idle--;
	servPtr->queue.wait.num--;
	argPtr->connPtr = connPtr;
    	Ns_MutexUnlock(&servPtr->queue.lock);
	
	/*
	 * Re-initialize and run the connection.
	 */

	sprintf(connPtr->idstr, "cns%d", connPtr->id);
	connPtr->headers = headers;
	connPtr->outputheaders = outputheaders;
	ConnRun(connPtr);

	/*
	 * Perform various garbage collection tasks.  Note
	 * the order is significant:  The driver freeProc could
	 * possibly use Tcl and Tcl deallocate callbacks
	 * could possibly access header and/or request data.
	 */

	if (connPtr->interp != NULL) {
            Ns_TclDeAllocateInterp(connPtr->interp);
	}
	if (connPtr->request != NULL) {
            Ns_FreeRequest(connPtr->request);
	}
        if (connPtr->authUser != NULL) {
	    ns_free(connPtr->authUser);
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

	Ns_MutexLock(&servPtr->queue.lock);
	argPtr->connPtr = NULL;
	if (connPtr->prevPtr != NULL) {
	    connPtr->prevPtr->nextPtr = connPtr->nextPtr;
	} else {
	    servPtr->queue.active.firstPtr = connPtr->nextPtr;
	}
	if (connPtr->nextPtr != NULL) {
	    connPtr->nextPtr->prevPtr = connPtr->prevPtr;
	} else {
	    servPtr->queue.active.lastPtr = connPtr->prevPtr;
	}
	servPtr->threads.idle++;
	connPtr->nextPtr = servPtr->queue.freePtr;
	servPtr->queue.freePtr = connPtr;
	if (connPtr->nextPtr == NULL) {
	    /*
	     * If this thread just free'd up the busy server,
	     * run the ready procs to signal other subsystems.
	     */

	    Ns_MutexUnlock(&servPtr->queue.lock);
	    NsRunAtReadyProcs();
	    Ns_MutexLock(&servPtr->queue.lock);
	}
    }
    servPtr->threads.idle--;
    servPtr->threads.current--;
    if (servPtr->threads.current == 0) {
    	Ns_CondBroadcast(&servPtr->queue.cond);
    }
    if (servPtr->queue.shutdown) {
	p = "shutdown pending";
    } else {
	p = "no waiting connections";
    }
    joinThread = servPtr->threads.last;
    Ns_ThreadSelf(&servPtr->threads.last);
    Ns_MutexUnlock(&servPtr->queue.lock);
    Ns_SetFree(headers);
    Ns_SetFree(outputheaders);
    if (joinThread != NULL) {
	JoinConnThread(&joinThread);
    }
    Ns_ThreadExit(argPtr);
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
    
    status = NS_ERROR;
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
        (void) Ns_ConnReturnBadRequest(conn, "Invalid HTTP request");
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
            Ns_ConnReturnBadRequest(conn, "Invalid HTTP headers");
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

    status = Ns_AuthorizeRequest(connPtr->servPtr->server,
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
	    Ns_ConnReturnForbidden(conn);
	}
	break;

    case NS_UNAUTHORIZED:
	if (Ns_ConnFlushContent(conn) == NS_OK) {
	    Ns_ConnReturnUnauthorized(conn);
	}
	break;

    case NS_ERROR:
    default:
	Ns_ConnReturnInternalError(conn);
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
    NsClsCleanup(connPtr);
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
    int            n;
    char    	   save;
    
    p = auth;
    while (*p != '\0' && !isspace(UCHAR(*p))) {
        ++p;
    }
    if (*p != '\0') {
    	save = *p;
	*p = '\0';
        if (STRIEQ(auth, "Basic")) {
    	    q = p + 1;
            while (*q != '\0' && isspace(UCHAR(*q))) {
                ++q;
            }
	    n = strlen(q) + 3;
	    connPtr->authUser = ns_malloc(n);
            n = Ns_HtuuDecode(q, (unsigned char *) connPtr->authUser, n);
            connPtr->authUser[n] = '\0';
            q = strchr(connPtr->authUser, ':');
            if (q != NULL) {
                *q++ = '\0';
                connPtr->authPasswd = q;
            }
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
CreateConnThread(NsServer *servPtr)
{
    Ns_Thread thread;
    Arg *argPtr;

    argPtr = ns_malloc(sizeof(Arg));
    argPtr->servPtr = servPtr;
    argPtr->connPtr = NULL;
    Ns_ThreadCreate(NsConnThread, argPtr, 0, &thread);
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
    Arg *argPtr;

    Ns_ThreadJoin(threadPtr, (void **) &argPtr);
    ns_free(argPtr);
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

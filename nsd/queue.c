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
 * queue.c --
 *
 *	Routines for the managing the virtual server connection queue
 *	and service threads.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/queue.c,v 1.24 2003/11/16 15:04:14 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is allocated for each new thread.  The
 * connPtr arg is used for the proc arg callback to list conn
 * info for running threads.
 */

typedef struct {
    ConnPool *poolPtr;
    Conn *connPtr;
} Arg;

/*
 * Local functions defined in this file
 */

static void ConnRun(Conn *connPtr);	/* Connection run routine. */
static void CreateConnThread(ConnPool *poolPtr);
static void JoinConnThread(Ns_Thread *threadPtr);
static void AppendConn(Tcl_DString *dsPtr, Conn *connPtr, char *state);
static void AppendConnList(Tcl_DString *dsPtr, Conn *firstPtr,
    	char *state);

/*
 * Static variables defined in this file.
 */

static Ns_Tls argtls;
static int poolid;


/*
 *----------------------------------------------------------------------
 *
 * NsInitQueue --
 *
 *	Init connection queue.
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
NsInitQueue(void)
{
    Ns_TlsAlloc(&argtls, NULL);
    poolid = Ns_UrlSpecificAlloc();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_QueueConn --
 *
 *	Queue a connection from a loadable driver (no longer supported).
 *
 * Results:
 *	NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_QueueConn(void *drv, void *arg)
{
    return NS_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetConn --
 *
 *	Return the current connection in this thread.
 *
 * Results:
 *	Pointer to conn or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Ns_Conn *
Ns_GetConn(void)
{
    Arg *argPtr;

    argPtr = Ns_TlsGet(&argtls);
    return (argPtr ? ((Ns_Conn *) argPtr->connPtr) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsMapPool --
 *
 *	Map a method/URL to the given pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Requests for given URL's will be serviced by given pool.
 *
 *----------------------------------------------------------------------
 */

void
NsMapPool(ConnPool *poolPtr, char *map)
{
    char *server = poolPtr->servPtr->server;
    char **mv;
    int mc;

    if (Tcl_SplitList(NULL, map, &mc, (CONST char***)&mv) == TCL_OK) {
	if (mc == 2) {
	    Ns_UrlSpecificSet(server, mv[0], mv[1], poolid, poolPtr, 0, NULL);
	    Ns_Log(Notice, "pool[%s]: mapped %s %s -> %s", server, mv[0], mv[1], 
               poolPtr->pool);
	}
	ckfree((char *) mv);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsQueueConn --
 *
 *	Append a connection to the run queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Conneciton will run shortly.
 *
 *----------------------------------------------------------------------
 */

void
NsQueueConn(Conn *connPtr)
{
    NsServer *servPtr = connPtr->servPtr;
    ConnPool *poolPtr = NULL;
    int create = 0;

    /*
     * Select server connection pool.
     */

    if (connPtr->request != NULL) {
	poolPtr = Ns_UrlSpecificGet(connPtr->server, connPtr->request->method,
				    connPtr->request->url, poolid);
    }
    if (poolPtr == NULL) {
	poolPtr = servPtr->pools.defaultPtr;
    }

   /*
    * Queue connection.
    */

    Ns_MutexLock(&servPtr->pools.lock);
#if 0
    connPtr->id = servPtr->pools.nextconnid++;
#endif
    if (poolPtr->queue.wait.firstPtr == NULL) {
        poolPtr->queue.wait.firstPtr = connPtr;
    } else {
        poolPtr->queue.wait.lastPtr->nextPtr = connPtr;
    }
    poolPtr->queue.wait.lastPtr = connPtr;
    connPtr->nextPtr = NULL;
    if (poolPtr->threads.idle == 0
            && poolPtr->threads.current < poolPtr->threads.max) {
        ++poolPtr->threads.idle;
        ++poolPtr->threads.current;
        create = 1;
    }
    ++poolPtr->queue.wait.num;
    Ns_MutexUnlock(&servPtr->pools.lock);
    if (create) {
    	CreateConnThread(poolPtr);
    } else {
	Ns_CondSignal(&poolPtr->queue.cond);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclServerObjCmd --
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
NsTclServerObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		  Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    ConnPool *poolPtr = servPtr->pools.defaultPtr;
    char buf[100], *pool;
    Tcl_DString ds;
    static CONST char *opts[] = {
	 "active", "all", "connections", "keepalive", "pools", "queued",
	 "threads", "waiting", NULL, 
    };
    enum {
	 SActiveIdx, SAllIdx, SConnectionsIdx, SKeepaliveIdx, SPoolsIdx,
	 SQueuedIdx, SThreadsIdx, SWaitingIdx,
    } opt;

    if (objc != 2 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?pool?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 2) {
	poolPtr = servPtr->pools.defaultPtr;
    } else {
	pool = Tcl_GetString(objv[2]);
	poolPtr = servPtr->pools.firstPtr;
	while (poolPtr != NULL && !STREQ(poolPtr->pool, pool)) {
	    poolPtr = poolPtr->nextPtr;
	}
	if (poolPtr == NULL) {
	    Tcl_AppendResult(interp, "no such pool: ", pool, NULL);
	    return TCL_ERROR;
	}
    }
    Ns_MutexLock(&servPtr->pools.lock);
    switch (opt) {
    case SPoolsIdx:
	poolPtr = servPtr->pools.firstPtr;
	while (poolPtr != NULL) {
	    Tcl_AppendElement(interp, poolPtr->pool);
	    poolPtr = poolPtr->nextPtr;
	}
	break;
	  
    case SWaitingIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(poolPtr->queue.wait.num));
	break;

    case SKeepaliveIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(nsconf.keepalive.npending));
	break;

    case SConnectionsIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int) servPtr->pools.nextconnid));
	break;

    case SThreadsIdx:
        sprintf(buf, "min %d", poolPtr->threads.min);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "max %d", poolPtr->threads.max);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "current %d", poolPtr->threads.current);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "idle %d", poolPtr->threads.idle);
        Tcl_AppendElement(interp, buf);
        sprintf(buf, "stopping 0");
        Tcl_AppendElement(interp, buf);
	break;

    case SActiveIdx:
    case SQueuedIdx:
    case SAllIdx:
    	Tcl_DStringInit(&ds);
	if (opt != SQueuedIdx) {
	    AppendConnList(&ds, poolPtr->queue.active.firstPtr, "running");
	}
	if (opt != SActiveIdx) {
	    AppendConnList(&ds, poolPtr->queue.wait.firstPtr, "queued");
	}
        Tcl_DStringResult(interp, &ds);
    }
    Ns_MutexUnlock(&servPtr->pools.lock);
    return TCL_OK;
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
    ConnPool *poolPtr;
    int n;

    poolPtr = servPtr->pools.firstPtr;
    while (poolPtr != NULL) {
    	poolPtr->threads.current = poolPtr->threads.idle = poolPtr->threads.min;
    	for (n = 0; n < poolPtr->threads.min; ++n) {
    	    CreateConnThread(poolPtr);
	}
	poolPtr = poolPtr->nextPtr;
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
    ConnPool *poolPtr;

    Ns_Log(Notice, "serv: stopping server: %s", servPtr->server);
    Ns_MutexLock(&servPtr->pools.lock);
    servPtr->pools.shutdown = 1;
    Ns_MutexUnlock(&servPtr->pools.lock);
    poolPtr = servPtr->pools.firstPtr;
    while (poolPtr != NULL) {
    	Ns_CondBroadcast(&poolPtr->queue.cond);
	poolPtr = poolPtr->nextPtr;
    }
}

void
NsWaitServer(NsServer *servPtr, Ns_Time *toPtr)
{
    ConnPool *poolPtr;
    Ns_Thread joinThread;
    int status;
    
    status = NS_OK;
    poolPtr = servPtr->pools.firstPtr;
    Ns_MutexLock(&servPtr->pools.lock);
    while (poolPtr != NULL && status == NS_OK) {
    	while (status == NS_OK &&
	       (poolPtr->queue.wait.firstPtr != NULL || poolPtr->threads.current > 0)) {
	    status = Ns_CondTimedWait(&poolPtr->queue.cond, &servPtr->pools.lock, toPtr);
    	}
	poolPtr = poolPtr->nextPtr;
    }
    joinThread = servPtr->pools.joinThread;
    servPtr->pools.joinThread = NULL;
    Ns_MutexUnlock(&servPtr->pools.lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "serv: timeout waiting for connection thread exit");
    } else {
	if (joinThread != NULL) {
	    JoinConnThread(&joinThread);
	}
	Ns_Log(Notice, "serv: connection threads stopped");
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
    	AppendConn(dsPtr, argPtr->connPtr, "running");
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
    ConnPool	    *poolPtr = argPtr->poolPtr;
    NsServer	    *servPtr = poolPtr->servPtr;
    Conn            *connPtr;
    Ns_Time          wait, *timePtr;
    unsigned int     id;
    Ns_DString	     ds;
    int              status, ncons, ready;
    char            *msg;
    Ns_Thread	     joinThread;
    
    /*
     * Set the conn thread name.
     */

    Ns_TlsSet(&argtls, argPtr);
    Ns_MutexLock(&servPtr->pools.lock);
    id = poolPtr->threads.nextid++;
    Ns_MutexUnlock(&servPtr->pools.lock);
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "-conn:", servPtr->server, NULL);
    if (poolPtr->pool != NULL) {
    	Ns_DStringVarAppend(&ds, ":", poolPtr->pool, NULL);
    }
    Ns_DStringPrintf(&ds, ":%d", id);
    Ns_ThreadSetName(ds.string);
    Ns_DStringFree(&ds);
    ncons = poolPtr->threads.maxconns;
    msg = "exceeded max connections per thread";
    
    /*
     * Start handling connections.
     */

    Ns_MutexLock(&servPtr->pools.lock);
    while (poolPtr->threads.maxconns <= 0 || ncons-- > 0) {

	/*
	 * Wait for a connection to arrive, exiting if one doesn't
	 * arrive in the configured timeout period.
	 */

	if (poolPtr->threads.current <= poolPtr->threads.min) {
	    timePtr = NULL;
	} else {
	    Ns_GetTime(&wait);
	    Ns_IncrTime(&wait, poolPtr->threads.timeout, 0);
	    timePtr = &wait;
	}

	status = NS_OK;
    	while (!servPtr->pools.shutdown
		&& status == NS_OK
		&& poolPtr->queue.wait.firstPtr == NULL) {
	    status = Ns_CondTimedWait(&poolPtr->queue.cond, &servPtr->pools.lock, timePtr);
	}
	if (poolPtr->queue.wait.firstPtr == NULL) {
	    msg = "timeout waiting for connection";
	    break;
	}

	/*
	 * Pull the first connection off the waiting list.
	 */

    	connPtr = poolPtr->queue.wait.firstPtr;
    	poolPtr->queue.wait.firstPtr = connPtr->nextPtr; 
    	if (poolPtr->queue.wait.lastPtr == connPtr) {
	    poolPtr->queue.wait.lastPtr = NULL;
    	}
	connPtr->nextPtr = NULL;
	connPtr->prevPtr = poolPtr->queue.active.lastPtr;
	if (poolPtr->queue.active.lastPtr != NULL) {
	    poolPtr->queue.active.lastPtr->nextPtr = connPtr;
	}
	poolPtr->queue.active.lastPtr = connPtr;
	if (poolPtr->queue.active.firstPtr == NULL) {
	    poolPtr->queue.active.firstPtr = connPtr;
	}
	poolPtr->threads.idle--;
	poolPtr->queue.wait.num--;
	argPtr->connPtr = connPtr;

	/*
	 * Run the connection.
	 */

    	Ns_MutexUnlock(&servPtr->pools.lock);
	Ns_GetTime(&connPtr->times.run);
        ConnRun(connPtr);
	Ns_MutexLock(&servPtr->pools.lock);

	/*
	 * Remove from the active list and push on the free list.
	 */

	argPtr->connPtr = NULL;
	if (connPtr->prevPtr != NULL) {
	    connPtr->prevPtr->nextPtr = connPtr->nextPtr;
	} else {
	    poolPtr->queue.active.firstPtr = connPtr->nextPtr;
	}
	if (connPtr->nextPtr != NULL) {
	    connPtr->nextPtr->prevPtr = connPtr->prevPtr;
	} else {
	    poolPtr->queue.active.lastPtr = connPtr->prevPtr;
	}
	poolPtr->threads.idle++;
	ready = 0; /* NB: Need a new way to detect "ready again". */
	
	/*
	 * Free the connection and run at ready procs if the server
	 * can handled new connections.
	 */
	 
	Ns_MutexUnlock(&servPtr->pools.lock);
        NsFreeConn(connPtr);
	if (ready) {
	    NsRunAtReadyProcs();
	}
	Ns_MutexLock(&servPtr->pools.lock);
    }
    poolPtr->threads.idle--;
    poolPtr->threads.current--;
    if (poolPtr->threads.current == 0) {
    	Ns_CondBroadcast(&poolPtr->queue.cond);
    }
    if (servPtr->pools.shutdown) {
	msg = "shutdown pending";
    }
    Ns_Log(Notice, "exiting: %s", msg);
    joinThread = servPtr->pools.joinThread;
    Ns_ThreadSelf(&servPtr->pools.joinThread);
    Ns_MutexUnlock(&servPtr->pools.lock);
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
    Ns_Conn 	  *conn = (Ns_Conn *) connPtr;
    NsServer	  *servPtr = connPtr->servPtr;
    int            i, status;
	
    /*
     * Initialize the conn.
     */
     
    if (servPtr->opts.hdrcase != Preserve) {
	for (i = 0; i < Ns_SetSize(connPtr->headers); ++i) {
    	    if (servPtr->opts.hdrcase == ToLower) {
		Ns_StrToLower(Ns_SetKey(connPtr->headers, i));
	    } else {
		Ns_StrToUpper(Ns_SetKey(connPtr->headers, i));
	    }
	}
    }

    /*
     * Run the request.
     */

    if (connPtr->request->protocol != NULL && connPtr->request->host != NULL) {
	status = NsConnRunProxyRequest((Ns_Conn *) connPtr);
    } else {
	status = NsRunFilters(conn, NS_FILTER_PRE_AUTH);
	if (status == NS_OK) {
	    status = Ns_AuthorizeRequest(servPtr->server,
			connPtr->request->method, connPtr->request->url, 
			connPtr->authUser, connPtr->authPasswd, connPtr->peer);
	    switch (status) {
	    case NS_OK:
		status = NsRunFilters(conn, NS_FILTER_POST_AUTH);
		if (status == NS_OK) {
		    status = Ns_ConnRunRequest(conn);
		}
		break;

	    case NS_FORBIDDEN:
		Ns_ConnReturnForbidden(conn);
		break;

	    case NS_UNAUTHORIZED:
		Ns_ConnReturnUnauthorized(conn);
		break;

	    case NS_ERROR:
	    default:
		Ns_ConnReturnInternalError(conn);
		break;
	    }
        } else if (status != NS_FILTER_RETURN) {
            /* if not ok or filter_return, then the pre-auth filter coughed
             * an error.  We are not going to proceed, but also we
             * can't count on the filter to have sent a response
             * back to the client.  So, send an error response.
             */
            Ns_ConnReturnInternalError(conn);
            status = NS_FILTER_RETURN; /* to allow tracing to happen */
        }
    }
    Ns_ConnClose(conn);
    if (status == NS_OK || status == NS_FILTER_RETURN) {
	status = NsRunFilters(conn, NS_FILTER_TRACE);
	if (status == NS_OK) {
	    (void) NsRunFilters(conn, NS_FILTER_VOID_TRACE);
	    NsRunTraces(conn);
	}
    }

    /*
     * Perform various garbage collection tasks.  Note
     * the order is significant:  The driver freeProc could
     * possibly use Tcl and Tcl deallocate callbacks
     * could possibly access header and/or request data.
     */

    NsRunCleanups(conn);
    Ns_FreeConnInterp(conn);
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
CreateConnThread(ConnPool *poolPtr)
{
    Ns_Thread thread;
    Arg *argPtr;

    argPtr = ns_malloc(sizeof(Arg));
    argPtr->poolPtr = poolPtr;
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
AppendConn(Tcl_DString *dsPtr, Conn *connPtr, char *state)
{
    char buf[100];
    char  *p;
    Ns_Time now, diff;

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
	Ns_GetTime(&now);
	Ns_DiffTime(&now, &connPtr->times.queue, &diff);
	sprintf(buf, "%ld.%ld", diff.sec, diff.usec);
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
    while (firstPtr != NULL) {
	AppendConn(dsPtr, firstPtr, state);
	firstPtr = firstPtr->nextPtr;
    }
}

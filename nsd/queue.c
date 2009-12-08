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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/queue.c,v 1.47 2009/12/08 04:12:19 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include <math.h>

/*
 * The following structure is allocated for each new thread.  The
 * connPtr arg is used for the proc arg callback to list conn
 * info for running threads.
 */

typedef struct ConnData {
    struct ConnData *nextPtr;
    Pool *poolPtr;
    Conn *connPtr;
    Ns_Thread thread;
} ConnData;

/*
 * Local functions defined in this file
 */

static void ConnRun(Conn *connPtr);	/* Connection run routine. */
static void AppendConnList(Tcl_DString *dsPtr, Conn *firstPtr, char *state);

/*
 * Static variables defined in this file.
 */

static Ns_Tls        ctdtls;
static Ns_Mutex	     connlock;
static Ns_Mutex	     joinlock;
static ConnData     *joinPtr;


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
    Ns_TlsAlloc(&ctdtls, NULL);
    Ns_MutexSetName(&connlock, "ns:connlock");
    Ns_MutexSetName(&joinlock, "ns:joinlock");
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
    ConnData *dataPtr;

    dataPtr = Ns_TlsGet(&ctdtls);
    return (dataPtr ? ((Ns_Conn *) dataPtr->connPtr) : NULL);
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
 *	Connection will run shortly.
 *
 *----------------------------------------------------------------------
 */

void
NsQueueConn(Conn *connPtr)
{
    Pool *poolPtr = NsGetConnPool(connPtr);
    int create = 0;

    /*
     * Queue connection.
     */

    connPtr->flags |= NS_CONN_RUNNING;
    Ns_MutexLock(&poolPtr->lock);
    ++poolPtr->threads.queued;
    if (poolPtr->queue.wait.firstPtr == NULL) {
        poolPtr->queue.wait.firstPtr = connPtr;
    } else {
        poolPtr->queue.wait.lastPtr->nextPtr = connPtr;
    }
    poolPtr->queue.wait.lastPtr = connPtr;
    connPtr->nextPtr = NULL;

    if (poolPtr->threads.waiting == 0
        && poolPtr->threads.current < poolPtr->threads.max) {
        /* 
           Create a new thread if no thread is waiting and the number
           of currently starting or running threads is below max.
        */
        create = 1;
    }

    poolPtr->queue.wait.num ++;
    
    if (create) {
        poolPtr->threads.current ++;
        Ns_MutexUnlock(&poolPtr->lock);
        NsCreateConnThread(poolPtr, 1);
    } else if (poolPtr->threads.waiting > 0) {
        /*
          There are threads waiting. Signal to process
          the request.
         */
        Ns_CondSignal(&poolPtr->cond);
        Ns_MutexUnlock(&poolPtr->lock);
    } else {
        /* 
           We might have missed signaling, since we are already using
           max resources, and no thread is available. In such a case
           the autorecovery at thread exist has to care to process
           the outstanding requests 
        */
        Ns_MutexUnlock(&poolPtr->lock);
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
    Pool *poolPtr;
    char buf[100], *pool;
    Tcl_DString ds;
    static CONST char *opts[] = {
	 "active", "all", "connections", "keepalive", "pools", "queued",
	 "threads", "waiting", NULL, 
    };
    enum {
	 SActiveIdx, SAllIdx, SConnectionsIdx, SKeepaliveIdx, SPoolsIdx,
	 SQueuedIdx, SThreadsIdx, SWaitingIdx,
    } _nsmayalias opt;

    if (objc != 2 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?pool?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    if (opt == SPoolsIdx) {
	return NsTclListPoolsObjCmd(arg, interp, objc, objv);
    }
    if (objc == 2) {
        pool = "default";
    } else {
	pool = Tcl_GetString(objv[2]);
    }
    if (NsTclGetPool(interp, pool, &poolPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_MutexLock(&poolPtr->lock);
    switch (opt) {
    case SPoolsIdx:
	/* NB: Silence compiler. */
	break;
	  
    case SWaitingIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(poolPtr->queue.wait.num));
	break;

    case SKeepaliveIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(0/*nsconf.keepalive.npending*/));
	break;

    case SConnectionsIdx:
        Tcl_SetObjResult(interp, Tcl_NewIntObj((int) poolPtr->threads.nextid));
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
    Ns_MutexUnlock(&poolPtr->lock);
    return TCL_OK;
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
 *	See NsAppendConn.
 *
 *----------------------------------------------------------------------
 */

void
NsConnArgProc(Tcl_DString *dsPtr, void *arg)
{
    ConnData *dataPtr = arg;
    
    Ns_MutexLock(&connlock);
    if (dataPtr->connPtr != NULL) {
        NsAppendConn(dsPtr, dataPtr->connPtr, "running");
    } else {
    	Tcl_DStringAppendElement(dsPtr, "");
    }
    Ns_MutexUnlock(&connlock);
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
    ConnData  	    *dataPtr = arg;
    Pool            *poolPtr = dataPtr->poolPtr;
    Conn            *connPtr;
    Ns_Time          wait, *timePtr;
    char             name[100];
    int              status, ncons;
    char            *msg;
    double           spread;
    
    /*
     * Set the conn thread name.
     */

    Ns_TlsSet(&ctdtls, dataPtr);
    Ns_MutexLock(&poolPtr->lock);
    sprintf(name, "-%s:%d-", poolPtr->name, poolPtr->threads.nextid++);
    Ns_MutexUnlock(&poolPtr->lock);
    Ns_ThreadSetName(name);

    /* spread is a value of 1.0 +- specified percentage, 
       i.e. between 0.0 and 2.0 when the configured percentage is 100 */
    spread = 1.0 + (2 * poolPtr->threads.spread * Ns_DRand() - poolPtr->threads.spread) / 100.0;

    ncons = round(poolPtr->threads.maxconns * spread);
    msg = "exceeded max connections per thread";
    
    /*
     * Start handling connections.
     */

    Ns_MutexLock(&poolPtr->lock);

    poolPtr->threads.starting--;
    poolPtr->threads.idle++;

    while (poolPtr->threads.maxconns <= 0 || ncons-- > 0) {

	/*
	 * Wait for a connection to arrive, exiting if one doesn't
	 * arrive in the configured timeout period.
	 */
        
	if (poolPtr->threads.current <= poolPtr->threads.min) {
	    timePtr = NULL;
	} else {
	    Ns_GetTime(&wait);
	    Ns_IncrTime(&wait, round(poolPtr->threads.timeout * spread), 0);
	    timePtr = &wait;
	}

        status = NS_OK;
        while (!poolPtr->shutdown
               && status == NS_OK
               && poolPtr->queue.wait.firstPtr == NULL) {
            /* 
               nothing is queued, we wait for a queue entry 
            */
            poolPtr->threads.waiting++;
            status = Ns_CondTimedWait(&poolPtr->cond, &poolPtr->lock, timePtr);
            poolPtr->threads.waiting--;
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

         Ns_MutexUnlock(&poolPtr->lock);

         /*
          * Run the connection.
          */

         Ns_MutexLock(&connlock);
         dataPtr->connPtr = connPtr;
         Ns_MutexUnlock(&connlock);
         
         Ns_GetTime(&connPtr->times.run);
         ConnRun(connPtr);
         Ns_MutexLock(&connlock);
         dataPtr->connPtr = NULL;
         Ns_MutexUnlock(&connlock);
         
         /*
          * Remove from the active list and push on the free list.
          */

         Ns_MutexLock(&poolPtr->lock);
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
         Ns_MutexUnlock(&poolPtr->lock);
         NsFreeConn(connPtr);
         Ns_MutexLock(&poolPtr->lock);
    }
    
    /*
     * Append this thread to list of threads to reap.
     */
    
    Ns_MutexLock(&joinlock);
    dataPtr->nextPtr = joinPtr;
    joinPtr = dataPtr;
    Ns_MutexUnlock(&joinlock);
    
    /*
     * Mark this thread as no longer active.
     */
    
    if (poolPtr->shutdown) {
        msg = "shutdown pending";
    }
    poolPtr->threads.current--;
    poolPtr->threads.idle--;
    
    if (((poolPtr->queue.wait.num > 0 
          && poolPtr->threads.idle == 0 
          && poolPtr->threads.starting == 0
          )
         || (poolPtr->threads.current < poolPtr->threads.min)
         ) && !poolPtr->shutdown) {
        /* 
           Recreate a thread when on of the condings hold
           - there are more queue entries are still waiting, 
           but no thread is either starting or idle, or
           - there are less than minthreads connection threads alive.
        */
        poolPtr->threads.current ++;
        Ns_MutexUnlock(&poolPtr->lock);
        NsCreateConnThread(poolPtr, 0); /* joinThreads == 0 to avoid deadlock */
    } else if (poolPtr->queue.wait.num > 0 && poolPtr->threads.waiting > 0) {
        /*
          Wake up a waiting thread
        */
        Ns_CondSignal(&poolPtr->cond);
        Ns_MutexUnlock(&poolPtr->lock);
    } else {
        Ns_MutexUnlock(&poolPtr->lock);
    }
    
    Ns_Log(Notice, "exiting: %s", msg);
    Ns_ThreadExit(dataPtr);
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
    Tcl_Encoding    encoding = NULL;
    Ns_Conn 	  *conn = (Ns_Conn *) connPtr;
    NsServer	  *servPtr = connPtr->servPtr;
    int		   i, status;
	
    /*
     * Initialize the connection encodings. 
     */
    
    encoding = NsGetInputEncoding(connPtr);
    if (encoding == NULL) {
    	encoding = NsGetOutputEncoding(connPtr);
	if (encoding == NULL) {
	    encoding = connPtr->servPtr->urlEncoding;
	}
    }
    Ns_ConnSetUrlEncoding((Ns_Conn *) connPtr, encoding);
    if (connPtr->servPtr->opts.hdrcase != Preserve) {
	for (i = 0; i < Ns_SetSize(connPtr->headers); ++i) {
    	    if (connPtr->servPtr->opts.hdrcase == ToLower) {
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
     * Cleanup the connections, calling any registered cleanup traces
     * followed by free the connection interp if it was used.
     */

    NsRunCleanups(conn);
    NsFreeConnInterp(connPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsCreateConnThread --
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

void
NsCreateConnThread(Pool *poolPtr, int joinThreads)
{
    ConnData *dataPtr;

    /*
     * Reap any dead threads.
     */

    if (joinThreads) {
        NsJoinConnThreads();
    }

    /*
     * Create a new connection thread.
     */

    dataPtr = ns_malloc(sizeof(ConnData));
    dataPtr->poolPtr = poolPtr;
    dataPtr->connPtr = NULL;
    Ns_MutexLock(&poolPtr->lock);
    poolPtr->threads.starting ++;
    Ns_MutexUnlock(&poolPtr->lock);
    Ns_ThreadCreate(NsConnThread, dataPtr, 0, &dataPtr->thread);
}
 

/*
 *----------------------------------------------------------------------
 *
 * NsJoinConnThreads --
 *
 *	Join any connection threads which have exited.
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
NsJoinConnThreads(void)
{
    ConnData *firstPtr;
    void *arg;

    Ns_MutexLock(&joinlock);
    firstPtr = joinPtr;
    joinPtr = NULL;
    Ns_MutexUnlock(&joinlock);
    while (firstPtr != NULL) {
	Ns_ThreadJoin(&firstPtr->thread, &arg);
	firstPtr = firstPtr->nextPtr;
	ns_free(arg);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAppendConn --
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

void
NsAppendConn(Tcl_DString *dsPtr, Conn *connPtr, char *state)
{
    Ns_Time now, diff;

    Ns_GetTime(&now);
    Ns_DiffTime(&now, &connPtr->times.queue, &diff);
    Tcl_DStringStartSublist(dsPtr);
    Ns_DStringPrintf(dsPtr, "%d", connPtr->id);
    Tcl_DStringAppendElement(dsPtr, Ns_ConnPeer((Ns_Conn *) connPtr));
    Tcl_DStringAppendElement(dsPtr, state);
    NsAppendRequest(dsPtr, connPtr->request);
    Ns_DStringPrintf(dsPtr, " %ld.%ld %d",
		     diff.sec, diff.usec, connPtr->nContentSent);
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
	NsAppendConn(dsPtr, firstPtr, state);
	firstPtr = firstPtr->nextPtr;
    }
}

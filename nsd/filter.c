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
 * filter.c --
 *
 * Support for connection filters, traces, and cleanups.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/filter.c,v 1.10 2002/09/28 19:23:39 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following stuctures maintain connection filters
 * and traces.       
 */

typedef struct Filter {
    struct Filter *nextPtr;
    Ns_FilterProc *proc;
    char          *method;
    char          *url;
    int            when;
    void          *arg;
} Filter;

typedef struct Trace {
    struct Trace    *nextPtr;
    Ns_TraceProc    *proc;
    void            *arg;
} Trace;

static Trace *NewTrace(Ns_TraceProc *proc, void *arg);
static void RunTraces(Ns_Conn *conn, Trace *firstPtr);
static void *RegisterCleanup(NsServer *servPtr, Ns_TraceProc *proc,
			     void *arg);


/*
 *----------------------------------------------------------------------
 * Ns_RegisterFilter --
 *
 *      Register a filter function to handle a method/URL combination.
 *
 * Results:
 *      Returns a pointer to an opaque object that contains the filter
 *	information.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterFilter(char *server, char *method, char *url,
    Ns_FilterProc *proc, int when, void *arg)
{
    NsServer *servPtr = NsGetServer(server);
    Filter *fPtr, **fPtrPtr;

    if (servPtr == NULL) {
	return NULL;
    }
    fPtr = ns_malloc(sizeof(Filter));
    fPtr->proc = proc;
    fPtr->method = ns_strdup(method);
    fPtr->url = ns_strdup(url);
    fPtr->when = when;
    fPtr->arg = arg;
    fPtr->nextPtr = NULL;
    fPtrPtr = &servPtr->filter.firstFilterPtr;
    while (*fPtrPtr != NULL) {
    	fPtrPtr = &((*fPtrPtr)->nextPtr);
    }
    *fPtrPtr = fPtr;
    return (void *) fPtr;
}


/*
 *----------------------------------------------------------------------
 * NsRunFilters --
 *
 *      Execute each registered filter function in the Filter list.
 *
 * Results:
 *      Returns the status returned from the registered filter function.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsRunFilters(Ns_Conn *conn, int why)
{
    Conn *connPtr = (Conn *) conn;
    Filter *fPtr;
    char *method, *url;
    int status;

    status = NS_OK;
    if (conn->request != NULL) {
	method = conn->request->method;
	url = conn->request->url;
	fPtr = connPtr->servPtr->filter.firstFilterPtr;
	while (fPtr != NULL && status == NS_OK) {
	    if ((fPtr->when & why)
		&& Tcl_StringMatch(method, fPtr->method)
		&& Tcl_StringMatch(url, fPtr->url)) {
		status = (*fPtr->proc)(fPtr->arg, conn, why);
	    }
	    fPtr = fPtr->nextPtr;
	}
	if (status == NS_FILTER_BREAK ||
	    (why == NS_FILTER_TRACE && status == NS_FILTER_RETURN)) {
	    status = NS_OK;
	}
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 * Ns_RegisterServerTrace --
 *
 *      Register a connection trace procedure.  Traces registered
 *  	with this procedure are only called in FIFO order if the
 *  	connection request procedure successfully responds to the
 *  	clients request.
 *
 * Results:
 *	Pointer to trace.
 *
 * Side effects:
 *      Proc will be called in FIFO order at end of successfull
 *  	connections.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterServerTrace(char *server, Ns_TraceProc * proc, void *arg)
{
    NsServer *servPtr = NsGetServer(server);
    Trace *tracePtr, **tPtrPtr;

    if (servPtr == NULL) {
	return NULL;
    }
    tracePtr = NewTrace(proc, arg);
    tPtrPtr = &servPtr->filter.firstTracePtr;
    while (*tPtrPtr != NULL) {
    	tPtrPtr = &((*tPtrPtr)->nextPtr);
    }
    *tPtrPtr = tracePtr;
    tracePtr->nextPtr = NULL;
    return (void *) tracePtr;
}


/*
 *----------------------------------------------------------------------
 * Ns_RegisterCleanup, Ns_RegisterConnCleanup --
 *
 *      Register a connection cleanup trace procedure.  Traces
 *  	registered with this procedure are always called in LIFO
 *  	order at the end of connection no matter the result code
 *  	from the connection's request procedure (i.e., the procs
 *  	are called even if the client drops connection).
 *
 * Results:
 *	Pointer to trace.
 *
 * Side effects:
 *      Proc will be called in LIFO order at end of all connections.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterConnCleanup(char *server, Ns_TraceProc *proc, void *arg)
{
    NsServer *servPtr = NsGetServer(server);

    return RegisterCleanup(servPtr, proc, arg);
}

void *
Ns_RegisterCleanup(Ns_TraceProc *proc, void *arg)
{
    NsServer *servPtr = NsGetInitServer();

    return RegisterCleanup(servPtr, proc, arg);
}

static void *
RegisterCleanup(NsServer *servPtr, Ns_TraceProc *proc, void *arg)
{
    Trace *tracePtr;

    if (servPtr == NULL) {
	return NULL;
    }
    tracePtr = NewTrace(proc, arg);
    tracePtr->nextPtr = servPtr->filter.firstCleanupPtr;
    servPtr->filter.firstCleanupPtr = tracePtr;
    return (void *) tracePtr;
}


/*
 *----------------------------------------------------------------------
 * RunTraces, NsRunTraces, NsRunCleanups --
 *
 *      Execute each registered trace.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Depends on registered traces, if any.
 *
 *----------------------------------------------------------------------
 */

void
NsRunTraces(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    RunTraces(conn, connPtr->servPtr->filter.firstTracePtr);
}

void
NsRunCleanups(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    RunTraces(conn, connPtr->servPtr->filter.firstCleanupPtr);
}

static void
RunTraces(Ns_Conn *conn, Trace *tracePtr)
{
    while (tracePtr != NULL) {
    	(*tracePtr->proc)(tracePtr->arg, conn);
	tracePtr = tracePtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 * NewTrace --
 *
 *      Create a new trace object to be added to the cleanup or
 *	trace list.
 *
 * Results:
 *      ns_malloc'ed trace structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Trace *
NewTrace(Ns_TraceProc *proc, void *arg)
{
    Trace *tracePtr;

    tracePtr = ns_malloc(sizeof(Trace));
    tracePtr->proc = proc;
    tracePtr->arg = arg;
    return tracePtr;
}

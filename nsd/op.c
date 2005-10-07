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
 * op.c --
 *
 *	Routines to register, unregister, and run connection request
 *  	routines (previously known as "op procs").
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/op.c,v 1.15 2005/10/07 00:48:23 dossy Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define MAX_RECURSION 3       /* Max return direct recursion limit. */


/*
 * The following structure defines a request procedure including user
 * routine and client data.
 */

typedef struct {
    int		    refcnt;
    Ns_OpProc      *proc;
    Ns_Callback    *delete;
    void           *arg;
    unsigned int    flags;
} Req;

/*
 * Static functions defined in this file.
 */

static void FreeReq(void *arg);

/*
 * Static variables defined in this file.
 */

static Ns_Mutex	      ulock;
static int            uid;


/*
 *----------------------------------------------------------------------
 *
 * NsInitRequests --
 *
 *	Initialize the request API.
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
NsInitRequests(void)
{
    uid = Ns_UrlSpecificAlloc();
    Ns_MutexInit(&ulock);
    Ns_MutexSetName(&ulock, "nsd:requests");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterRequest --
 *
 *	Register a new procedure to be called to service matching
 *  	given method and url pattern.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Delete procedure of previously registered request, if any,
 *  	will be called unless NS_OP_NODELETE flag is set.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterRequest(char *server, char *method, char *url, Ns_OpProc *proc,
    Ns_Callback *delete, void *arg, int flags)
{
    Req *reqPtr;

    reqPtr = ns_malloc(sizeof(Req));
    reqPtr->proc = proc;
    reqPtr->delete = delete;
    reqPtr->arg = arg;
    reqPtr->flags = flags;
    reqPtr->refcnt = 1;
    Ns_MutexLock(&ulock);
    Ns_UrlSpecificSet(server, method, url, uid, reqPtr, flags, FreeReq);
    Ns_MutexUnlock(&ulock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetRequest --
 *
 *	Return the procedures and context for a given method and url
 *  	pattern.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	
 *
 *----------------------------------------------------------------------
 */

void
Ns_GetRequest(char *server, char *method, char *url, Ns_OpProc **procPtr,
    Ns_Callback **deletePtr, void **argPtr, int *flagsPtr)
{
    Req *reqPtr;

    Ns_MutexLock(&ulock);
    reqPtr = Ns_UrlSpecificGet(server, method, url, uid);
    if (reqPtr != NULL) {
        *procPtr = reqPtr->proc;
        *deletePtr = reqPtr->delete;
        *argPtr = reqPtr->arg;
        *flagsPtr = reqPtr->flags;
    } else {
	*procPtr = NULL;
	*deletePtr = NULL;
	*argPtr = NULL;
	*flagsPtr = 0;
    }
    Ns_MutexUnlock(&ulock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UnRegisterRequest --
 *
 *	Remove the procedure which would run for the given method and
 *  	url pattern.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Requests deleteProc may run.
 *
 *----------------------------------------------------------------------
 */

void
Ns_UnRegisterRequest(char *server, char *method, char *url, int inherit)
{
    Ns_MutexLock(&ulock);
    Ns_UrlSpecificDestroy(server, method, url, uid,
    			  inherit ? 0 : NS_OP_NOINHERIT);
    Ns_MutexUnlock(&ulock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnRunRequest --
 *
 *	Locate and execute the procedure for the given method and
 *  	url pattern.
 *
 * Results:
 *	Standard request procedure result, normally NS_OK.
 *
 * Side effects:
 *  	Depends on request procedure.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnRunRequest(Ns_Conn *conn)
{
    Req *reqPtr;
    Conn *connPtr = (Conn *) conn;
    int  status;
    char *server = Ns_ConnServer(conn);

    /*
     * Return a quick unavailable error on overflow.
     */

    if (connPtr->flags & NS_CONN_OVERFLOW) {
        return Ns_ConnReturnServiceUnavailable(conn);
    }

    /*
     * Prevent infinite internal redirect loops.
     */

    if (connPtr->recursionCount > MAX_RECURSION) {
        Ns_Log(Error, "return: failed to redirect '%s %s': "
               "exceeded recursion limit of %d",
               conn->request->method, conn->request->url, MAX_RECURSION);
        return Ns_ConnReturnInternalError(conn);
    }

    Ns_MutexLock(&ulock);
    reqPtr = Ns_UrlSpecificGet(server, conn->request->method,
    	    	    	       conn->request->url, uid);
    if (reqPtr == NULL) {
    	Ns_MutexUnlock(&ulock);
        return Ns_ConnReturnNotFound(conn);
    }
    ++reqPtr->refcnt;
    Ns_MutexUnlock(&ulock);
    status = (*reqPtr->proc) (reqPtr->arg, conn);
    Ns_MutexLock(&ulock);
    FreeReq(reqPtr);
    Ns_MutexUnlock(&ulock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnRedirect --
 *
 *	Perform an internal redirect by updating the connection's
 *  	request URL and re-authorizing and running the request.  This
 *  	Routine is used in FastPath to redirect to directory files
 *  	(e.g., index.html) and in return.c to redirect by HTTP result
 *  	code (e.g., custom not-found handler).
 *
 * Results:
 *	Standard request procedure result, normally NS_OK.
 *
 * Side effects:
 *  	Depends on request procedure.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnRedirect(Ns_Conn *conn, char *url)
{
    Conn *connPtr = (Conn *) conn;
    int status;

    ++connPtr->recursionCount;

    /*
     * Update the request URL.
     */
   
    Ns_SetRequestUrl(conn->request, url);

    /*
     * Re-authorize and run the request.
     */

    status = Ns_AuthorizeRequest(Ns_ConnServer(conn), conn->request->method,
				 conn->request->url, conn->authUser,
	 			 conn->authPasswd, Ns_ConnPeer(conn));
    switch (status) {
    case NS_OK:
        status = Ns_ConnRunRequest(conn);
        break;
    case NS_FORBIDDEN:
        status = Ns_ConnReturnForbidden(conn);
        break;
    case NS_UNAUTHORIZED:
        status = Ns_ConnReturnUnauthorized(conn);
        break;
    case NS_ERROR:
    default:
        status = Ns_ConnReturnInternalError(conn);
        break;
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterProxyRequest --
 *
 *	Register a new procedure to be called to proxy matching
 *  	given method and protocol pattern.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Delete procedure of previously registered request, if any.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterProxyRequest(char *server, char *method, char *protocol,
    Ns_OpProc *proc, Ns_Callback *delete, void *arg)
{
    NsServer	*servPtr;
    Req		*reqPtr;
    Ns_DString   ds;
    int 	 new;
    Tcl_HashEntry *hPtr;

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
	Ns_Log(Error, "Ns_RegisterProxyRequest: no such server: %s", server);
	return;
    }
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, method, protocol, NULL);
    reqPtr = ns_malloc(sizeof(Req));
    reqPtr->refcnt = 1;
    reqPtr->proc = proc;
    reqPtr->delete = delete;
    reqPtr->arg = arg;
    reqPtr->flags = 0;
    Ns_MutexLock(&servPtr->request.plock);
    hPtr = Tcl_CreateHashEntry(&servPtr->request.proxy, ds.string, &new);
    if (!new) {
	FreeReq(Tcl_GetHashValue(hPtr));
    }
    Tcl_SetHashValue(hPtr, reqPtr);
    Ns_MutexUnlock(&servPtr->request.plock);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_UnRegisterProxyRequest --
 *
 *	Remove the procedure which would run for the given method and
 *  	protocol.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Request's deleteProc may run.
 *
 *----------------------------------------------------------------------
 */

void
Ns_UnRegisterProxyRequest(char *server, char *method, char *protocol)
{
    NsServer	  *servPtr;
    Ns_DString 	   ds;
    Tcl_HashEntry *hPtr;

    servPtr = NsGetServer(server);
    if (servPtr != NULL) {
    	Ns_DStringInit(&ds);
    	Ns_DStringVarAppend(&ds, method, protocol, NULL);
    	Ns_MutexLock(&servPtr->request.plock);
    	hPtr = Tcl_FindHashEntry(&servPtr->request.proxy, ds.string);
    	if (hPtr != NULL) {
	    FreeReq(Tcl_GetHashValue(hPtr));
    	    Tcl_DeleteHashEntry(hPtr);
	}
    	Ns_MutexUnlock(&servPtr->request.plock);
	Ns_DStringFree(&ds);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnRunProxyRequest --
 *
 *	Locate and execute the procedure for the given method and
 *  	protocol pattern.
 *
 * Results:
 *	Standard request procedure result, normally NS_OK.
 *
 * Side effects:
 *  	Depends on request procedure.
 *
 *----------------------------------------------------------------------
 */

int
NsConnRunProxyRequest(Ns_Conn *conn)
{
    Conn	  *connPtr = (Conn *) conn;
    NsServer	  *servPtr = connPtr->servPtr;
    Ns_Request    *request = conn->request;
    Req		  *reqPtr = NULL;
    int		   status;
    Ns_DString	   ds;
    Tcl_HashEntry *hPtr;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, request->method, request->protocol, NULL);
    Ns_MutexLock(&servPtr->request.plock);
    hPtr = Tcl_FindHashEntry(&servPtr->request.proxy, ds.string);
    if (hPtr != NULL) {
	reqPtr = Tcl_GetHashValue(hPtr);
	++reqPtr->refcnt;
    }
    Ns_MutexUnlock(&servPtr->request.plock);
    if (reqPtr == NULL) {
	status = Ns_ConnReturnNotFound(conn);
    } else {
	status = (*reqPtr->proc) (reqPtr->arg, conn);
    	Ns_MutexLock(&servPtr->request.plock);
	FreeReq(reqPtr);
    	Ns_MutexUnlock(&servPtr->request.plock);
    }
    Ns_DStringFree(&ds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeReq --
 *
 *  	URL space callback to delete a request structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Depends on request delete procedure.
 *
 *----------------------------------------------------------------------
 */

static void
FreeReq(void *arg)
{
    Req *reqPtr = (Req *) arg;

    if (--reqPtr->refcnt == 0) {
    	if (reqPtr->delete != NULL) {
	    (*reqPtr->delete) (reqPtr->arg);
        }
        ns_free(reqPtr);
    }
}


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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/op.c,v 1.9 2001/12/18 22:32:17 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

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

static Ns_Mutex	      lock;
static int            reqId = -1;


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
    Ns_MutexLock(&lock);
    if (reqId < 0) {
	reqId = Ns_UrlSpecificAlloc();
	Ns_MutexSetName(&lock, "ns:request");
    }
    Ns_UrlSpecificSet(server, method, url, reqId, reqPtr, flags, FreeReq);
    Ns_MutexUnlock(&lock);
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

    Ns_MutexLock(&lock);
    reqPtr = (Req *) Ns_UrlSpecificGet(server, method, url, reqId);
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
    Ns_MutexUnlock(&lock);
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
    Ns_MutexLock(&lock);
    Ns_UrlSpecificDestroy(server, method, url, reqId,
    	inherit ? 0 : NS_OP_NOINHERIT);
    Ns_MutexUnlock(&lock);
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
    int  status;
    char *server = Ns_ConnServer(conn);

    Ns_MutexLock(&lock);
    reqPtr = (Req *) Ns_UrlSpecificGet(server, conn->request->method,
    	    	    	    	       conn->request->url, reqId);
    if (reqPtr == NULL) {
	status = Ns_ConnReturnNotFound(conn);
    } else {
	++reqPtr->refcnt;
	Ns_MutexUnlock(&lock);
    	status = (*reqPtr->proc) (reqPtr->arg, conn);
	Ns_MutexLock(&lock);
	FreeReq(reqPtr);
    }
    Ns_MutexUnlock(&lock);
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
    int status;

    /*
     * Update the request URL.
     */
   
    Ns_SetRequestUrl(conn->request, url);

    /*
     * Re-authorize and run the request.
     */

    status = Ns_AuthorizeRequest(Ns_ConnServer(conn),
	conn->request->method, conn->request->url, conn->authUser,
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
    NsServer   *servPtr = NsGetServer(server);
    Req *reqPtr;
    Ns_DString  ds;
    Tcl_HashEntry *hPtr;
    int 	new;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, method, protocol, NULL);
    hPtr = Tcl_CreateHashEntry(&servPtr->request.proxy, ds.string, &new);
    if (!new) {
	reqPtr = (Req *) Tcl_GetHashValue(hPtr);
	FreeReq(reqPtr);
    }
    reqPtr = (Req *) ns_malloc(sizeof(Req));
    reqPtr->refcnt = 1;
    reqPtr->proc = proc;
    reqPtr->delete = delete;
    reqPtr->arg = arg;
    reqPtr->flags = 0;
    Tcl_SetHashValue(hPtr, reqPtr);
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
    NsServer	  *servPtr = NsGetServer(server);
    Req		  *reqPtr;
    Ns_DString 	   ds;
    Tcl_HashEntry *hPtr;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, method, protocol, NULL);
    hPtr = Tcl_FindHashEntry(&servPtr->request.proxy, ds.string);
    if (hPtr != NULL) {
	reqPtr = (Req *) Tcl_GetHashValue(hPtr);
    	FreeReq(reqPtr);
    	Tcl_DeleteHashEntry(hPtr);
    }
    Ns_DStringFree(&ds);
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
    Req		  *reqPtr;
    Ns_DString 	   ds;
    Tcl_HashEntry *hPtr;
    int		   status;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, request->method, request->protocol, NULL);
    hPtr = Tcl_FindHashEntry(&servPtr->request.proxy, ds.string);
    if (hPtr != NULL) {
	reqPtr = (Req *) Tcl_GetHashValue(hPtr);
	status = (*reqPtr->proc) (reqPtr->arg, conn);
    } else {
	status = Ns_ConnReturnNotFound(conn);
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

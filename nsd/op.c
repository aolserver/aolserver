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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/op.c,v 1.5 2001/01/15 18:53:16 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a request procedure including user
 * routine and client data.
 */

typedef struct {
    Ns_OpProc      *procPtr;
    Ns_Callback    *deleteProcPtr;
    void           *arg;
    unsigned int    flags;
} Req;

/*
 * Static functions defined in this file.
 */

static void DeleteReq(void *arg);

/*
 * Static variables defined in this file.
 */

static int            reqId = -1;
static Tcl_HashTable  proxyReqHash;


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
Ns_RegisterRequest(char *server, char *method, char *url, Ns_OpProc *procPtr,
    Ns_Callback *deleteProcPtr, void *arg, int flags)
{
    Req *reqPtr;

    reqPtr = ns_malloc(sizeof(Req));
    reqPtr->procPtr = procPtr;
    reqPtr->deleteProcPtr = deleteProcPtr;
    reqPtr->arg = arg;
    reqPtr->flags = flags;
    if (reqId < 0) {
	reqId = Ns_UrlSpecificAlloc();
    }

    Ns_UrlSpecificSet(nsServer, method, url, reqId, reqPtr, flags, DeleteReq);
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
Ns_GetRequest(char *ignored, char *method, char *url, Ns_OpProc **procPtrPtr,
    Ns_Callback **deleteProcPtrPtr, void **argPtr, int *flagsPtr)
{
    Req *reqPtr;

    reqPtr = (Req *) Ns_UrlSpecificGet(nsServer, method, url, reqId);
    if (reqPtr != NULL) {
        *procPtrPtr = reqPtr->procPtr;
        *deleteProcPtrPtr = reqPtr->deleteProcPtr;
        *argPtr = reqPtr->arg;
        *flagsPtr = reqPtr->flags;
    } else {
	*procPtrPtr = NULL;
	*deleteProcPtrPtr = NULL;
	*argPtr = NULL;
	*flagsPtr = 0;
    }
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
    Ns_UrlSpecificDestroy(nsServer, method, url, reqId,
    	inherit ? 0 : NS_OP_NOINHERIT);
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

    reqPtr = (Req *) Ns_UrlSpecificGet(nsServer, conn->request->method,
    	    	    	    	       conn->request->url, reqId);
    if (reqPtr == NULL) {
	status = Ns_ConnReturnNotFound(conn);
    } else {
    	status = (*reqPtr->procPtr) (reqPtr->arg, conn);
    }
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

    status = Ns_AuthorizeRequest(NULL, conn->request->method,
    	    	    	         conn->request->url, conn->authUser,
				 conn->authPasswd, Ns_ConnPeer(conn));
    switch (status) {
    case NS_OK:
        status = Ns_ConnRunRequest(conn);
        break;
    case NS_FORBIDDEN:
        status = Ns_ConnFlushContent(conn);
        if (status == NS_OK) {
            status = Ns_ConnReturnForbidden(conn);
        }
        break;
    case NS_UNAUTHORIZED:
        status = Ns_ConnFlushContent(conn);
        if (status == NS_OK) {
            status = Ns_ConnReturnUnauthorized(conn);
        }
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
    Ns_OpProc *procPtr, Ns_Callback *deleteProcPtr, void *arg)
{
    Ns_DString  ds;
    Tcl_HashEntry *hPtr;
    int 	new;

    Ns_DStringInit(&ds);

    if (Ns_InfoStarted()) {
	Ns_Log(Error, "op: failed to register proxy request: "
	       "cannot do so after server startup");
	goto done;
    }

    Ns_DStringVarAppend(&ds, method, protocol, NULL);
    hPtr = Tcl_CreateHashEntry(&proxyReqHash, Ns_DStringValue(&ds),
	&new);
    if (new) {
	Req *reqPtr;

	reqPtr = (Req *) ns_malloc(sizeof(Req));
	reqPtr->procPtr = procPtr;
	reqPtr->deleteProcPtr = deleteProcPtr;
	reqPtr->arg = arg;
	reqPtr->flags = 0;
	Tcl_SetHashValue(hPtr, reqPtr);
    }
    else {
	Ns_Log(Error, "op: failed to register proxy request: "
	       "a proxy request is already registered "
	       "for method '%s', protocol '%s'", method, protocol);
    }

    if (new) {
	Ns_Log(Debug, "op: proxy request registered for method: %s, protocol: %s",
	       method, protocol);
    }
    
 done:
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
    Req		  *reqPtr;
    Ns_DString 	   ds;
    Tcl_HashEntry *hPtr;

    Ns_DStringInit(&ds);

    if (Ns_InfoShutdownPending() == NS_FALSE) {
	Ns_Log(Error, "op: failed to unregister proxy request: "
	       "can not do so before server shutdown");
	goto done;
    }

    Ns_DStringVarAppend(&ds, method, protocol, NULL);
    hPtr = Tcl_FindHashEntry(&proxyReqHash, Ns_DStringValue(&ds));
    if (hPtr != NULL) {
	reqPtr = (Req *) Tcl_GetHashValue(hPtr);
        assert (reqPtr != NULL);
    } else {
	Ns_Log(Error, "op: "
	       "failed to unregister proxy request for method '%s', protocol '%s'",
	       method, protocol);
	goto done;
    }

    DeleteReq(reqPtr);
    Tcl_DeleteHashEntry(hPtr);
    Ns_Log(Debug, "op: proxy request unregistered for method '%s', protocol '%s'",
	   method, protocol);
done:
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitProxyRequests --
 *
 *	This routine initializes the Tcl hash for proxy requests.
 *
 *----------------------------------------------------------------------
 */

void NsInitProxyRequests(void)
{
    Tcl_InitHashTable(&proxyReqHash, TCL_STRING_KEYS);
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
    Req		  *reqPtr;
    Ns_DString 	   ds;
    Tcl_HashEntry *hPtr;
    int		   status;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, conn->request->method, conn->request->protocol,
			NULL);
    hPtr = Tcl_FindHashEntry(&proxyReqHash, Ns_DStringValue(&ds));
    if (hPtr != NULL) {
	reqPtr = (Req *) Tcl_GetHashValue(hPtr);
        assert (reqPtr != NULL);
	status = (*reqPtr->procPtr) (reqPtr->arg, conn);
    } else {
	status = Ns_ConnReturnNotFound(conn);
    }

    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DeleteReq --
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
DeleteReq(void *arg)
{
    Req *reqPtr = (Req *) arg;

    if (reqPtr->deleteProcPtr != NULL) {
	(*reqPtr->deleteProcPtr) (reqPtr->arg);
    }
    ns_free(reqPtr);
}

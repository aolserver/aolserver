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
 * auth.c --
 *
 *	URL level HTTP authorization support.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/auth.c,v 1.7 2002/06/12 23:08:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_AuthorizeRequest --
 *
 *	Check for proper HTTP authorization of a request.
 *
 * Results:
 *	User supplied routine is expected to return NS_OK if authorization
 *	is allowed, NS_UNAUTHORIZED if a correct username/passwd could
 *	allow authorization, NS_FORBIDDEN if no username/passwd would ever
 *	allow access, or NS_ERROR on error.
 *
 * Side effects:
 *	Depends on user supplied routine.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AuthorizeRequest(char *server, char *method, char *url,
	char *user, char *passwd, char *peer)
{
    NsServer *servPtr = NsGetServer(server);

    if (servPtr == NULL || servPtr->request.authProc == NULL) {
    	return NS_OK;
    }
    return (*servPtr->request.authProc)(server, method, url, user, passwd, peer);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SetRequestAuthorizeProc --
 *
 *	Set the proc to call when authorizing requests.
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
Ns_SetRequestAuthorizeProc(char *server, Ns_RequestAuthorizeProc *proc)
{
    NsServer *servPtr = NsGetServer(server);

    if (servPtr != NULL) {
	servPtr->request.authProc = proc;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRequestAuthorizeCmd --
 *
 *	Implments ns_requestauthorize. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRequestAuthorizeCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    NsInterp	   *itPtr = arg;
    int             status;

    if ((argc != 5) && (argc != 6)) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " method url authuser authpasswd [ipaddr]\"",
			 (char *) NULL);
        return TCL_ERROR;
    }
    status = Ns_AuthorizeRequest(itPtr->servPtr->server, argv[1], argv[2],
	argv[3], argv[4], argv[5]);
    switch (status) {
    case NS_OK:
        Tcl_SetResult(interp, "OK", TCL_STATIC);
        break;
	
    case NS_ERROR:
        Tcl_SetResult(interp, "ERROR", TCL_STATIC);
        break;
	
    case NS_FORBIDDEN:
        Tcl_SetResult(interp, "FORBIDDEN", TCL_STATIC);
        break;
	
    case NS_UNAUTHORIZED:
        Tcl_SetResult(interp, "UNAUTHORIZED", TCL_STATIC);
        break;
	
    default:
        Tcl_AppendResult(interp, "Could not check ", argv[2],
                         " permission of URL ", argv[1], NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRequestAuthorizeObjCmd --
 *
 *	Implments ns_requestauthorize as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRequestAuthorizeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp   *itPtr = arg;
    int         status;
    Tcl_Obj    *result;

    if ((objc != 5) && (objc != 6)) {
        Tcl_WrongNumArgs(interp, 1, objv, 
			"method url authuser authpasswd [ipaddr]");
        return TCL_ERROR;
    }

    status = Ns_AuthorizeRequest(itPtr->servPtr->server, 
			Tcl_GetString(objv[1]), 
			Tcl_GetString(objv[2]),
			Tcl_GetString(objv[3]), 
			Tcl_GetString(objv[4]), 
			Tcl_GetString(objv[5]));

    switch (status) {
	case NS_OK:
	    Tcl_SetResult(interp, "OK", TCL_STATIC);
	    break;
	
	case NS_ERROR:
	    Tcl_SetResult(interp, "ERROR", TCL_STATIC);
	    break;
		
	case NS_FORBIDDEN:
	    Tcl_SetResult(interp, "FORBIDDEN", TCL_STATIC);
	    break;
	
	case NS_UNAUTHORIZED:
	    Tcl_SetResult(interp, "UNAUTHORIZED", TCL_STATIC);
	    break;
	
	default:
	    result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "Could not check ", 
			Tcl_GetString(objv[2]),
			" permission of URL ", 
			Tcl_GetString(objv[1]), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
    }
    
    return TCL_OK;
}

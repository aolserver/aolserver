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
 * auth.c --
 *
 *	URL level HTTP authorization support.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/auth.c,v 1.2 2000/05/02 14:39:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following pointers specify the procs to call when authorizing
 * requests/users.  By default, no authorization is performed.
 */

static Ns_RequestAuthorizeProc *authProcPtr;
static Ns_UserAuthorizeProc    *userProcPtr; 

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


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
    if (authProcPtr == NULL) {
    	return NS_OK;
    }
    return (*authProcPtr)(server, method, url, user, passwd, peer);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AuthorizeUser --
 *
 *	Verify that a user's password matches his name.
 *	passwd is the unencrypted password.
 *
 * Results:
 *	NS_OK or NS_ERROR; if none registered, NS_ERROR.
 *
 * Side effects:
 *	Depends on the supplied routine.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AuthorizeUser(char *user, char *passwd)
{
    if (userProcPtr == NULL) {
	return NS_ERROR;
    }
    return (*userProcPtr)(user, passwd);
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
Ns_SetRequestAuthorizeProc(char *server, Ns_RequestAuthorizeProc *procPtr)
{
    authProcPtr = procPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SetUserAuthorizeProc --
 *
 *	Set the proc to call when authorizing users.
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
Ns_SetUserAuthorizeProc(Ns_UserAuthorizeProc *procPtr)
{
    userProcPtr = procPtr;
}

/* 
 *==========================================================================
 * Exported functions
 *==========================================================================
 */


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
NsTclRequestAuthorizeCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    int             status;

    if ((argc != 5) && (argc != 6)) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " method url authuser authpasswd [ipaddr]\"",
			 (char *) NULL);
        return TCL_ERROR;
    }
    if (argc == 5) {
        status = Ns_RequestAuthorize(Ns_TclInterpServer(interp),
                                     argv[1], argv[2], argv[3], argv[4], NULL);
    } else {
        status = Ns_RequestAuthorize(Ns_TclInterpServer(interp),
                                     argv[1], argv[2], argv[3], argv[4],
				     argv[5]);
    }
    
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


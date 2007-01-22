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
 * tclresp.c --
 *
 *	Tcl commands for returning data to the user agent. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclresp.c,v 1.21 2007/01/22 03:23:15 rmadilo Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int Result(Tcl_Interp *interp, int result);


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetConn --
 *
 *	Return current connection for interp.
 *
 * Results:
 *	TCL_OK if a connection is active, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Given connPtr will be set with conn if not NULL.
 *
 *----------------------------------------------------------------------
 */

int
NsTclGetConn(NsInterp *itPtr, Ns_Conn **connPtr)
{
    if (itPtr->conn == NULL) {
	Tcl_SetResult(itPtr->interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    if (connPtr != NULL) {
    	*connPtr = itPtr->conn;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHeadersObjCmd --
 *
 *	Spit out initial HTTP response; this is for backwards 
 *	compatibility only. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclHeadersObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
    int      status, len;
    Ns_Conn *conn;
    char    *type;

    if (objc < 3 || objc > 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "connid status ?type len?");
        return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc < 4) {
        type = NULL;
    } else {
        type = Tcl_GetString(objv[3]);
    }
    if (objc < 5) {
        len = 0;
    } else if (Tcl_GetIntFromObj(interp, objv[4], &len) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_ConnSetRequiredHeaders(conn, type, len);
    return Result(interp, Ns_ConnFlushHeaders(conn, status));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnObjCmd --
 *
 *	Implements ns_return as obj command. 
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
NsTclReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		  Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    int      status, result, len;
    char    *data, *type;

    if (objc != 4 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type string");
        return TCL_ERROR;
    }
    if (objc == 5 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-3], &status) != TCL_OK) {
	return TCL_ERROR;
    }
    data = Tcl_GetStringFromObj(objv[objc-1], &len);
    type = Tcl_GetString(objv[objc-2]);
    result = Ns_ConnReturnCharData(conn, status, data, len, type);
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRespondObjCmd --
 *
 *	Implements ns_respond as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See ns_respond. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRespondObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj *CONST objv[])
{
    int          status;
    char        *type;
    char        *string;
    char        *filename;
    Tcl_Channel	 chan;
    int          length;
    Ns_Conn     *conn;
    int		 retval;
    int          i;

    status = 200;
    type = NULL;
    string = NULL;
    filename = NULL;
    chan = NULL;
    length = -1;

    if (objc < 3) {
error:
        Tcl_WrongNumArgs(interp, 1, objv,
		"?-status status? ?-type type? { ?-string string?"
		" | ?-file filename? | ?-fileid fileid? }"
		" ?-length length? ?-headers setid?");
        return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Loop over every argument and set the appropriate options.
     */
    
    for (i = 0; i < objc; i++) {
	char *carg = Tcl_GetString(objv[i]);
        if (*carg != '-') {
	    continue;
	}
	if (++i >= objc) {
	    goto error;
	}

	if (STRIEQ(carg, "-status")) {
	    if (Tcl_GetIntFromObj(interp, objv[i], &status) != TCL_OK) {
		goto error;
	    }

	} else if (STRIEQ(carg, "-type")) {
	    type = Tcl_GetString(objv[i]);

	} else if (STRIEQ(carg, "-string")) {
	    string = Tcl_GetString(objv[i]);

	} else if (STRIEQ(carg, "-file")) {
	    filename = Tcl_GetString(objv[i]);

	} else if (STRIEQ(carg, "-fileid")) {
	    if (Ns_TclGetOpenChannel(interp, carg, 0, 1, &chan) != TCL_OK) {
		goto error;
	    }

	} else if (STRIEQ(carg, "-length")) {
	    if (Tcl_GetIntFromObj(interp, objv[i], &length) != TCL_OK) {
		goto error;
	    }

	} else if (STRIEQ(carg, "-headers")) {
	    Ns_Set         *set;

	    set = Ns_TclGetSet(interp, Tcl_GetString(objv[i]));
	    if (set == NULL) {
		Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
			"Illegal ns_set id: \"", Tcl_GetString(objv[i]), "\"",
			NULL);
		return TCL_ERROR;
	    }
	    Ns_ConnReplaceHeaders(conn, set);
        }
    }

    /*
     * Exactly one of chan, filename, string must be specified.
     */
    
    if ((chan != NULL) + (filename != NULL) + (string != NULL) != 1) {
	Tcl_SetResult(interp, "must specify only one of -string, -file, "
		"or -type", TCL_STATIC);
        return TCL_ERROR;
    }

    if (chan != NULL) {
	/*
	 * We'll be returning an open channel
	 */
	
        if (length < 0) {
	    Tcl_SetResult(interp, "length required when -fileid is used", 
		    TCL_STATIC);
	    return TCL_ERROR;
        }

	retval = Ns_ConnReturnOpenChannel(conn, status, type, chan, length);

    } else if (filename != NULL) {
	/*
	 * We'll be returning a file by name
	 */
	
        retval = Ns_ConnReturnFile(conn, status, type, filename);

    } else {
	/*
	 * We'll be returning a string now.
	 */

	retval = Ns_ConnReturnCharData(conn, status, string, length, type);
    }

    return Result(interp, retval);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFileObjCmd --
 *
 *	Return an open file. (ns_returnfile) 
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
NsTclReturnFileObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		      Tcl_Obj *CONST objv[])
{
    int      status;
    Ns_Conn *conn;
    char    *type, *file;

    if (objc != 4 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type file");
        return TCL_ERROR;
    }
    if (objc == 5 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-3], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    file = Tcl_GetString(objv[objc-1]);
    type = Tcl_GetString(objv[objc-2]);
    return Result(interp, Ns_ConnReturnFile(conn, status, type, file));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFpObjCmd --
 *
 *	Implements ns_returnfp. (actually accepts any open channel)
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
NsTclReturnFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj *CONST objv[])
{
    int          len, status, result;
    Tcl_Channel	 chan;
    Ns_Conn     *conn;
    char	*type;

    if (objc != 5 && objc != 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type fileId len");
        return TCL_ERROR;
    }
    if (objc == 6 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-4], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-1], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[objc-2]), 0, 1, &chan)
		!= TCL_OK) {
        return TCL_ERROR;
    }
    type = Tcl_GetString(objv[objc-3]);
    result = Ns_ConnReturnOpenChannel(conn, status, type, chan, len);
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnBadRequestObjCmd --
 *
 *	Implements ns_returnbadrequest as obj command. 
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
NsTclReturnBadRequestObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			    Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    char    *reason;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? reason");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    reason = Tcl_GetString(objv[objc-1]);
    return Result(interp, Ns_ConnReturnBadRequest(conn, reason));
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnObjCmd --
 * NsTclReturnNotFoundObjCmd --
 * NsTclReturnUnauthorizedObjCmd --
 * NsTclReturnForbiddenCmd --
 *
 *	Implement the ns_returnnotfound, ns_returnunauthorized, and
 *	ns_returnforbidden generic return commands.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will call proc that is clientdata. 
 *
 *----------------------------------------------------------------------
 */

static int
ReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc, 
		Tcl_Obj *CONST objv[], int (*proc) (Ns_Conn *))
{
    Ns_Conn *conn;

    if (objc != 1 && objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid?");
        return TCL_ERROR;
    }
    if (objc == 2 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, (*proc)(conn));
}

int
NsTclReturnNotFoundObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			  Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnNotFound);
}

int
NsTclReturnUnauthorizedObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			      Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnUnauthorized);
}

int
NsTclReturnForbiddenObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			   Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnForbidden);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnErrorObjCmd --
 *
 *	Implements ns_tclreturnerror as obj command.
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
NsTclReturnErrorObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj *CONST objv[])
{
    int      status, result;
    Ns_Conn *conn;
    char    *msg;

    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status message");
        return TCL_ERROR;
    }
    if (objc == 4 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-2], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    msg = Tcl_GetString(objv[objc-1]);
    result = Ns_ConnReturnAdminNotice(conn, status, "Request Error",  msg);
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnNoticeCmd --
 *
 *	Implements ns_returnnotice and ns_returnadminnotice commands.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
ReturnNoticeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int admin)
{
    int      status, result;
    Ns_Conn *conn;
    char    *longMessage = NULL;
    int      statusArg = 0, messageArg = 0, longMessageArg = 0;

    /* find the arguments.  There are 4 cases (in the order they're checked):
     *    0     1       2       3        4
     *   cmd  status  msg                       (3 args)
     *   cmd  connId  status  msg               (4 args)
     *   cmd  status  msg     longmsg           (4 args)
     *   cmd  connId  status  msg      longmsg  (5 args)
     */

    if (argc == 3) {
	statusArg = 1;
	messageArg = 2;

    } else if (argc == 4) {
	/* NB: Ignored old "c" arg. */
	if (argv[1][0] == 'c') {
	    statusArg = 2;
	    messageArg = 3;
	} else {
	    statusArg = 1;
	    messageArg = 2;
	    longMessageArg = 3;
	}

    } else if (argc == 5) {
	statusArg = 2;
	messageArg = 3;
	longMessageArg = 4;

    } else {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " status title ?message?\"", NULL);
        return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Get the status value
     */
    
    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
	return TCL_ERROR;
    }
    if (longMessageArg != 0) {
	longMessage = argv[longMessageArg];
    }
    if (admin) {
	result = Ns_ConnReturnAdminNotice(conn, status, argv[messageArg], longMessage);
    } else {
	result = Ns_ConnReturnNotice(conn, status, argv[messageArg], longMessage);
    }
    return Result(interp, result);
}

int
NsTclReturnNoticeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnNoticeCmd(arg, interp, argc, argv, 0);
}

int
NsTclReturnAdminNoticeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnNoticeCmd(arg, interp, argc, argv, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnRedirectObjCmd --
 *
 *	Implements ns_returnredirect as obj command. 
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
NsTclReturnRedirectObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			  Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    char    *location;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? location");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    location = Tcl_GetString(objv[objc-1]);
    return Result(interp, Ns_ConnReturnRedirect(conn, location));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInternalRedirectObjCmd --
 *
 *	Implements ns_internalredirect as obj command. 
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
NsTclInternalRedirectObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			  Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    char    *location;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? location");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    location = Tcl_GetString(objv[objc-1]);
    return Result(interp, Ns_ConnRedirect(conn, location));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteObjCmd --
 *
 *	Implements ns_write as obj command.
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
NsTclWriteObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		 Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    char    *bytes;
    int      length;
    int      result;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? string");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * ns_write will treat data it is given as binary, until
     * it is specifically given permission to do otherwise through
     * the WriteEncodedFlag on the current conn.  This flag is
     * manipulated via ns_startcontent or ns_conn write_encoded
     */

    if (Ns_ConnGetWriteEncodedFlag(conn)
	    && (Ns_ConnGetEncoding(conn) != NULL)) {
        bytes = Tcl_GetStringFromObj(objv[objc-1], &length);
        result = Ns_WriteCharConn(conn, bytes, length);
    } else {
        bytes = (char *) Tcl_GetByteArrayFromObj(objv[objc-1], &length);
        result = Ns_WriteConn(conn, bytes, length);
    }
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnSendFpObjCmd --
 *
 *	Implements ns_connsendfp as obj command.
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
NsTclConnSendFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		      Tcl_Obj *CONST objv[])
{
    Ns_Conn     *conn;
    Tcl_Channel	 chan;
    int          len;

    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? fp len");
        return TCL_ERROR;
    }
    if (objc == 4 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[objc-2]), 0, 1, &chan) 
			!= TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-1], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ConnSendChannel(conn, chan, len) != NS_OK) {
	Tcl_AppendResult(interp, "could not send ", 
		Tcl_GetString(objv[objc-1]), " bytes from channel ", 
		Tcl_GetString(objv[objc-2]), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Result --
 *
 *	Return a simple boolean result based on a connection response
 *	status code.
 *
 * Results:
 *	Always TCL_OK.
 *
 * Side effects:
 *	Will set interp result.
 *
 *----------------------------------------------------------------------
 */

static int
Result(Tcl_Interp *interp, int result)
{
    /* Tcl_SetBooleanObj(Tcl_GetObjResult(interp), result == NS_OK ? 1 : 0); */
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj((result == NS_OK ? 1 : 0)));
    return TCL_OK;
}

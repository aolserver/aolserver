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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclresp.c,v 1.15 2003/01/18 19:24:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int CheckId(Tcl_Interp *interp, char *id);
static int Result(Tcl_Interp *interp, int result);
static int GetConn(ClientData arg, Tcl_Interp *interp, Ns_Conn **connPtr);


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
NsTclHeadersObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status, len;
    Ns_Conn *conn;
    char    *type;

    if (objc < 3 || objc > 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "connid status ?type len?");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
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
NsTclReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
    int      status, result;

    if (objc != 4 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type string");
        return TCL_ERROR;
    }
    if (objc == 5 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-3], &status) != TCL_OK) {
	return TCL_ERROR;
    }
    result = Ns_ConnReturnData(conn, status, Tcl_GetString(objv[objc-1]), -1, 
	    Tcl_GetString(objv[objc-2]));
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
NsTclRespondObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int          status;
    char        *type;
    char        *string;
    char        *filename;
    Tcl_Channel	 chan;
    int          length;
    Ns_Conn     *conn;
    int          retval;
    int          i;

    status = 200;
    type = NULL;
    string = NULL;
    filename = NULL;
    chan = NULL;
    length = -1;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-status status? ?-type type? { ?-string string?"
			" | ?-file filename? | ?-fileid fileid? }"
			" ?-length length? ?-headers setid?");
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Loop over every argument and set the appropriate options.
     */
    
    retval = TCL_OK;
    for (i = 0; (i < objc) && (retval == TCL_OK); i++) {
		char *carg = Tcl_GetString(objv[i]);
        if (*carg == '-') {
            if (strcasecmp(carg, "-status") == 0) {
                if (!((++i < objc) &&
                        (Tcl_GetIntFromObj(interp, objv[i], &status) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(carg, "-type") == 0) {
                if (++i >= objc) {
                    retval = TCL_ERROR;
                } else {
                    type = Tcl_GetString(objv[i]);
                }

            } else if (strcasecmp(carg, "-string") == 0) {
                if (++i >= objc) {
                    retval = TCL_ERROR;
                } else {
                    string = Tcl_GetString(objv[i]);
                }

            } else if (strcasecmp(carg, "-file") == 0) {
                if (++i >= objc) {
                    retval = TCL_ERROR;
                } else {
                    filename = Tcl_GetString(objv[i]);
                }

            } else if (strcasecmp(carg, "-fileid") == 0) {
                if (!((++i < objc) &&
                        (Ns_TclGetOpenChannel(interp, carg, 0, 1,
					      &chan) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(carg, "-length") == 0) {
                if (!((++i < objc) &&
                        (Tcl_GetIntFromObj(interp, objv[i], &length) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(carg, "-headers") == 0) {
                if (++i >= objc) {
                    retval = TCL_ERROR;
                } else {
                    Ns_Set         *set;

                    set = Ns_TclGetSet(interp, Tcl_GetString(objv[i]));
                    if (set == NULL) {
			Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "Illegal ns_set id: \"",
				Tcl_GetString(objv[i]), "\"", NULL);
                        return TCL_ERROR;
                    }
                    Ns_ConnReplaceHeaders(conn, set);
                }
            }
        }
    }

    /*
     * Exactly one of chan, filename, string must be specified.
     */
    
    i = (chan != NULL ? 1 : 0) +
	(filename != NULL ? 1 : 0) +
	(string != NULL ? 1 : 0);
    if (i != 1) {
	Tcl_SetResult(interp, "must specify at least one of -string, -file, "
			      "or -type \n", TCL_STATIC);
        retval = TCL_ERROR;
    }

    /*
     * If an error has been set prior to now, complain and return.
     */
    
    if (retval == TCL_ERROR) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-status status? ?-type type? { ?-string string?"
			" | ?-file filename? | ?-fileid fileid? }"
			" ?-length length? ?-headers setid?");
        return TCL_ERROR;
    }
    if (chan != NULL) {
	/*
	 * We'll be returning an open channel
	 */
	
        if (length < 0) {
	    Tcl_SetResult(interp, "length required when -fileid is used.", 
		    TCL_STATIC);
	    return TCL_ERROR;
        }
	retval = Ns_ConnReturnOpenChannel(conn, status, type, chan, length);

    } else if (filename != NULL) {
	/*
	 * We'll be returining a file by name
	 */
	
        retval = Ns_ConnReturnFile(conn, status, type, filename);

    } else {
	/*
	 * We'll be returning a string now.
	 */

	retval = Ns_ConnReturnData(conn, status, string, length, type);
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
NsTclReturnFileObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status;
    Ns_Conn *conn;

    if (objc != 4 && objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type file");
        return TCL_ERROR;
    }
    if (objc == 5 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-3], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnFile(conn, status, Tcl_GetString(objv[objc-2]), 
				Tcl_GetString(objv[objc-1])));
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
NsTclReturnFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int          len, status;
    Tcl_Channel	 chan;
    Ns_Conn     *conn;

    if (objc != 5 && objc != 6) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status type fileId len");
        return TCL_ERROR;
    }
    if (objc == 6 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-4], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-1], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[objc-2]), 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnOpenChannel(conn, status, Tcl_GetString(objv[objc-3]), chan, len));
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
NsTclReturnBadRequestObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? reason");
        return TCL_ERROR;
    }
    if (objc == 3 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnBadRequest(conn, Tcl_GetString(objv[objc-1])));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSimpleReturnCmd --
 *
 *	A generic way of returning from tcl; this implements 
 *	ns_returnnotfound and ns_returnforbidden. It uses the 
 *	clientdata to know what to do. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will call proc that is clientdata. 
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * NsTclSimpleReturnObjCmd --
 *
 *	A generic way of returning from tcl; this implements 
 *	ns_returnnotfound and ns_returnforbidden. It uses the 
 *	clientdata to know what to do. 
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
    if (objc == 2 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, (*proc)(conn));
}

int
NsTclReturnNotFoundObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnNotFound);
}

int
NsTclReturnUnauthorizedObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return ReturnObjCmd(arg, interp, objc, objv, Ns_ConnReturnUnauthorized);
}

int
NsTclReturnForbiddenObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
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
NsTclReturnErrorObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int      status;
    Ns_Conn *conn;

    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? status message");
        return TCL_ERROR;
    }
    if (objc == 4 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[objc-2], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp,
    Ns_ConnReturnAdminNotice(conn, status, "Request Error", 
	    Tcl_GetString(objv[objc-1])));
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
	if (NsIsIdConn(argv[1]) == NS_TRUE) {
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
    if (GetConn(arg, interp, &conn) != TCL_OK) {
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
NsTclReturnRedirectObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? location");
        return TCL_ERROR;
    }
    if (objc == 3 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnRedirect(conn, 
				Tcl_GetString(objv[objc-1])));
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
NsTclWriteObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn *conn;
	char    *bytes;
	int      length;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? string");
        return TCL_ERROR;
    }
    if (objc == 3 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    bytes = (char *) Tcl_GetByteArrayFromObj(objv[objc-1], &length);
    return Result(interp,Ns_WriteConn(conn, bytes, length));
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
NsTclConnSendFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_Conn     *conn;
    Tcl_Channel	 chan;
    int          len;

    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? fp len");
        return TCL_ERROR;
    }
    if (objc == 4 && !CheckId(interp, Tcl_GetString(objv[1]))) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
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
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "could not send ", 
		Tcl_GetString(objv[objc-1]),
    		" bytes from channel ", 
		Tcl_GetString(objv[objc-2]), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
CheckId(Tcl_Interp *interp, char *id)
{
    if (!NsIsIdConn(id)) {
	Tcl_AppendResult(interp, "invalid connid: ", id, NULL);
	return 0;
    }
    return 1;
}


static int
Result(Tcl_Interp *interp, int result)
{
    Tcl_SetResult(interp, result == NS_OK ? "1" : "0", TCL_STATIC);
    return TCL_OK;
}


static int
GetConn(ClientData arg, Tcl_Interp *interp, Ns_Conn **connPtr)
{
    NsInterp *itPtr = arg;

    if (itPtr->conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    *connPtr = itPtr->conn;
    return TCL_OK;
}

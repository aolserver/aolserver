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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclresp.c,v 1.7 2001/03/22 21:30:17 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int CheckId(Tcl_Interp *interp, char *id);
static int Result(Tcl_Interp *interp, int result);
static int GetConn(ClientData arg, Tcl_Interp *interp, Ns_Conn **connPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsTclHeadersCmd --
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
NsTclHeadersCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int      status, len;
    Ns_Conn *conn;
    char    *type;

    if (argc < 3 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " connid status ?type len?\"", NULL);
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argc < 4) {
        type = NULL;
    } else {
        type = argv[3];
    }
    if (argc < 5) {
        len = 0;
    } else if (Tcl_GetInt(interp, argv[4], &len) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_ConnSetRequiredHeaders(conn, type, len);
    return Result(interp, Ns_ConnFlushHeaders(conn, status));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnCmd --
 *
 *	Implements ns_return. 
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
NsTclReturnCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Conn *conn;
    int      status, len, result;
    char    *str;

    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
			 argv[0], " ?connid? status type string\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 5 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-3], &status) != TCL_OK) {
	return TCL_ERROR;
    }
    str = argv[argc-1];
    len = strlen(str);
    Ns_ConnSetRequiredHeaders(conn, argv[argc-2], len);
    result = Ns_ConnFlushHeaders(conn, status);
    if (result == NS_OK) {
        result = Ns_WriteConn(conn, str, len);
    }
    return Result(interp, result);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRespondCmd --
 *
 *	Implements ns_respond. 
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
NsTclRespondCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
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
    length = 0;

    if (argc < 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?-status status? ?-type type? { ?-string string?"
            " | ?-file filename? | ?-fileid fileid? }"
            " ?-length length? ?-headers setid?\"", NULL);
        return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Loop over every argument and set the appropriate options.
     */
    
    retval = TCL_OK;
    for (i = 0; (i < argc) && (retval == TCL_OK); i++) {
        if (*argv[i] == '-') {
            if (strcasecmp(argv[i], "-status") == 0) {
                if (!((++i < argc) &&
                        (Tcl_GetInt(interp, argv[i], &status) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(argv[i], "-type") == 0) {
                if (++i >= argc) {
                    retval = TCL_ERROR;
                } else {
                    type = argv[i];
                }

            } else if (strcasecmp(argv[i], "-string") == 0) {
                if (++i >= argc) {
                    retval = TCL_ERROR;
                } else {
                    string = argv[i];
                }

            } else if (strcasecmp(argv[i], "-file") == 0) {
                if (++i >= argc) {
                    retval = TCL_ERROR;
                } else {
                    filename = argv[i];
                }

            } else if (strcasecmp(argv[i], "-fileid") == 0) {
                if (!((++i < argc) &&
                        (Ns_TclGetOpenChannel(interp, argv[i], 0, 1,
					      &chan) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(argv[i], "-length") == 0) {
                if (!((++i < argc) &&
                        (Tcl_GetInt(interp, argv[i], &length) == TCL_OK))) {
                    retval = TCL_ERROR;
                }

            } else if (strcasecmp(argv[i], "-headers") == 0) {
                if (++i >= argc) {
                    retval = TCL_ERROR;
                } else {
                    Ns_Set         *set;

                    set = Ns_TclGetSet(interp, argv[i]);
                    if (set == NULL) {
                        Tcl_AppendResult(interp, "Illegal ns_set id: \"",
                            argv[i], "\"", NULL);
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
	Tcl_AppendResult(interp, 
			 "Need to specify at least one of -string, -file, or "
			 "-type \n", NULL);
        retval = TCL_ERROR;
    }

    /*
     * If an error has been set prior to now, complain and return.
     */
    
    if (retval == TCL_ERROR) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?-status status? ?-type type? { ?-string string?"
            " | ?-file filename? | ?-fileid fileid? }"
            " ?-length length? ?-headers setid?\"", NULL);
        return TCL_ERROR;
    }
    if (chan != NULL) {
	/*
	 * We'll be returning an open channel
	 */
	
        if (length == 0) {
            Tcl_AppendResult(interp, "Length required when -fileid is used.",
                NULL);
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
	
        if (length == 0) {
            length = strlen(string);
        }
        Ns_ConnSetRequiredHeaders(conn, type, length);
	retval = Ns_ConnFlushHeaders(conn, status);
	if (retval == NS_OK) {
            retval = Ns_WriteConn(conn, string, length);
	}
    }

    return Result(interp, retval);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFile --
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
NsTclReturnFileCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int      status;
    Ns_Conn *conn;

    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?connid? status type file\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 5 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-3], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnFile(conn, status, argv[argc-2], argv[argc-1]));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnFpCmd --
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
NsTclReturnFpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int          len, status;
    Tcl_Channel	 chan;
    Ns_Conn     *conn;

    if (argc != 5 && argc != 6) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?connid? status type fileId len\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 6 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-4], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-1], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[argc-2], 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnOpenChannel(conn, status, argv[argc-3], chan, len));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnBadRequestCmd --
 *
 *	Implements ns_returnbadrequest. 
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
NsTclReturnBadRequestCmd(ClientData arg, Tcl_Interp *interp, int argc,
			  char **argv)
{
    Ns_Conn *conn;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid? reason\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 3 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnBadRequest(conn, argv[argc-1]));
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

static int
ReturnCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int (*proc) (Ns_Conn *))
{
    Ns_Conn *conn;

    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid?\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 2 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, (*proc)(conn));
}

int
NsTclReturnNotFoundCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, interp, argc, argv, Ns_ConnReturnNotFound);
}

int
NsTclReturnUnauthorizedCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, interp, argc, argv, Ns_ConnReturnUnauthorized);
}

int
NsTclReturnForbiddenCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, interp, argc, argv, Ns_ConnReturnForbidden);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnErrorCmd --
 *
 *	Implements ns_tclreturnerror 
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
NsTclReturnErrorCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    int      status;
    Ns_Conn *conn;

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " ?connid? status message\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 4 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-2], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    return Result(interp,
	Ns_ConnReturnAdminNotice(conn, status, "Request Error", argv[argc-1]));
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
 * NsTclReturnRedirectCmd --
 *
 *	Implements ns_returnredirect. 
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
NsTclReturnRedirectCmd(ClientData arg, Tcl_Interp *interp, int argc,
		       char **argv)
{
    Ns_Conn *conn;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid? location ", NULL);
        return TCL_ERROR;
    }
    if (argc == 3 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, Ns_ConnReturnRedirect(conn, argv[argc-1]));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteCmd --
 *
 *	Implements ns_write 
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
NsTclWriteCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Conn *conn;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid? string", NULL);
        return TCL_ERROR;
    }
    if (argc == 3 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    return Result(interp, Ns_ConnPuts(conn, argv[argc-1]));
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnSendFpCmd --
 *
 *	Implements ns_connsendfp.
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
NsTclConnSendFpCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    Ns_Conn     *conn;
    Tcl_Channel	 chan;
    int          len;
    int		 fpArg = 1, lengthArg = 2;

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid? fp len ", NULL);
        return TCL_ERROR;
    }
    if (argc == 4 && !CheckId(interp, argv[1])) {
	return TCL_ERROR;
    }
    if (GetConn(arg, interp, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[argc-2], 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[argc-1], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ConnSendChannel(conn, chan, len) != NS_OK) {
	Tcl_AppendResult(interp, "could not send ", argv[argc-1],
	    " bytes from channel ", argv[argc-2], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
CheckId(Tcl_Interp *interp, char *id)
{
    if (!NsIsIdConn(id)) {
	Tcl_AppendResult(interp, "invalid connid: ", id, NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
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

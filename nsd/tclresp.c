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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclresp.c,v 1.4 2000/08/25 13:49:57 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


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
NsTclHeadersCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int      status;
    int      len;
    Ns_Conn *conn;
    char    *type;

    if (argc < 3 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " connid status ?type len?\"", NULL);
        return TCL_ERROR;
    }
    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
        Tcl_AppendResult(interp, "no such connid \"", argv[1], "\"", NULL);
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

    /*
     * Set the required headers and then flush them out.
     */
    
    Ns_HeadersRequired(conn, type, len);
    if (Ns_HeadersFlush(conn, status) == NS_OK) {
        Tcl_AppendResult(interp, "1", NULL);
    } else {
        Tcl_AppendResult(interp, "0", NULL);
    }
    
    return TCL_OK;
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
NsTclReturnCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int      status;
    int      len;
    Ns_Conn *conn;
    int      statusArg = 1, typeArg = 2, stringArg = 3;

    if (argc == 5) {
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	statusArg = 2;
	typeArg = 3;
	stringArg = 4;
    } else if (argc < 4 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
			 argv[0], " status type string\"", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
        return TCL_ERROR;
    }

    len = strlen(argv[stringArg]);

    Ns_HeadersRequired(conn, argv[typeArg], len);

    if ((Ns_HeadersFlush(conn, status) == NS_OK) &&
        (Ns_WriteConn(conn, argv[stringArg], len) == NS_OK)) {
	
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
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
NsTclRespondCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
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
    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
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
                    Ns_HeadersReplace(conn, set);
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
        if (Ns_ConnReturnOpenChannel(conn, status, type, chan,
				     length) != NS_OK) {
            retval = TCL_ERROR;
        }
    } else if (filename != NULL) {
	/*
	 * We'll be returining a file by name
	 */
	
        if (Ns_ReturnFile(conn, status, type, filename) != NS_OK) {
            retval = TCL_ERROR;
        }
    } else {
	/*
	 * We'll be returning a string now.
	 */
	
        if (length == 0) {
            length = strlen(string);
        }
        Ns_HeadersRequired(conn, type, length);
        if ((Ns_HeadersFlush(conn, status) == NS_OK) &&
            (Ns_WriteConn(conn, string, length) == NS_OK)) {
            retval = TCL_OK;
        } else {
            retval = TCL_ERROR;
        }
    }
    if (retval == TCL_OK) {
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
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
NsTclReturnFileCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int      status;
    Ns_Conn *conn;
    int	     statusArg = 1, typeArg = 2, filenameArg = 3;

    if (argc == 5) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error.
	 */
	
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	statusArg = 2;
	typeArg = 3;
	filenameArg = 4;
    } else if (argc < 4 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " status type file\"", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ReturnFile(conn, status, argv[typeArg], 
		      argv[filenameArg]) == NS_OK) {
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
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
NsTclReturnFpCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int          length;
    int          status;
    Tcl_Channel	 chan;
    Ns_Conn     *conn;
    int		 statusArg = 1, typeArg = 2, fileIdArg = 3, lengthArg = 4;

    if (argc == 6) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error.
	 */
	
	if (!NsIsIdConn(argv[1])) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	statusArg = 2;
	typeArg = 3;
	fileIdArg = 4;
	lengthArg = 5;
    } else if (argc < 5 || argc > 6) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " status type fileId len\"", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[lengthArg], &length) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[fileIdArg], 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ConnReturnOpenChannel(conn, status, argv[typeArg], chan,
				 length) == NS_OK) {

	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }
    return TCL_OK;
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
NsTclReturnBadRequestCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			  char **argv)
{
    Ns_Conn *conn;
    int	     reasonArg = 1;

    if (argc == 3) {
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	reasonArg = 2;
    } else if (argc < 2 || argc > 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " reason\"", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Ns_ReturnBadRequest(conn, argv[reasonArg]) != NS_OK) {
	Tcl_AppendResult(interp, "0", NULL);
    } else {
	Tcl_AppendResult(interp, "1", NULL);
    }
    return TCL_OK;
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

int
NsTclSimpleReturnCmd(ClientData clientData, Tcl_Interp *interp, 
		      int argc, char **argv)
{
    int      (*proc) (Ns_Conn *);
    Ns_Conn *conn;

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    proc = (int (*) (Ns_Conn *)) clientData;

    if (argc == 2) {
	if (!NsIsIdConn(argv[1])) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
    } else if (argc < 1 || argc > 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], "\"", NULL);
        return TCL_ERROR;
    }

    if ((*proc) (conn) != NS_OK) {
	Tcl_AppendResult(interp, "0", NULL);
    } else {
	Tcl_AppendResult(interp, "1", NULL);
    }

    return TCL_OK;
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
NsTclReturnErrorCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    int      status;
    Ns_Conn *conn;
    int	     statusArg = 1, messageArg = 2;

    if (argc == 4) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error.
	 */
	
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	statusArg = 2;
	messageArg = 3;
    } else if (argc < 3 || argc > 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " status message\"", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Ns_ReturnAdminNotice(conn, status, "Request Error",
			     argv[messageArg]) == NS_OK) {
	
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnNoticeCmd --
 *
 *	Implements ns_returnnotice. 
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
NsTclReturnNoticeCmd(ClientData dummy, Tcl_Interp *interp, 
		     int argc, char **argv)
{
    int      status;
    Ns_Conn *conn;
    char    *longMessage = NULL;
    int      statusArg = 0, messageArg = 0, longMessageArg = 0;

    /*
     * This function is pretty much identical to NsTclReturnAdminNoticeCmd
     */

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

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

    assert (statusArg != 0);
    assert (messageArg != 0);

    /*
     * Get the status value
     */
    
    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
	return TCL_ERROR;
    }

    if (longMessageArg != 0) {
	longMessage = argv[longMessageArg];
    }


    if (Ns_ReturnNotice(conn, status, argv[messageArg],
			longMessage) == NS_OK) {

	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclReturnAdminNoticeCmd --
 *
 *	Implements ns_returnadminnotice. 
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
NsTclReturnAdminNoticeCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			   char **argv)
{
    int      status;
    Ns_Conn *conn;
    char    *longMessage = NULL;
    int	     statusArg = 0, messageArg = 0, longMessageArg = 0;

    /*
     * This function is pretty much identical to NsTclReturnNoticeCmd
     */

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

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
	if (NsIsIdConn(argv[1])) {
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

    assert (statusArg != 0);
    assert (messageArg != 0);

    /*
     * Get the status value
     */
    
    if (Tcl_GetInt(interp, argv[statusArg], &status) != TCL_OK) {
	return TCL_ERROR;
    }

    if (longMessageArg != 0) {
	longMessage = argv[longMessageArg];
    }

    if (Ns_ReturnAdminNotice(conn, status, argv[messageArg], 
			     longMessage) == NS_OK) {
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
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
NsTclReturnRedirectCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		       char **argv)
{
    Ns_Conn *conn;
    int	     locationArg = 1;

    if (argc == 3) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error
	 */
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	locationArg = 2;
    } else if (argc > 3 || argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " location ", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Ns_ReturnRedirect(conn, argv[locationArg]) == NS_OK) {
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }

    return TCL_OK;
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
NsTclWriteCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Conn *conn;
    int	     stringArg = 1; /* Assume no-conn parameter usage */

    if (argc == 3) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error
	 */
	
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	stringArg = 2;
    } else if (argc < 2 || argc > 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string", NULL);
        return TCL_ERROR;
    }

    conn = Ns_TclGetConn(interp);
    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }
    if (Ns_PutsConn(conn, argv[stringArg]) == NS_OK) {
	Tcl_AppendResult(interp, "1", NULL);
    } else {
	Tcl_AppendResult(interp, "0", NULL);
    }
    return TCL_OK;
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
NsTclConnSendFpCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    Ns_Conn     *conn;
    Tcl_Channel	 chan;
    int          len;
    int		 fpArg = 1, lengthArg = 2;

    if (argc == 4) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error
	 */
	
	if (NsIsIdConn(argv[1]) == NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	fpArg = 2;
	lengthArg = 3;
    } else if (argc > 4 || argc < 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " fp len ", NULL);
        return TCL_ERROR;
    }
    
    conn = Ns_TclGetConn(interp);

    if (conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }

    if (Ns_TclGetOpenChannel(interp, argv[fpArg], 0, 1, &chan) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[lengthArg], &len) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Ns_ConnSendChannel(conn, chan, len) != NS_OK) {
        sprintf(interp->result, "could not send %d bytes from %s", 
		len, argv[2]);
        return TCL_ERROR;
    }
    return TCL_OK;
}



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
 * conn.c --
 *
 *      Manage the Ns_Conn structure
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/conn.c,v 1.26 2002/08/26 02:05:13 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int GetChan(Tcl_Interp *interp, char *id, Tcl_Channel *chanPtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnHeaders --
 *
 *	Get the headers 
 *
 * Results:
 *	An Ns_Set containing HTTP headers from the client 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConnHeaders(Ns_Conn *conn)
{
    return conn->headers;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnOutputHeaders --
 *
 *	Get the output headers
 *
 * Results:
 *	A writeable Ns_Set containing headers to send back to the client
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_ConnOutputHeaders(Ns_Conn *conn)
{
    return conn->outputheaders;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnAuthUser --
 *
 *	Get the authenticated user 
 *
 * Results:
 *	A pointer to a string with the username 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnAuthUser(Ns_Conn *conn)
{
    return conn->authUser;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnAuthPasswd --
 *
 *	Get the authenticated user's password 
 *
 * Results:
 *	A pointer to a string with the user's plaintext password 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnAuthPasswd(Ns_Conn *conn)
{
    return conn->authPasswd;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentLength --
 *
 *	Get the content length from the client 
 *
 * Results:
 *	An integer content length, or 0 if none sent 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnContentLength(Ns_Conn *conn)
{
    return conn->contentLength;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContent --
 *
 *	Return pointer to start of content.
 *
 * Results:
 *	Start of content.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnContent(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->reqPtr->content;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnServer --
 *
 *	Get the server name 
 *
 * Results:
 *	A string ptr to the server name 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnServer(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->servPtr->server;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResponseStatus --
 *
 *	Get the HTTP reponse code that will be sent 
 *
 * Results:
 *	An integer response code (e.g., 200 for OK) 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResponseStatus(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->responseStatus;

}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentSent --
 *
 *	Return the number of bytes sent to the browser after headers.
 *
 * Results:
 *	Bytes sent.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnContentSent(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->nContentSent;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResponseLength --
 *
 *	Get the response length 
 *
 * Results:
 *	integer, number of bytes to send 
 *
 * Side effects:
 *	None.	
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResponseLength(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->responseLength;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeer --
 *
 *	Get the peer's internet address 
 *
 * Results:
 *	A string IP address 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnPeer(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->reqPtr->peer;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPeerPort --
 *
 *	Get the port from which the peer is coming 
 *
 * Results:
 *	An integer port # 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnPeerPort(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->reqPtr->port;
}


/*
 *----------------------------------------------------------------------
 * Ns_SetLocationProc --
 *
 *      Set pointer to custom routine that acts like Ns_ConnLocation();
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SetLocationProc(char *server, Ns_LocationProc *procPtr)
{
    NsServer *servPtr = NsGetServer(server);

    if (servPtr != NULL) {
	servPtr->locationProc = procPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnLocation --
 *
 *	Get the location of this connection. It is of the form 
 *	METHOD://HOSTNAME:PORT 
 *
 * Results:
 *	a string URL, not including path 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnLocation(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    char *location;

    if (connPtr->servPtr->locationProc != NULL) {
        location = (*connPtr->servPtr->locationProc)(conn);
    } else {
	location = connPtr->drvPtr->location;
    }
    return location;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnHost --
 *
 *	Get the address of the current connection 
 *
 * Results:
 *	A string address 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnHost(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->drvPtr->address;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPort --
 *
 *	What server port is this connection on? 
 *
 * Results:
 *	Integer port number 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnPort(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->drvPtr->port;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSock --
 *
 *	Return the underlying socket for a connection.
 *
 * Results:
 *	A driver name 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSock(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return (connPtr->sockPtr ? connPtr->sockPtr->sock : -1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnDriverName --
 *
 *	Return the name of this driver 
 *
 * Results:
 *	A driver name 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnDriverName(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->drvPtr->name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnDriverContext --
 *
 *	Get the conn-wide context for this driver 
 *
 * Results:
 *	The driver-supplied context 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_ConnDriverContext(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return (void *)(connPtr->sockPtr ? connPtr->sockPtr->arg : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnId --
 *
 *	Return the connection id.
 *
 * Results:
 *	Integer id.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnId(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->id;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnModifiedSince --
 *
 *	Has the data the url points to changed since a given time? 
 *
 * Results:
 *	NS_TRUE if data modified, NS_FALSE otherwise.
 *
 * Side effects:
 *	None 
 *
 * NOTE: This doesn't do a strict time check.  If the server flags aren't
 *       set to check modification, or if there wasn't an 'If-Modified-Since'
 *       header in the request, then this'll always return true
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnModifiedSince(Ns_Conn *conn, time_t since)
{
    Conn	   *connPtr = (Conn *) conn;
    char           *hdr;

    if (connPtr->servPtr->opts.modsince) {
        hdr = Ns_SetIGet(conn->headers, "If-Modified-Since");
        if (hdr != NULL && Ns_ParseHttpTime(hdr) >= since) {
	    return NS_FALSE;
        }
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetEncoding, Ns_ConnSetEncoding --
 *
 *	Get (set) the Tcl_Encoding for the connection which is used
 *	to convert input forms to proper UTF.
 *
 * Results:
 *	Pointer to Tcl_Encoding (get) or NULL (set).
 *
 * Side effects:
 *	See Ns_ConnGetQuery().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_ConnGetEncoding(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->encoding;
}

void
Ns_ConnSetEncoding(Ns_Conn *conn, Tcl_Encoding encoding)
{
    Conn *connPtr = (Conn *) conn;

    connPtr->encoding = encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * NsIsIdConn --
 *
 *	Given an conn ID, could this be a conn ID?
 *
 * Results:
 *	Boolean. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsIsIdConn(char *connId)
{
    if (connId == NULL || *connId != 'c') {
	return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnObjCmd --
 *
 *	Implements ns_conn as an obj command. 
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConnObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp     *itPtr = arg;
    Ns_Conn      *conn;
    Conn         *connPtr;
    Ns_Set       *form;
    Ns_Request   *request;
    Tcl_Encoding  encoding;
    Tcl_Channel   chan;
    Tcl_Obj	 *result;
    int		  idx, off, len;


    static CONST char *opts[] = {
	 "authpassword", "authuser", "close", "content", "contentlength",
	 "copy", "driver", "encoding", "files", "flags", "form",
	 "headers", "host", "id", "isconnected", "location", "method",
	 "outputheaders", "peeraddr", "peerport", "port", "protocol",
	 "query", "request", "server", "sock", "start", "status",
	 "url", "urlc", "urlv", "version", NULL,
    };
    enum ISubCmdIdx {
	 CAuthPasswordIdx, CAuthUserIdx, CCloseIdx, CContentIdx,
	 CContentLengthIdx, CCopyIdx, CDriverIdx, CEncodingIdx,
	 CFilesIdx, CFlagsIdx, CFormIdx, CHeadersIdx, CHostIdx,
	 CIdIdx, CIsConnectedIdx, CLocationIdx, CMethodIdx,
	 COutputHeadersIdx, CPeerAddrIdx, CPeerPortIdx, CPortIdx,
	 CProtocolIdx, CQueryIdx, CRequestIdx, CServerIdx, CSockIdx,
	 CStartIdx, CStatusIdx, CUrlIdx, CUrlcIdx, CUrlvIdx,
	 CVersionIdx,
    } opt;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }

    result  = Tcl_GetObjResult(interp);
    connPtr = (Conn *) conn = itPtr->conn;

    /*
     * Only the "isconnected" option operates without a conn.
     */

    if (opt == CIsConnectedIdx) {
	Tcl_SetBooleanObj(result, connPtr ? 0 : 1);
	return TCL_OK;
    }
    if (connPtr == NULL) {
	Tcl_SetResult(interp, "no current connection", TCL_STATIC);
        return TCL_ERROR;
    }

    request = connPtr->request;
    switch (opt) {

	case CIsConnectedIdx:
	    /* NB: Not reached - silence compiler warning. */
	    break;
		
	case CUrlvIdx:
	    if (objc == 2 || (objc == 3 && NsIsIdConn(Tcl_GetString(objv[2])))) {
		for (idx = 0; idx < request->urlc; idx++) {
		    Tcl_AppendElement(interp, request->urlv[idx]);
		}
	    } else if (Tcl_GetIntFromObj(interp, objv[2], &idx) != TCL_OK) {
		return TCL_ERROR;
	    } else if (idx >= 0 && idx < request->urlc) {
		Tcl_SetResult(interp, request->urlv[idx], TCL_STATIC);
	    }
	    break;

	case CAuthUserIdx:
	    Tcl_SetResult(interp, connPtr->authUser, TCL_STATIC);
	    break;
	    
	case CAuthPasswordIdx:
	    Tcl_SetResult(interp, connPtr->authPasswd, TCL_STATIC);
	    break;

	case CContentIdx:
	    Tcl_SetResult(interp, Ns_ConnContent(conn), TCL_STATIC);
	    break;
	    
	case CContentLengthIdx:
	    Tcl_SetIntObj(result, conn->contentLength);
	    break;

	case CEncodingIdx:
	    if (objc > 2) {
		encoding = Ns_GetEncoding(Tcl_GetString(objv[2]));
		if (encoding == NULL) {
		    Tcl_AppendResult(interp, "no such encoding: ",
			Tcl_GetString(objv[2]), NULL);
		    return TCL_ERROR;
		}
		connPtr->encoding = encoding;
	    }
	    if (connPtr->encoding != NULL) {
		Tcl_SetResult(interp, (char *) Tcl_GetEncodingName(connPtr->encoding),
			      TCL_STATIC);
	    }
	    break;
	
	case CPeerAddrIdx:
	    Tcl_SetResult(interp, Ns_ConnPeer(conn), TCL_STATIC);
	    break;
	
	case CPeerPortIdx:
	    Tcl_SetIntObj(result, Ns_ConnPeerPort(conn));
	    break;

	case CHeadersIdx:
	    if (itPtr->nsconn.flags & CONN_TCLHDRS) {
		Tcl_SetResult(interp, itPtr->nsconn.hdrs, TCL_STATIC);
	    } else {
		Ns_TclEnterSet(interp, connPtr->headers, NS_TCL_SET_STATIC);
		strcpy(itPtr->nsconn.hdrs, Tcl_GetStringResult(interp));
		itPtr->nsconn.flags |= CONN_TCLHDRS;
	    }
	    break;
	
	case COutputHeadersIdx:
	    if (itPtr->nsconn.flags & CONN_TCLOUTHDRS) {
		Tcl_SetResult(interp, itPtr->nsconn.outhdrs, TCL_STATIC);
	    } else {
		Ns_TclEnterSet(interp, connPtr->outputheaders, NS_TCL_SET_STATIC);
		strcpy(itPtr->nsconn.outhdrs, Tcl_GetStringResult(interp));
		itPtr->nsconn.flags |= CONN_TCLOUTHDRS;
	    }
	    break;
	
	case CFormIdx:
	    if (itPtr->nsconn.flags & CONN_TCLFORM) {
		Tcl_SetResult(interp, itPtr->nsconn.form, TCL_STATIC);
	    } else {
		form = Ns_ConnGetQuery(conn);
		if (form == NULL) {
		    itPtr->nsconn.form[0] = '\0';
		} else {
		    Ns_TclEnterSet(interp, form, NS_TCL_SET_STATIC);
		    strcpy(itPtr->nsconn.form, Tcl_GetStringResult(interp));
		}
		itPtr->nsconn.flags |= CONN_TCLFORM;
	    }
	    break;

	case CFilesIdx:
	    Tcl_SetResult(interp, connPtr->files.string, TCL_STATIC);
	    break;

	case CCopyIdx:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 1, objv, "copy off len chan");
		return TCL_ERROR;
	    }
	    if (Tcl_GetIntFromObj(interp, objv[2], &off) != TCL_OK ||
		Tcl_GetIntFromObj(interp, objv[3], &len) != TCL_OK ||
		GetChan(interp, Tcl_GetString(objv[4]), &chan) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (off < 0 || off > connPtr->reqPtr->length) {
		Tcl_AppendResult(interp, "invalid offset: ", Tcl_GetString(objv[2]), NULL);
		return TCL_ERROR;
	    }
	    if (len < 0 || len > (connPtr->reqPtr->length - off)) {
		Tcl_AppendResult(interp, "invalid length: ", Tcl_GetString(objv[3]), NULL);
		return TCL_ERROR;
	    }
	    if (Tcl_WriteChars(chan, connPtr->reqPtr->content + off, len) != len) {
		Tcl_AppendResult(interp, "could not write ", Tcl_GetString(objv[3]), " bytes to ",
		    Tcl_GetString(objv[4]), ": ", Tcl_PosixError(interp), NULL);
		return TCL_ERROR;
	    }
	    break;

	case CRequestIdx:
	    Tcl_SetResult(interp, request->line, TCL_STATIC);
	    break;

	case CMethodIdx:
	    Tcl_SetResult(interp, request->method, TCL_STATIC);
	    break;

	case CProtocolIdx:
	    Tcl_SetResult(interp, request->protocol, TCL_STATIC);
	    break;

	case CHostIdx:
	    Tcl_SetResult(interp, request->host, TCL_STATIC);
	    break;
	
	case CPortIdx:
	    Tcl_SetIntObj(result, request->port);
	    break;

	case CUrlIdx:
	    Tcl_SetResult(interp, request->url, TCL_STATIC);
	    break;
	
	case CQueryIdx:
	    Tcl_SetResult(interp, request->query, TCL_STATIC);
	    break;
	
	case CUrlcIdx:
	    Tcl_SetIntObj(result, request->urlc);
	    break;
	
	case CVersionIdx:
	    Tcl_SetDoubleObj(result, request->version);
	    break;

	case CLocationIdx:
	    Tcl_SetResult(interp, Ns_ConnLocation(conn), TCL_STATIC);
	    break;

	case CDriverIdx:
	    Tcl_SetResult(interp, Ns_ConnDriverName(conn), TCL_STATIC);
	    break;
    
	case CServerIdx:
	    Tcl_SetResult(interp, Ns_ConnServer(conn), TCL_STATIC);
	    break;

	case CStatusIdx:
	    Tcl_SetIntObj(result, Ns_ConnResponseStatus(conn));
	    break;

	case CSockIdx:
	    Tcl_SetIntObj(result, Ns_ConnSock(conn));
	    break;
	
	case CIdIdx:
	    Tcl_SetIntObj(result, Ns_ConnId(conn));
	    break;
	
	case CFlagsIdx:
	    Tcl_SetIntObj(result, connPtr->flags);
	    break;

	case CStartIdx:
	    Ns_TclSetTimeObj(result, &connPtr->startTime);
	    break;

	case CCloseIdx:
	    if (Ns_ConnClose(conn) != NS_OK) {
		Tcl_SetResult(interp, "could not close connection", TCL_STATIC);
		return TCL_ERROR;
	    }
	    break;
	    
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnCmd --
 *
 *	Implments ns_conn. 
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConnCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp   *itPtr = arg;
    Ns_Conn    *conn = itPtr->conn;
    Conn       *connPtr = (Conn *) conn;
    Ns_Set     *form;
    Ns_Request *request;
    Tcl_Encoding encoding;
    char	buf[50];
    int		idx;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " cmd ", NULL);
        return TCL_ERROR;
    }

    /*
     * Only the "isconnected" command operates without a conn.
     */

    if (STREQ(argv[1], "isconnected")) {
	Tcl_SetResult(interp, (connPtr == NULL) ? "0" : "1", TCL_STATIC);
	return TCL_OK;
    } else if (connPtr == NULL) {
        Tcl_AppendResult(interp, "no current connection", NULL);
        return TCL_ERROR;
    }

    request = connPtr->request;
    if (STREQ(argv[1], "urlv")) {
	if (argc == 2 || (argc == 3 && NsIsIdConn(argv[2]))) {
	    for (idx = 0; idx < request->urlc; idx++) {
		Tcl_AppendElement(interp, request->urlv[idx]);
	    }
	} else if (Tcl_GetInt(interp, argv[2], &idx) != TCL_OK) {
	    return TCL_ERROR;
	} else if (idx >= 0 && idx < request->urlc) {
	    Tcl_SetResult(interp, request->urlv[idx], TCL_VOLATILE);
	}

    } else if (STREQ(argv[1], "authuser")) {
        Tcl_SetResult(interp, connPtr->authUser, TCL_STATIC);

    } else if (STREQ(argv[1], "authpassword")) {
        Tcl_SetResult(interp, connPtr->authPasswd, TCL_STATIC);

    } else if (STREQ(argv[1], "content")) {
	Tcl_SetResult(interp, Ns_ConnContent(conn), TCL_VOLATILE);

    } else if (STREQ(argv[1], "contentlength")) {
        sprintf(buf, "%u", (unsigned) conn->contentLength);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "encoding")) {
	if (argc > 2) {
	    encoding = Ns_GetEncoding(argv[2]);
	    if (encoding == NULL) {
		Tcl_AppendResult(interp, "no such encoding: ",
		    argv[2], NULL);
		return TCL_ERROR;
	    }
	    connPtr->encoding = encoding;
	}
	if (connPtr->encoding != NULL) {
	    Tcl_SetResult(interp, (char *) Tcl_GetEncodingName(connPtr->encoding), TCL_VOLATILE);
	}

    } else if (STREQ(argv[1], "peeraddr")) {
        Tcl_SetResult(interp, Ns_ConnPeer(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "peerport")) {
	sprintf(buf, "%d", Ns_ConnPeerPort(conn));
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "headers")) {
	if (itPtr->nsconn.flags & CONN_TCLHDRS) {
            Tcl_SetResult(interp, itPtr->nsconn.hdrs, TCL_STATIC);
	} else {
            Ns_TclEnterSet(interp, connPtr->headers, NS_TCL_SET_STATIC);
	    strcpy(itPtr->nsconn.hdrs, Tcl_GetStringResult(interp));
	    itPtr->nsconn.flags |= CONN_TCLHDRS;
	}

    } else if (STREQ(argv[1], "outputheaders")) {
	if (itPtr->nsconn.flags & CONN_TCLOUTHDRS) {
            Tcl_SetResult(interp, itPtr->nsconn.outhdrs, TCL_STATIC);
	} else {
            Ns_TclEnterSet(interp, connPtr->outputheaders, NS_TCL_SET_STATIC);
	    strcpy(itPtr->nsconn.outhdrs, Tcl_GetStringResult(interp));
	    itPtr->nsconn.flags |= CONN_TCLOUTHDRS;
	}

    } else if (STREQ(argv[1], "form")) {
	if (itPtr->nsconn.flags & CONN_TCLFORM) {
            Tcl_SetResult(interp, itPtr->nsconn.form, TCL_STATIC);
	} else {
            form = Ns_ConnGetQuery(conn);
            if (form == NULL) {
		itPtr->nsconn.form[0] = '\0';
	    } else {
                Ns_TclEnterSet(interp, form, NS_TCL_SET_STATIC);
		strcpy(itPtr->nsconn.form, Tcl_GetStringResult(interp));
	    }
	    itPtr->nsconn.flags |= CONN_TCLFORM;
	}

    } else if (STREQ(argv[1], "files")) {
	Tcl_SetResult(interp, connPtr->files.string, TCL_VOLATILE);

    } else if (STREQ(argv[1], "copy")) {
	Tcl_Channel chan;
	int off, len;

	if (argc != 5) {
	    Tcl_AppendResult(interp, "wrong # of args: should be: \"",
		argv[0], " copy off len chan\"", NULL);
	    return TCL_ERROR;
	}
	if (Tcl_GetInt(interp, argv[2], &off) != TCL_OK ||
	    Tcl_GetInt(interp, argv[3], &len) != TCL_OK ||
	    GetChan(interp, argv[4], &chan) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (off < 0 || off > connPtr->reqPtr->length) {
	    Tcl_AppendResult(interp, "invalid offset: ", argv[2], NULL);
	    return TCL_ERROR;
	}
	if (len < 0 || len > (connPtr->reqPtr->length - off)) {
	    Tcl_AppendResult(interp, "invalid length: ", argv[3], NULL);
	    return TCL_ERROR;
	}
	if (Tcl_WriteChars(chan, connPtr->reqPtr->content + off, len) != len) {
	    Tcl_AppendResult(interp, "could not write ", argv[3], " bytes to ",
		argv[4], ": ", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}

    } else if (STREQ(argv[1], "request")) {
        Tcl_SetResult(interp, request->line, TCL_STATIC);

    } else if (STREQ(argv[1], "method")) {
        Tcl_SetResult(interp, request->method, TCL_STATIC);

    } else if (STREQ(argv[1], "protocol")) {
        Tcl_SetResult(interp, request->protocol, TCL_STATIC);

    } else if (STREQ(argv[1], "host")) {
        Tcl_SetResult(interp, request->host, TCL_STATIC);

    } else if (STREQ(argv[1], "port")) {
        sprintf(buf, "%d", request->port);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "url")) {
        Tcl_SetResult(interp, request->url, TCL_STATIC);

    } else if (STREQ(argv[1], "query")) {
        Tcl_SetResult(interp, request->query, TCL_STATIC);

    } else if (STREQ(argv[1], "urlc")) {
        sprintf(buf, "%d", request->urlc);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "version")) {
        sprintf(buf, "%1.1f", request->version);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "location")) {
        Tcl_SetResult(interp, Ns_ConnLocation(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "driver")) {
	Tcl_AppendResult(interp, Ns_ConnDriverName(conn), NULL);

    } else if (STREQ(argv[1], "server")) {
        Tcl_SetResult(interp, Ns_ConnServer(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "status")) {
        sprintf(buf, "%d", Ns_ConnResponseStatus(conn));
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "sock")) {
	sprintf(buf, "%d", Ns_ConnSock(conn));
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "id")) {
	sprintf(buf, "%d", Ns_ConnId(conn));
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "flags")) {
	sprintf(buf, "%d", connPtr->flags);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "start")) {
	sprintf(buf, "%d", connPtr->startTime.sec);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);

    } else if (STREQ(argv[1], "close")) {
        if (Ns_ConnClose(conn) != NS_OK) {
            Tcl_SetResult(interp, "could not close connection", TCL_STATIC);
            return TCL_ERROR;
        }

    } else {
        Tcl_AppendResult(interp, "unknown command \"", argv[1],
			 "\":  should be "
                         "authpassword, "
                         "authuser, "
                         "close, "
                         "content, "
                         "contentlength, "
                         "driver, "
                         "encoding, "
                         "flags, "
                         "files, "
                         "form, "
                         "headers, "
                         "host, "
                         "isconnected, "
                         "location, "
                         "method, "
                         "outputheaders, "
                         "peeraddr, "
			 "peerport, "
                         "port, "
                         "protocol, "
                         "url, "
                         "query, "
                         "server, "
			 "sock, "
                         "start, "
                         "status, "
                         "urlc, "
                         "urlv, "
                         "or version", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteContentCmd --
 *
 *	Implments ns_writecontent. 
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
NsTclWriteContentCmd(ClientData arg, Tcl_Interp *interp, int argc,
		     char **argv)
{
    NsInterp	*itPtr = arg;
    Tcl_Channel  chan;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?connid? channel", NULL);
        return TCL_ERROR;
    }
    if (argc == 3 && !NsIsIdConn(argv[1])) {
	Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }
    if (GetChan(interp, argv[argc-1], &chan) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_Flush(chan);
    if (Ns_ConnCopyToChannel(itPtr->conn, itPtr->conn->contentLength, chan) != NS_OK) {
        Tcl_SetResult(interp, "could not copy content (likely client disconnect)",
	    TCL_STATIC);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteContentObjCmd --
 *
 *	Implments ns_writecontent as obj command. 
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
NsTclWriteContentObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp	*itPtr = arg;
    Tcl_Channel  chan;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? channel");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsIsIdConn(Tcl_GetString(objv[1]))) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "bad connid: \"", 
		Tcl_GetString(objv[1]), "\"", NULL);
	return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
        return TCL_ERROR;
    }
    if (GetChan(interp, Tcl_GetString(objv[objc-1]), &chan) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_Flush(chan);
    if (Ns_ConnCopyToChannel(itPtr->conn, itPtr->conn->contentLength, chan) != NS_OK) {
        Tcl_SetResult(interp, "could not copy content (likely client disconnect)",
		TCL_STATIC);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}


static int
GetChan(Tcl_Interp *interp, char *id, Tcl_Channel *chanPtr)
{
    Tcl_Channel chan;
    int mode;

    chan = Tcl_GetChannel(interp, id, &mode);
    if (chan == (Tcl_Channel) NULL) {
        return TCL_ERROR;
    }
    if ((mode & TCL_WRITABLE) == 0) {
        Tcl_AppendResult(interp, "channel \"", id,
                "\" wasn't opened for writing", (char *) NULL);
        return TCL_ERROR;
    }
    *chanPtr = chan;
    return TCL_OK;
}

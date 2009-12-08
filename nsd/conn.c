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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/conn.c,v 1.50 2009/12/08 04:12:19 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static void SetFlag(Ns_Conn *conn, int bit, int flag);
static int GetChan(Tcl_Interp *interp, char *id, Tcl_Channel *chanPtr);
static int GetIndices(Tcl_Interp *interp, Conn *connPtr, Tcl_Obj **objv,
		      int *offPtr, int *lenPtr);
static Tcl_Channel MakeConnChannel(Ns_Conn *conn, int spliceout);



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
 *	Get the content length to be sent from the client.
 *	Note the data may not have been all received if
 *	called in a "read" filter callback.
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
 * Ns_ConnContentAvail --
 *
 *	Get the content currently available from the client.
 *	This will generally be all content unless it's called
 *	during a read filter callback during upload.
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
Ns_ConnContentAvail(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->avail;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContent --
 *
 *	Return pointer to start of content of length conn->contetLength.
 *	Note the content is likely, but not guaranteed to be, null
 *	terminated. Specifically, in the case of file mapped content
 *	which ends on a page boundry, a terminating null may not be
 *	present.  The content is safe to modify in place.
 *
 * Results:
 *	Start of content or NULL on mapping failure.
 *
 * Side effects:
 *	Content file will be mapped if currently only in an open file.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnContent(Ns_Conn *conn)
{
    return NsConnContent(conn, NULL, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentOnDisk --
 *
 *	Return 1 if the content has been copied to a temp file, either
 *	because it was greater than maxinput, or because Ns_ConnContentFd
 *	has been called.  Returns 0, otherwise.  This is useful in the case
 *	the application wants to be as efficient as possible, and not cause
 *	excess file creation or mmap()ing.
 *
 * Results:
 *	0 if content only in RAM, 1 if in /tmp file.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnContentOnDisk(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->tfd > -1;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnContentFd --
 *
 *	Return open fd with request content.  The fd is owned by the
 *	connection and should not be closed by the caller.
 *
 * Results:
 *	Open temp file or -1 on new temp file failure.
 *
 * Side effects:
 *	Content will be copied to a temp file if it is currently only
 *	in memory.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnContentFd(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    int len, fd;
    char *err;

    if (connPtr->tfd < 0) {
	err = NULL;
	fd = Ns_GetTemp();
	if (fd < 0) {
	    err = "Ns_GetTemp";
	} else if ((len = connPtr->contentLength) > 0) {
	    if (write(fd, connPtr->content, len) != len)  {
		err = "write";
	    } else if (lseek(fd, (off_t) 0, SEEK_SET) != 0) {
		err = "lseek";
	    }
	    if (err) {
		Ns_ReleaseTemp(fd);
	    }
	}
	if (!err) {
	    connPtr->tfd = fd;
	} else {
	    Ns_Log(Error, "conn[%d]: could not get fd: %s failed: %s",
		connPtr->id, err, strerror(errno));
	}
    }
    return connPtr->tfd;
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

    return connPtr->server;
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
    return Ns_ConnGetStatus(conn);
}

int
Ns_ConnGetStatus(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;

    return connPtr->status;
}

void
Ns_ConnSetStatus(Ns_Conn *conn, int status)
{
    Conn           *connPtr = (Conn *) conn;

    connPtr->status = status;
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

    return connPtr->peer;
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

    return connPtr->port;
}


/*
 *----------------------------------------------------------------------
 * Ns_SetConnLocationProc --
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
Ns_SetConnLocationProc(Ns_LocationProc *procPtr)
{
    NsServer *servPtr = NsGetInitServer();

    if (servPtr != NULL) {
	servPtr->locationProc = procPtr;
    }
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
    NsServer *servPtr = connPtr->servPtr;
    char *location;

    if (servPtr->locationProc != NULL) {
        location = (*servPtr->locationProc)(conn);
    } else {
	location = connPtr->location;
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
 * Ns_ConnStartTime --
 *
 *	Return the Start Time
 *
 * Results:
 *	Ns_Time value
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Time *
Ns_ConnStartTime(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return &connPtr->times.queue;
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

    if (connPtr->servPtr->opts.flags & SERV_MODSINCE) {
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
 * Ns_ConnGetType, Ns_ConnSetType --
 *
 *	Get (set) the response mime type.
 *
 * Results:
 *	Pointer to current type.
 *
 * Side effects:
 *	May update connection enconding.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnGetType(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->type;
}

void
Ns_ConnSetType(Ns_Conn *conn, char *type)
{
    Conn *connPtr = (Conn *) conn;
    NsServer *servPtr = connPtr->servPtr;
    Tcl_Encoding encoding;
    Ns_DString ds;
    char *charset;
    int len;

    /*
     * If the output type is text, set the output encoding based on 
     * the type charset.  If the charset is missing, use the server
     * default.
     */

    Ns_DStringInit(&ds);
    if (type != NULL && (strncmp(type, "text/", 5) == 0)) {
	encoding = NULL;
    	charset = Ns_FindCharset(type, &len);
    	if (charset == NULL && (charset = servPtr->defcharset) != NULL) {
	    Ns_DStringVarAppend(&ds, type, "; charset=", charset, NULL);
	    type = ds.string;
	    len = ds.length;
	}
    	if (charset != NULL) {
	    encoding = Ns_GetCharsetEncodingEx(charset, len);
	}
	Ns_ConnSetEncoding(conn, encoding);
    }
    ns_free(connPtr->type);
    connPtr->type = ns_strcopy(type);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetEncoding, Ns_ConnSetEncoding --
 *
 *	Get (set) the Tcl_Encoding for the connection which is used
 *	to convert from UTF to specified output character set.
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

    return connPtr->outputEncoding;
}

void
Ns_ConnSetEncoding(Ns_Conn *conn, Tcl_Encoding encoding)
{
    Conn *connPtr = (Conn *) conn;

    connPtr->outputEncoding = encoding;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetUrlEncoding, Ns_ConnSetUrlEncoding --
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
Ns_ConnGetUrlEncoding(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    return connPtr->urlEncoding;
}

void
Ns_ConnSetUrlEncoding(Ns_Conn *conn, Tcl_Encoding encoding)
{
    Conn *connPtr = (Conn *) conn;

    connPtr->urlEncoding = encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCheckConnId --
 *
 *	Given an conn ID, could this be a conn ID?
 *
 * Results:
 *	Boolean. 
 *
 * Side effects:
 *	If interp is non-null, an error message will be left if
 *	necessary.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCheckConnId(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    char *id = Tcl_GetString(objPtr);

    if (id[0] != 'c') {
	if (interp != NULL) {
	    Tcl_AppendResult(interp, "invalid connid: ", id, NULL);
	}
	return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetWriteEncodedFlag --
 * Ns_ConnGetKeepAliveFlag --
 * Ns_ConnGetGzipFlag --
 *
 *	Get the current write encoding, keepalive, or gzip flag.
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
Ns_ConnGetWriteEncodedFlag(Ns_Conn *conn)
{
    return (conn->flags & NS_CONN_WRITE_ENCODED);
}

int
Ns_ConnGetKeepAliveFlag(Ns_Conn *conn)
{
    return (conn->flags & NS_CONN_KEEPALIVE);
}

int
Ns_ConnGetGzipFlag(Ns_Conn *conn)
{
    return (conn->flags & NS_CONN_GZIP);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetWriteEncodedFlag --
 * Ns_ConnSetKeepAliveFlag --
 * Ns_ConnSetGzipFlag --
 *
 *	Set the current write encoding, keepalive, or gzip flag.
 *
 * Results:
 *	void. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnSetWriteEncodedFlag(Ns_Conn *conn, int flag)
{
    SetFlag(conn, NS_CONN_WRITE_ENCODED, flag);
}

void
Ns_ConnSetKeepAliveFlag(Ns_Conn *conn, int flag)
{
    SetFlag(conn, NS_CONN_KEEPALIVE, flag);
}

void
Ns_ConnSetGzipFlag(Ns_Conn *conn, int flag)
{
    SetFlag(conn, NS_CONN_GZIP, flag);
}

static void
SetFlag(Ns_Conn *conn, int bit, int flag)
{
    if (flag) {
        conn->flags |= bit;
    } else {
        conn->flags &= ~bit;
    }
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
    Tcl_Encoding  encoding, *encodingPtr;
    Tcl_Channel   chan;
    Tcl_Obj	 *result;
    Tcl_HashSearch search;
    Ns_ConnFile	 *filePtr;
    int		  idx, off, len, flag, fd;
    char	 *content;

    static CONST char *opts[] = {
         "authpassword", "authuser", "channel", "close",
	 "contentavail", "content", "contentlength", "contentsentlength",
	 "contentchannel", "copy", "driver", "encoding", "files",
	 "fileoffset", "filelength", "fileheaders", "flags", "form",
	 "headers", "host", "id", "isconnected", "location", "method",
	 "outputheaders", "peeraddr", "peerport", "port", "protocol",
	 "query", "request", "server", "sock", "start", "status",
	 "url", "urlc", "urlencoding", "urlv", "version",
	 "write_encoded", "interp", NULL
    };
    enum {
	 CAuthPasswordIdx, CAuthUserIdx, CChannelIdx, CCloseIdx, CAvailIdx, CContentIdx,
	 CContentLengthIdx, CContentSentLenIdx, CContentChannelIdx, CCopyIdx, CDriverIdx,
	 CEncodingIdx, CFilesIdx, CFileOffIdx, CFileLenIdx,
	 CFileHdrIdx, CFlagsIdx, CFormIdx, CHeadersIdx, CHostIdx,
	 CIdIdx, CIsConnectedIdx, CLocationIdx, CMethodIdx,
	 COutputHeadersIdx, CPeerAddrIdx, CPeerPortIdx, CPortIdx,
	 CProtocolIdx, CQueryIdx, CRequestIdx, CServerIdx, CSockIdx,
	 CStartIdx, CStatusIdx, CUrlIdx, CUrlcIdx, CUrlEncodingIdx,
	 CUrlvIdx, CVersionIdx, CWriteEncodedIdx, CInterpIdx
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

    /*
     * Only the "isconnected" option operates without a conn.
     */

    if (opt == CIsConnectedIdx) {
	Tcl_SetBooleanObj(result, itPtr->conn ? 1 : 0);
	return TCL_OK;
    }
    if (NsTclGetConn(itPtr, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    request = conn->request;
    connPtr = (Conn *) conn;
    switch (opt) {

	case CIsConnectedIdx:
	    /* NB: Not reached - silence compiler warning. */
	    break;
		
	case CUrlvIdx:
	    if (objc == 2 || (objc == 3 && NsTclCheckConnId(NULL, objv[2]))) {
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

	case CAvailIdx:
	    Tcl_SetIntObj(result, connPtr->avail);
	    break;

	case CContentChannelIdx:
	    fd = Ns_ConnContentFd(conn);
	    if (fd >= 0 && (fd = dup(fd)) >= 0) {
		/* NB: Dup the fd so the channel can be safely closed later. */
		chan = Tcl_MakeFileChannel((ClientData) fd, TCL_READABLE);
	 	if (chan == NULL) {
		    close(fd);
		} else {
		    Tcl_RegisterChannel(interp, chan);
		    Tcl_SetResult(interp, Tcl_GetChannelName(chan),
				  TCL_VOLATILE);
		}
	    }
	    break;

  
	case CContentIdx:
	    if (objc != 2 && objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "?off len?");
		return TCL_ERROR;
	    }
	    if (objc == 2) {
	    	Tcl_SetResult(interp, Ns_ConnContent(conn), TCL_STATIC);
	    } else {
		if (GetIndices(interp, connPtr, objv+2, &off, &len) != TCL_OK) {
		    return TCL_ERROR;
		}
		result = Tcl_NewStringObj(Ns_ConnContent(conn)+off, len);
		Tcl_SetObjResult(interp, result);
	    }
	    break;
	    
	case CContentLengthIdx:
	    Tcl_SetIntObj(result, conn->contentLength);
	    break;

	case CEncodingIdx:
	case CUrlEncodingIdx:
	    if (opt == CEncodingIdx) {
		encodingPtr = &connPtr->outputEncoding;
	    } else {
		encodingPtr = &connPtr->urlEncoding;
	    }
	    if (objc > 2) {
    		encoding = Ns_GetEncoding(Tcl_GetString(objv[2]));
    		if (encoding == NULL) {
		    Tcl_AppendResult(interp, "no such encoding: ",
				     Tcl_GetString(objv[2]), NULL);
		    return TCL_ERROR;
		}
		*encodingPtr = encoding;
	    }
	    encoding = *encodingPtr;
	    if (encoding != NULL) {
    	    	Tcl_SetResult(interp, (char *) Tcl_GetEncodingName(encoding),
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
	     /* NB: Ignore any cached form if query is no longer valid. */
	    if (!NsCheckQuery(conn)) {
		itPtr->nsconn.flags &= ~CONN_TCLFORM;
	    }
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
	    if (objc != 2) {
		Tcl_WrongNumArgs(interp, 2, objv, NULL);
		return TCL_ERROR;
	    }
	    filePtr = Ns_ConnFirstFile(conn, &search);
	    while (filePtr != NULL) {
		Tcl_AppendElement(interp, filePtr->name);
		filePtr = Ns_ConnNextFile(&search);
	    }
	    break;

	case CFileOffIdx:
	case CFileLenIdx:
	case CFileHdrIdx:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "file");
		return TCL_ERROR;
	    }
	    filePtr = Ns_ConnGetFile(conn, Tcl_GetString(objv[2]));
	    if (filePtr == NULL) {
		Tcl_AppendResult(interp, "no such file: ",
				 Tcl_GetString(objv[2]), NULL);
		return TCL_ERROR;
	    }
	    if (opt == CFileOffIdx) {
	    	Tcl_SetLongObj(result, (long) filePtr->offset);
	    } else if (opt == CFileLenIdx) {
	    	Tcl_SetLongObj(result, (long) filePtr->length);
	    } else {
		Ns_TclEnterSet(interp, filePtr->headers, NS_TCL_SET_STATIC);
	    }
	    break;

	case CCopyIdx:
	    if (objc != 5) {
		Tcl_WrongNumArgs(interp, 2, objv, "off len chan");
		return TCL_ERROR;
	    }
	    if (GetIndices(interp, connPtr, objv+2, &off, &len) != TCL_OK ||
		GetChan(interp, Tcl_GetString(objv[4]), &chan) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (len > 0) {
	    	content = Ns_ConnContent(conn);
	    	if (content == NULL) {
		    Tcl_SetResult(interp, "could not get content", TCL_STATIC);
		    return TCL_ERROR;
		}
	    	if (Tcl_Write(chan, content + off, len) != len) {
		    Tcl_AppendResult(interp, "could not write ",
			Tcl_GetString(objv[3]), " bytes to ",
		    	Tcl_GetString(objv[4]), ": ",
			Tcl_PosixError(interp), NULL);
		    return TCL_ERROR;
		}
	    }
	    break;

        case CWriteEncodedIdx:
	    if (objc > 2) {
		if (Tcl_GetBooleanFromObj(interp, objv[2], &flag) != TCL_OK) {
                    return TCL_ERROR;
                }
                Ns_ConnSetWriteEncodedFlag(conn, flag);
           }
	   Tcl_SetBooleanObj(result, Ns_ConnGetWriteEncodedFlag(conn));
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
	    if (objc > 2) {
		int status;
		if (Tcl_GetIntFromObj(interp, objv[2], &status) != TCL_OK) {
		    return TCL_ERROR;
		}
		Ns_ConnSetStatus(conn, status);
	    }
	    Tcl_SetIntObj(result, Ns_ConnGetStatus(conn));
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
	    Ns_TclSetTimeObj(result, &connPtr->times.queue);
	    break;

	case CCloseIdx:
	    if (Ns_ConnClose(conn) != NS_OK) {
		Tcl_SetResult(interp, "could not close connection", TCL_STATIC);
		return TCL_ERROR;
	    }
	    break;

        case CChannelIdx:
	  chan = MakeConnChannel(conn, 1);
	  if (chan == NULL) {
            Tcl_AppendResult(interp, Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
	  }
	  Tcl_RegisterChannel(interp, chan);
	  Tcl_SetStringObj(result, Tcl_GetChannelName(chan), -1);
	  break;

        case CContentSentLenIdx:
	  if (objc == 2) {
	    Tcl_SetIntObj(result, connPtr->nContentSent);
	  } else if (objc == 3) {
	    if (Tcl_GetIntFromObj(interp, objv[2], &connPtr->nContentSent) != TCL_OK) {
	      return TCL_ERROR;
	    }
	  } else {
	    Tcl_WrongNumArgs(interp, 2, objv, "?value?");
	    return TCL_ERROR;
	  }
	  break;

	case CInterpIdx:
	    Tcl_SetLongObj(result, (long) interp);
	    break;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteContentObjCmd --
 *
 *	Implements ns_writecontent as obj command. 
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
    Ns_Conn	*conn;
    Tcl_Channel  chan;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "?connid? channel");
        return TCL_ERROR;
    }
    if (objc == 3 && !NsTclCheckConnId(interp, objv[1])) {
	return TCL_ERROR;
    }
    if (NsTclGetConn(itPtr, &conn) != TCL_OK) {
        return TCL_ERROR;
    }
    if (GetChan(interp, Tcl_GetString(objv[objc-1]), &chan) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_Flush(chan);
    if (Ns_ConnCopyToChannel(conn, (size_t) conn->contentLength, chan) != NS_OK) {
        Tcl_SetResult(interp, "could not copy content (likely client disconnect)",
		TCL_STATIC);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStartContentObjCmd --
 *
 *      Set connPtr->sendState to "Content" and set the charset/encoding
 *      to use for further data.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	connPtr->sendState and connPtr->encoding may be set.
 *
 *----------------------------------------------------------------------
 */

int
NsTclStartContentObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
                        Tcl_Obj **objv)
{
    Tcl_Encoding  encoding;
    Ns_Conn	 *conn;
    char	 *opt;
    static CONST char *flags[] = {
	"-charset", "-type", NULL
    };
    enum {
	FCharsetIdx, FTypeIdx
    } flag;

    if (objc != 1 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "?-charset charset|-type type?");
	return TCL_ERROR;
    }
    encoding = NULL;
    if (objc == 3) {
	if (Tcl_GetIndexFromObj(interp, objv[1], flags, "flag", 0,
		(int *) &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
	opt = Tcl_GetString(objv[2]);
	switch (flag) {
	case FCharsetIdx:
	    encoding = Ns_GetCharsetEncoding(opt);
	    break;
	case FTypeIdx:
	    encoding  = Ns_GetTypeEncoding(opt);
	    break;
	}
	if (encoding == NULL) {
	    Tcl_AppendResult(interp, "no encoding for ",
		(flags[flag])+1, " \"", opt, "\"", NULL);
	    return TCL_ERROR;
	}
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    } 
    Ns_ConnSetWriteEncodedFlag(conn, NS_TRUE);
    Ns_ConnSetEncoding(conn, encoding);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetChan --
 *
 *	Return an open channel.
 *
 * Results:
 *	TCL_OK if given a valid channel id, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Channel is set in given chanPtr or error message left in
 *	given interp.
 *
 *----------------------------------------------------------------------
 */

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


/*
 *----------------------------------------------------------------------
 *
 * GetIndices --
 *
 *	Return offset and length from given Tcl_Obj's.
 *
 * Results:
 *	TCL_OK if objects are valid offsets, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Given offPtr and lenPtr are updated with indices or error
 *	message is left in the interp.
 *
 *----------------------------------------------------------------------
 */

static int
GetIndices(Tcl_Interp *interp, Conn *connPtr, Tcl_Obj **objv, int *offPtr, int *lenPtr)
{
    int off, len;

    if (Tcl_GetIntFromObj(interp, objv[0], &off) != TCL_OK ||
	Tcl_GetIntFromObj(interp, objv[1], &len) != TCL_OK) {
	return TCL_ERROR;
    }
    if (off < 0 || off > connPtr->contentLength) {
	Tcl_AppendResult(interp, "invalid offset: ", Tcl_GetString(objv[0]), NULL);
	return TCL_ERROR;
    }
    if (len < 0 || len > (connPtr->contentLength - off)) {
	Tcl_AppendResult(interp, "invalid length: ", Tcl_GetString(objv[1]), NULL);
	return TCL_ERROR;
    }
    *offPtr = off;
    *lenPtr = len;
    return TCL_OK;
}
/*----------------------------------------------------------------------------
 * MakeConnChannel --
 *
 *      Wraps a Tcl channel arround the current connection socket
 *      and returns the channel handle to the caller.
 *  
 * Result:
 *      Tcl_Channel handle or NULL.
 *
 * Side Effects:
 *      Flushes the connection socket before dup'ing.
 *      The resulting Tcl channel is set to blocking mode.
 *
 *----------------------------------------------------------------------------
 */
static Tcl_Channel
MakeConnChannel(Ns_Conn *conn, int spliceout)
{
    Tcl_Channel chan;
    int         sock;
    Conn       *connPtr = (Conn *) conn;
    
    /*
     * Assures the socket is flushed
     */

    Ns_WriteConn(conn, NULL, 0);

    if (spliceout) {
        sock = connPtr->sockPtr->sock;
        connPtr->sockPtr->sock = -1;
    } else {
        sock = ns_sockdup(connPtr->sockPtr->sock);
    }

    if (sock == -1) {
        return NULL;
    }
    
    /*
     * At this point we may also set some other
     * chan config options (binary,encoding, etc)
     */

    Ns_SockSetBlocking(sock);

    /*
     * Wrap a Tcl TCP channel arround the socket.
     */
    
    chan = Tcl_MakeTcpClientChannel((ClientData)sock);
    if (chan == NULL && spliceout) {
        connPtr->sockPtr->sock = sock;
    }

    return chan;
}

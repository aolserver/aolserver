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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/conn.c,v 1.10 2001/03/22 21:30:17 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#define IOBUFSZ 2048

/*
 * Local functions defined in this file
 */

static int ConnSend(Ns_Conn *, int nsend, Tcl_Channel chan,
    	    	    FILE *fp, int fd);
static int ConnCopy(Ns_Conn *conn, size_t tocopy, Ns_DString *dsPtr,
    	    	    Tcl_Channel chan, FILE *fp, int fd);

static Ns_LocationProc *locationPtr = NULL;

/*
 * Macros for executing connection driver procedures.
 */
 
#define CONN_CLOSED(conn)		((conn)->flags & NS_CONN_CLOSED)


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnInit --
 *
 *	initialize the socket driver (when the connection is made) 
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Depends on socket driver 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnInit(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    if (connPtr->drvPtr->initProc != NULL &&
	(*connPtr->drvPtr->initProc)(connPtr->drvData) != NS_OK) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnRead --
 *
 *	Read data from the connection, either by doing a read() or 
 *	calling the registered read function if it's a socket driver 
 *
 * Results:
 *	Number of bytes read, or -1 if error 
 *
 * Side effects:
 *	Data will be read from a socket (or whatever the socket 
 *	driver implements) 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnRead(Ns_Conn *conn, void *vbuf, int toread)
{
    Conn *connPtr = (Conn *) conn;
    int nread;

    if (CONN_CLOSED(connPtr)) {
	nread = -1;
    } else {
    	nread = (*connPtr->drvPtr->readProc)(connPtr->drvData, vbuf, toread);
        if (nread > 0 && connPtr->readState == Content) {
            connPtr->nContent += nread;
	}
    }
    return nread;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnWrite --
 *
 *	Writes data to a socket/socket driver 
 *
 * Results:
 *	Number of bytes written, -1 for error 
 *
 * Side effects:
 *	Stuff may be written to a socket/socket driver.
 *
 *      NOTE: This may not write all of the data you send it!
 *            Use Ns_WriteConn if that's your desire!
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnWrite(Ns_Conn *conn, void *vbuf, int towrite)
{
    Conn *connPtr = (Conn *) conn;
    int nwrote;
    
    if (CONN_CLOSED(connPtr)) {
	nwrote = -1;
    } else {
    	nwrote = (*connPtr->drvPtr->writeProc)(connPtr->drvData, vbuf, towrite);
    	if (nwrote > 0 && connPtr->sendState == Content) {
            connPtr->nContentSent += nwrote;
    	}
    }
    return nwrote;
}


/*
 *-----------------------------------------------------------------
 *
 * Ns_ConnClose - Close a connection.
 *
 * Results:
 *	Always NS_OK.
 * 
 * Side effects:
 *	The underlying socket in the connection is closed or moved
 *	to the waiting keep-alive list.
 *
 *-----------------------------------------------------------------
 */

int
Ns_ConnClose(Ns_Conn *conn)
{
    Conn             *connPtr = (Conn *)conn;
    
    if (!CONN_CLOSED(connPtr)) {
        if (!NsKeepAlive(conn)) {
    	    (*connPtr->drvPtr->closeProc)(connPtr->drvData);
	}
	connPtr->flags |= NS_CONN_CLOSED;
	if (connPtr->interp != NULL) {
	    NsRunAtClose(connPtr->interp);
	}
    }
    return NS_OK;
}


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

    if (connPtr->peer == NULL && connPtr->drvPtr->peerProc != NULL) {
	connPtr->peer = (*connPtr->drvPtr->peerProc)(conn);
	if (connPtr->peer != NULL) {
	    strncpy(connPtr->peerBuf, connPtr->peer,
	    	    sizeof(connPtr->peerBuf)-1);
	}
	connPtr->peer = connPtr->peerBuf;
    }
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

    if (connPtr->drvPtr->peerPortProc == NULL) {
	return 0;
    }
    return (*connPtr->drvPtr->peerPortProc)(connPtr->drvData);
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
    locationPtr = procPtr;
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

    if (locationPtr != NULL) {
        return (*locationPtr)(conn);
    } else if (connPtr->drvPtr->locationProc == NULL) {
	return NULL;
    }
    return (*connPtr->drvPtr->locationProc)(connPtr->drvData);
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

    if (connPtr->drvPtr->hostProc == NULL) {
	return NULL;
    }
    return (*connPtr->drvPtr->hostProc)(connPtr->drvData);
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

    if (connPtr->drvPtr->portProc == NULL) {
	return 0;
    }
    return (*connPtr->drvPtr->portProc)(connPtr->drvData);
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

    if (connPtr->drvPtr->sockProc == NULL) {
	return -1;
    }
    return (*connPtr->drvPtr->sockProc)(connPtr->drvData);
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

    if (connPtr->drvPtr->nameProc == NULL) {
	return NULL;
    }
    return (*connPtr->drvPtr->nameProc)(connPtr->drvData);
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

    return connPtr->drvData;
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
 * Ns_ConnReadLine --
 *
 *	Read a line (\r or \n terminated) from the conn 
 *
 * Results:
 *	NS_OK or NS_ERROR 
 *
 * Side effects:
 *	Stuff may be read 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReadLine(Ns_Conn *conn, Ns_DString *dsPtr, int *nreadPtr)
{
    Conn	   *connPtr = (Conn *) conn;
    char            buf[1];
    int             n, nread;
    int		    ret = NS_OK;

    nread = 0;
    do {
        n = Ns_ConnRead(conn, buf, 1);
        if (n == 1) {
            ++nread;
            if (buf[0] == '\n') {
                n = 0;
            } else {
                Ns_DStringNAppend(dsPtr, buf, 1);
            }
        }
    } while (n == 1 && nread <= connPtr->servPtr->limits.maxline);
    if (n < 0) {
        ret = NS_ERROR;
    } else {
	n = dsPtr->length;
	if (n > 0 && dsPtr->string[n-1] == '\r') {
	    Ns_DStringTrunc(dsPtr, n-1);
	}
    }
    if (nreadPtr != NULL) {
	*nreadPtr = nread;
    }
    return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_WriteConn --
 *
 *	This will write a buffer to the conn. It promises to write 
 *	all of it. 
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	Stuff may be written 
 *
 *----------------------------------------------------------------------
 */

int
Ns_WriteConn(Ns_Conn *conn, char *buf, int len)
{
    int             nwrote;
    int             status;

    status = NS_OK;
    while (len > 0 && status == NS_OK) {
        nwrote = Ns_ConnWrite(conn, buf, len);
        if (nwrote < 0) {
            status = NS_ERROR;
        } else {
            len -= nwrote;
            buf += nwrote;
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPuts --
 *
 *	Write a null-terminated string to the conn; no trailing 
 *	newline will be appended despite the name. 
 *
 * Results:
 *	See Ns_WriteConn 
 *
 * Side effects:
 *	See Ns_WriteConn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnPuts(Ns_Conn *conn, char *string)
{
    return Ns_WriteConn(conn, string, strlen(string));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSendDString --
 *
 *	Write contents of a DString
 *
 * Results:
 *	See Ns_WriteConn 
 *
 * Side effects:
 *	See Ns_WriteConn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSendDString(Ns_Conn *conn, Ns_DString *dsPtr)
{
    return Ns_WriteConn(conn, dsPtr->string, dsPtr->length);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSendChannel, Fp, Fd --
 *
 *	Send an open channel, FILE, or fd.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	See ConnSend().
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnSendChannel(Ns_Conn *conn, Tcl_Channel chan, int nsend)
{
    return ConnSend(conn, nsend, chan, NULL, -1);
}

int
Ns_ConnSendFp(Ns_Conn *conn, FILE *fp, int nsend)
{
    return ConnSend(conn, nsend, NULL, fp, -1);
}

int
Ns_ConnSendFd(Ns_Conn *conn, int fd, int nsend)
{
    Conn *connPtr = (Conn*) conn;
    int status, min;

    min = connPtr->servPtr->limits.sendfdmin;
    if (CONN_CLOSED(connPtr)) {
	status = NS_ERROR; 
    } else if (connPtr->drvPtr->sendFdProc != NULL && nsend > min) {
    	status = (*connPtr->drvPtr->sendFdProc)(connPtr->drvData, fd, nsend);
    } else {
        status = ConnSend(conn, nsend, NULL, NULL, fd);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCopyToDString, File, Fd, Channel --
 *
 *	Copy data from a connection to a dstring, channel, FILE, or 
 *	fd. 
 *
 * Results:
 *	NS_OK or NS_ERROR 
 *
 * Side effects:
 *	See ConnCopy().
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnCopyToDString(Ns_Conn *conn, size_t ncopy, Ns_DString *dsPtr)
{
    return ConnCopy(conn, ncopy, dsPtr, NULL, NULL, -1);
}

int
Ns_ConnCopyToChannel(Ns_Conn *conn, size_t ncopy, Tcl_Channel chan)
{
    return ConnCopy(conn, ncopy, NULL, chan, NULL, -1);
}

int
Ns_ConnCopyToFile(Ns_Conn *conn, size_t ncopy, FILE *fp)
{
    return ConnCopy(conn, ncopy, NULL, NULL, fp, -1);
}

int
Ns_ConnCopyToFd(Ns_Conn *conn, size_t ncopy, int fd)
{
    return ConnCopy(conn, ncopy, NULL, NULL, NULL, fd);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnFlushContent --
 *
 *	Finish reading the data waiting to be read.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	NOTE: Content only gets read if the server's 'flushcontent' flag
 *            is set to true
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnFlushContent(Ns_Conn *conn)
{
    Conn           *connPtr;
    char            buf[IOBUFSZ];
    int             nread, nflush, toread, status;

    connPtr = (Conn *) conn;
    status = NS_OK;
    if (connPtr->servPtr->opts.flushcontent && connPtr->contentLength > 0) {
        nflush = connPtr->contentLength - connPtr->nContent;
        while (nflush > 0) {
            toread = nflush;
            if (toread > sizeof(buf)) {
                toread = sizeof(buf);
            }
            nread = Ns_ConnRead(conn, buf, toread);
            if (nread <= 0) {
                status = NS_ERROR;
                break;
            }
            nflush -= nread;
        }
    }

    return status;
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
 * Ns_ConnGets --
 *
 *	Read in a string from a connection, stopping when either 
 *	we've run out of data, hit a newline, or had an error 
 *
 * Results:
 *	Pointer to given buffer or NULL on error.
 *
 * Side effects:
 *	
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ConnGets(char *buf, size_t bufsize, Ns_Conn *conn)
{
    char *p;

    p = buf;
    while (bufsize > 1) {
	if (Ns_ConnRead(conn, p, 1) != 1) {
	    return NULL;
	}
        if (*p++ == '\n') {
            break;
	}
        --bufsize;
    }
    *p = '\0';
    return buf;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReadHeaders --
 *
 *	Read the headers and insert them into the passed-in set 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Stuff will be read from the conn 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReadHeaders(Ns_Conn *conn, Ns_Set *set, int *nreadPtr)
{
    Ns_DString      ds;
    Conn           *connPtr = (Conn *) conn;
    int             status, nread, nline, max;

    Ns_DStringInit(&ds);
    nread = 0;
    status = NS_OK;
    max = connPtr->servPtr->limits.maxheaders;
    while (nread < max && status == NS_OK) {
        Ns_DStringTrunc(&ds, 0);
        status = Ns_ConnReadLine(conn, &ds, &nline);
        if (status == NS_OK) {
            nread += nline;
            if (nread > max) {
                status = NS_ERROR;
            } else {
                if (ds.string[0] == '\0') {
		    connPtr->readState = Content;
                    break;
                }
                status = Ns_ParseHeader(set, ds.string,
			connPtr->servPtr->opts.hdrcase);
            }
        }
    }
    if (nreadPtr != NULL) {
	*nreadPtr = nread;
    }
    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseHeader --
 *
 *	Consume a header line, handling header continuation, placing
 *	results in given set.
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

int
Ns_ParseHeader(Ns_Set *set, char *line, Ns_HeaderCaseDisposition disp)
{
    char           *key, *sep;
    char           *value;
    int             index;
    Ns_DString	    ds;

    /* 
     * Header lines are first checked if they continue a previous
     * header indicated by any preceeding white space.  Otherwise,
     * they must be in well form key: value form.
     */

    if (isspace(UCHAR(*line))) {
        index = Ns_SetLast(set);
        if (index < 0) {
	    return NS_ERROR;	/* Continue before first header. */
        }
        while (isspace(UCHAR(*line))) {
            ++line;
        }
        if (*line != '\0') {
	    value = Ns_SetValue(set, index);
	    Ns_DStringInit(&ds);
	    Ns_DStringVarAppend(&ds, value, " ", line, NULL);
	    Ns_SetPutValue(set, index, ds.string);
	    Ns_DStringFree(&ds);
	}
    } else {
        sep = strchr(line, ':');
        if (sep == NULL) {
	    return NS_ERROR;	/* Malformed header. */
	}
        *sep = '\0';
        value = sep + 1;
        while (*value != '\0' && isspace(UCHAR(*value))) {
            ++value;
        }
        index = Ns_SetPut(set, line, value);
        key = Ns_SetKey(set, index);
	if (disp == ToLower) {
            while (*key != '\0') {
	        if (isupper(UCHAR(*key))) {
            	    *key = tolower(UCHAR(*key));
		}
            	++key;
	    }
	} else if (disp == ToUpper) {
            while (*key != '\0') {
	        if (islower(UCHAR(*key))) {
		    *key = toupper(UCHAR(*key));
		}
		++key;
	    }
        }
        *sep = ':';
    }
    return NS_OK;
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
    Ns_Request *request;
    Ns_Set     *form;
    int		urlv, idx;

    if (argc < 2) {
badargs:
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " cmd ", NULL);
        return TCL_ERROR;
    }
    urlv = STREQ(argv[1], "urlv");
    if (!urlv) {
	if (argc > 3) {
	    goto badargs;
	}
	if (argc == 3 && !NsIsIdConn(argv[2])) {
badconn:
	    Tcl_AppendResult(interp, "invalid connid: \"", argv[2], "\"", NULL);
	    return TCL_ERROR;
	}
    } else {
	/*
	 * Special treatment for urlv command.
	 */

	switch (argc) {
	case 2:
	    idx = -1;
	    break;

	case 3:
	    /* NB: Ambiguous, check conn id then assume arg is index. */
	    if (NsIsIdConn(argv[2])) {
		idx = -1;
	    } else if (Tcl_GetInt(interp, argv[2], &idx) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;

	case 4:
	    if (!NsIsIdConn(argv[2])) {
		goto badconn;
	    }
	    if (Tcl_GetInt(interp, argv[3], &idx) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;

	default:
	    goto badargs;
	}
    }

    if (STREQ(argv[1], "isconnected")) {
	Tcl_SetResult(interp, (connPtr == NULL) ? "0" : "1", TCL_STATIC);
	return TCL_OK;
    }

    /*
     * All remaining commands require a conn.
     */

    if (connPtr == NULL) {
        Tcl_AppendResult(interp, "no current connection", NULL);
        return TCL_ERROR;
    }
    request = connPtr->request;
    if (urlv) {
	if (idx < 0) {
	    for (idx = 0; idx < request->urlc; idx++) {
	        Tcl_AppendElement(interp, request->urlv[idx]);
	    }
	} else if (idx >= 0 && idx < request->urlc) {
	    Tcl_SetResult(interp, request->urlv[idx], TCL_VOLATILE);
	}
    } else if (STREQ(argv[1], "authuser")) {
        Tcl_SetResult(interp, connPtr->authUser, TCL_STATIC);

    } else if (STREQ(argv[1], "authpassword")) {
        Tcl_SetResult(interp, connPtr->authPasswd, TCL_STATIC);

    } else if (STREQ(argv[1], "contentlength")) {
        sprintf(interp->result, "%u", (unsigned) conn->contentLength);

    } else if (STREQ(argv[1], "peeraddr")) {
        Tcl_SetResult(interp, Ns_ConnPeer(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "peerport")) {
	sprintf(interp->result, "%d", Ns_ConnPeerPort(conn));

    } else if (STREQ(argv[1], "headers")) {
	if (itPtr->nsconn.flags & CONN_TCLHDRS) {
            Tcl_SetResult(interp, itPtr->nsconn.hdrs, TCL_STATIC);
	} else {
            Ns_TclEnterSet(interp, connPtr->headers, NS_TCL_SET_STATIC);
	    strcpy(itPtr->nsconn.hdrs, interp->result);
	    itPtr->nsconn.flags |= CONN_TCLHDRS;
	}

    } else if (STREQ(argv[1], "outputheaders")) {
	if (itPtr->nsconn.flags & CONN_TCLHDRS) {
            Tcl_SetResult(interp, itPtr->nsconn.outhdrs, TCL_STATIC);
	} else {
            Ns_TclEnterSet(interp, connPtr->outputheaders, NS_TCL_SET_STATIC);
	    strcpy(itPtr->nsconn.outhdrs, interp->result);
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
        	strcpy(itPtr->nsconn.form, interp->result);
	    }
	    itPtr->nsconn.flags |= CONN_TCLFORM;
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
        sprintf(interp->result, "%d", request->port);

    } else if (STREQ(argv[1], "url")) {
        Tcl_SetResult(interp, request->url, TCL_STATIC);

    } else if (STREQ(argv[1], "query")) {
        Tcl_SetResult(interp, request->query, TCL_STATIC);

    } else if (STREQ(argv[1], "urlc")) {
        sprintf(interp->result, "%d", request->urlc);

    } else if (STREQ(argv[1], "version")) {
        sprintf(interp->result, "%1.1f", request->version);

    } else if (STREQ(argv[1], "location")) {
        Tcl_SetResult(interp, Ns_ConnLocation(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "driver")) {
	Tcl_AppendResult(interp, Ns_ConnDriverName(conn), NULL);

    } else if (STREQ(argv[1], "server")) {
        Tcl_SetResult(interp, Ns_ConnServer(conn), TCL_STATIC);

    } else if (STREQ(argv[1], "status")) {
        sprintf(interp->result, "%d", Ns_ConnResponseStatus(conn));

    } else if (STREQ(argv[1], "sock")) {
	sprintf(interp->result, "%d", Ns_ConnSock(conn));

    } else if (STREQ(argv[1], "id")) {
	sprintf(interp->result, "%d", Ns_ConnId(conn));

    } else if (STREQ(argv[1], "flags")) {
	sprintf(interp->result, "%d", connPtr->flags);

    } else if (STREQ(argv[1], "start")) {
	sprintf(interp->result, "%d", (int) connPtr->startTime);

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
                         "flags, "
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
    int          mode;
    int	         fileArg = 1;   /* assume no-conn parameter usage */
    Tcl_Channel  chan;

    if (argc == 3) {
	/*
	 * They must have specified a conn ID.  Make sure it's a valid
	 * conn ID.  If not, it's an error.
	 */
	
	if (NsIsIdConn(argv[1])== NS_FALSE) {
	    Tcl_AppendResult(interp, "bad connid: \"", argv[1], "\"", NULL);
	    return TCL_ERROR;
	}
	fileArg = 2;
    } else if (argc > 3 || argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " cmd ", NULL);
        return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
        Tcl_AppendResult(interp, "no connection", NULL);
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, argv[fileArg], &mode);
    if (chan == (Tcl_Channel) NULL) {
        return TCL_ERROR;
    }
    if ((mode & TCL_WRITABLE) == 0) {
        Tcl_AppendResult(interp, "channel \"", argv[fileArg],
                "\" wasn't opened for writing", (char *) NULL);
        return TCL_ERROR;
    }
    Tcl_Flush(chan);
    if (Ns_ConnCopyToChannel(itPtr->conn, itPtr->conn->contentLength, chan) != NS_OK) {
        Tcl_AppendResult(interp, "Error writing content: ",
	    Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnCopy --
 *
 *	Copy connection content to a dstring, channel, FILE, or fd.
 *
 * Results:
 *  	NS_OK or NS_ERROR if not all content could be read.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ConnCopy(Ns_Conn *conn, size_t tocopy, Ns_DString *dsPtr,
    	 Tcl_Channel chan, FILE *fp, int fd)
{
    char        buf[IOBUFSZ];
    char       *bufPtr;
    int		toread, towrite, nread, nwrote;

    while (tocopy > 0) {
        toread = tocopy;
        if (toread > sizeof(buf)) {
            toread = sizeof(buf);
        }
        nread = Ns_ConnRead(conn, buf, toread);
	if (nread < 0) {
	    return NS_ERROR;
	}
	towrite = nread;
    	bufPtr = buf;
        while (towrite > 0) {
	    if (dsPtr != NULL) {
	    	Ns_DStringNAppend(dsPtr, bufPtr, nread);
		nwrote = nread;
    	    } else if (chan != NULL) {
		nwrote = Tcl_Write(chan, buf, nread);
    	    } else if (fp != NULL) {
        	nwrote = fwrite(bufPtr, 1, nread, fp);
        	if (ferror(fp)) {
		    nwrote = -1;
		}
	    } else {
	    	nwrote = write(fd, bufPtr, nread);
	    }
	    if (nwrote < 0) {
	    	return NS_ERROR;
	    }
            towrite -= nwrote;
            bufPtr += nwrote;
        }
        tocopy -= nread;
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ConnSend --
 *
 *	Send content from a channel, FILE, or fd.
 *
 * Results:
 *  	NS_OK or NS_ERROR if a write failed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ConnSend(Ns_Conn *conn, int nsend, Tcl_Channel chan, FILE *fp, int fd)
{
    Conn    	   *connPtr = (Conn *) conn;
    int             toread, nread, status;
    char            buf[IOBUFSZ];

    status = NS_OK;
    while (status == NS_OK && nsend > 0) {
        toread = nsend;
        if (toread > sizeof(buf)) {
            toread = sizeof(buf);
        }
	if (chan != NULL) {
	    nread = Tcl_Read(chan, buf, toread);
	} else if (fp != NULL) {
            nread = fread(buf, 1, toread, fp);
            if (ferror(fp)) {
	    	nread = -1;
	    }
    	} else {
	    nread = read(fd, buf, toread);
    	}
	if (nread == -1) {
	    status = NS_ERROR;
	} else if (nread == 0) { 
            nsend = 0;	/* NB: Silently ignore a truncated file. */
	} else if ((status = Ns_WriteConn(conn, buf, nread)) == NS_OK) {
            nsend -= nread;
	}
    }
    return status;
}

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
 * connio.c --
 *
 *      Handle connection I/O.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/connio.c,v 1.1 2001/04/23 21:16:11 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#define IOBUFSZ 2048

/*
 * Local functions defined in this file
 */

static int ConnSend(Ns_Conn *conn, int nsend, Tcl_Channel chan,
    	    	    FILE *fp, int fd);
static int ConnCopy(Ns_Conn *conn, size_t ncopy, Tcl_Channel chan,
		    FILE *fp, int fd);
 

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
    
    if (connPtr->sockPtr == NULL) {
	nwrote = -1;
    } else {
	nwrote = NsSockSend(connPtr->sockPtr, vbuf, towrite);
    	if (nwrote > 0 && (conn->flags & NS_CONN_SENTHDRS)) {
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
    int		      keep;
    
    if (connPtr->sockPtr != NULL) {
	keep = (conn->flags & NS_CONN_KEEPALIVE) ? 1 : 0;
	NsSockClose(connPtr->sockPtr, keep);
	connPtr->sockPtr = NULL;
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

    while (len > 0) {
        nwrote = Ns_ConnWrite(conn, buf, len);
        if (nwrote < 0) {
	    return NS_ERROR;
	}
        len -= nwrote;
        buf += nwrote;
    }
    return NS_OK;
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
    return ConnSend(conn, nsend, NULL, NULL, fd);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnRead --
 *
 *	Copy data from read-ahead buffers.
 *
 * Results:
 *	Number of bytes copied.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnRead(Ns_Conn *conn, void *vbuf, int toread)
{
    Conn *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;

    if (connPtr->sockPtr == NULL) {
	return -1;
    }
    if (toread > reqPtr->avail) {
	toread = reqPtr->avail;
    }
    memcpy(vbuf, reqPtr->next, toread);
    reqPtr->next  += toread;
    reqPtr->avail -= toread;
    return toread;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCopyToDString --
 *
 *	Copy data from a connection to a dstring.
 *
 * Results:
 *	NS_OK or NS_ERROR 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnCopyToDString(Ns_Conn *conn, size_t tocopy, Ns_DString *dsPtr)
{
    Conn *connPtr = (Conn *) conn;
    Request *reqPtr = connPtr->reqPtr;
    int ncopy = (int) tocopy;

    if (connPtr->sockPtr == NULL || reqPtr->avail < ncopy) {
	return NS_ERROR;
    }
    Ns_DStringNAppend(dsPtr, reqPtr->next, ncopy);
    reqPtr->next  += ncopy;
    reqPtr->avail -= ncopy;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCopyToFile, Fd, Channel --
 *
 *	Copy data from a connection to a channel, FILE, or fd. 
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
Ns_ConnCopyToChannel(Ns_Conn *conn, size_t ncopy, Tcl_Channel chan)
{
    return ConnCopy(conn, ncopy, chan, NULL, -1);
}

int
Ns_ConnCopyToFile(Ns_Conn *conn, size_t ncopy, FILE *fp)
{
    return ConnCopy(conn, ncopy, NULL, fp, -1);
}

int
Ns_ConnCopyToFd(Ns_Conn *conn, size_t ncopy, int fd)
{
    return ConnCopy(conn, ncopy, NULL, NULL, fd);
}


/*
 *----------------------------------------------------------------------
 *
 * ConnCopy --
 *
 *	Copy connection content to a channel, FILE, or fd.
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
ConnCopy(Ns_Conn *conn, size_t tocopy, Tcl_Channel chan, FILE *fp, int fd)
{
    Conn       *connPtr = (Conn *) conn;
    Request    *reqPtr = connPtr->reqPtr;
    int		nwrote;
    int		ncopy = (int) tocopy;

    if (connPtr->sockPtr == NULL || reqPtr->avail < ncopy) {
	return NS_ERROR;
    }
    while (ncopy > 0) {
	if (chan != NULL) {
	    nwrote = Tcl_Write(chan, reqPtr->next, ncopy);
    	} else if (fp != NULL) {
            nwrote = fwrite(reqPtr->next, 1, ncopy, fp);
            if (ferror(fp)) {
		nwrote = -1;
	    }
	} else {
	    nwrote = write(fd, reqPtr->next, ncopy);
	}
	if (nwrote < 0) {
	    return NS_ERROR;
	}
	ncopy -= nwrote;
	reqPtr->next  += nwrote;
	reqPtr->avail -= nwrote;
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

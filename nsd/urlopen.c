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
 * urlopen.c --
 *
 *	Make outgoing HTTP requests.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/urlopen.c,v 1.17 2003/12/28 00:22:06 scottg Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define BUFSIZE 2048

typedef struct Stream {
    SOCKET	sock;
    int		error;
    int		cnt;
    char       *ptr;
    char	buf[BUFSIZE+1];
} Stream;

/*
 * Local functions defined in this file
 */

static int GetLine(Stream *sPtr, Ns_DString *dsPtr);
static int FillBuf(Stream *sPtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_FetchPage --
 *
 *	Fetch a page off of this very server. Url must reference a 
 *	file in the filesystem. 
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	The file contents will be put into the passed-in dstring.
 *
 *----------------------------------------------------------------------
 */

int
Ns_FetchPage(Ns_DString *dsPtr, char *url, char *server)
{
    Ns_DString path;
    int        fd;
    int        nread;
    char       buf[1024];

    Ns_DStringInit(&path);
    Ns_UrlToFile(&path, server, url);
    fd = open(path.string, O_RDONLY|O_BINARY);
    Ns_DStringFree(&path);
    if (fd < 0) {
        return NS_ERROR;
    }
    while ((nread = read(fd, buf, sizeof(buf))) > 0) {
        Ns_DStringNAppend(dsPtr, buf, nread);
    }
    close(fd);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_FetchURL --
 *
 *	Open up an HTTP connection to an arbitrary URL. 
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Page contents will be appended to the passed-in dstring.
 *	Headers returned to us will be put into the passed-in Ns_Set.
 *	The set name will be changed to a copy of the HTTP status line.
 *
 *----------------------------------------------------------------------
 */

int
Ns_FetchURL(Ns_DString *dsPtr, char *url, Ns_Set *headers)
{
    SOCKET  	    sock;
    char    	   *p;
    Ns_DString      ds;
    Stream  	    stream;
    Ns_Request     *request;
    int     	    status, tosend, n;

    status = NS_ERROR;    
    sock = INVALID_SOCKET;
    Ns_DStringInit(&ds);

    /*
     * Parse the URL and open a connection.
     */

    Ns_DStringVarAppend(&ds, "GET ", url, " HTTP/1.0", NULL);
    request = Ns_ParseRequest(ds.string);
    if (request == NULL || request->protocol == NULL ||
	!STREQ(request->protocol, "http") || request->host == NULL) {
        Ns_Log(Notice, "urlopen: invalid url '%s'", url);
        goto done;
    }
    if (request->port == 0) {
        request->port = 80;
    }
    sock = Ns_SockConnect(request->host, request->port);    
    if (sock == INVALID_SOCKET) {
	Ns_Log(Error, "urlopen: failed to connect to '%s': '%s'",
	       url, ns_sockstrerror(ns_sockerrno));
	goto done;
    }

    /*
     * Send a simple HTTP GET request.
     */
     
    Ns_DStringTrunc(&ds, 0);
    Ns_DStringVarAppend(&ds, "GET ", request->url, NULL);
    if (request->query != NULL) {
        Ns_DStringVarAppend(&ds, "?", request->query, NULL);
    }
    Ns_DStringAppend(&ds, " HTTP/1.0\r\nAccept: */*\r\n\r\n");
    p = ds.string;
    tosend = ds.length;
    while (tosend > 0) {
        n = send(sock, p, tosend, 0);
        if (n == SOCKET_ERROR) {
            Ns_Log(Error, "urlopen: failed to send data to '%s': '%s'",
		   url, ns_sockstrerror(ns_sockerrno));
            goto done;
        }
        tosend -= n;
	p += n;
    }

    /*
     * Buffer the socket and read the response line and then
     * consume the headers, parsing them into any given header set.
     */

    stream.cnt = 0;
    stream.error = 0;
    stream.ptr = stream.buf;
    stream.sock = sock;
    if (!GetLine(&stream, &ds)) {
	goto done;
    }
    if (headers != NULL && strncmp(ds.string, "HTTP", 4) == 0) {
	if (headers->name != NULL) {
	    ns_free(headers->name);
	}
	headers->name = Ns_DStringExport(&ds);
    }
    do {
	if (!GetLine(&stream, &ds)) {
	    goto done;
	}
	if (ds.length > 0
	    && headers != NULL
	    && Ns_ParseHeader(headers, ds.string, Preserve) != NS_OK) {
	    goto done;
	}
    } while (ds.length > 0);

    /*
     * Without any check on limit or total size, foolishly read
     * the remaining content into the dstring.
     */

    do {
    	Ns_DStringNAppend(dsPtr, stream.ptr, stream.cnt);
    } while (FillBuf(&stream));
    if (!stream.error) {
    	status = NS_OK;
    }
    
 done:
    if (request != NULL) {
        Ns_FreeRequest(request);
    }
    if (sock != INVALID_SOCKET) {
        ns_sockclose(sock);
    }
    Ns_DStringFree(&ds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetUrlObjCmd --
 *
 *	Implements ns_geturl. 
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
NsTclGetUrlObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = arg;
    Ns_DString  ds;
    Ns_Set     *headers;
    int         status, code;
    char       *url;

    if ((objc != 3) && (objc != 2)) {
        Tcl_WrongNumArgs(interp, 1, objv, "url ?headersSetIdVar?");
        return TCL_ERROR;
    }

    code = TCL_ERROR;
    if (objc == 2) {
        headers = NULL;
    } else {
        headers = Ns_SetCreate(NULL);
    }
    Ns_DStringInit(&ds);
    url = Tcl_GetString(objv[1]);
    if (url[0] == '/') {
	status = Ns_FetchPage(&ds, Tcl_GetString(objv[1]), itPtr->servPtr->server);
    } else {
        status = Ns_FetchURL(&ds, Tcl_GetString(objv[1]), headers);
    }
    if (status != NS_OK) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "could not fetch: ", 
		Tcl_GetString(objv[1]), NULL);
	if (headers != NULL) {
	    Ns_SetFree(headers);
	}
	goto done;
    }
    if (objc == 3) {
        Ns_TclEnterSet(interp, headers, NS_TCL_SET_DYNAMIC);
        if (Tcl_ObjSetVar2(interp, objv[2], NULL, Tcl_GetObjResult(interp),
		TCL_LEAVE_ERR_MSG) == NULL) {
	    goto done;
	}
    }
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    code = TCL_OK;

done:
    Ns_DStringFree(&ds);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * FillBuf --
 *
 *	Fill the socket stream buffer.
 *
 * Results:
 *	1 if fill ok, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
FillBuf(Stream *sPtr)
{
    int n;
    
    n = recv(sPtr->sock, sPtr->buf, BUFSIZE, 0);
    if (n <= 0) {
    	if (n < 0) {
	    Ns_Log(Error, "urlopen: "
		   "failed to fill socket stream buffer: '%s'", strerror(errno));
	    sPtr->error = 1;
	}
    	return 0;
    }
    sPtr->buf[n] = '\0';
    sPtr->ptr = sPtr->buf;
    sPtr->cnt = n;
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * GetLine --
 *
 *	Copy the next line from the stream to a dstring, trimming
 *	the \n and \r.
 *
 * Results:
 *	1 or 0.
 *
 * Side effects:
 *	The dstring is truncated on entry.
 *
 *----------------------------------------------------------------------
 */

static int
GetLine(Stream *sPtr, Ns_DString *dsPtr)
{
    char *eol;
    int n;

    Ns_DStringTrunc(dsPtr, 0);
    do {
	if (sPtr->cnt > 0) {
	    eol = strchr(sPtr->ptr, '\n');
	    if (eol == NULL) {
		n = sPtr->cnt;
	    } else {
		*eol++ = '\0';
		n = eol - sPtr->ptr;
	    }
	    Ns_DStringNAppend(dsPtr, sPtr->ptr, n - 1);
	    sPtr->ptr += n;
	    sPtr->cnt -= n;
	    if (eol != NULL) {
		n = dsPtr->length;
		if (n > 0 && dsPtr->string[n-1] == '\r') {
		    Ns_DStringTrunc(dsPtr, n-1);
		}
		return 1;
	    }
	}
    } while (FillBuf(sPtr));
    return 0;
}

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
 * return.c --
 *
 *	Functions that return data to a browser. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/return.c,v 1.6 2000/10/13 23:17:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define MAX_RECURSION 3       /* Max return direct recursion limit. */

/*
 * Local functions defined in this file
 */

static int ReturnRedirect(Ns_Conn *conn, int status, int *resultPtr);
static int ReturnOpen(Ns_Conn *conn, int status, char *type, Tcl_Channel chan,
		      FILE *fp, int fd, int len);

/*
 * Static variables defined in this file.
 */

/*
 * This structure connections HTTP response codes to their descriptions.
 */

static struct {
    int	  status;
    char *reason;
} reasons[] = {
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {301, "Moved"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {403, "Forbidden"},
    {404, "Not Found"},
    {405, "Method Not Allowed"},
    {406, "Not Acceptable"},
    {407, "Proxy Authentication Required"},
    {408, "Request Timeout"},
    {409, "Conflict"},
    {410, "Gone"},
    {411, "Length Required"},
    {412, "Precondition Failed"},
    {413, "Request Entity Too Large"},
    {414, "Request-URI Too Long"},
    {415, "Unsupported Media Type"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"}
};

static Tcl_HashTable   redirectTable;
static int             nreasons = (sizeof(reasons) / sizeof(reasons[0]));


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterReturn --
 *
 *      Associate a URL with a status. Rather than return the
 *	default error page for this status, a redirect will be
 *	issued to the url.
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
Ns_RegisterReturn(int status, char *url)
{
    Tcl_HashEntry *hPtr;
    int            new;

    hPtr = Tcl_CreateHashEntry(&redirectTable, (char *) status, &new);
    if (!new) {
	ns_free(Tcl_GetHashValue(hPtr));
    }
    if (url == NULL) {
	Tcl_DeleteHashEntry(hPtr);
    } else {
	Tcl_SetHashValue(hPtr, ns_strdup(url));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnConstructHeaders --
 *
 *	Put the header of an HTTP response into the dstring. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Content length and connection-keepalive headers will be added 
 *	if possible. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnConstructHeaders(Ns_Conn *conn, Ns_DString *dsPtr)
{
    int   i, length;
    char *reason;
    char  buf[100];
    char *value, *keep;
    char *key, *lengthHdr;
    Conn *connPtr;

    /*
     * Construct the HTTP response status line.
     */

    connPtr = (Conn *) conn;
    sprintf(buf, "%d", connPtr->responseStatus);
    reason = "Unknown Reason";
    for (i = 0; i < nreasons; i++) {
	if (reasons[i].status == connPtr->responseStatus) {
	    reason = reasons[i].reason;
	    break;
	}
    }
    Ns_DStringVarAppend(dsPtr, "HTTP/1.0 ", buf, " ", reason, "\r\n", NULL);

    /*
     * Output any headers.
     */

    if (conn->outputheaders != NULL) {
	/*
	 * Update the response length value directly from the
	 * header to be sent, i.e., don't trust programmers
	 * correctly called Ns_ConnSetLengthHeader().
	 */

	length = connPtr->responseLength;
	lengthHdr = Ns_SetIGet(conn->outputheaders, "content-length");
	if (lengthHdr != NULL) {
	    connPtr->responseLength = atoi(lengthHdr);
	}
	
	/*
	 * Output a connection keep-alive header only on
	 * basic HTTP status 200 GET responses which included
	 * a valid and correctly set content-length header.
	 */

	if (nsconf.keepalive.enabled &&
	    connPtr->headers != NULL &&
	    connPtr->request != NULL &&
	    connPtr->responseStatus == 200 &&
	    lengthHdr != NULL &&
	    connPtr->responseLength == length &&
	    STREQ(connPtr->request->method, "GET") &&
	    (key = Ns_SetIGet(conn->headers, "connection")) != NULL &&
	    STRIEQ(key, "keep-alive")) {
	    connPtr->keepAlive = 1;
	    keep = "keep-alive";
	} else {
	    keep = "close";
	}
	Ns_ConnCondSetHeaders(conn, "Connection", keep);

	for (i = 0; i < Ns_SetSize(conn->outputheaders); i++) {
	    key = Ns_SetKey(conn->outputheaders, i);
	    value = Ns_SetValue(conn->outputheaders, i);
	    if (key != NULL && value != NULL) {
		Ns_DStringAppend(dsPtr, key);
		Ns_DStringNAppend(dsPtr, ": ", 2);
		Ns_DStringAppend(dsPtr, value);
		Ns_DStringNAppend(dsPtr, "\r\n", 2);
	    }
	}
    }
    Ns_DStringNAppend(dsPtr, "\r\n", 2);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnFlushHeaders --
 *
 *	Send out a well-formed set of HTTP headers with the given 
 *	status. 
 *
 * Results:
 *	Number of bytes written. 
 *
 * Side effects:
 *	See Ns_ConnConstructHeaders. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnFlushHeaders(Ns_Conn *conn, int status)
{
    int   result;
    Conn *connPtr;

    connPtr = (Conn *) conn;
    connPtr->responseStatus = status;
    if (!(conn->flags & NS_CONN_SKIPHDRS)) {
	Ns_DString ds;
	Ns_DStringInit(&ds);
	Ns_ConnConstructHeaders(conn, &ds);
        result = Ns_WriteConn(conn, ds.string, ds.length);
        Ns_DStringFree(&ds);
    } else {
        result = NS_OK;
    }
    connPtr->sendState = Content;
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetHeaders --
 *
 *	Add an output header. 
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
Ns_ConnSetHeaders(Ns_Conn *conn, char *field, char *value)
{
    Ns_SetPut(conn->outputheaders, field, value);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnCondSetHeaders --
 *
 *	Add an output header, only if it doesn't already exist. 
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
Ns_ConnCondSetHeaders(Ns_Conn *conn, char *field, char *value)
{
    if (Ns_SetIGet(conn->outputheaders, field) == NULL) {
        Ns_SetPut(conn->outputheaders, field, value);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReplaceHeaders --
 *
 *	Free the existing outpheaders and set them to a copy of 
 *	newheaders. 
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
Ns_ConnReplaceHeaders(Ns_Conn *conn, Ns_Set *newheaders)
{
    newheaders = Ns_SetCopy(newheaders);
    Ns_SetFree(conn->outputheaders);
    conn->outputheaders = newheaders;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetRequiredHeaders --
 *
 *	Set a sane set of minimal headers for any response:
 *	MIME-Version, Date, Server, Content-Type, Content-Length
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
Ns_ConnSetRequiredHeaders(Ns_Conn *conn, char *type, int length)
{
    Ns_DString ds;

    /*
     * Set the standard mime and date headers.
     */

    Ns_DStringInit(&ds);
    Ns_HeadersCondPut(conn, "MIME-Version", "1.0");
    Ns_HeadersCondPut(conn, "Date", Ns_HttpTime(&ds, NULL));
    Ns_DStringTrunc(&ds, 0);

    /*
     * Set the standard server header, prepending "NaviServer/2.0"
     * if AOLpress support is enabled.
     */

    if (nsconf.serv.aolpress) {
    	Ns_DStringAppend(&ds, "NaviServer/2.0 ");
    }
    Ns_DStringVarAppend(&ds, Ns_InfoServer(), "/", Ns_InfoVersion(), NULL);
    Ns_HeadersCondPut(conn, "Server", ds.string);

    /*
     * Set the type and/or length headers if provided.  Note
     * that a valid length is required for connection keep-alive.
     */

    if (type != NULL) {
    	Ns_ConnSetTypeHeader(conn, type);
    }
    if (length > 0) {
	Ns_ConnSetLengthHeader(conn, length);
    }

    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetTypeHeader --
 *
 *	Sets the Content-Type HTTP output header 
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
Ns_ConnSetTypeHeader(Ns_Conn *conn, char *type)
{
    Ns_HeadersPut(conn, "Content-Type", type);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetLengthHeader --
 *
 *	Set the Content-Length output header. 
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
Ns_ConnSetLengthHeader(Ns_Conn *conn, int length)
{
    char  buf[100];
    Conn *connPtr;

    connPtr = (Conn *) conn;
    connPtr->responseLength = length;
    sprintf(buf, "%d", length);
    Ns_HeadersPut(conn, "Content-Length", buf);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetLastModifiedHeader --
 *
 *	Set the Last-Modified output header. 
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
Ns_ConnSetLastModifiedHeader(Ns_Conn *conn, time_t *mtime)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_HeadersPut(conn, "Last-Modified", Ns_HttpTime(&ds, mtime));
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetExpiresHeader --
 *
 *	Set the Expires output header. 
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
Ns_ConnSetExpiresHeader(Ns_Conn *conn, char *expires)
{
    Ns_HeadersPut(conn, "Expires", expires);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnPrintfHeader --
 *
 *	Write a printf-style string right to the conn. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will write to the conn; you're expected to format this like a 
 *	header (like "foo: bar\n"). 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnPrintfHeader(Ns_Conn *conn, char *fmt,...)
{
    int result;

    if (conn->request != NULL && conn->request->version >= 1.0) {
        char    buf[4096];
        va_list ap;

        va_start(ap, fmt);
        vsprintf(buf, fmt, ap);
        va_end(ap);
        result = Ns_PutsConn(conn, buf);
    } else {
        result = NS_OK;
    }
    
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResetReturn --
 *
 *	Deprecated 
 *
 * Results:
 *	NS_OK 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResetReturn(Ns_Conn *conn)
{
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnAdminNotice --
 *
 *	Return a short notice to a client to contact system 
 *	administrator. 
 *
 * Results:
 *	See Ns_ConnReturnNotice 
 *
 * Side effects:
 *	See Ns_ConnReturnNotice 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnAdminNotice(Ns_Conn *conn, int status, char *title, char *notice)
{
    return Ns_ConnReturnNotice(conn, status, title, notice);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNotice --
 *
 *	Return a short notice to a client. 
 *
 * Results:
 *	See Ns_ReturnHtml. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotice(Ns_Conn *conn, int status, char *title, char *notice)
{
    Ns_DString ds;
    int        result;

    Ns_DStringInit(&ds);
    if (title == NULL) {
        title = "Server Message";
    }
    Ns_DStringVarAppend(&ds,
			"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
			"<HTML><HEAD>\n"
			"<TITLE>", title, "</TITLE>\n"
			"</HEAD><BODY>\n"
			"<H2>", title, "</H2>\n", NULL);
    if (notice != NULL) {
    	Ns_DStringVarAppend(&ds, notice, "\n", NULL);
    }
    Ns_DStringAppend(&ds, "</BODY></HTML>\n");

    result = Ns_ReturnHtml(conn, status, ds.string, ds.length);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnData --
 *
 *	Sets required headers, dumps them, and then writes your data. 
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	May set numerous headers, will close connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnData(Ns_Conn *conn, int status, char *data, int len, char *type)
{
    int result;

    if (len == -1) {
        len = strlen(data);
    }
    Ns_ConnSetRequiredHeaders(conn, type, len);
    result = Ns_ConnFlushHeaders(conn, status);
    if (result == NS_OK) {
	if (!(conn->flags & NS_CONN_SKIPBODY)) {
    	    result = Ns_WriteConn(conn, data, len);
	}
	if (result == NS_OK) {
	    result = Ns_ConnClose(conn);
	}
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnHtml --
 *
 *	Return data of type text/html to client. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	See Ns_ConnReturnData 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnHtml(Ns_Conn *conn, int status, char *html, int len)
{
    return Ns_ConnReturnData(conn, status, html, len, "text/html");
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOk --
 *
 *	Return a status message to the client. 
 *
 * Results:
 *	See Ns_ReturnStatus 
 *
 * Side effects:
 *	See Ns_ReturnStatus
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnOk(Ns_Conn *conn)
{
    return Ns_ReturnStatus(conn, 200);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNoResponse --
 *
 *	Return a status message to the client. 
 *
 * Results:
 *	See Ns_ReturnStatus
 *
 * Side effects:
 *	See Ns_ReturnStatus 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNoResponse(Ns_Conn *conn)
{
    return Ns_ReturnStatus(conn, 204);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnRedirect --
 *
 *	Return a 302 Redirection to the client, or 204 No Content if 
 *	url is null. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnRedirect(Ns_Conn *conn, char *url)
{
    Ns_DString ds, msg;
    int        result;

    Ns_DStringInit(&ds);
    Ns_DStringInit(&msg);
    if (url != NULL) {
        if (*url == '/') {
            Ns_DStringAppend(&ds, Ns_ConnLocation(conn));
        }
        Ns_DStringAppend(&ds, url);
        Ns_HeadersPut(conn, "Location", ds.string);
	Ns_DStringVarAppend(&msg, "<A HREF=\"", ds.string,
			    "\">The requested URL has moved here.</A>", NULL);
	result = Ns_ReturnNotice(conn, 302, "Redirection", msg.string);
    } else {
	result = Ns_ReturnNotice(conn, 204, "No Content", msg.string);
    }
    Ns_DStringFree(&msg);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnBadRequest --
 *
 *	Return an 'invalid request' HTTP status line with an error 
 *	message. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnBadRequest(Ns_Conn *conn, char *reason)
{
    Ns_DString ds;
    int        result;

    if (ReturnRedirect(conn, 401, &result)) {
	return result;
    }
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds,
		     "The HTTP request presented by your browser is invalid.");
    if (reason != NULL) {
        Ns_DStringVarAppend(&ds, "<P>\n", reason, NULL);
    }
    result = Ns_ReturnNotice(conn, 400, "Invalid Request", ds.string);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnUnauthorized --
 *
 *	Return a 401 Unauthorized response, which will prompt the 
 *	user for a Basic authentication username/password. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnUnauthorized(Ns_Conn *conn)
{
    Ns_DString  ds;
    int	        result;

    if (ReturnRedirect(conn, 401, &result)) {
	return result;
    }
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "Basic realm=\"", nsconf.serv.realm, "\"", NULL);
    Ns_HeadersPut(conn, "WWW-Authenticate", ds.string);
    Ns_DStringFree(&ds);

    return Ns_ReturnNotice(conn, 401, "Access Denied",
			   "The requested URL requires a "
			   "valid username and password.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnForbidden --
 *
 *	Return a 403 Forbidden response. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnForbidden(Ns_Conn *conn)
{
    int result;

    if (ReturnRedirect(conn, 403, &result)) {
	return result;
    }
    return Ns_ReturnNotice(conn, 403, "Forbidden",
			   "The requested URL cannot be accessed.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNotFound --
 *
 *	Return a 404 Not Found response. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotFound(Ns_Conn *conn)
{
    int result;

    if (ReturnRedirect(conn, 404, &result)) {
	return result;
    }
    return Ns_ReturnNotice(conn, 404, "Not Found",
			   "The requested URL was not found.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNotModified --
 *
 *	Return a 304 Not Modified response. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotModified(Ns_Conn *conn)
{
    return Ns_ReturnStatus(conn, 304);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnNotImplemented --
 *
 *	Return a 501 Not Implemented response. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotImplemented(Ns_Conn *conn)
{
    int result;

    if (ReturnRedirect(conn, 501, &result)) {
	return result;
    }
    return Ns_ReturnNotice(conn, 501, "Not Implemented",
			   "The requested URL is not implemented.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnInternalError --
 *
 *	Return a 500 Internal Error response. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnInternalError(Ns_Conn *conn)
{
    int result;

    Ns_SetTrunc(conn->outputheaders, 0);
    if (ReturnRedirect(conn, 500, &result)) {
	return result;
    }
    return Ns_ReturnNotice(conn, 500, "Server Error",
			   "The requested URL cannot be accessed "
			   "due to a system error.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnStatus --
 *
 *	Return an arbitrary status code.
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will close the connection. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnStatus(Ns_Conn *conn, int status)
{
    int result;
    
    if (ReturnRedirect(conn, status, &result)) {
    	return result;
    }
    Ns_HeadersRequired(conn, NULL, 0);
    Ns_ConnFlushHeaders(conn, status);
    return Ns_ConnClose(conn);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOpenChannel --
 *
 *	Send an open channel out the conn. 
 *
 * Results:
 *	See ReturnOpen. 
 *
 * Side effects:
 *	See ReturnOpen. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnOpenChannel(Ns_Conn *conn, int status, char *type,
			 Tcl_Channel chan, int len)
{
    return ReturnOpen(conn, status, type, chan, NULL, -1, len);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOpenFile --
 *
 *	Send an open file out the conn. 
 *
 * Results:
 *	See ReturnOpen. 
 *
 * Side effects:
 *	See ReturnOpen. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnOpenFile(Ns_Conn *conn, int status, char *type, FILE *fp, int len)
{
    return ReturnOpen(conn, status, type, NULL, fp, -1, len);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOpenFd --
 *
 *	Send an open fd out the conn. 
 *
 * Results:
 *	See ReturnOpen. 
 *
 * Side effects:
 *	See ReturnOpen. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnOpenFd(Ns_Conn *conn, int status, char *type, int fd, int len)
{
    return ReturnOpen(conn, status, type, NULL, NULL, fd, len);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnFile --
 *
 *	Send the contents of a file out the conn. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will set required headers, including mime type if type is null.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnFile(Ns_Conn *conn, int status, char *type, char *filename)
{
    struct stat st;
    int         fd;
    int         result;

    /*
     * Make sure the file exists, or return an appropriate error.
     */
    
    if (stat(filename, &st) != 0) {
        if (errno == ENOENT) {
            return Ns_ReturnNotFound(conn);
        } else if (errno == EACCES) {
            return Ns_ReturnForbidden(conn);
        } else {
            Ns_Log(Error, "return: failed to return file: "
		   "returnfile(%s): stat(%s) failed: '%s'",
		   Ns_ConnServer(conn), filename, strerror(errno));
            return Ns_ReturnInternalError(conn);
        }
    } else if (!Ns_ConnModifiedSince(conn, st.st_mtime)) {
        return Ns_ReturnNotModified(conn);
    }
    
    /*
     * Determine the mime type for this file if none was specified.
     */
    
    if (type == NULL) {
        type = Ns_GetMimeType(filename);
    }

    /*
     * Set sundry required headers.
     */
    
    Ns_HeadersRequired(conn, type, st.st_size);
    Ns_ConnSetLastModifiedHeader(conn, &st.st_mtime);

    /*
     * If this is a HEAD request, just flush the
     * headers.  Otherwise. flush the headers
     * and then open and return the file content.
     */

    if (conn->flags & NS_CONN_SKIPBODY) {
	result = Ns_ConnFlushHeaders(conn, status);
    } else {
    	fd = open(filename, O_RDONLY|O_BINARY);
    	if (fd < 0) {
            return Ns_ReturnInternalError(conn);
	}
    	result = Ns_ConnFlushHeaders(conn, status);
    	if (result == NS_OK) {
            result = Ns_ConnSendFd(conn, fd, st.st_size);
	}
    	close(fd);
    }
    if (result == NS_OK) {
        result = Ns_ConnClose(conn);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitReturn --
 *
 *	Initialize this file. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Checks config file. 
 *
 *----------------------------------------------------------------------
 */

void
NsInitReturn(void)
{
    Ns_Set *set;
    int     status, i;
    char   *path, *key, *url;

    Tcl_InitHashTable(&redirectTable, TCL_ONE_WORD_KEYS);

    /*
     * Process return redirects, e.g., not found 404.
     */

    path = Ns_ConfigGetPath(nsServer, NULL, "redirects", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	url = Ns_SetValue(set, i);
	status = atoi(key);
	if (status <= 0) {
	    Ns_Log(Error, "return: invalid redirect '%s=%s'", key, url);
	} else {
	    Ns_Log(Notice, "return: redirecting '%d' to '%s'", status, url);
	    Ns_RegisterReturn(status, url);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnRedirect --
 *
 *	Return the appropriate redirect for a given status code. 
 *
 * Results:
 *	0 if no redir exists, 1 if one does. 
 *
 * Side effects:
 *	May write and close the conn. 
 *
 *----------------------------------------------------------------------
 */

static int
ReturnRedirect(Ns_Conn *conn, int status, int *resultPtr)
{
    Tcl_HashEntry *hPtr;
    Conn    	  *connPtr;

    connPtr = (Conn *) conn;
    hPtr = Tcl_FindHashEntry(&redirectTable, (char *) status);
    if (hPtr != NULL) {
	if (++connPtr->recursionCount > MAX_RECURSION) {
	    Ns_Log(Error, "return: failed to redirect '%d': "
		   "exceeded recursion limit of %d", status, MAX_RECURSION);
	} else {
    	    *resultPtr = Ns_ConnRedirect(conn, Tcl_GetHashValue(hPtr));
	    return 1;
	}
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnOpen --
 *
 *	Dump an open 'something' to the conn. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Will close the connection on success. 
 *
 *----------------------------------------------------------------------
 */

static int
ReturnOpen(Ns_Conn *conn, int status, char *type, Tcl_Channel chan, FILE *fp,
	   int fd, int len)
{
    int result;

    Ns_HeadersRequired(conn, type, len);
    result = Ns_ConnFlushHeaders(conn, status);
    if (result == NS_OK) {
	if (chan != NULL) {
	    result = Ns_ConnSendChannel(conn, chan, len);
	} else if (fp != NULL) {
	    result = Ns_ConnSendFp(conn, fp, len);
	} else {
	    result = Ns_ConnSendFd(conn, fd, len);
	}
    }
    if (result == NS_OK) {
        result = Ns_ConnClose(conn);
    }
    return result;
}


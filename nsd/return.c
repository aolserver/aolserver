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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/return.c,v 1.50 2006/07/07 03:27:22 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static void RegisterRedirect(NsServer *servPtr, int status, char *url);
static int ReturnRedirect(Ns_Conn *conn, int status, int *resultPtr);
static int ReturnOpen(Ns_Conn *conn, int status, char *type, Tcl_Channel chan,
		      FILE *fp, int fd, off_t off, int len);
static int ReturnData(Ns_Conn *conn, int status, char *data, int len,
                          char *type, int direct);
static int HdrEq(Ns_Set *hdrs, char *key, char *value);
static int CheckKeep(Ns_Conn *conn, int status);

/*
 * This structure connections HTTP response codes to their descriptions.
 */

static struct {
    int	  status;
    char *reason;
} reasons[] = {
    {100, "Continue"},
    {101, "Switching Protocols"},
    {102, "Processing"},
    {200, "OK"},
    {201, "Created"},
    {202, "Accepted"},
    {203, "Non-Authoritative Information"},
    {204, "No Content"},
    {205, "Reset Content"},
    {206, "Partial Content"},
    {207, "Multi-Status"},
    {300, "Multiple Choices"},
    {301, "Moved"},
    {302, "Found"},
    {303, "See Other"},
    {304, "Not Modified"},
    {305, "Use Proxy"},
    {307, "Temporary Redirect"},
    {400, "Bad Request"},
    {401, "Unauthorized"},
    {402, "Payment Required"},
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
    {416, "Requested Range Not Satisfiable"},
    {417, "Expectation Failed"},
    {422, "Unprocessable Entity"},
    {423, "Locked"},
    {424, "Method Failure"},
    {425, "Insufficient Space On Resource"},
    {500, "Internal Server Error"},
    {501, "Not Implemented"},
    {502, "Bad Gateway"},
    {503, "Service Unavailable"},
    {504, "Gateway Timeout"},
    {505, "HTTP Version Not Supported"},
    {507, "Insufficient Storage"}
};

/*
 * Static variables defined in this file.
 */

static int nreasons = (sizeof(reasons) / sizeof(reasons[0]));


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterRedirect, Ns_RegisterReturn --
 *
 *	Associate a URL with a status. Rather than return the
 *	default error page for this status, a redirect will be
 *	issued to the url.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	A NULL url will remove a previous redirect, if any.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterReturn(int status, char *url)
{
    RegisterRedirect(NsGetInitServer(), status, url);
}

void
Ns_RegisterRedirect(char *server, int status, char *url)
{
    RegisterRedirect(NsGetServer(server), status, url);
}

static void
RegisterRedirect(NsServer *servPtr, int status, char *url)
{
    Tcl_HashEntry *hPtr;
    int            new;

    if (servPtr != NULL) {
    	hPtr = Tcl_CreateHashEntry(&servPtr->request.redirect,
				   (char *) status, &new);
    	if (!new) {
	    ns_free(Tcl_GetHashValue(hPtr));
    	}
    	if (url == NULL) {
	    Tcl_DeleteHashEntry(hPtr);
    	} else {
	    Tcl_SetHashValue(hPtr, ns_strdup(url));
	}
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
    Conn *connPtr = (Conn *) conn;
    int   i, status;
    char *reason;
    char *value, *keep;
    char *key;

    /*
     * Construct the HTTP response status line.
     */

    status = Ns_ConnGetStatus(conn);
    reason = "Unknown Reason";
    for (i = 0; i < nreasons; i++) {
	if (reasons[i].status == status) {
	    reason = reasons[i].reason;
	    break;
	}
    }
    Ns_DStringPrintf(dsPtr, "HTTP/%u.%u %d %s\r\n", 
		     _MIN((connPtr->major), nsconf.http.major),
		     _MIN((connPtr->minor), nsconf.http.minor),
		     status, reason);

    /*
     * Output any headers.
     */

    if (conn->outputheaders != NULL) {
	/*
	 * Set keep-alive if the driver and connection support it.
	 */

	if (!Ns_ConnGetKeepAliveFlag(conn) && CheckKeep(conn, status)) {
	    Ns_ConnSetKeepAliveFlag(conn, NS_TRUE);
	}
	if (Ns_ConnGetKeepAliveFlag(conn)) {
	    keep = "keep-alive";
	} else {
	    keep = "close";
	}
	Ns_ConnCondSetHeaders(conn, "Connection", keep);

	/*
	 * Output all headers.
	 */

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
 * Ns_ConnQueueHeaders --
 *
 *	Format basic headers to be sent on the connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See Ns_ConnConstructHeaders. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_ConnQueueHeaders(Ns_Conn *conn, int status)
{
    Conn *connPtr = (Conn *) conn;

    if (!(conn->flags & NS_CONN_SENTHDRS)) {
	Ns_ConnSetStatus(conn, status);
    	if (!(conn->flags & NS_CONN_SKIPHDRS)) {
	    Ns_ConnConstructHeaders(conn, &connPtr->obuf);
	    connPtr->nContentSent -= connPtr->obuf.length;
    	}
    	conn->flags |= NS_CONN_SENTHDRS;
    }
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
 *	See Ns_ConnQueueHeaders. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnFlushHeaders(Ns_Conn *conn, int status)
{
    Ns_ConnQueueHeaders(conn, status);
    return Ns_WriteConn(conn, NULL, 0);
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
    Ns_SetFree(conn->outputheaders);
    conn->outputheaders = Ns_SetCopy(newheaders);
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
Ns_ConnSetRequiredHeaders(Ns_Conn *conn, char *newtype, int length)
{
    Conn *connPtr = (Conn *) conn;
    char *type;
    Ns_DString ds;

    /*
     * Set the standard mime and date headers.
     */

    Ns_DStringInit(&ds);
    Ns_ConnCondSetHeaders(conn, "MIME-Version", "1.0");
    Ns_ConnCondSetHeaders(conn, "Date", Ns_HttpTime(&ds, NULL));
    Ns_DStringTrunc(&ds, 0);

    /*
     * Set the standard server header, prepending "NaviServer/2.0"
     * if AOLpress support is enabled.
     */

    if (connPtr->servPtr->opts.flags & SERV_AOLPRESS) {
    	Ns_DStringAppend(&ds, "NaviServer/2.0 ");
    }
    Ns_DStringVarAppend(&ds, Ns_InfoServerName(), "/", Ns_InfoServerVersion(), NULL);
    Ns_ConnCondSetHeaders(conn, "Server", ds.string);

    /*
     * Set the type and/or length headers if provided.  Note
     * that a valid length is required for connection keep-alive.
     */

    type = Ns_ConnGetType(conn);
    if (type != newtype) {
	Ns_ConnSetType(conn, newtype);
	type = Ns_ConnGetType(conn);
    }
    if (type) {
	Ns_ConnSetTypeHeader(conn, type);
    }
    if (length >= 0) {
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
    Ns_ConnSetHeaders(conn, "Content-Type", type);
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
    Ns_ConnSetHeaders(conn, "Content-Length", buf);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnSetLastModifiedHeader --
 *
 *	Set the Last-Modified output header if it isn't already set. 
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
    Ns_ConnCondSetHeaders(conn, "Last-Modified", Ns_HttpTime(&ds, mtime));
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
    Ns_ConnSetHeaders(conn, "Expires", expires);
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
    Ns_DString ds;
    va_list ap;

    if (conn->request == NULL || conn->request->version < 1.0) {
	return NS_OK;
    }
    Ns_DStringInit(&ds);
    va_start(ap, fmt);
    Ns_DStringVPrintf(&ds, fmt, ap);
    va_end(ap);
    result = Ns_ConnSendDString(conn, &ds);
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnResetReturn --
 *
 *	Reset the connection, clearing any queued headers, so a
 *	basic result may be sent.
 *
 * Results:
 *	NS_OK if connection could be cleared, NS_ERROR if data has
 *	already been sent.
 *
 * Side effects:
 *	Will truncate queued headers.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnResetReturn(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    if (connPtr->nContentSent) {
	return NS_ERROR;
    }

    /*
     * Clear queued headers and reset status and type.
     */

    Ns_DStringFree(&connPtr->obuf);
    Ns_ConnSetType(conn, NULL);
    Ns_ConnSetStatus(conn, 0);
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
 *	See Ns_ConnReturnHtml. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnNotice(Ns_Conn *conn, int status, char *title, char *notice)
{
    Conn *connPtr = (Conn *) conn;
    NsServer *servPtr = connPtr->servPtr;
    Ns_DString ds;
    int        result;

    Ns_DStringInit(&ds);
    if (title == NULL) {
        title = "Server Message";
    }
    Ns_DStringVarAppend(&ds,
			"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML 2.0//EN\">\n"
			"<HTML>\n<HEAD>\n"
			"<TITLE>", title, "</TITLE>\n"
			"</HEAD>\n<BODY>\n"
			"<H2>", title, "</H2>\n", NULL);
    if (notice != NULL) {
    	Ns_DStringVarAppend(&ds, notice, "\n", NULL);
    }

    /*
     * Detailed server information at the bottom of the page.
     */
    if (servPtr->opts.flags & SERV_NOTICEDETAIL) {
	Ns_DStringVarAppend(&ds, "<P ALIGN=RIGHT><SMALL><I>",
			    Ns_InfoServerName(), "/",
			    Ns_InfoServerVersion(), " on ",
			    Ns_ConnLocation(conn), "</I></SMALL></P>\n",
			    NULL);
    }

    /*
     * Padding that suppresses those horrible MSIE friendly errors.
     * NB: Because we pad inside the body we may pad more than needed.
     */
    if (status >= 400) {
	while (ds.length < servPtr->limits.errorminsize) {
	    Ns_DStringAppend(&ds, "                    ");
	}
    }
    
    Ns_DStringVarAppend(&ds, "\n</BODY></HTML>\n", NULL);
    
    result = Ns_ConnReturnHtml(conn, status, ds.string, ds.length);
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
   return ReturnData(conn, status, data, len, type, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnCharData --
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
Ns_ConnReturnCharData(Ns_Conn *conn, int status, char *data, int len, char *type)
{
   return ReturnData(conn, status, data, len, type, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnData --
 *
 *	Sets required headers, dumps them, and then writes your data. 
 *      If direct is true, calls Ns_ConnFlushDirect which bypasses
 *	character encoding and/or gzip compression.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	See Ns_ConnFlush.
 *
 *----------------------------------------------------------------------
 */

static int
ReturnData(Ns_Conn *conn, int status, char *data, int len, char *type,
	   int direct)
{
    Ns_ConnSetStatus(conn, status);
    Ns_ConnSetType(conn, type);
    if (direct) {
    	return Ns_ConnFlushDirect(conn, data, len, 0);
    }
    return Ns_ConnFlush(conn, data, len, 0);
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
    return Ns_ConnReturnCharData(conn, status, html, len, "text/html");
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
    return Ns_ConnReturnStatus(conn, 200);
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
    return Ns_ConnReturnStatus(conn, 204);
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
        Ns_ConnSetHeaders(conn, "Location", ds.string);
	Ns_DStringVarAppend(&msg, "<A HREF=\"", ds.string,
		"\">The requested URL has moved here.</A>", NULL);
	result = Ns_ConnReturnNotice(conn, 302, "Redirection", msg.string);
    } else {
	result = Ns_ConnReturnNotice(conn, 204, "No Content", msg.string);
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

    if (ReturnRedirect(conn, 400, &result)) {
	return result;
    }
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds,
	"The HTTP request presented by your browser is invalid.");
    if (reason != NULL) {
        Ns_DStringVarAppend(&ds, "<P>\n", reason, NULL);
    }
    result = Ns_ConnReturnNotice(conn, 400, "Invalid Request", ds.string);
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
    Conn *connPtr = (Conn *) conn;
    Ns_DString  ds;
    int	        result;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "Basic realm=\"",
	connPtr->servPtr->opts.realm, "\"", NULL);
    Ns_ConnSetHeaders(conn, "WWW-Authenticate", ds.string);
    Ns_DStringFree(&ds);

    if (ReturnRedirect(conn, 401, &result)) {
	return result;
    }
    return Ns_ConnReturnNotice(conn, 401, "Access Denied",
	"The requested URL cannot be accessed because a "
	"valid username and password are required.");
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
    return Ns_ConnReturnNotice(conn, 403, "Forbidden",
	"The requested URL cannot be accessed by this server.");
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
    return Ns_ConnReturnNotice(conn, 404, "Not Found",
	"The requested URL was not found on this server.");
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
    return Ns_ConnReturnStatus(conn, 304);
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
    return Ns_ConnReturnNotice(conn, 501, "Not Implemented",
	"The requested URL or method is not implemented "
	"by this server.");
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
    return Ns_ConnReturnNotice(conn, 500, "Server Error",
	"The requested URL cannot be accessed "
	"due to a system error on this server.");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnServiceUnavailable --
 *
 *	Return a 503 Service Unavailable response. 
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
Ns_ConnReturnServiceUnavailable(Ns_Conn *conn)
{
    int result;

    if (ReturnRedirect(conn, 503, &result)) {
	return result;
    }
    return Ns_ConnReturnNotice(conn, 503, "Service Unavailable",
	"The requested URL cannot be accessed "
	"because the server is temporarily unable "
        "to fulfill your request.  Please try again later.");
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
    Ns_ConnSetRequiredHeaders(conn, NULL, 0);
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
    return ReturnOpen(conn, status, type, chan, NULL, -1, -1, len);
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
    return ReturnOpen(conn, status, type, NULL, fp, -1, -1, len);
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
    return ReturnOpen(conn, status, type, NULL, NULL, fd, -1, len);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnOpenFdEx --
 *
 *	Send an open fd out the conn starting at given offset.  The
 *	current file position is not changed.
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
Ns_ConnReturnOpenFdEx(Ns_Conn *conn, int status, char *type, int fd,
		      off_t off, int len)
{
    return ReturnOpen(conn, status, type, NULL, NULL, fd, off, len);
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
    NsServer      *servPtr;

    connPtr = (Conn *) conn;
    servPtr = connPtr->servPtr;
    hPtr = Tcl_FindHashEntry(&servPtr->request.redirect, (char *) status);
    if (hPtr != NULL) {
        *resultPtr = Ns_ConnRedirect(conn, Tcl_GetHashValue(hPtr));
        return 1;
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
ReturnOpen(Ns_Conn *conn, int status, char *type, Tcl_Channel chan,
	FILE *fp, int fd, off_t off, int len)
{
    int result;

    Ns_ConnSetRequiredHeaders(conn, type, len);
    Ns_ConnQueueHeaders(conn, status);
    if (chan != NULL) {
	result = Ns_ConnSendChannel(conn, chan, len);
    } else if (fp != NULL) {
	result = Ns_ConnSendFp(conn, fp, len);
    } else if (off < 0) {
	result = Ns_ConnSendFd(conn, fd, len);
    } else {
	result = Ns_ConnSendFdEx(conn, fd, off, len);
    }
    if (result == NS_OK) {
        result = Ns_ConnClose(conn);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * HdrEq --
 *
 *	Test if given set contains a key which matches given value.
 *
 * Results:
 *	1 if there is a match, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
HdrEq(Ns_Set *set, char *name, char *value)
{
    char *hdrvalue;

    if (set != NULL
	&& (hdrvalue = Ns_SetIGet(set, name)) != NULL
	&& STRIEQ(hdrvalue, value)) {
	return 1;
    }
    return 0;
}


static int
CheckKeep(Ns_Conn *conn, int status)
{
    Conn *connPtr = (Conn *) conn;
    char *hdr;

    /*
     * First, ensure the driver supports keep-alive, the request method
     * was GET, and the client sent a connection: keep-alive header.
     */

    if (connPtr->drvPtr->keepwait > 0 &&
	conn->request != NULL &&
	STREQ(conn->request->method, "GET") &&
	HdrEq(conn->headers, "connection", "keep-alive")) {

	/*
	 * Status 304, without any content, is ok.
	 */

    	if (status == 304) {
	    return 1;
    	}

	/*
	 * Status 200 requires either chunked encoding or a a valid
	 * content-length header.
	 */

	if (status == 200) {
    	    if (HdrEq(conn->outputheaders, "transfer-encoding", "chunked")) {
		return 1;
	    }
    	    hdr = Ns_SetIGet(conn->outputheaders, "content-length");
    	    if (hdr != NULL &&
		((int) strtol(hdr, NULL, 10) == connPtr->responseLength)) {
	    	return 1;
	    }
	}
    }

    /*
     * Test for keep-alive failed.
     */

    return 0;
}

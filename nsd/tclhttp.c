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
 * tclhttp.c --
 *
 *	Support for the ns_http command.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclhttp.c,v 1.1 2001/04/25 00:25:58 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure and flags maintains the state of a
 * background http request.
 */
 
#define REQ_SEND 	1
#define REQ_RECV 	2
#define REQ_DONE 	4
#define REQ_CANCEL	8
#define REQ_EOF		16
#define REQ_ERR		32
#define REQ_ANY		(0xff)

typedef struct {
    SOCKET sock;
    int state;
    char *next;
    int len;
    Tcl_DString ds;
} Http;

static Ns_Mutex lock;
static Ns_Cond cond;
static int nextid;
static Tcl_HashTable ids;

/*
 * Local functions defined in this file
 */

static Ns_SockProc Send;
static Ns_SockProc Recv;
static Ns_SockProc Cancel;
static int Done(SOCKET sock, Http *httpPtr, int state);
static int HttpGet(SOCKET sock, char *host, char *file, Ns_Set *hdrs, Tcl_DString *dsPtr);
static void HttpResult(Tcl_Interp *interp, char *response, int *statusPtr, Ns_Set *hdrs);
static int SplitUrl(Tcl_DString *dsPtr, char *url, char **hostPtr, int *portPtr, char **filePtr);




static int
SplitUrl(Tcl_DString *dsPtr, char *url, char **hostPtr, int *portPtr, char **filePtr)
{
    int hlen;
    char *host, *file, *p;

    if (strncmp(url, "http://", 7) != 0 || url[7] == '\0') {
	return 0;
    }
    host = url + 7;
    file = strchr(host, '/');
    if (file != NULL) {
    	*file = '\0';
    }
    Tcl_DStringAppend(dsPtr, host, strlen(host)+1);
    hlen = dsPtr->length;
    if (file != NULL) {
	*file = '/';
    } else {
	file = "/";
    }
    Tcl_DStringAppend(dsPtr, file, -1);
    host = dsPtr->string;
    file = host + hlen;
    p = strchr(host, ':');
    if (p == NULL) {
	*portPtr = 80;
    } else {
    	*portPtr = atoi(p+1);
	*p = '\0';
    }
    *hostPtr = host;
    *filePtr = file;
    return 1;
}


static void
HttpReq(Tcl_DString *dsPtr, char *host, char *file, Ns_Set *hdrset)
{
    char *server = Ns_InfoServerName();
    char *version = Ns_InfoServerVersion();
    int i;

    Ns_DStringVarAppend(dsPtr, "GET ", file, " HTTP/1.0\r\n", NULL);
    if (hdrset == NULL) {
        Ns_DStringVarAppend(dsPtr,
	    "User-Agent: ", server, "/", version, "\r\n"
	    "Connection: close\r\n"
	    "Host: ", host, "\r\n", NULL);
    } else {
        for (i = 0; i < Ns_SetSize(hdrset); i++) {
            Ns_DStringVarAppend(dsPtr, Ns_SetKey(hdrset, i), ": ", 
                                Ns_SetValue(hdrset, i), "\r\n", NULL);
        }
        Ns_SetTrunc(hdrset, 0);
    }
    Tcl_DStringAppend(dsPtr, "\r\n", 2);
}


static void
HttpResult(Tcl_Interp *interp, char *response, int *statusPtr, Ns_Set *hdrs)
{
    float version;
    int firsthdr, len, status;
    char *eoh, *body, *p;

    eoh = strstr(response, "\r\n\r\n");
    if (eoh != NULL) {
	body = eoh + 4;
    } else {
	eoh = strstr(response, "\n\n");
	if (eoh != NULL) {
	    body = eoh + 2;
	}
    }
    Tcl_SetResult(interp, body, TCL_VOLATILE);
    if (eoh != NULL) {
	*eoh = '\0';

    /*
     * Parse the headers saved in the dstring if requested.
     */

    if (statusPtr == NULL) {
	statusPtr = &status;
    }
    if (sscanf(response, "HTTP/%f %d", &version, statusPtr) != 2) {
        statusPtr = 0;
    }
    if (hdrs != NULL) {
	firsthdr = 1;
	p = response;
	while ((eoh = strchr(p, '\n')) != NULL) {
	    *eoh++ = '\0';
	    len = strlen(p);
	    if (len > 0 && p[len-1] == '\r') {
		p[len-1] = '\0';
	    }
	    if (firsthdr) {
		if (hdrs->name != NULL) {
		    ns_free(hdrs->name);
		}
		hdrs->name = ns_strdup(p);
		firsthdr = 0;
	    } else if (Ns_ParseHeader(hdrs, p, ToLower) != NS_OK) {
		break;
	    }
	    p = eoh;
	}
    }
}
}


static int
Send(SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;
    int n;

    n = send(sock, httpPtr->next, httpPtr->len, 0);
    if (n < 0) {
	Tcl_DStringFree(&httpPtr->ds);
    	return Done(sock, httpPtr, (REQ_DONE|REQ_ERR));
    }
    httpPtr->next += n;
    httpPtr->len -= n;
    if (httpPtr->len == 0) {
	shutdown(sock, 1);
	Tcl_DStringTrunc(&httpPtr->ds, 0);
	Ns_MutexLock(&lock);
	httpPtr->state = REQ_RECV;
	Ns_MutexUnlock(&lock);
    	Ns_SockCallback(sock, Recv, arg, NS_SOCK_READ);
    }
    return NS_TRUE;
}


static int
Recv(SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;
    char buf[1024];
    int n, state;

    n = recv(sock, buf, sizeof(buf), 0);
    if (n > 0) {
	Tcl_DStringAppend(&httpPtr->ds, buf, n);
	return NS_TRUE;
    }
    state = REQ_DONE;
    if (n < 0) {
	state |= REQ_ERR;
    } else {
	state |= REQ_EOF;
    }
    return Done(sock, httpPtr, state);
}


static int
Cancel(SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;

    return Done(sock, httpPtr, (REQ_CANCEL|REQ_DONE));
}


static int
Done(SOCKET sock, Http *httpPtr, int state)
{
    Ns_MutexLock(&lock);
    httpPtr->state = state;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
    return NS_FALSE;
}


int
NsTclHttpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    Http *httpPtr;
    Tcl_DString ds, resp;
    char *cmd, buf[20], *host, *file;
    int result, state, port, new, status;
    SOCKET sock;
    Tcl_HashEntry *hPtr;
    static int first;
    if(!first) {
    	Tcl_InitHashTable(&ids, TCL_STRING_KEYS);
	first = 1;
    }

    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " command ?args ...?\"", NULL);
    	return TCL_ERROR;
    }

    result = TCL_ERROR;    
    cmd = argv[1];
    if (STREQ(cmd, "get") || STREQ(cmd, "queue")) {
	Tcl_DStringInit(&ds);
    	if (!SplitUrl(&ds, argv[2], &host, &port, &file)) {
	    Tcl_AppendResult(interp, "invalid url: ", argv[2], NULL);
	} else if ((sock = Ns_SockAsyncConnect(host, port)) == INVALID_SOCKET) {
	    Tcl_AppendResult(interp, "could not connect to : ", host, NULL);
	} else if (*cmd == 'g') {
	    Tcl_DStringInit(&resp);
	    if (!HttpGet(sock, host, file, NULL, &resp)) {
	    	Tcl_AppendResult(interp, "could not send request: ", host, NULL);
	    } else {
		HttpResult(interp, resp.string, NULL, NULL);
		result = TCL_OK;
	    }
	    ns_sockclose(sock);
	    Tcl_DStringFree(&resp);
	} else {
    	    httpPtr = ns_malloc(sizeof(Http));
	    httpPtr->state = REQ_SEND;
	    httpPtr->sock = sock;
    	    Tcl_DStringInit(&httpPtr->ds);
    	    HttpReq(&httpPtr->ds, host, file, NULL);
    	    httpPtr->next = httpPtr->ds.string;
    	    httpPtr->len = httpPtr->ds.length;
    	    Ns_SockCallback(sock, Send, httpPtr, NS_SOCK_WRITE);
	    Ns_MutexLock(&lock);
	    do {
    	    	sprintf(buf, "http%d", ++nextid);
	 	hPtr = Tcl_CreateHashEntry(&ids, buf, &new);
	    } while (!new);
	    Tcl_SetHashValue(hPtr, httpPtr);
	    Ns_MutexUnlock(&lock);
	    Tcl_SetResult(interp, buf, TCL_VOLATILE);
	    result = TCL_OK;
	}
	Tcl_DStringFree(&ds);
    } else if (STREQ(cmd, "wait") || STREQ(cmd, "cancel")) {
    	Ns_MutexLock(&lock);
	hPtr = Tcl_FindHashEntry(&ids, argv[2]);
	if (hPtr != NULL) {
	    httpPtr= Tcl_GetHashValue(hPtr);
	    Tcl_DeleteHashEntry(hPtr);
	    state = httpPtr->state;
	    if (*cmd == 'c' && !(state & REQ_DONE)) {
    		Ns_SockCallback(httpPtr->sock, Cancel, httpPtr, NS_SOCK_WRITE|NS_SOCK_READ);
	    }
    	    while (!(httpPtr->state & REQ_DONE)) {
		Ns_CondWait(&cond, &lock);
    	    }
	}
	Ns_MutexUnlock(&lock);
	if (hPtr == NULL) {
	    Tcl_AppendResult(itPtr->interp, "no such request: ", argv[2], NULL);
	} else {
	    if (*cmd == 'c') {
		sprintf(buf, "%d", state);
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	    } else if ((httpPtr->state & REQ_EOF)) {
		HttpResult(interp, httpPtr->ds.string, NULL, NULL);
	    }
    	    Tcl_DStringFree(&httpPtr->ds);
    	    ns_sockclose(httpPtr->sock);
    	    ns_free(httpPtr);
	    result = TCL_OK;
	}
    } else {
    	Tcl_AppendResult(interp, "unknown command \"", cmd,
	    "\": should be get, queue, wait, or cancel", NULL);
    }
    return result;
}


static int
HttpGet(SOCKET sock, char *host, char *file, Ns_Set *hdrs, Tcl_DString *dsPtr)
{
    int n, len;
    char *ptr, buf[1024];

    if (Ns_SockWait(sock, NS_SOCK_WRITE, 1) != NS_OK) {
    	return 0;
    }
    
    HttpReq(dsPtr, host, file, hdrs);
    ptr = dsPtr->string;
    len = dsPtr->length;
    while (len > 0) {
	n = Ns_SockSend(sock, ptr, len, 1);
	if (n < 0) {
	    return 0;
	}
	ptr += n;
	len -= n;
    }
    Tcl_DStringTrunc(dsPtr, 0);
    do {
	n = Ns_SockRecv(sock, buf, sizeof(buf), 1);
	if (n > 0) {
	    Tcl_DStringAppend(dsPtr, buf, n);
	}
    } while (n > 0);
    if (n < 0) {
	return 0;
    }
    return 1;
}

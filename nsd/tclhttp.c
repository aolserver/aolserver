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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclhttp.c,v 1.16.2.5 2004/09/30 19:47:25 dossy Exp $, compiled: " __DATE__ " " __TIME__;

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
    size_t len;
    Ns_Time stime;
    Ns_Time etime;
    Tcl_DString ds;
} Http;

#define METHOD_GET  "GET"
#define METHOD_POST "POST"

/*
 * Local functions defined in this file
 */

static Ns_SockProc HttpSend;
static Ns_SockProc HttpRecv;
static Ns_SockProc HttpCancel;
static int HttpDone(SOCKET sock, Http *httpPtr, int state);
static Http *HttpOpen(char *method, char *url, Ns_Set *hdrs, char *body);
static void HttpClose(Http *httpPtr, int nb);
static int HttpAbort(Http *httpPtr);
static char *HttpResult(char *response, Ns_Set *hdrs);
static Ns_Mutex lock;
static Ns_Cond cond;


/*
 *----------------------------------------------------------------------
 *
 * NsTclHttpObjCmd --
 *
 *	Implements ns_http to handle async HTTP requests.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May queue an HTTP request.
 *
 *----------------------------------------------------------------------
 */

int
NsTclHttpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = arg;
    Http *httpPtr;
    char buf[50], *result, *carg;
    int new, status, n, timeidx;
    Ns_Time timeout, incr;
    Ns_Set *hdrs;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    static CONST char *opts[] = {
       "cancel", "cleanup", "queue", "wait", NULL
    };
    enum {
        HCancelIdx, HCleanupIdx, HQueueIdx, HWaitIdx
    } opt;

    char *method, *url, *body;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case HQueueIdx:
        if (objc < 4 || objc > 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "method url ?body? ?headers?");
            return TCL_ERROR;
        }

        method = Tcl_GetString(objv[2]);
        url    = Tcl_GetString(objv[3]);

        if (objc > 4) {
            body = Tcl_GetString(objv[4]);

            if (objc > 5) {
                if (Ns_TclGetSet2(interp, Tcl_GetString(objv[5]), &hdrs) != TCL_OK) {
                    return TCL_ERROR;
                }
            }
            else {
                hdrs = NULL;
            }
        }
        else {
            body = NULL;
            hdrs = NULL;
        }

        httpPtr = HttpOpen(method, url, hdrs, body);
        if (httpPtr == NULL) {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), 
                    "could not connect to : ", 
                    url, NULL);
            return TCL_ERROR;
        }
        Ns_GetTime(&httpPtr->stime);
        Ns_SockCallback(httpPtr->sock, HttpSend, httpPtr, NS_SOCK_WRITE);
        n = itPtr->https.numEntries;
        do {
            sprintf(buf, "http%d", n++);
            hPtr = Tcl_CreateHashEntry(&itPtr->https, buf, &new);
        } while (!new);
        Tcl_SetHashValue(hPtr, httpPtr);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
        break;

    case HCancelIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "id");
            return TCL_ERROR;
        }
        hPtr = Tcl_FindHashEntry(&itPtr->https, Tcl_GetString(objv[2]));
        if (hPtr == NULL) {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "no such request: ", 
                                   Tcl_GetString(objv[2]), NULL);
            return TCL_ERROR;
        }
        httpPtr = Tcl_GetHashValue(hPtr);
        Tcl_DeleteHashEntry(hPtr);
        Tcl_SetIntObj(Tcl_GetObjResult(interp), HttpAbort(httpPtr));
        break;

    case HWaitIdx:
        if (objc < 4 || objc > 8) {
            Tcl_WrongNumArgs(interp, 2, objv, "id resultsVar ?timeout? ?headers? ?-servicetime svcTime?");
            return TCL_ERROR;
        }
        carg = Tcl_GetString(objv[objc - 2]);
        if (STRIEQ(carg, "-servicetime")) {
            timeidx = objc - 1;
            objc -= 2; /* so I don't have to refactor the rest of the code */
        } else {
            timeidx = 0;
        }
        if (objc < 5) {
            incr.sec  = 2;
            incr.usec = 0;
        } else if (Ns_TclGetTimeFromObj(interp, objv[4], &incr) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_GetTime(&timeout);
        Ns_IncrTime(&timeout, incr.sec, incr.usec);
        if (objc < 6) {
            hdrs = NULL;
        } else if (Ns_TclGetSet2(interp, Tcl_GetString(objv[5]), &hdrs) != TCL_OK) {
            return TCL_ERROR;
        }
        hPtr = Tcl_FindHashEntry(&itPtr->https, Tcl_GetString(objv[2]));
        if (hPtr == NULL) {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "no such request: ", 
                    Tcl_GetString(objv[2]), NULL);
            return TCL_ERROR;
        }
        httpPtr= Tcl_GetHashValue(hPtr);
        status = NS_OK;
        Ns_MutexLock(&lock);
        while (status == NS_OK && !(httpPtr->state & REQ_DONE)) {
            status = Ns_CondTimedWait(&cond, &lock, &timeout);
        }
        Ns_MutexUnlock(&lock);
        if (timeidx > 0) {
            Ns_Time dtime;
            if (status != NS_OK) {
                /* if we timed-out, we didn't capture this yet */
                Ns_GetTime(&httpPtr->etime);
            }

            Ns_DiffTime(&httpPtr->etime, &httpPtr->stime, &dtime);
            snprintf(buf, 50, "%ld:%ld", dtime.sec, dtime.usec);
            Tcl_SetVar(interp, Tcl_GetString(objv[timeidx]), buf, TCL_LEAVE_ERR_MSG);
        }
        if (status != NS_OK) {
            httpPtr = NULL;
            result = "timeout";
        } else {
            if (httpPtr->state & REQ_EOF) {
                result = HttpResult(httpPtr->ds.string, hdrs);
            } else {
                status = NS_ERROR;
                result = "error";
            }
        }
        result = (char*)Tcl_SetVar(interp, Tcl_GetString(objv[3]), result, TCL_LEAVE_ERR_MSG);
        if (httpPtr != NULL) {
            Tcl_DeleteHashEntry(hPtr);
            HttpClose(httpPtr, 0);
        } 
        if (result == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetBooleanObj(Tcl_GetObjResult(interp), status == NS_OK ? 1 : 0);
        break;

    case HCleanupIdx:
        hPtr = Tcl_FirstHashEntry(&itPtr->https, &search);
        while (hPtr != NULL) {
            httpPtr = Tcl_GetHashValue(hPtr);
            (void) HttpAbort(httpPtr);
            hPtr = Tcl_NextHashEntry(&search);
        }
        Tcl_DeleteHashTable(&itPtr->https);
        Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);
        break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpOpen --
 *
 *        Open a connection to the given URL host and construct
 *        an Http structure to fetch the file.
 *
 * Results:
 *        Pointer to Http struct or NULL on error.
 *
 * Side effects:
 *        Will open a socket connection.
 *
 *----------------------------------------------------------------------
 */

Http *
HttpOpen(char *method, char *url, Ns_Set *hdrs, char *body)
{
    Http *httpPtr = NULL;
    SOCKET sock;
    char *host, *file, *port;
    int i;
    int body_len = 0;
    char lenbuf[20];

    if (strncmp(url, "http://", 7) != 0 || url[7] == '\0') {
        return NULL;
    }
    host = url + 7;
    file = strchr(host, '/');
    if (file != NULL) {
        *file = '\0';
    }
    port = strchr(host, ':');
    if (port == NULL) {
        i = 80;
    } else {
        *port = '\0';
        i = atoi(port+1);
    }
    sock = Ns_SockAsyncConnect(host, i);
    if (port != NULL) {
        *port = ':';
    }
    if (sock != INVALID_SOCKET) {
        httpPtr = ns_malloc(sizeof(Http));
        httpPtr->state = REQ_SEND;
        httpPtr->sock = sock;
        Tcl_DStringInit(&httpPtr->ds);
        if (file != NULL) {
            *file = '/';
        }
        Ns_DStringAppend(&httpPtr->ds, method);
        Ns_StrToUpper(Ns_DStringValue(&httpPtr->ds));
        Ns_DStringVarAppend(&httpPtr->ds, " ", file ? file : "/", " HTTP/1.0\r\n", NULL);
        if (file != NULL) {
            *file = '\0';
        }
        Ns_DStringVarAppend(&httpPtr->ds,
            "User-Agent: ", Ns_InfoServerName(), "/", Ns_InfoServerVersion(), "\r\n"
            "Connection: close\r\n"
            "Host: ", host, "\r\n", NULL);
        if (file != NULL) {
            *file = '/';
        }
        if (hdrs != NULL) {
            for (i = 0; i < Ns_SetSize(hdrs); i++) {
                Ns_DStringVarAppend(&httpPtr->ds,
                    Ns_SetKey(hdrs, i), ": ", Ns_SetValue(hdrs, i), "\r\n", NULL);
            }
        }

        if (body != NULL) {
            body_len = strlen(body);

            if (body_len > 0) {
                sprintf(lenbuf, "%d", body_len);
                Ns_DStringVarAppend(&httpPtr->ds,
                                    "Content-Length: ",
                                    lenbuf,
                                    "\r\n",
                                    NULL);
            }
        }

        Tcl_DStringAppend(&httpPtr->ds, "\r\n", 2);

        if (body_len > 0) {
            Tcl_DStringAppend(&httpPtr->ds, body, body_len);
        }

        httpPtr->next = httpPtr->ds.string;
        httpPtr->len = httpPtr->ds.length;
    }
    if (file != NULL) {
        *file = '/';
    }
    return httpPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * HttpResult --
 *
 *        Parse an Http response for the result body and headers.
 *
 * Results:
 *        Pointer body within Http buffer.
 *
 * Side effects:
 *        Will append parsed response headers to given hdrs if
 *        not NULL.
 *
 *----------------------------------------------------------------------
 */

static char *
HttpResult(char *response, Ns_Set *hdrs)
{
    int firsthdr, len;
    char *eoh, *body, *p;

    body = response;
    eoh = strstr(response, "\r\n\r\n");
    if (eoh != NULL) {
        body = eoh + 4;
    } else {
        eoh = strstr(response, "\n\n");
        if (eoh != NULL) {
            body = eoh + 2;
        }
    }
    if (eoh != NULL) {
        *eoh = '\0';
    }

    /*
     * Parse the headers saved in the dstring if requested.
     */

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
    return body;
}


static void
HttpClose(Http *httpPtr, int nb)
{
    Tcl_DStringFree(&httpPtr->ds);
    if (nb) {
        ns_socknbclose(httpPtr->sock);
    } else {
        ns_sockclose(httpPtr->sock);
    }
    ns_free(httpPtr);
}


static int
HttpSend(SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;
    int n;

    n = send(sock, httpPtr->next, httpPtr->len, 0);
    if (n < 0) {
        Tcl_DStringFree(&httpPtr->ds);
        return HttpDone(sock, httpPtr, (REQ_DONE|REQ_ERR));
    }
    httpPtr->next += n;
    httpPtr->len -= n;
    if (httpPtr->len == 0) {
        shutdown(sock, 1);
        Tcl_DStringTrunc(&httpPtr->ds, 0);
        Ns_MutexLock(&lock);
        httpPtr->state = REQ_RECV;
        Ns_MutexUnlock(&lock);
        Ns_SockCallback(sock, HttpRecv, arg, NS_SOCK_READ);
    }
    return NS_TRUE;
}


static int
HttpRecv(SOCKET sock, void *arg, int why)
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
    Ns_GetTime(&httpPtr->etime);
    return HttpDone(sock, httpPtr, state);
}


static int
HttpCancel(SOCKET sock, void *arg, int why)
{
    Http *httpPtr = arg;

    return HttpDone(sock, httpPtr, (REQ_CANCEL|REQ_DONE));
}


static int
HttpDone(SOCKET sock, Http *httpPtr, int state)
{
    Ns_MutexLock(&lock);
    httpPtr->state = state;
    Ns_MutexUnlock(&lock);
    Ns_CondBroadcast(&cond);
    return NS_FALSE;
}


static int
HttpAbort(Http *httpPtr)
{
    int state;

    Ns_MutexLock(&lock);
    state = httpPtr->state;
    if (!(state & REQ_DONE)) {
        Ns_SockCancelCallbackEx(httpPtr->sock, HttpCancel, httpPtr);
        while (!(httpPtr->state & REQ_DONE)) {
            Ns_CondWait(&cond, &lock);
        }
    }
    Ns_MutexUnlock(&lock);
    HttpClose(httpPtr, 1);
    return state;
}

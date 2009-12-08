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
 * tclsock.c --
 *
 *	Tcl commands that let you do TCP sockets. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclsock.c,v 1.25 2009/12/08 04:12:19 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is used for a socket callback.
 */

typedef struct TclSockCallback {
    char       *server;
    Tcl_Channel chan;
    int    	when;
    char        script[1];
} TclSockCallback;

/*
 * The following structure is used for a socket listen callback.
 */

typedef struct ListenCallback {
    char *server;
    char script[1];
} ListenCallback;

/*
 * Local functions defined in this file
 */

static int GetSet(Tcl_Interp *interp, char *flist, int write, fd_set ** ppset,
		  fd_set * pset, int *maxPtr);
static void AppendReadyFiles(Tcl_Interp *interp, fd_set * pset, int write,
			     char *flist, Tcl_DString *pds);
static int EnterSock(Tcl_Interp *interp, SOCKET sock);
static int EnterDup(Tcl_Interp *interp, SOCKET sock);
static int EnterDupedSocks(Tcl_Interp *interp, SOCKET sock);
static int SockSetBlockingObj(char *value, Tcl_Interp *interp, int objc, 
				Tcl_Obj *CONST objv[]);
static Ns_SockProc SockListenCallback;
static Ns_QueueWaitProc WaitCallback;

void
NsTclSockArgProc(Tcl_DString *dsPtr, void *arg)
{
    TclSockCallback *cbPtr = arg;

    Tcl_DStringAppendElement(dsPtr, cbPtr->script);
}
 

/*
 *----------------------------------------------------------------------
 *
 * NsTclQueWaitObjCmd --
 *
 *	Implement the ns_quewait command to register a Tcl script
 *	Ns_QueueWait callback.  Unlike the general ns_sockcallback,
 *	the script will execute later in the same, connection-bound
 *	interp which calls ns_quewait.  Thus the interface is
 *	closer to Tcl's standard "fileevent" command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Depends on callbacks.
 *
 *----------------------------------------------------------------------
 */

int
NsTclQueWaitObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    TclSockCallback *cbPtr;
    Ns_Conn *conn;
    Tcl_Channel chan;
    SOCKET sock;
    int when;
    Ns_Time timeout;
    static CONST char *opt[] = {
        "readable", "writable", NULL
    };
    enum {
        ReadIdx, WriteIdx
    } idx;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId event timeout script");
        return TCL_ERROR;
    }
    if (NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, Tcl_GetString(objv[1]), NULL);
    if (chan == NULL) {
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[2], opt, "event", 0,
                	    (int *) &idx) != TCL_OK) {
	return TCL_ERROR;
    }
    switch (idx) {
    case ReadIdx:
	when = NS_SOCK_READ;
	break;
    case WriteIdx:
	when = NS_SOCK_WRITE;
	break;
    }
    if (Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]),
			(idx == WriteIdx) ? 1 :0, (int *) &sock) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Ns_TclGetTimeFromObj(interp, objv[3], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Ns_QueueWait(conn, sock, WaitCallback, objv[4], when, &timeout) != NS_OK) {
	Tcl_SetResult(interp, "could not register sock wait", TCL_STATIC);
        return TCL_ERROR;
    }
    Tcl_IncrRefCount(objv[4]);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetHostObjCmd, NsTclGetAddrObjCmd --
 *
 *	Performs a forward or reverse DNS lookup.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Puts a hostname into the tcl result. 
 *
 *----------------------------------------------------------------------
 */

static int
GetObjCmd(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[], int byaddr)
{
    Ns_DString  ds;
    char       *opt, *addr;
    int         all = 0;
    int         status;

    if (byaddr) {
        if (objc < 2 || objc > 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "?-all? address");
            return TCL_ERROR;
        }
    } else {
        if (objc != 2) {
            Tcl_WrongNumArgs(interp, 1, objv, "address");
            return TCL_ERROR;
        }
    }
    opt = Tcl_GetString(objv[1]);
    if (objc >= 3 && STREQ(opt, "-all")) {
        all = 1;
        addr = Tcl_GetString(objv[2]);
    } else {
        addr = opt;
    }

    Ns_DStringInit(&ds);
    if (byaddr) {
        if (all) {
            status = Ns_GetAllAddrByHost(&ds, addr);
        } else {
            status = Ns_GetAddrByHost(&ds, addr);
        }
    } else {
        status = Ns_GetHostByAddr(&ds, addr);
    }
    if (status == NS_TRUE) {
    	Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    }
    Ns_DStringFree(&ds);
    if (status != NS_TRUE) {
        Tcl_AppendResult(interp, "could not lookup ", addr, NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
NsTclGetHostObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return GetObjCmd(interp, objc, objv, 0);
}

int
NsTclGetAddrObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return GetObjCmd(interp, objc, objv, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockSetBlockingObjCmd --
 *
 *	Sets a socket blocking. 
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
NsTclSockSetBlockingObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return SockSetBlockingObj("1", interp, objc, objv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockSetNonBlockingObjCmd --
 *
 *	Sets a socket nonblocking. 
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
NsTclSockSetNonBlockingObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return SockSetBlockingObj("0", interp, objc, objv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockNReadObjCmd --
 *
 *	Gets the number of bytes that a socket has waiting to be 
 *	read. 
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
NsTclSockNReadObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int         nread;
    Tcl_Channel	chan;
    SOCKET      sock;
    char	buf[20];

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId");
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, Tcl_GetString(objv[1]), NULL);
    if (chan == NULL || Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]), 0,
	    (int *) &sock) != TCL_OK) {
	return TCL_ERROR;
    }
    if (ns_sockioctl(sock, FIONREAD, &nread) != 0) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "ns_sockioctl failed: ", 
		 Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    nread += Tcl_InputBuffered(chan);
    sprintf(buf, "%d", nread);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}
    

/*
 *----------------------------------------------------------------------
 *
 * NsTclSockListenObjCmd --
 *
 *	Listen on a TCP port. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will listen on a port. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSockListenObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    SOCKET  sock;
    char   *addr;
    int     port;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "address port");
        return TCL_ERROR;
    }
    addr = Tcl_GetString(objv[1]);
    if (STREQ(addr, "*")) {
        addr = NULL;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &port) != TCL_OK) {
        return TCL_ERROR;
    }
    sock = Ns_SockListen(addr, port);
    if (sock == INVALID_SOCKET) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "could not listen on \"",
		Tcl_GetString(objv[1]), ":", 
		Tcl_GetString(objv[2]), "\"", NULL);
        return TCL_ERROR;
    }
    return EnterSock(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockAcceptObjCmd --
 *
 *	Accept a connection from a listening socket. 
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
NsTclSockAcceptObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    SOCKET sock;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId");
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]), 0, (int *) &sock) != TCL_OK) {
        return TCL_ERROR;
    }
    sock = Ns_SockAccept(sock, NULL, 0);
    if (sock == INVALID_SOCKET) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "accept failed: ",
		 Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return EnterDupedSocks(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockCheckObjCmd --
 *
 *	Check if a socket is still connected, useful for nonblocking. 
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
NsTclSockCheckObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Obj *objPtr;
    SOCKET sock;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId");
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]), 1, (int *) &sock) != TCL_OK) {
	return TCL_ERROR;
    }
    if (send(sock, NULL, 0, 0) != 0) {
	objPtr = Tcl_NewBooleanObj(0);
    } else {
	objPtr = Tcl_NewBooleanObj(1);
    }
    Tcl_SetObjResult(interp, objPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockOpenObjCmd --
 *
 *	Open a tcp connection to a host/port. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will open a connection. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSockOpenObjCmd(
    ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]
)
{
    SOCKET sock;
    char *host, *lhost = NULL, *opt, *val;
    int lport = 0, port, first, async = 0, timeout = -1;

    if (objc < 3 || objc > 9) {
      syntax:
        Tcl_WrongNumArgs(interp, 1, objv,
                "?(-nonblock | -async) | -timeout seconds? "
                "?-localhost host? ?-localport port? host port");
        return TCL_ERROR;
    }
    
    /*
     * Parse optional arguments.  Note that either the:
     *     -nonblock | -async
     * or
     *     -timeout seconds
     * combinations are accepted.
     */

    for (first = 1; first < objc; first++) {
        opt= Tcl_GetString(objv[first]);
        if (*opt != '-') {
            break; /* End of options */
        }
        if (STREQ(opt, "-nonblock") || STREQ(opt, "-async")) {
            if (timeout >= 0) {
                goto syntax;
            }
            async = 1;
        } else if (STREQ(opt, "-localhost")) {
            if (++first >= objc) {
                goto syntax;
            }
            lhost = Tcl_GetString(objv[first]);
            if (*lhost == 0) {
                Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                        "invalid hostname: must not be empty", NULL);
                return TCL_ERROR;
            }
        } else if (STREQ(opt, "-timeout")) {
            if (++first >= objc || async) {
                goto syntax;
            }
            if (Tcl_GetIntFromObj(interp, objv[first], &timeout) != TCL_OK) {
                return TCL_ERROR;
            }
        } else if (STREQ(opt, "-localport")) {
            if (++first >= objc) {
                goto syntax;
            }
            if (Tcl_GetIntFromObj(interp, objv[first], &lport) != TCL_OK) {
                return TCL_ERROR;
            }
            if (lport < 0) {
                val = Tcl_GetString(objv[first]);
                Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                        "invalid port: ", val, "; must be > 0", NULL);
                return TCL_ERROR;
            }
        } else {
            Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                    "invalid option: \"", opt, "\"", NULL);
            return TCL_ERROR;   
        }
    }

    if ((objc - first) != 2) {
        goto syntax;
    }

    /*
     * Get the host to connect to. Bark on invalid entry.
     */

    host = Tcl_GetString(objv[first]);
    if (*host == 0) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "invalid hostname: must not be empty", NULL);
        return TCL_ERROR;
    }

    /*
     * Get the port to connect to. Bark on invalid entry.
     */

    if (Tcl_GetIntFromObj(interp, objv[first+1], &port) != TCL_OK) {
        return TCL_ERROR;
    } else if (port < 0) {
        val = Tcl_GetString(objv[first+1]);
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "invalid port: ", val, "; must be > 0", NULL);
        return TCL_ERROR;
    }

    /*
     * Perform the connection.
     */

    if (async) {
        sock = Ns_SockAsyncConnect2(host, port, lhost, lport);
    } else if (timeout < 0) {
        sock = Ns_SockConnect2(host, port, lhost, lport);
    } else {
        sock = Ns_SockTimedConnect2(host, port, lhost, lport, timeout);
    }

    if (sock == INVALID_SOCKET) {
        char *why = Tcl_GetErrno() ? Tcl_PosixError(interp) : "reason unknown";
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                "can't connect to \"", host, ":",
                Tcl_GetString(objv[first+1]), "\"; ", why, NULL);
        return TCL_ERROR;
    }

    return EnterDupedSocks(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSelectObjCmd --
 *
 *	Imlements select: basically a tcl version of
 *	select(2).
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
NsTclSelectObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    fd_set          rset, wset, eset, *rPtr, *wPtr, *ePtr;
    int	    maxfd;
    int             i, status, arg;
    Tcl_Channel	    chan;
    struct timeval  tv, *tvPtr;
    Tcl_DString     dsRfd, dsNbuf;
    Tcl_Obj   **fobjv;
    int         fobjc;
    Ns_Time     timeout;

    status = TCL_ERROR;
    if (objc != 6 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-timeout sec? rfds wfds efds");
        return TCL_ERROR;
    }
    if (objc == 4) {
        tvPtr = NULL;
        arg = 1;
    } else {
        tvPtr = &tv;
        if (strcmp(Tcl_GetString(objv[1]), "-timeout") != 0) {
        	Tcl_WrongNumArgs(interp, 1, objv, "?-timeout sec? rfds wfds efds");
	    	return TCL_ERROR;
        }
        if (Ns_TclGetTimeFromObj(interp, objv[2], &timeout) != TCL_OK) {
            return TCL_ERROR;
        }
        tv.tv_sec = timeout.sec;
        tv.tv_usec = timeout.usec;
        arg = 3;
    }

    /*
     * Readable fd's are treated differently because they may
     * have buffered input. Before doing a select, see if they
     * have any waiting data that's been buffered by the channel.
     */
   
	if (Tcl_ListObjGetElements(interp, objv[arg++], &fobjc, &fobjv) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_DStringInit(&dsRfd);
    Tcl_DStringInit(&dsNbuf);
    for (i = 0; i < fobjc; ++i) {
	chan = Tcl_GetChannel(interp, Tcl_GetString(fobjv[i]), NULL);
	if (chan == NULL) {
	    goto done;
			}
	if (Tcl_InputBuffered(chan) > 0) {
	    Tcl_DStringAppendElement(&dsNbuf, Tcl_GetString(fobjv[i]));
	} else {
	    Tcl_DStringAppendElement(&dsRfd, Tcl_GetString(fobjv[i]));
	}
    }

    if (dsNbuf.length > 0) {
	/*
	 * Since at least one read fd had buffered input,
	 * turn the select into a polling select just
	 * to pick up anything else ready right now.
	 */
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	tvPtr = &tv;
    }
    maxfd = 0;
    if (GetSet(interp, dsRfd.string, 0, &rPtr, &rset, &maxfd) != TCL_OK) {
	goto done;
    }
    if (GetSet(interp, Tcl_GetString(objv[arg++]), 1, &wPtr, &wset, &maxfd) != TCL_OK) {
	goto done;
    }
    if (GetSet(interp, Tcl_GetString(objv[arg++]), 0, &ePtr, &eset, &maxfd) != TCL_OK) {
	goto done;
    }

    /*
     * Return immediately if we're not doing a select on anything.
     */
    
    if (dsNbuf.length == 0 &&
	rPtr == NULL &&
	wPtr == NULL &&
	ePtr == NULL &&
	tvPtr == NULL) {
	
	status = TCL_OK;
    } else {

	/*
	 * Actually perform the select.
	 */
	
    	do {
	    i = select(maxfd + 1, rPtr, wPtr, ePtr, tvPtr);
	} while (i < 0 && errno == EINTR);

    	if (i == INVALID_SOCKET) {
	    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "select failed: ",
		    Tcl_PosixError(interp), NULL);
    	} else {
	    if (i == 0) {
		/*
		 * The sets can have any random value now
		 */
		
		if (rPtr != NULL) {
		    FD_ZERO(rPtr);
		}
		if (wPtr != NULL) {
		    FD_ZERO(wPtr);
		}
		if (ePtr != NULL) {
		    FD_ZERO(ePtr);
		}
	    }
	    AppendReadyFiles(interp, rPtr, 0, dsRfd.string, &dsNbuf);
	    arg -= 2;
	    AppendReadyFiles(interp, wPtr, 1, Tcl_GetString(objv[arg++]), NULL);
	    AppendReadyFiles(interp, ePtr, 0, Tcl_GetString(objv[arg++]), NULL);
	    status = TCL_OK;
	}
    }
    
done:
    Tcl_DStringFree(&dsRfd);
    Tcl_DStringFree(&dsNbuf);
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSocketPairObjCmd --
 *
 *	Create a new socket pair. 
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
NsTclSocketPairObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    SOCKET socks[2];

    if (ns_sockpair(socks) != 0) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "ns_sockpair failed:  ", 
		 Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    if (EnterSock(interp, socks[0]) != TCL_OK) {
	ns_sockclose(socks[1]);
	return TCL_ERROR;
    }
    return EnterSock(interp, socks[1]);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockCallbackCmd --
 *
 *	Register a Tcl callback to be run when a certain state exists 
 *	on a socket. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	A callback will be registered. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSockCallbackObjCmd(ClientData arg, Tcl_Interp *interp, int objc, 
				Tcl_Obj *CONST objv[])
{
    SOCKET sock;
    int     when;
    char   *s;
    TclSockCallback *cbPtr;
    NsInterp *itPtr = arg;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId script when");
        return TCL_ERROR;
    }
    s = Tcl_GetString(objv[3]);
    when = 0;
    while (*s != '\0') {
        if (*s == 'r') {
            when |= NS_SOCK_READ;
        } else if (*s == 'w') {
            when |= NS_SOCK_WRITE;
        } else if (*s == 'e') {
            when |= NS_SOCK_EXCEPTION;
        } else if (*s == 'x') {
            when |= NS_SOCK_EXIT;
        } else {
	    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid when specification \"",
		    Tcl_GetString(objv[3]), 
		    "\": should be one or more of r, w, e, or x", 
		    NULL);
            return TCL_ERROR;
        }
        ++s;
    }
    if (when == 0) {
	Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid when specification \"",
		Tcl_GetString(objv[3]), 
		"\": should be one or more of r, w, e, or x", 
		NULL);
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]),
	    (when & NS_SOCK_WRITE), (int *) &sock) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Pass a dup of the socket to the callback thread, allowing
     * this thread's cleanup to close the current socket.  It's
     * not possible to simply register the channel again with
     * a NULL interp because the Tcl channel code is not entirely
     * thread safe.
     */

    sock = ns_sockdup(sock);
    cbPtr = ns_malloc(sizeof(TclSockCallback) + Tcl_GetCharLength(objv[2]));
    cbPtr->server = itPtr->servPtr->server;
    cbPtr->chan = NULL;
    cbPtr->when = when;
    strcpy(cbPtr->script, Tcl_GetString(objv[2]));
    if (Ns_SockCallback(sock, NsTclSockProc, cbPtr,
    	    	    	when | NS_SOCK_EXIT) != NS_OK) {
	Tcl_SetResult(interp, "could not register callback", TCL_STATIC);
	ns_sockclose(sock);
        ns_free(cbPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockListenCallbackObjCmd --
 *
 *	Listen on a socket and register a callback to run when 
 *	connections arrive. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will register a callback and listen on a socket. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSockListenCallbackObjCmd(ClientData arg, Tcl_Interp *interp, int objc, 
				Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = arg;
    ListenCallback *lcbPtr;
    int       port;
    char     *addr;

    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "address port script");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &port) != TCL_OK) {
        return TCL_ERROR;
    }
    addr = Tcl_GetString(objv[1]);
    if (STREQ(addr, "*")) {
        addr = NULL;
    }
    lcbPtr = ns_malloc(sizeof(ListenCallback) + Tcl_GetCharLength(objv[3]));
    lcbPtr->server = itPtr->servPtr->server;
    strcpy(lcbPtr->script, Tcl_GetString(objv[3]));
    if (Ns_SockListenCallback(addr, port, SockListenCallback, lcbPtr) != NS_OK) {
	Tcl_SetResult(interp, "could not register callback", TCL_STATIC);
        ns_free(lcbPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockSetBlockingObj --
 *
 *	Set a socket blocking. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
SockSetBlockingObj(char *value, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Channel chan;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "sockId");
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, Tcl_GetString(objv[1]), NULL);
    if (chan == NULL) {
	return TCL_ERROR;
    }
    return Tcl_SetChannelOption(interp, chan, "-blocking", value);
}


/*
 *----------------------------------------------------------------------
 *
 * AppendReadyFiles --
 *
 *	Find files in an fd_set that are selected and append them to 
 *	the tcl result, and also an optional passed-in dstring. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Ready files will be appended to pds if not null, and also 
 *	interp result. 
 *
 *----------------------------------------------------------------------
 */

static void
AppendReadyFiles(Tcl_Interp *interp, fd_set *setPtr, int write, char *flist,
		 Tcl_DString *dsPtr)
{
    int           fargc;
    char        **fargv;
    SOCKET sock;
    Tcl_DString   ds;

    Tcl_DStringInit(&ds);
    if (dsPtr == NULL) {
	dsPtr = &ds;
    }
    Tcl_SplitList(interp, flist, &fargc, (CONST char***)&fargv);
    while (fargc--) {
        Ns_TclGetOpenFd(interp, fargv[fargc], write, (int *) &sock);
        if (FD_ISSET(sock, setPtr)) {
            Tcl_DStringAppendElement(dsPtr, fargv[fargc]);
        }
    }

    /*
     * Append the ready files to the tcl interp.
     */
    
    Tcl_AppendElement(interp, dsPtr->string);
    ckfree((char *) fargv);
    Tcl_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * GetSet --
 *
 *	Take a Tcl list of files and set bits for each in the list in 
 *	an fd_set. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will set bits in fd_set. ppset may be NULL on error, or
 *	a valid fd_set on success. Max fd will be returned in *maxPtr.
 *
 *----------------------------------------------------------------------
 */

static int
GetSet(Tcl_Interp *interp, char *flist, int write, fd_set **setPtrPtr,
       fd_set *setPtr, int *maxPtr)
{
    SOCKET sock;
    int    fargc;
    char **fargv;
    int    status;

    if (Tcl_SplitList(interp, flist, &fargc, (CONST char***)&fargv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (fargc == 0) {

	/*
	 * Tcl_SplitList failed, so abort.
	 */
	
	ckfree((char *)fargv);
        *setPtrPtr = NULL;
        return TCL_OK;
    } else {
        *setPtrPtr = setPtr;
    }
    
    FD_ZERO(setPtr);
    status = TCL_OK;

    /*
     * Loop over each file, try to get its FD, and set the bit in
     * the fd_set.
     */
    
    while (fargc--) {
        if (Ns_TclGetOpenFd(interp, fargv[fargc], write,
			    (int *) &sock) != TCL_OK) {
            status = TCL_ERROR;
            break;
        }
        if (sock > (SOCKET) *maxPtr) {
            *maxPtr = (int) sock;
        }
        FD_SET(sock, setPtr);
    }
    ckfree((char *) fargv);
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * EnterSock, EnterDup, EnterDupedSocks --
 *
 *	Append a socket handle to the tcl result and register its 
 *	channel.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Will create channel, append handle to result. 
 *
 *----------------------------------------------------------------------
 */

static int
EnterSock(Tcl_Interp *interp, SOCKET sock)
{
    Tcl_Channel chan;

    chan = Tcl_MakeTcpClientChannel((ClientData) sock);
    if (chan == NULL) {
	Tcl_AppendResult(interp, "could not open socket", NULL);
        ns_sockclose(sock);
        return TCL_ERROR;
    }
    Tcl_SetChannelOption(interp, chan, "-translation", "binary");
    Tcl_RegisterChannel(interp, chan);
    Tcl_AppendElement(interp, Tcl_GetChannelName(chan));
    return TCL_OK;
}

static int
EnterDup(Tcl_Interp *interp, SOCKET sock)
{
    sock = ns_sockdup(sock);
    if (sock == INVALID_SOCKET) {
        Tcl_AppendResult(interp, "could not dup socket: ", 
			 ns_sockstrerror(errno), NULL);
        return TCL_ERROR;
    }
    return EnterSock(interp, sock);
}

static int
EnterDupedSocks(Tcl_Interp *interp, SOCKET sock)
{
    if (EnterSock(interp, sock) != TCL_OK ||
    	EnterDup(interp, sock) != TCL_OK) {
    	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockProc --
 *
 *	This is the C wrapper callback that is registered from 
 *	callback. 
 *
 * Results:
 *	NS_TRUE or NS_FALSE on error 
 *
 * Side effects:
 *	Will run Tcl script. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSockProc(SOCKET sock, void *arg, int why)
{
    Tcl_Interp  *interp;
    Tcl_DString  script;
    Tcl_Obj	*objPtr;
    char        *w;
    int          result, ok;
    TclSockCallback    *cbPtr = arg;

    if (why != NS_SOCK_EXIT || (cbPtr->when & NS_SOCK_EXIT)) {
	Tcl_DStringInit(&script);
	interp = Ns_TclAllocateInterp(cbPtr->server);
	if (cbPtr->chan == NULL) {
	    /*
	     * Create and register the channel on first use.  Because
	     * the Tcl channel code is not entirely thread safe, it's
	     * not possible for the scheduling thread to create and
	     * register the channel.
	     */
		
    	    cbPtr->chan = Tcl_MakeTcpClientChannel((ClientData) sock);
    	    if (cbPtr->chan == NULL) {
		Ns_Log(Error, "could not make channel for sock: %d", sock);
		why = NS_SOCK_EXIT;
		goto fail;
	    }
	    Tcl_RegisterChannel(NULL, cbPtr->chan);
    	    Tcl_SetChannelOption(NULL, cbPtr->chan, "-translation", "binary");
	}
	Tcl_RegisterChannel(interp, cbPtr->chan);
        Tcl_DStringAppend(&script, cbPtr->script, -1);
        Tcl_DStringAppendElement(&script, Tcl_GetChannelName(cbPtr->chan));
        if (why == NS_SOCK_READ) {
            w = "r";
        } else if (why == NS_SOCK_WRITE) {
            w = "w";
        } else if (why == NS_SOCK_EXCEPTION) {
            w = "e";
        } else {
            w = "x";
        }
        Tcl_DStringAppendElement(&script, w);
        result = Tcl_EvalEx(interp, script.string, script.length, 0);
	if (result != TCL_OK) {
            Ns_TclLogError(interp);
	} else {
	    objPtr = Tcl_GetObjResult(interp);
	    result = Tcl_GetBooleanFromObj(interp, objPtr, &ok);
	    if (result != TCL_OK || !ok) {
	    	why = NS_SOCK_EXIT;
	    }
	}
	Ns_TclDeAllocateInterp(interp);
	Tcl_DStringFree(&script);
    }
    if (why == NS_SOCK_EXIT) {
fail:
	if (cbPtr->chan != NULL) {
	    Tcl_UnregisterChannel(NULL, cbPtr->chan);
	} else {
	    ns_sockclose(sock);
	}
        ns_free(cbPtr);
	return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * WaitCallback --
 *
 *	This is the C wrapper Ns_QueueWait callback registered in 
 *	ns_quewait. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will run Tcl script. 
 *
 *----------------------------------------------------------------------
 */

static void
WaitCallback(Ns_Conn *conn, SOCKET sock, void *arg, int why)
{
    Tcl_Interp *interp = Ns_GetConnInterp(conn);
    Tcl_Obj *objPtr = arg;
    Tcl_DString ds;
    char *s;
    int len;

    Tcl_DStringInit(&ds);
    s = Tcl_GetStringFromObj(objPtr, &len);
    Tcl_DStringAppend(&ds, s, len);

    /*
     * NB: While the C interface allows for a single callback
     * to be registered (NS_SOCK_READ | NS_SOCK_WRITE), the
     * ns_quewait command enforces only readable or writable
     * at a time.
     */

    if (why == 0) {
	s = "timeout";
    } else if (why & NS_SOCK_READ) {
	s = "readable";
    } else if (why & NS_SOCK_WRITE) {
	s = "writable";
    } else if (why & NS_SOCK_DROP) { 
	s = "dropped";
    }
    Tcl_DStringAppendElement(&ds, s);
    if (Tcl_EvalEx(interp, ds.string, ds.length, 0) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_DStringFree(&ds);
    Tcl_DecrRefCount(objPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SockListenCallback --
 *
 *	This is the C wrapper callback that is registered from 
 *	listencallback. 
 *
 * Results:
 *	NS_TRUE or NS_FALSE on error 
 *
 * Side effects:
 *	Will run Tcl script. 
 *
 *----------------------------------------------------------------------
 */

static int
SockListenCallback(SOCKET sock, void *arg, int why)
{
    ListenCallback *lcbPtr = arg;
    Tcl_Interp  *interp;
    Tcl_DString  script;
    Tcl_Obj	*listPtr, **objv;
    int          result, objc;

    interp = Ns_TclAllocateInterp(lcbPtr->server);
    result = EnterDupedSocks(interp, sock);
    if (result == TCL_OK) {
	listPtr = Tcl_GetObjResult(interp);
	if (Tcl_ListObjGetElements(interp, listPtr, &objc, &objv) == TCL_OK && objc == 2) {
	    Tcl_DStringInit(&script);
            Tcl_DStringAppend(&script, lcbPtr->script, -1);
	    Tcl_DStringAppendElement(&script, Tcl_GetString(objv[0]));
	    Tcl_DStringAppendElement(&script, Tcl_GetString(objv[1]));
            result = Tcl_EvalEx(interp, script.string, script.length, 0);
	    Tcl_DStringFree(&script);
	}
    }
    if (result != TCL_OK) {
        Ns_TclLogError(interp);
    }
    Ns_TclDeAllocateInterp(interp);
    return NS_TRUE;
}

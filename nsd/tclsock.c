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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclsock.c,v 1.6 2001/03/14 01:09:56 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is used for a socket callback.
 */

typedef struct Callback {
    char       *server;
    Tcl_Channel chan;
    int    	when;
    char        script[1];
} Callback;

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
		  fd_set * pset, SOCKET *maxPtr);
static void AppendReadyFiles(Tcl_Interp *interp, fd_set * pset, int write,
			     char *flist, Tcl_DString *pds);
static int EnterSock(Tcl_Interp *interp, SOCKET sock);
static int EnterDup(Tcl_Interp *interp, SOCKET sock);
static int EnterDupedSocks(Tcl_Interp *interp, SOCKET sock);
static int SockSetBlocking(char *value, Tcl_Interp *interp, int argc,
			   char **argv);
static Ns_SockProc SockListenCallback;

#ifdef WIN32
#define SockError(i)	NsWin32ErrMsg(GetLastError())
#else
#define SockError(i)	Tcl_PosixError((i))
#endif


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetByCmd --
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
GetCmd(Tcl_Interp *interp, int argc, char **argv, int byaddr)
{
    Ns_DString  ds;
    int         status;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
			 argv[0], " address\"", NULL);
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    if (byaddr) {
	status = Ns_GetAddrByHost(&ds, argv[1]);
    } else {
	status = Ns_GetHostByAddr(&ds, argv[1]);
    }
    if (status == NS_TRUE) {
    	Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    }
    Ns_DStringFree(&ds);
    if (status != NS_TRUE) {
	Tcl_AppendResult(interp, "could not lookup ", argv[1], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
NsTclGetHostCmd(ClientData arg, Tcl_Interp *interp, int argc,
		       char **argv)
{
    return GetCmd(interp, argc, argv, 0);
}

int
NsTclGetAddrCmd(ClientData arg, Tcl_Interp *interp, int argc,
		       char **argv)
{
    return GetCmd(interp, argc, argv, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockSetBlockingCmd --
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
NsTclSockSetBlockingCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			 char **argv)
{
    return SockSetBlocking("1", interp, argc, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockSetNonBlockingCmd --
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
NsTclSockSetNonBlockingCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			    char **argv)
{
    return SockSetBlocking("0", interp, argc, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockNReadCmd --
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
NsTclSockNReadCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int         nread;
    Tcl_Channel	chan;
    SOCKET      sock;
    char	buf[20];

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " sockId\"", NULL);
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, argv[1], NULL);
    if (chan == NULL || Ns_TclGetOpenFd(interp, argv[1], 0,
	    (int *) &sock) != TCL_OK) {
	return TCL_ERROR;
    }
    if (ns_sockioctl(sock, FIONREAD, &nread) != 0) {
        Tcl_AppendResult(interp, "ns_sockioctl failed: ", 
			 SockError(interp), NULL);
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
 * NsTclSockListenCmd --
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
NsTclSockListenCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    SOCKET  sock;
    char   *addr;
    int     port;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " address port\"", NULL);
        return TCL_ERROR;
    }
    addr = argv[1];
    if (STREQ(addr, "*")) {
        addr = NULL;
    }
    if (Tcl_GetInt(interp, argv[2], &port) != TCL_OK) {
        return TCL_ERROR;
    }
    sock = Ns_SockListen(addr, port);
    if (sock == INVALID_SOCKET) {
        Tcl_AppendResult(interp, "could not listen on \"",
            argv[1], ":", argv[2], "\"", NULL);
        return TCL_ERROR;
    }
    return EnterSock(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockAcceptCmd --
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
NsTclSockAcceptCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    SOCKET sock;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " sockId\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, argv[1], 0, (int *) &sock) != TCL_OK) {
        return TCL_ERROR;
    }
    sock = Ns_SockAccept(sock, NULL, 0);
    if (sock == INVALID_SOCKET) {
        Tcl_AppendResult(interp, "accept failed: ",
			 SockError(interp), NULL);
        return TCL_ERROR;
    }
    return EnterDupedSocks(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockCheckCmd --
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
NsTclSockCheckCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    SOCKET sock;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " sockId\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, argv[1], 1, (int *) &sock) != TCL_OK) {
	return TCL_ERROR;
    }
    if (send(sock, NULL, 0, 0) != 0) {
        interp->result = "0";
    } else {
        interp->result = "1";
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockOpenCmd --
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
NsTclSockOpenCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    SOCKET sock;
    int port;
    int timeout;
    int first;
    int async;

    if (argc < 3 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " ?-nonblock|-timeout seconds? host port\"", NULL);
        return TCL_ERROR;
    }
    first = 1;
    async = 0;
    timeout = -1;
    if (argc == 4) {

	/*
	 * ns_sockopen -nonblock host port
	 */
	
        if (!STREQ(argv[1], "-nonblock") && !STREQ(argv[1], "-async")) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0],
			     " ?-nonblock|-timeout seconds? host port\"",
			     NULL);
	    return TCL_ERROR;
        }

        first = 2;
        async = 1;
    } else if (argc == 5) {

	/*
	 * ns_sockopen -timeout seconds host port
	 */

        if (!STREQ(argv[1], "-timeout")) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0],
			     " ?-nonblock|-timeout seconds? host port\"",
			     NULL);
	    return TCL_ERROR;
        }
        if (Tcl_GetInt(interp, argv[2], &timeout) != TCL_OK) {
            return TCL_ERROR;
        }
        first = 3;
    }
    if (Tcl_GetInt(interp, argv[first + 1], &port) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Perform the connection.
     */
    
    if (async) {
        sock = Ns_SockAsyncConnect(argv[first], port);
    } else if (timeout < 0) {
        sock = Ns_SockConnect(argv[first], port);
    } else {
        sock = Ns_SockTimedConnect(argv[first], port, timeout);
    }
    if (sock == INVALID_SOCKET) {
        Tcl_AppendResult(interp, "could not connect to \"",
            argv[first], ":", argv[first + 1], "\"", NULL);
        return TCL_ERROR;
    }

    return EnterDupedSocks(interp, sock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSelectCmd --
 *
 *	Imlements ns_sockselect: basically a tcl version of
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
NsTclSelectCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    fd_set          rset, wset, eset, *rPtr, *wPtr, *ePtr;
    SOCKET	    maxfd;
    int             i, status, arg;
    Tcl_Channel	    chan;
    struct timeval  tv, *tvPtr;
    Tcl_DString     dsRfd, dsNbuf;
    char	  **fargv;
    int		    fargc;

    status = TCL_ERROR;
    if (argc != 6 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " ?-timeout sec? rfds wfds efds\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 4) {
        tvPtr = NULL;
        arg = 1;
    } else {
        tvPtr = &tv;
        if (strcmp(argv[1], "-timeout") != 0) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " ?-timeout sec? rfds wfds efds\"",
			     NULL);
	    return TCL_ERROR;
        }
        tv.tv_usec = 0;
        if (Tcl_GetInt(interp, argv[2], &i) != TCL_OK) {
            return TCL_ERROR;
        }
        tv.tv_sec = i;
        arg = 3;
    }

    /*
     * Readable fd's are treated differently because they may
     * have buffered input. Before doing a select, see if they
     * have any waiting data that's been buffered by the channel.
     */
    
    if (Tcl_SplitList(interp, argv[arg++], &fargc, &fargv) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_DStringInit(&dsRfd);
    Tcl_DStringInit(&dsNbuf);
    for (i = 0; i < fargc; ++i) {
	chan = Tcl_GetChannel(interp, fargv[i], NULL);
	if (chan == NULL) {
	    goto done;
    	}
	if (Tcl_InputBuffered(chan) > 0) {
	    Tcl_DStringAppendElement(&dsNbuf, fargv[i]);
	} else {
	    Tcl_DStringAppendElement(&dsRfd, fargv[i]);
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
    if (GetSet(interp, argv[arg++], 1, &wPtr, &wset, &maxfd) != TCL_OK) {
	goto done;
    }
    if (GetSet(interp, argv[arg++], 0, &ePtr, &eset, &maxfd) != TCL_OK) {
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
	} while (i < 0 && ns_sockerrno == EINTR);

    	if (i == -1) {
	    Tcl_AppendResult(interp, "select failed: ",
			     SockError(interp), NULL);
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
	    AppendReadyFiles(interp, wPtr, 1, argv[arg++], NULL);
	    AppendReadyFiles(interp, ePtr, 0, argv[arg++], NULL);
	    status = TCL_OK;
	}
    }
    
done:
    Tcl_DStringFree(&dsRfd);
    Tcl_DStringFree(&dsNbuf);
    ckfree((char *) fargv);
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSocketPairCmd --
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
NsTclSocketPairCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    SOCKET  socks[2];
    char     buf[20];
    int      result;

    if (ns_sockpair(socks) != 0) {
        Tcl_AppendResult(interp, "ns_sockpair failed:  ", 
			 SockError(interp), NULL);
        return TCL_ERROR;
    }
    if (EnterSock(interp, socks[1]) != TCL_OK) {
	ns_sockclose(socks[0]);
	return TCL_ERROR;
    }
    strcpy(buf, interp->result);
    result = EnterSock(interp, socks[0]);
    if (result == TCL_OK) {
    	Tcl_AppendElement(interp, buf);
    }
    return result;
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

void
NsTclSockArgProc(Tcl_DString *dsPtr, void *arg)
{
    Callback *cbPtr = arg;

    Tcl_DStringAppendElement(dsPtr, cbPtr->script);
}
 
int
NsTclSockCallbackCmd(ClientData arg, Tcl_Interp *interp, int argc,
		      char **argv)
{
    SOCKET  sock;
    int     when;
    char   *s;
    Callback *cbPtr;
    NsInterp *itPtr = arg;

    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " sockId script when\"", NULL);
        return TCL_ERROR;
    }
    s = argv[3];
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
            Tcl_AppendResult(interp, "invalid when specification \"",
                argv[3], "\": should be one or more of r, w, e, or x", NULL);
            return TCL_ERROR;
        }
        ++s;
    }
    if (when == 0) {
	Tcl_AppendResult(interp, "invalid when specification \"", argv[3],
			 "\": should be one or more of r, w, e, or x", NULL);
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, argv[1],
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
    cbPtr = ns_malloc(sizeof(Callback) + strlen(argv[2]));
    cbPtr->server = itPtr->servPtr->server;
    cbPtr->chan = NULL;
    cbPtr->when = when;
    strcpy(cbPtr->script, argv[2]);
    if (Ns_SockCallback(sock, NsTclSockProc, cbPtr,
    	    	    	when | NS_SOCK_EXIT) != NS_OK) {
        interp->result = "could not register callback";
	ns_sockclose(sock);
        ns_free(cbPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockListenCallbackCmd --
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
NsTclSockListenCallbackCmd(ClientData arg, Tcl_Interp *interp, int argc,
			    char **argv)
{
    NsInterp *itPtr = arg;
    ListenCallback *lcbPtr;
    int       port;
    char     *addr, *script;

    if (argc != 4) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " address port script\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &port) != TCL_OK) {
        return TCL_ERROR;
    }
    addr = argv[1];
    if (STREQ(addr, "*")) {
        addr = NULL;
    }
    lcbPtr = ns_malloc(sizeof(ListenCallback) + strlen(argv[3]));
    lcbPtr->server = itPtr->servPtr->server;
    strcpy(lcbPtr->script, argv[3]);
    if (Ns_SockListenCallback(addr, port, SockListenCallback, lcbPtr) != NS_OK) {
        interp->result = "could not register callback";
        ns_free(lcbPtr);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockSetBlocking --
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
SockSetBlocking(char *value, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Channel chan;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " sockId\"", NULL);
        return TCL_ERROR;
    }
    chan = Tcl_GetChannel(interp, argv[1], NULL);
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
 *	interp->result. 
 *
 *----------------------------------------------------------------------
 */

static void
AppendReadyFiles(Tcl_Interp *interp, fd_set *setPtr, int write, char *flist,
		 Tcl_DString *dsPtr)
{
    int           fargc;
    char        **fargv;
    SOCKET        sock;
    Tcl_DString   ds;

    Tcl_DStringInit(&ds);
    if (dsPtr == NULL) {
	dsPtr = &ds;
    }
    Tcl_SplitList(interp, flist, &fargc, &fargv);
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
       fd_set *setPtr, SOCKET *maxPtr)
{
    SOCKET sock;
    int    fargc;
    char **fargv;
    int    status;

    if (Tcl_SplitList(interp, flist, &fargc, &fargv) != TCL_OK) {
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
        if (sock > *maxPtr) {
            *maxPtr = sock;
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
    Tcl_SetResult(interp, Tcl_GetChannelName(chan), TCL_VOLATILE);
    return TCL_OK;
}

static int
EnterDup(Tcl_Interp *interp, SOCKET sock)
{
    sock = ns_sockdup(sock);
    if (sock == INVALID_SOCKET) {
        Tcl_AppendResult(interp, "could not dup socket: ", 
			 ns_sockstrerror(ns_sockerrno), NULL);
        return TCL_ERROR;
    }
    return EnterSock(interp, sock);
}

static int
EnterDupedSocks(Tcl_Interp *interp, SOCKET sock)
{
    Tcl_DString ds;

    if (EnterDup(interp, sock) != TCL_OK) {
    	return TCL_ERROR;
    }
    Tcl_DStringInit(&ds);
    Tcl_DStringAppendElement(&ds, interp->result);
    if (EnterSock(interp, sock) != TCL_OK) {
    	return TCL_ERROR;
    }
    Tcl_DStringAppendElement(&ds, interp->result);
    Tcl_DStringResult(interp, &ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSockProc --
 *
 *	This is the C wrapper callback that is registered from 
 *	ns_sockcallback. 
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
    char        *w;
    int          result;
    Callback    *cbPtr = arg;

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
        result = NsTclEval(interp, script.string);
	if (result != TCL_OK) {
            Ns_TclLogError(interp);
	} else if (!STREQ(interp->result, "1")) {
	    why = NS_SOCK_EXIT;
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
 * SockListenCallback --
 *
 *	This is the C wrapper callback that is registered from 
 *	ns_socklistencallback. 
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
    char       **sockv;
    int          sockc, result;

    interp = Ns_TclAllocateInterp(lcbPtr->server);
    result = EnterDupedSocks(interp, sock);
    if (result == TCL_OK) {
	Tcl_SplitList(interp, interp->result, &sockc, &sockv);
	Tcl_DStringInit(&script);
        Tcl_DStringAppend(&script, lcbPtr->script, -1);
	Tcl_DStringAppendElement(&script, sockv[0]);
	Tcl_DStringAppendElement(&script, sockv[1]);
	ckfree((char *) sockv);
        result = NsTclEval(interp, script.string);
	Tcl_DStringFree(&script);
    }
    if (result != TCL_OK) {
        Ns_TclLogError(interp);
    }
    Ns_TclDeAllocateInterp(interp);
    return NS_TRUE;
}

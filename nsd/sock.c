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
 * sock.c --
 *
 *	Wrappers and convenience functions for TCP/IP stuff. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/sock.c,v 1.7 2001/11/05 20:23:38 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

/*
 * Local functions defined in this file
 */

static int SockConnect(char *host, int port, char *lhost, int lport, int async);


/*
 *----------------------------------------------------------------------
 *
 * NsSockRecv --
 *
 *	Timed recv() from a non-blocking socket.
 *
 * Results:
 *	# bytes read 
 *
 * Side effects:
 *  	May wait for given timeout.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockRecv(int sock, void *buf, int toread, int timeout)
{
    int		nread;

    nread = recv(sock, buf, toread, 0);
    if (nread == -1
	&& errno == EWOULDBLOCK
	&& Ns_SockWait(sock, NS_SOCK_READ, timeout) == NS_OK) {
	nread = recv(sock, buf, toread, 0);
    }
    return nread;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockSend --
 *
 *	Timed send() to a non-blocking socket.
 *	NOTE: This may not write all of the data you send it!
 *
 * Results:
 *	Number of bytes written, -1 for error 
 *
 * Side effects:
 *  	May wait given timeout.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockSend(int sock, void *buf, int towrite, int timeout)
{
    int nwrote;

    nwrote = send(sock, buf, towrite, 0);
    if (nwrote == -1
    	&& errno == EWOULDBLOCK
	&& Ns_SockWait(sock, NS_SOCK_WRITE, timeout) == NS_OK) {
    	nwrote = send(sock, buf, towrite, 0);
    }
    return nwrote;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockWait --
 *
 *	Wait for I/O.
 *
 * Results:
 *	NS_OK, NS_TIMEOUT, or NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockWait(int sock, int what, int timeout)
{
    struct pollfd pfd;
    int n;

    if (timeout < 0) {
    	return NS_TIMEOUT;
    }
    timeout *= 1000;
    pfd.fd = sock;
    switch (what) {
    case NS_SOCK_READ:
	pfd.events = POLLRDNORM;
	break;
    case NS_SOCK_WRITE:
	pfd.events = POLLWRNORM;
	break;
    case NS_SOCK_EXCEPTION:
	pfd.events = POLLRDBAND;
	break;
    default:
	return NS_ERROR;
	break;
    }
    do {
	n = poll(&pfd, 1, timeout);
    } while (n < 0 && errno == EINTR);
    if (n > 0) {
	return NS_OK;
    }
    return NS_TIMEOUT;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListen --
 *
 *	Listen for connections with default backlog.
 *
 * Results:
 *	A socket or -1 on error. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockListen(char *address, int port)
{
    return Ns_SockListenEx(address, port, nsconf.backlog);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockAccept --
 *
 *	Accept a TCP socket, setting close on exec.
 *
 * Results:
 *	A socket or -1 on error. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockAccept(int lsock, struct sockaddr *saPtr, int *lenPtr)
{
    int sock;

    sock = accept(lsock, saPtr, lenPtr);
    if (sock != -1) {
	if (Ns_CloseOnExec(sock) != NS_OK) {
	    close(sock);
	    sock = -1;
	}
    }
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockBind --
 *
 *	Create a TCP socket and bind it to the passed-in address. 
 *
 * Results:
 *	A socket or -1 on error. 
 *
 * Side effects:
 *	Will set SO_REUSEADDR on the socket. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_BindSock(struct sockaddr_in *saPtr)
{
    return Ns_SockBind(saPtr);
}

int
Ns_SockBind(struct sockaddr_in *saPtr)
{
    int sock;
    int n;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock != -1 && Ns_CloseOnExec(sock) != NS_OK) {
        close(sock);
	sock = -1;
    }
    if (sock != -1) {
        n = 1;
        if (saPtr->sin_port != 0) {
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &n, sizeof(n));
        }
        if (bind(sock, (struct sockaddr *) saPtr,
		 sizeof(struct sockaddr_in)) != 0) {
            close(sock);
            sock = -1;
        }
    }
    
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockConnect --
 *
 *	Open a TCP connection to a host/port. 
 *
 * Results:
 *	A socket, or -1 on error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockConnect(char *host, int port)
{
    return SockConnect(host, port, NULL, 0, 0);
}

int
Ns_SockConnect2(char *host, int port, char *lhost, int lport)
{
    return SockConnect(host, port, lhost, lport, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockAsyncConnect --
 *
 *	Like Ns_SockConnect, but uses a nonblocking socket. 
 *
 * Results:
 *	A socket, or -1 on error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockAsyncConnect(char *host, int port)
{
    return SockConnect(host, port, NULL, 0, 1);
}

int
Ns_SockAsyncConnect2(char *host, int port, char *lhost, int lport)
{
    return SockConnect(host, port, lhost, lport, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockTimedConnect --
 *
 *	Like Ns_SockConnect, but with an optional timeout in seconds. 
 *
 * Results:
 *	A socket, or -1 on error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockTimedConnect(char *host, int port, int timeout)
{
    return Ns_SockTimedConnect2(host, port, NULL, 0, timeout);
}

int
Ns_SockTimedConnect2(char *host, int port, char *lhost, int lport, int timeout)
{
    int         sock;
    int		   len, err;

    /*
     * Connect to the host asynchronously and wait for
     * it to connect.
     */
    
    sock = SockConnect(host, port, lhost, lport, 1);
    if (sock != -1) {
	len = sizeof(err);
    	if (Ns_SockWait(sock, NS_SOCK_WRITE, timeout) == NS_OK
		&& getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *) &err, &len) == 0
		&& err == 0) {
	    return sock;
	}
	close(sock);
	sock = -1;
    }
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockSetNonBlocking --
 *
 *	Set a socket nonblocking. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockSetNonBlocking(int sock)
{
    unsigned long   i;

    i = 1;
    if (ioctl(sock, FIONBIO, &i) == -1) {
        return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockSetBlocking --
 *
 *	Set a socket blocking. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockSetBlocking(int sock)
{
    unsigned long   i;

    i = 0;
    if (ioctl(sock, FIONBIO, &i) == -1) {
        return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetSockAddr --
 *
 *	Take a host/port and fill in a sockaddr_in structure 
 *	appropriately. Host may be an IP address or a DNS name. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	May perform DNS query. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetSockAddr(struct sockaddr_in *saPtr, char *host, int port)
{
    struct in_addr  ia;
    Ns_DString ds;

    if (host == NULL) {
        ia.s_addr = htonl(INADDR_ANY);
    } else {
        ia.s_addr = inet_addr(host);
        if (ia.s_addr == INADDR_NONE) {
    	    Ns_DStringInit(&ds);
	    if (Ns_GetAddrByHost(&ds, host) == NS_TRUE) {
		ia.s_addr = inet_addr(ds.string);
	    }
    	    Ns_DStringFree(&ds);
	    if (ia.s_addr == INADDR_NONE) {
		return NS_ERROR;
	    }
	}
    }
    memset(saPtr, 0, sizeof(struct sockaddr_in));
    saPtr->sin_family = AF_INET;
    saPtr->sin_addr = ia;
    saPtr->sin_port = htons((unsigned short) port);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockPipe --
 *
 *	Create a pair of unix-domain sockets. 
 *
 * Results:
 *	See socketpair(2) 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockPipe(int socks[2])
{
    if (pipe(socks) != 0) {
        return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockConnect --
 *
 *	Open a TCP connection to a host/port. 
 *
 * Results:
 *	A socket or -1 on error. 
 *
 * Side effects:
 *	If async is true, the returned socket will be nonblocking. 
 *
 *----------------------------------------------------------------------
 */

static int
SockConnect(char *host, int port, char *lhost, int lport, int async)
{
    int             sock;
    struct sockaddr_in lsa;
    struct sockaddr_in sa;
    int                err;

    if (Ns_GetSockAddr(&sa, host, port) != NS_OK ||
	Ns_GetSockAddr(&lsa, lhost, lport) != NS_OK) {
        return -1;
    }
    sock = Ns_SockBind(&lsa);
    if (sock != -1) {
        if (async) {
            Ns_SockSetNonBlocking(sock);
        }
        if (connect(sock, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
            err = errno;
            if (!async || (err != EINPROGRESS && err != EWOULDBLOCK)) {
                close(sock);
                sock = -1;
            }
        }
        if (async && sock != -1) {
            Ns_SockSetBlocking(sock);
        }
    }
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockCloseLater --
 *
 *	Register a callback to close a socket when writable.  This
 *	is necessary for timed-out async connecting sockets on NT.
 *
 * Results:
 *	NS_OK or NS_ERROR from Ns_SockCallback.
 *
 * Side effects:
 *	Socket will be closed sometime in the future.
 *
 *----------------------------------------------------------------------
 */

static int
CloseLater(int sock, void *arg, int why)
{
    close(sock);
    return NS_FALSE;
}

int
Ns_SockCloseLater(int sock)
{
    return Ns_SockCallback(sock, CloseLater, NULL, NS_SOCK_WRITE);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockErrno --
 *
 *	Errno/GetLastError utility routines. 
 *
 * Results:
 *	See code.
 *
 * Side effects:
 *	May set last error.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ClearSockErrno(void)
{
    errno = 0;
}

int
Ns_GetSockErrno(void)
{
    return errno;
}

void
Ns_SetSockErrno(int err)
{
    errno = err;
}

char           *
Ns_SockStrError(int err)
{
    return strerror(err);
}

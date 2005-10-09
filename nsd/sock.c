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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/sock.c,v 1.15 2005/10/09 16:16:54 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#ifndef INADDR_NONE
#define INADDR_NONE -1
#endif

/*
 * Local functions defined in this file
 */

static SOCKET SockConnect(char *host, int port, char *lhost, int lport, int async);
static SOCKET SockSetup(SOCKET sock);


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
Ns_SockRecv(SOCKET sock, void *buf, int toread, int timeout)
{
    int		nread;

    nread = recv(sock, buf, toread, 0);
    if (nread == -1
	&& ns_sockerrno == EWOULDBLOCK
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
Ns_SockSend(SOCKET sock, void *buf, int towrite, int timeout)
{
    int nwrote;

    nwrote = send(sock, buf, towrite, 0);
    if (nwrote == -1
    	&& ns_sockerrno == EWOULDBLOCK
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
Ns_SockWait(SOCKET sock, int what, int seconds)
{
    return Ns_SockWaitEx(sock, what, seconds * 1000);
}

int
Ns_SockWaitEx(SOCKET sock, int what, int ms)
{
    Ns_Time timeout;
    struct pollfd pfd;
    int n;

    if (ms < 0) {
	n = 0;
    } else {
    	Ns_GetTime(&timeout);
    	Ns_IncrTime(&timeout, 0, ms * 1000);
    	pfd.fd = sock;
    	switch (what) {
    	case NS_SOCK_READ:
	    pfd.events = POLLIN;
	    break;
    	case NS_SOCK_WRITE:
	    pfd.events = POLLOUT;
	    break;
    	case NS_SOCK_EXCEPTION:
	    pfd.events = POLLPRI;
	    break;
    	default:
	    return NS_ERROR;
	    break;
    	}
    	n = NsPoll(&pfd, 1, &timeout);
    }
    return (n ? NS_OK : NS_TIMEOUT);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListen, Ns_SockListenEx --
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

SOCKET
Ns_SockListen(char *address, int port)
{
    return Ns_SockListenEx(address, port, nsconf.backlog);
}

SOCKET
Ns_SockListenEx(char *address, int port, int backlog)
{
    SOCKET sock;
    struct sockaddr_in sa;

    if (Ns_GetSockAddr(&sa, address, port) != NS_OK) {
	return -1;
    }
#ifdef WIN32
    sock = Ns_SockBind(&sa);
#else
    sock = NsSockGetBound(&sa);
    if (sock == INVALID_SOCKET) {
	sock = Ns_SockBind(&sa);
    }
#endif
    if (sock != INVALID_SOCKET && listen(sock, backlog) != 0) {
	ns_sockclose(sock);
	sock = INVALID_SOCKET;
    }
    return sock;
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

SOCKET
Ns_SockAccept(SOCKET lsock, struct sockaddr *saPtr, int *lenPtr)
{
    SOCKET sock;

    sock = accept(lsock, saPtr, lenPtr);
    if (sock != INVALID_SOCKET) {
	sock = SockSetup(sock);
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

SOCKET
Ns_BindSock(struct sockaddr_in *saPtr)
{
    return Ns_SockBind(saPtr);
}

SOCKET
Ns_SockBind(struct sockaddr_in *saPtr)
{
    SOCKET sock;
    int n;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock != INVALID_SOCKET) {
	sock = SockSetup(sock);
    }
    if (sock != INVALID_SOCKET) {
        n = 1;
        if (saPtr->sin_port != 0) {
            setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &n, sizeof(n));
        }
        if (bind(sock, (struct sockaddr *) saPtr,
		 sizeof(struct sockaddr_in)) != 0) {
            ns_sockclose(sock);
            sock = INVALID_SOCKET;
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

SOCKET
Ns_SockConnect(char *host, int port)
{
    return SockConnect(host, port, NULL, 0, 0);
}

SOCKET
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

SOCKET
Ns_SockAsyncConnect(char *host, int port)
{
    return SockConnect(host, port, NULL, 0, 1);
}

SOCKET
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

SOCKET
Ns_SockTimedConnect(char *host, int port, int timeout)
{
    return Ns_SockTimedConnect2(host, port, NULL, 0, timeout);
}

SOCKET
Ns_SockTimedConnect2(char *host, int port, char *lhost, int lport, int timeout)
{
    SOCKET         sock;
    int		   len, err;

    /*
     * Connect to the host asynchronously and wait for
     * it to connect.
     */
    
    sock = SockConnect(host, port, lhost, lport, 1);
    if (sock != INVALID_SOCKET) {
	len = sizeof(err);
    	if (Ns_SockWait(sock, NS_SOCK_WRITE, timeout) == NS_OK
		&& getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *) &err, &len) == 0
		&& err == 0) {
	    return sock;
	}
	ns_sockclose(sock);
	sock = INVALID_SOCKET;
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
Ns_SockSetNonBlocking(SOCKET sock)
{
    unsigned long   i;

    i = 1;
    if (ns_sockioctl(sock, FIONBIO, &i) == -1) {
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
Ns_SockSetBlocking(SOCKET sock)
{
    unsigned long   i;

    i = 0;
    if (ns_sockioctl(sock, FIONBIO, &i) == -1) {
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
Ns_SockPipe(SOCKET socks[2])
{
    if (ns_sockpair(socks) != 0) {
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

static SOCKET
SockConnect(char *host, int port, char *lhost, int lport, int async)
{
    SOCKET             sock;
    struct sockaddr_in lsa;
    struct sockaddr_in sa;
    int                err;

    if (Ns_GetSockAddr(&sa, host, port) != NS_OK ||
	Ns_GetSockAddr(&lsa, lhost, lport) != NS_OK) {
        return INVALID_SOCKET;
    }
    sock = Ns_SockBind(&lsa);
    if (sock != INVALID_SOCKET) {
        if (async) {
            Ns_SockSetNonBlocking(sock);
        }
        if (connect(sock, (struct sockaddr *) &sa, sizeof(sa)) != 0) {
            err = ns_sockerrno;
            if (!async || (err != EINPROGRESS && err != EWOULDBLOCK)) {
                ns_sockclose(sock);
                sock = INVALID_SOCKET;
            }
        }
        if (async && sock != INVALID_SOCKET) {
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
CloseLater(SOCKET sock, void *arg, int why)
{
    ns_sockclose(sock);
    return NS_FALSE;
}

int
Ns_SockCloseLater(SOCKET sock)
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
#ifdef _WIN32
    SetLastError(0);
#else
    errno = 0;
#endif
}

int
Ns_GetSockErrno(void)
{
#ifdef _WIN32
    return (int) WSAGetLastError();
#else
    return errno;
#endif
}

void
Ns_SetSockErrno(int err)
{
#ifdef _WIN32
    SetLastError((DWORD) err);
#else
    errno = err;
#endif
}

char           *
Ns_SockStrError(int err)
{
#ifdef _WIN32
    return NsWin32ErrMsg(err);
#else
    return strerror(err);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * SockSetup --
 *
 *	Setup new sockets for close-on-exec and possibly duped high.
 *
 * Results:
 *	Current or duped socket.
 *
 * Side effects:
 *	Original socket is closed if duped.
 *
 *----------------------------------------------------------------------
 */

static SOCKET
SockSetup(SOCKET sock)
{
#ifdef USE_DUPHIGH
    int nsock;

    nsock = fcntl(sock, F_DUPFD, 256);
    if (nsock != -1) {
	close(sock);
	sock = nsock;
    }
#endif
#ifndef _WIN32
    (void) fcntl(sock, F_SETFD, 1);
#endif
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_poll --
 *
 *	Poll file descriptors.  Emulation using select(2) is provided
 *	for platforms without poll(2).
 *
 * Results:
 *	See poll(2) man page.
 *
 * Side effects:
 *	See poll(2) man page.
 *
 *----------------------------------------------------------------------
 */

/* Copyright (C) 1994, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* Poll the file descriptors described by the NFDS structures starting at
   FDS.  If TIMEOUT is nonzero and not -1, allow TIMEOUT milliseconds for
   an event to occur; if TIMEOUT is -1, block until an event occurs.
   Returns the number of file descriptors with events, zero if timed out,
   or -1 for errors.  */

int
ns_poll(fds, nfds, timeout)
     struct pollfd *fds;
     unsigned long int nfds;
     int timeout;
{
#ifdef HAVE_POLL
 return poll(fds, nfds, timeout);
#else
 struct timeval tv, *tvp;
 fd_set rset, wset, xset;
 struct pollfd *f;
 int ready;
 int maxfd = 0;
 
 FD_ZERO (&rset);
 FD_ZERO (&wset);
 FD_ZERO (&xset);
 
 for (f = fds; f < &fds[nfds]; ++f)
   if (f->fd != -1)
   {
    if (f->events & POLLIN)
      FD_SET (f->fd, &rset);
    if (f->events & POLLOUT)
      FD_SET (f->fd, &wset);
    if (f->events & POLLPRI)
      FD_SET (f->fd, &xset);
    if (f->fd > maxfd && (f->events & (POLLIN|POLLOUT|POLLPRI)))
      maxfd = f->fd;
   }
 
 if (timeout < 0) {
    tvp = NULL;
 } else {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    tvp = &tv;
 }
 
 ready = select (maxfd + 1, &rset, &wset, &xset, tvp);
 if (ready > 0)
   for (f = fds; f < &fds[nfds]; ++f)
   {
    f->revents = 0;
    if (f->fd >= 0)
    {
     if (FD_ISSET (f->fd, &rset))
       f->revents |= POLLIN;
     if (FD_ISSET (f->fd, &wset))
       f->revents |= POLLOUT;
     if (FD_ISSET (f->fd, &xset))
       f->revents |= POLLPRI;
    }
   }
 
 return ready;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * NsPoll --
 *
 *	Poll file descriptors using an absolute timeout and restarting
 *	after any interrupts which may be received.
 *
 * Results:
 *	See poll(2) man page.
 *
 * Side effects:
 *	See poll(2) man page.
 *
 *----------------------------------------------------------------------
 */

int
NsPoll(struct pollfd *pfds, int nfds, Ns_Time *timeoutPtr)
{
    Ns_Time now, diff;
    int i, n, ms;

    /*
     * Clear revents.
     */

    for (i = 0; i < nfds; ++i) {
	pfds[i].revents = 0;
    }

    /*
     * Determine relative time from absolute time and continue polling
     * if any interrupts are received.
     */

    do {
	if (timeoutPtr == NULL) {
            ms = -1;
	} else {
	    Ns_GetTime(&now);
            if (Ns_DiffTime(timeoutPtr, &now, &diff) <= 0)  {
                ms = 0;
            } else {
            	ms = diff.sec * 1000 + diff.usec / 1000;
            }
	}
	n = ns_poll(pfds, (size_t) nfds, ms);
    } while (n < 0 && ns_sockerrno == EINTR);

    /*
     * Poll errors are not tolerated in AOLserver as they must indicate
     * a code error which if ignored could lead to data lose and/or
     * endless polling loops and error messages.
     */

    if (n < 0) {
	Ns_Fatal("poll() failed: %s", ns_sockstrerror(ns_sockerrno));
    }

    return n;
}

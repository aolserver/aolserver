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
 * nssock.c --
 *
 *	Call internal Ns_DriverInit.
 *
 */

#include "ns.h"

static Ns_DriverProc SockProc;
static int SockRecv(SOCKET sock, struct iovec *bufs, int nbufs);
static int SockSend(SOCKET sock, struct iovec *bufs, int nbufs);

int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Sock module init routine.
 *
 * Results:
 *	See Ns_DriverInit.
 *
 * Side effects:
 *	See Ns_DriverInit.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ModuleInit(char *server, char *module)
{
    Ns_DriverInitData init;
    char *path;
    int async;

    path = Ns_ConfigGetPath(server, module, NULL);
    if (!Ns_ConfigGetBool(path, "async", &async)) {
	async = 1;
    }

    /*
     * Initialize the driver with the async option so that the driver thread
     * will perform event-driven read-ahead of the request before
     * passing to the connection for processing.
     */

    init.version = NS_DRIVER_VERSION_1;
    init.name = "nssock";
    init.proc = SockProc;
    init.opts = (async ? NS_DRIVER_ASYNC : 0);
    init.arg = NULL;
    init.path = NULL;

    return Ns_DriverInit(server, module, &init);
}


/*
 *----------------------------------------------------------------------
 *
 * SockProc --
 *
 *	Socket driver callback proc.  This driver attempts efficient
 *	scatter/gatter I/O if requested and only blocks for the
 *	driver configured time once if no bytes are available.
 *
 * Results:
 *	For close and keep, always 0.  For send and recv, # of bytes
 *	processed or -1 on error or timeout.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
SockProc(Ns_DriverCmd cmd, Ns_Sock *sock, struct iovec *bufs, int nbufs)
{
    int n;

    switch (cmd) {
    case DriverRecv:
	n = SockRecv(sock->sock, bufs, nbufs);
	if (n < 0
	    && ns_sockerrno == EWOULDBLOCK
	    && Ns_SockWait(sock->sock, NS_SOCK_READ, sock->driver->recvwait) == NS_OK) {
	    n = SockRecv(sock->sock, bufs, nbufs);
	}
	break;

    case DriverSend:
	n = SockSend(sock->sock, bufs, nbufs);
	if (n < 0
	    && ns_sockerrno == EWOULDBLOCK
	    && Ns_SockWait(sock->sock, NS_SOCK_WRITE, sock->driver->sendwait) == NS_OK) {
	    n = SockSend(sock->sock, bufs, nbufs);
	}
	break;

    case DriverKeep:
    case DriverClose:
	/* NB: Nothing to do. */
	n = 0;
	break;

    default:
	/* Unsupported command. */
	n = -1;
	break;
    }
    return n;
}


static int
SockRecv(SOCKET sock, struct iovec *bufs, int nbufs)
{
#ifdef _WIN32
    int n, flags;

    flags = 0;
    if (WSARecv(sock, (LPWSABUF)bufs, nbufs, &n, &flags, NULL, NULL) != 0) {
	n = -1;
    }
    return n;
#else
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = bufs;
    msg.msg_iovlen = nbufs;
    return recvmsg(sock, &msg, 0);
#endif
}


static int
SockSend(SOCKET sock, struct iovec *bufs, int nbufs)
{
#ifdef _WIN32
    int n;

    if (WSASend(sock, (LPWSABUF)bufs, nbufs, &n, 0, NULL, NULL) != 0) {
	n = -1;
    }
    return n;
#else
    struct msghdr msg;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = bufs;
    msg.msg_iovlen = nbufs;
    return sendmsg(sock, &msg, 0);
#endif
}

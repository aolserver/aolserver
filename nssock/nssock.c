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
    /*
     * Initialize the driver with the async option so that the driver thread
     * will perform event-driven read-ahead of the request before
     * passing to the connection for processing.
     */

    return Ns_DriverInit(server, module, "nssock", SockProc, NULL, NS_DRIVER_ASYNC);
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
    struct msghdr msg;
    int n;

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = bufs;
    msg.msg_iovlen = nbufs;

    switch (cmd) {
    case DriverRecv:
    	n = recvmsg(sock->sock, &msg, 0);
	if (n < 0 && errno == EWOULDBLOCK
	    && Ns_SockWait(sock->sock, NS_SOCK_READ, sock->driver->recvwait) == NS_OK) {
	    n = recvmsg(sock->sock, &msg, 0);
	}
	break;

    case DriverSend:
    	n = sendmsg(sock->sock, &msg, 0);
	if (n < 0 && errno == EWOULDBLOCK
	    && Ns_SockWait(sock->sock, NS_SOCK_WRITE, sock->driver->sendwait) == NS_OK) {
    	    n = sendmsg(sock->sock, &msg, 0);
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

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
 * binder.c --
 *
 *	Support for the slave bind/listen process.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/binder.c,v 1.10 2000/11/06 18:10:58 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#include <sys/uio.h>

#ifdef HAVE_CMMSG

/*
 * Some platforms use BSD4.4 style message passing.  This structure is used
 * to pass the file descriptor between parent and slave.  Note that
 * the first 3 elements must match those of the struct cmsghdr.
 */

typedef struct CMsg {
	unsigned int len;
	int level;
	int type;
	int fds[1];
} CMsg;

#endif

#define REQUEST_SIZE (sizeof(struct sockaddr_in) + sizeof(int))
#define RESPONSE_SIZE (sizeof(int))

/*
 * Local functions defined in this file
 */

static int Listen(struct sockaddr_in *saPtr, int backlog);
static void Binder(void);
static void PreBind(char *line);

/*
 * Static variables in this file
 */

static int bindRequest[2];
static int bindResponse[2];
static int bindRunning;
static Tcl_HashTable preBound;
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenEx --
 *
 *	Create a new socket bound to the specified port and listening
 *	for new connections.
 *
 * Results:
 *	Socket descriptor or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_SockListenEx(char *address, int port, int backlog)
{
    SOCKET          sock;
    struct sockaddr_in sa;
    static int first = 1;
    Tcl_HashEntry *hPtr;
    char *addrstr;

    addrstr = address ? address : "0.0.0.0";

    if (first) {
	Ns_MutexSetName2(&lock, "ns", "binder");
	first = 0;
    }
    if (Ns_GetSockAddr(&sa, address, port) != NS_OK) {
	return -1;
    }

    /*
     * If there's no binder slave process or the port is not privileged
     * just call Listen directly.  Otherwise, send a socket message
     * with the desired socket address and backlog to the slave process
     * and wait for the response.
     */

    Ns_MutexLock(&lock);

    /*
     * First, check in the pre-bind table.
     */

    hPtr = Tcl_FindHashEntry(&preBound, (char *) &sa);
    if (hPtr != NULL) {
	sock = (int) Tcl_GetHashValue(hPtr);
	Tcl_DeleteHashEntry(hPtr);
	if (listen(sock, backlog) == 0) {
	    Ns_Log(Notice, "prebind: listen(%s,%d) = %d",
		   addrstr, port, sock);
	    goto done;
	}
	Ns_Log(Notice, "prebind: listen(%s,%d) failed: %s",
		   addrstr, port, strerror(errno));
	close(sock);
    }

    /*
     * Next, either bind local or through the binder process.
     */

    if (!bindRunning || port > 1024) {
	sock = Listen(&sa, backlog);
    } else {
	struct iovec    iov[2];
	struct msghdr   msg;
	int             err;
#ifdef HAVE_CMMSG
	CMsg		cm;
#endif

	iov[0].iov_base = (caddr_t) &backlog;
	iov[0].iov_len = sizeof(int);
	iov[1].iov_base = (caddr_t) &sa;
	iov[1].iov_len = sizeof(struct sockaddr_in);
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;
	if (sendmsg(bindRequest[1],
		    (struct msghdr *) &msg, 0) != REQUEST_SIZE) {
            Ns_Fatal("binder: sendmsg() failed: '%s'", strerror(errno));
	}

	iov[0].iov_base = (caddr_t) &err;
	iov[0].iov_len = sizeof(int);
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
#ifdef HAVE_CMMSG
	cm.len = sizeof(cm);
	cm.type = SCM_RIGHTS;
	cm.level = SOL_SOCKET;
	msg.msg_control = (void *) &cm;
	msg.msg_controllen = cm.len;
	msg.msg_flags = 0;
#else
	msg.msg_accrights = (caddr_t) &sock;
	msg.msg_accrightslen = sizeof(sock);
#endif
	if (recvmsg(bindResponse[0],
		    (struct msghdr *) &msg, 0) != RESPONSE_SIZE) {
            Ns_Fatal("binder: recvmsg() failed: '%s'", strerror(errno));
	}
#ifdef HAVE_CMMSG
	sock = cm.fds[0];
#endif
	/*
	 * Close-on-exec, while set in the binder process by default
	 * with Ns_SockBind, is not transmitted in the sendmsg and
	 * must be set again.
	 */

	if (sock != INVALID_SOCKET && Ns_CloseOnExec(sock) != NS_OK) {
	    close(sock);
	    sock = -1;
	}
	if (address == NULL) {
	    address = "0.0.0.0";
	}
	if (err == 0) {
	    Ns_Log(Notice, "binder: listen(%s,%d) = %d",
		   address, port, sock);
	} else {
	    Ns_SetSockErrno(err);
	    sock = -1;
	    Ns_Log(Error, "binder: listen(%s,%d) failed: '%s'",
	           address, port, ns_sockstrerror(ns_sockerrno));
	}
    }

done:
    Ns_MutexUnlock(&lock);
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitBinder --
 *
 *	Initialize the binder, pre-binding to any requested ports.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May pre-bind to one or more ports.
 *
 *----------------------------------------------------------------------
 */

void
NsInitBinder(char *args, char *file)
{
    char line[1024];
    FILE *fp;

    /*
     * Initialize the pre-bind table.
     */

    Tcl_InitHashTable(&preBound, sizeof(struct sockaddr_in)/sizeof(int));

    /*
     * Pre-bound to requested ports from the command line and/or
     * simple pre-bind file.
     */

    if (args != NULL) {
	PreBind(args);
    }
    if (file != NULL && (fp = fopen(file, "r")) != NULL) {
	while (fgets(line, sizeof(line), fp) != NULL) {
	    PreBind(line);
	}
	fclose(fp);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsForkBinder --
 *
 *	Fork of the slave bind/listen process.  This routine is called
 * 	by main() when the server starts as root.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The binderForked, bindRequest, and bindResponse static variables
 *	are updated.
 *
 *----------------------------------------------------------------------
 */

void
NsForkBinder(void)
{
    int pid, status;

    /*
     * Create two socket pipes, one for sending the request and one
     * for receiving the response.
     */

    if (ns_sockpair(bindRequest) != 0 || ns_sockpair(bindResponse) != 0) { 
	Ns_Fatal("binder: ns_sockpair() failed: '%s'", strerror(errno));
    }

    /*
     * Double-fork and run as a binder until the socket pairs are
     * closed.  The server double forks to avoid problems 
     * waiting for a child root process after the parent does a
     * setuid(), something which appears to confuse the
     * process-based Linux and SGI threads.
     */

    pid = ns_fork();
    if (pid < 0) {
	Ns_Fatal("binder: fork() failed: '%s'", strerror(errno));
    } else if (pid == 0) {
	pid = ns_fork();
	if (pid < 0) {
	    Ns_Fatal("binder: fork() failed: '%s'", strerror(errno));
	} else if (pid == 0) {
    	    close(bindRequest[1]);
    	    close(bindResponse[0]);
	    Binder();
	}
	exit(0);
    }
    if (Ns_WaitForProcess(pid, &status) != NS_OK) {
	Ns_Fatal("binder: Ns_WaitForProcess(%d) failed: '%s'",
		 pid, strerror(errno));
    } else if (status != 0) {
	Ns_Fatal("binder: process %d exited with non-zero status: %d",
		 pid, status);
    }
    Ns_MutexLock(&lock);
    bindRunning = 1;
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopBinder --
 *
 *	Close the socket to the binder after startup.  This is done
 *	to avoid a possible security risk of binding to privileged
 *	ports after startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Binder process will exit.
 *
 *----------------------------------------------------------------------
 */

void
NsStopBinder(void)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    char *addr;
    int sock, port;
    struct sockaddr_in *saPtr;

    Ns_MutexLock(&lock);

    /*
     * First, close any pre-bound sockets.
     */

    hPtr = Tcl_FirstHashEntry(&preBound, &search);
    while (hPtr != NULL) {
	sock = (int) Tcl_GetHashValue(hPtr);
	saPtr = (struct sockaddr_in *) Tcl_GetHashKey(&preBound, hPtr);
	addr = ns_inet_ntoa(saPtr->sin_addr);
	port = htons(saPtr->sin_port);
	Ns_Log(Warning, "prebind: closed unused: %s:%d = %d", addr, port, sock);
	close(sock);
	hPtr = Tcl_NextHashEntry(&search);
    }

    if (bindRunning) {
#ifdef __sgi
	Ns_Log(Warning, "binder: irix bug: binder left running");
#else
	close(bindRequest[1]);
	close(bindResponse[0]);
	close(bindRequest[0]);
	close(bindResponse[1]);
#endif
	bindRunning = 0;
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Listen --
 *
 *	Helper routine for creating a listening socket.
 *
 * Results:
 *	Socket descriptor or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Listen(struct sockaddr_in *saPtr, int backlog)
{
   int sock, err;

   sock = Ns_SockBind(saPtr);
   if (sock != -1 && listen(sock, backlog) != 0) {
	err = errno;
	close(sock);
	Ns_SetSockErrno(err);
	sock = -1;
   }
   return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * Binder --
 *
 *	Slave process bind/listen loop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sockets are created and sent to the parent on request.
 *
 *----------------------------------------------------------------------
 */

static void
Binder(void)
{
    struct sockaddr_in sa;
    struct iovec    iov[3];
    int             backlog;
    int             n, err, fd;
    struct msghdr   msg;
#ifdef HAVE_CMMSG
    CMsg	    cm;
#endif

    /*
     * Endlessly listen for socket bind requests.
     */

    for (;;) {
        iov[0].iov_base = (caddr_t) &backlog;
        iov[0].iov_len = sizeof(int);
        iov[1].iov_base = (caddr_t) &sa;
        iov[1].iov_len = sizeof(struct sockaddr_in);
	memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        do {
            n = recvmsg(bindRequest[0], (struct msghdr *) &msg, 0);
        } while (n == -1 && errno == EINTR);
        if (n == 0) {
	    break;
	}
        if (n != REQUEST_SIZE) {
            Ns_Fatal("binder: recvmsg() failed: '%s'", strerror(errno));
        }

	/*
	 * NB: Due to a bug in Solaris the slave process must
	 * call both bind() and listen() before returning the
	 * socket.  All other Unix versions would actually allow
	 * just performing the bind() in the slave and allowing
	 * the parent to perform the listen().
	 */

	fd = Listen(&sa, backlog);
	if (fd < 0) {
	    err = errno;
	} else {
	    err = 0;
	}
        iov[0].iov_base = (caddr_t) &err;
        iov[0].iov_len = sizeof(err);
	memset(&msg, 0, sizeof(msg));
        msg.msg_iov = iov;
        msg.msg_iovlen = 1;
        if (fd != -1) {
#ifdef HAVE_CMMSG
	    cm.len = sizeof(cm);
	    cm.level = SOL_SOCKET;
	    cm.type = SCM_RIGHTS;
	    cm.fds[0] = fd;
	    msg.msg_control = (void *) &cm;
	    msg.msg_controllen = cm.len;
	    msg.msg_flags = 0;
#else
            msg.msg_accrights = (caddr_t) &fd;
            msg.msg_accrightslen = sizeof(fd);
#endif
        }
        do {
            n = sendmsg(bindResponse[1], (struct msghdr *) &msg, 0);
        } while (n == -1 && errno == EINTR);
        if (n != RESPONSE_SIZE) {
            Ns_Fatal("binder: sendmsg() failed: '%s'", strerror(errno));
        }
        if (fd != -1) {
	
	    /*
	     * Close the socket as it won't be needed in the slave.
	     */

            close(fd);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PreBind --
 *
 *	Pre-bind to one or more ports in a comma-separated list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sockets are left in bound state for later listen 
 *	in Ns_SockListen.  
 *
 *----------------------------------------------------------------------
 */

static void
PreBind(char *line)
{
    struct sockaddr_in sa;
    Tcl_HashEntry *hPtr;
    int new, sock, port;
    char *err, *ent, *p, *q, *addr, *baddr;

    ent = line;
    do {
	p = strchr(ent, ',');
	if (p != NULL) {
	    *p = '\0';
	}
	baddr = NULL;
    	addr = "0.0.0.0";
    	q = strchr(ent, ':');
	if (q == NULL) {
	    port = atoi(ent);
	} else {
	    *q = '\0';
	    port = atoi(q+1);
	    baddr = addr = ent;
	}
	if (port == 0) {
	    err = "invalid port";
	} else if (Ns_GetSockAddr(&sa, baddr, port) != NS_OK) {
	    err = "invalid address";
	} else {
	    hPtr = Tcl_CreateHashEntry(&preBound, (char *) &sa, &new);
	    if (!new) {
	    	err = "duplicate entry";
	    } else if ((sock = Ns_SockBind(&sa)) == INVALID_SOCKET) {
	    	err = strerror(errno);
	    	Tcl_DeleteHashEntry(hPtr);
	    } else {
	    	Tcl_SetHashValue(hPtr, sock);
	    	err = NULL;
	    }
	}
	if (q != NULL) {
	    *q = ':';
	}
	if (p != NULL) {
	    *p++ = ',';
	}
	if (err != NULL) {
	    Ns_Log(Error, "prebind: invalid entry '%s': %s", ent, err);
	} else {
	    Ns_Log(Notice, "prebind: successful pre-bind to '%s'", ent);
	}
	ent = p;
    } while (ent != NULL);
}

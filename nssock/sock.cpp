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
 * SendFd support added by Robert Ennals (roberte@sco.com)
 * Currently this only supports the UnixWare sendv API. Feel free
 * to add support for your favourite OS.
 */

/* 
 * sock.cpp --
 *
 *	Routines for the nssock and nsssl socket drivers.  This
 *	code is more complicated than may seem necessary for a few
 *	reasons:
 *
 *	1.  The need for a graceful-close, i.e., a shutdown()
 *	    followed by draining any unread data before the close().
 *	    This is required to avoid confusing certain clients
 *	    with a FIN arriving before an ACK of unread data, e.g.,
 *	    the trailing \r\n not accounted for in the content-length
 *	    sent on an HTTP POST for the benefit of stupid CGI's.
 *
 *	2.  The careful delay of the listen() until just before
 *	    server startup is complete to avoid connections building
 *	    up during initialization.
 *
 *	3.  The immediate close() of the listen sockets at the start
 *	    of server shutdown to avoid accepting new connections
 *	    during the grace period while active connections exit.
 *
 *	4.  The hold of a connection which was accepted but could
 *	    not be queued.  This avoids a situation where the fast
 *	    moving SockThread could overwhelm the connection 
 *	    handling queue.
 *
 *	While complicated, experience at AOL has shown all these
 *	tweeks are useful to carefully feed new connections to
 *	the server core and close client connections.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nssock/Attic/sock.cpp,v 1.6 2000/11/06 18:08:01 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

#ifdef SSL
/*
 * nsssl
 */
#include "ssl.h"
#include "ssltcl.h"
#include "x509.h"

#define DEFAULT_PORT            443
#define DEFAULT_PROTOCOL        "https"
#define DEFAULT_NAME            "nsssl"
#define DRIVER_NAME             "nsssl"

#else
/*
 * nssock
 */
#define DEFAULT_PORT		80
#define DEFAULT_PROTOCOL 	"http"
#define DEFAULT_NAME		"nssock"
#define DRIVER_NAME             "nssock"

#endif

struct Conn;

/*
 * The following structure maintains the context
 * for each instance of the loaded socket(ssl) driver.
 */

typedef struct Driver {
    struct Driver *nextPtr;	    /* Next in list of drivers. */
    Ns_Driver	 driver;	    /* Ns_RegisterDriver handle. */
    char        *name;		    /* Config name, e.g., "sock1". */
    char        *location;	    /* Location, e.g, "http://foo:9090" */
    char        *address;	    /* Address in location. */
    int          port;		    /* Port in location. */
    char        *bindaddr;	    /* Numerical listen address. */
    int		 backlog;	    /* listen() backlog. */
    SOCKET	 sock;		    /* Listening socket. */
    struct Conn *firstFreeConnPtr;  /* First free conn, per-driver for
				     * per-driver bufsizes. */
    int     	 timeout;	    /* send()/recv() I/O timeout. */
    int     	 closewait;	    /* Graceful close timeout. */
    int		 bufsize;	    /* Bufsize (0 for SSL) */
#ifdef SSL
    void        *dssl;		    /* SSL per-driver context. */
#endif
} Driver;

typedef struct Conn {
    struct Conn *nextPtr;	    /* Next in closing, free lists. */
    struct Driver *drvPtr;	    /* Pointer to driver. */
    SOCKET	sock;		    /* Client socket. */
    char	peer[16];	    /* Client peer address. */
    int		port;		    /* Client port. */
    int		nrecv;		    /* Num bytes received. */
    int		nsend;		    /* Num bytes sent. */
    time_t	timeout;	    /* Graceful close absolute timeout. */
#ifdef SSL
    void       *cssl;		    /* SSL per-client context. */
#else
    int		cnt;		    /* Num bytes in read-ahead buffer. */
    char       *base;		    /* Base pointer in read-ahead buffer. */
    char	buf[1];		    /* Read-ahead buffer placeholder, Conn
				     * structure is actually allocated to 
				     * per-driver configured bufsize. */
#endif

} Conn;

/*
 * Driver callbacks defined below.
 */

static Ns_DriverStartProc SockStart;
static Ns_DriverStopProc SockStop;
static Ns_ConnReadProc SockRead;
static Ns_ConnWriteProc SockWrite;
static Ns_ConnCloseProc SockClose;
static Ns_ConnConnectionFdProc SockConnectionFd;
static Ns_ConnDetachProc SockDetach;
static Ns_ConnPeerProc SockPeer;
static Ns_ConnLocationProc SockLocation;
static Ns_ConnPeerPortProc SockPeerPort;
static Ns_ConnPortProc SockPort;
static Ns_ConnHostProc SockHost;
static Ns_ConnDriverNameProc SockName;
static Ns_ConnInitProc SockInit; 
#ifdef HAVE_SENDV
static Ns_ConnSendFdProc SockSendFd;
#endif

/*
 * Driver structure passed to Ns_RegisterDriver.
 */

static Ns_DrvProc sockProcs[] = {
    {Ns_DrvIdStart,        (void *) SockStart},
    {Ns_DrvIdStop,         (void *) SockStop},
    {Ns_DrvIdRead,         (void *) SockRead},
    {Ns_DrvIdWrite,        (void *) SockWrite},
    {Ns_DrvIdClose,        (void *) SockClose},
    {Ns_DrvIdHost,         (void *) SockHost},
    {Ns_DrvIdPort,         (void *) SockPort},
    {Ns_DrvIdName,         (void *) SockName},
    {Ns_DrvIdPeer,         (void *) SockPeer},
    {Ns_DrvIdPeerPort,     (void *) SockPeerPort},
    {Ns_DrvIdLocation,     (void *) SockLocation},
    {Ns_DrvIdConnectionFd, (void *) SockConnectionFd},
    {Ns_DrvIdDetach,       (void *) SockDetach},
    {Ns_DrvIdInit,         (void *) SockInit}, 
#ifdef HAVE_SENDV
    {Ns_DrvIdSendFd,       (void *) SockSendFd},
#endif
    {0,                    NULL}
};

/*
 * Static functions and variables defined in this file.
 */

static Ns_Callback SockShutdown;
static Ns_Callback SockReady;
static Ns_ThreadProc SockThread;
static void SockRelease(Conn *connPtr);
static void SockTrigger(void);

static Driver *firstDrvPtr;  /* First in list of all drivers. */
static Conn *firstClosePtr; /* First conn ready for graceful close. */
static Ns_Thread sockThread;/* Running SockThread. */
static SOCKET trigPipe[2];  /* Trigger to wakeup SockThread select(). */
static int shutdownPending; /* Flag to indicate shutdown. */
static Ns_Mutex lock;	    /* Lock around close list and shutdown flag. */

NS_EXPORT int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Sock module init routine.  Each instance is added to the
 *	driver list after resolving the locaation, address, and port.
 *
 * Results:
 *	NS_OK if initialized ok, NS_ERROR otherwise.
 *
 * Side effects:
 *	Listen socket will be opened later in SockStart called by
 *	driver core.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT int
Ns_ModuleInit(char *server, char *name)
{
    char *path,*address, *host, *bindaddr;
    int n;
    Ns_DString ds;
    struct in_addr  ia;
    struct hostent *he;
    Driver *drvPtr;
#ifdef SSL 
    char *cert, *key; 
#endif 
    
    path = Ns_ConfigGetPath(server, name, NULL);

#ifdef SSL 

    /*
     * Initialize the global and per-driver SSL.
     */

    if (NsSSLInitialize(server, name) != NS_OK) { 
        Ns_Log(Error, "%s: failed to initialize ssl driver", DRIVER_NAME);
        return NS_ERROR; 
    } 
    cert = Ns_ConfigGet(path, "certfile"); 
    if (cert == NULL) { 
        Ns_Log(Warning, "%s: certfile not specified", DRIVER_NAME); 
        return NS_OK; 
    } 
#ifdef SSL_EXPORT 
    n = 40;
#else
    n = 128;
#endif
    Ns_Log(Notice, "%s: initialized with %d-bit encryption", name, n);
#endif 
    
    /*
     * Determine the hostname used for the local address to bind
     * to and/or the HTTP location string.
     */

    host = Ns_ConfigGet(path, "hostname");
    bindaddr = address = Ns_ConfigGet(path, "address");

    /*
     * If the listen address was not specified, attempt to determine it
     * through a DNS lookup of the specified hostname or the server's
     * primary hostname.
     */

    if (address == NULL) {
        he = gethostbyname(host ? host : Ns_InfoHostname());

        /*
	 * If the lookup suceeded but the resulting hostname does not
	 * appear to be fully qualified, attempt a reverse lookup on the
	 * address which often return the fully qualified name.
	 *
	 * NB: This is a common, but sloppy configuration for a Unix
	 * network.
	 */

        if (he != NULL && he->h_name != NULL &&
	    strchr(he->h_name, '.') == NULL) {
            he = gethostbyaddr(he->h_addr, he->h_length, he->h_addrtype);
	}

	/*
	 * If the lookup suceeded, use the first address in host entry list.
	 */

        if (he == NULL || he->h_name == NULL) {
            Ns_Log(Error, "%s: "
		   "failed to resolve '%s': %s", DRIVER_NAME,
		   name, host ? host : Ns_InfoHostname(), strerror(errno));
	    return NS_ERROR;
	}
        if (*(he->h_addr_list) == NULL) {
            Ns_Log(Error, "%s: failed to get address: "
		   "null address list in (derived) host entry for '%s'",
		   DRIVER_NAME, he->h_name);
	    return NS_ERROR;
	}
        memcpy(&ia.s_addr, *(he->h_addr_list), sizeof(ia.s_addr));
        address = ns_inet_ntoa(ia);

	/*
	 * Finally, if no hostname was specified, set it to the hostname
	 * derived from the lookup(s) above.
	 */ 

	if (host == NULL) {
	    host = he->h_name;
	}
    }

    /*
     * If the hostname was not specified and not determined by the loookups
     * above, set it to the specified or derived IP address string.
     */

    if (host == NULL) {
	host = address;
    }

    /*
     * Allocate a new driver instance, initialize the SSL context,
     * and set configurable parameters.
     */

    drvPtr = ns_calloc(1, sizeof(Driver));
#ifdef SSL 
    key = Ns_ConfigGet(path, "keyfile"); 
    drvPtr->dssl = NsSSLCreateServer(cert, key); 
    if (drvPtr->dssl == NULL) { 
        ns_free(drvPtr); 
        return NS_ERROR; 
    } 
    drvPtr->bufsize = 0;
#else 
    if (!Ns_ConfigGetInt(path, "bufsize", &n) || n < 1) { 
        n = 16000; 
    }
    drvPtr->bufsize = n;
#endif 
    drvPtr->name = name;
    if (!Ns_ConfigGetInt(path, "socktimeout", &n) || n < 1) {
	n = 30;		/* NB: 30 seconds. */
    }
    drvPtr->timeout = n;
    if (!Ns_ConfigGetInt(path, "closewait", &n) || n < 0) {
	n = 2;		/* NB: 2 seconds */
    }
    drvPtr->closewait = n;

    /*
     * Determine the port and then set the HTTP location string either
     * as specified in the config file or constructed from the
     * hostname and port.
     */

    drvPtr->bindaddr = bindaddr;
    drvPtr->address = ns_strdup(address);
    if (!Ns_ConfigGetInt(path, "port", &drvPtr->port)) {
	drvPtr->port = DEFAULT_PORT;
    }
    if (!Ns_ConfigGetInt(path, "backlog", &drvPtr->backlog)) {
	drvPtr->backlog = 5;
    }
    drvPtr->location = Ns_ConfigGet(path, "location");
    if (drvPtr->location != NULL) {
	drvPtr->location = ns_strdup(drvPtr->location);
    } else {
    	Ns_DStringInit(&ds);
	Ns_DStringVarAppend(&ds, DEFAULT_PROTOCOL "://", host, NULL);
	if (drvPtr->port != DEFAULT_PORT) {
	    Ns_DStringPrintf(&ds, ":%d", drvPtr->port);
	}
	drvPtr->location = Ns_DStringExport(&ds);
    }

    /*
     * Register the driver and add to the drivers list.
     */

    drvPtr->driver = Ns_RegisterDriver(server, name, sockProcs, drvPtr);
    if (drvPtr->driver == NULL) {
	return NS_ERROR;
    }
    if (firstDrvPtr == NULL) {
	Ns_MutexSetName(&lock, DRIVER_NAME);
	Ns_RegisterAtShutdown(SockShutdown, NULL);
    }
    drvPtr->nextPtr = firstDrvPtr;
    firstDrvPtr = drvPtr;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockStart --
 *
 *	Listen on driver instance address/port and start SockThread
 *	if needed.
 *
 * Results:
 *	NS_OK or NS_ERROR if driver could not listen.
 *
 * Side effects:
 *	See SockThread.
 *
 *----------------------------------------------------------------------
 */

static int
SockStart(char *server, char *label, void **drvDataPtr)
{
    Driver *drvPtr = *((Driver **) drvDataPtr);

    /*
     * Create the listening socket and add to the Ports list.
     */

    drvPtr->sock = Ns_SockListenEx(drvPtr->bindaddr, drvPtr->port,
	drvPtr->backlog);
    if (drvPtr->sock == INVALID_SOCKET) {
	Ns_Log(Error, "%s: failed to listen on %s:%d: %s",
	    drvPtr->name, drvPtr->address ? drvPtr->address : "*",
	    drvPtr->port, ns_sockstrerror(ns_sockerrno));
	return NS_ERROR;
    }
    Ns_SockSetNonBlocking(drvPtr->sock);

    /*
     * Start the SockThread if necessary.
     */

    Ns_MutexLock(&lock);
    if (sockThread == NULL) {
	if (ns_sockpair(trigPipe) != 0) {
	    Ns_Fatal("%s: ns_sockpair() failed: %s",
		     DRIVER_NAME, ns_sockstrerror(ns_sockerrno));
	}
	Ns_ThreadCreate(SockThread, NULL, 0, &sockThread);
	Ns_RegisterAtReady(SockReady, NULL);
    }
    Ns_MutexUnlock(&lock);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockStop --
 *
 *	Trigger the SockThread to begin shutdown.  This callback is
 *	invoked by the driver core at the begging of server shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SockThread will close listen sockets and then exit after all
 *	outstanding connections are complete and closed.
 *
 *----------------------------------------------------------------------
 */

static void
SockStop(void *arg)
{
    Ns_MutexLock(&lock);
    if (sockThread != NULL && !shutdownPending) {
    	Ns_Log(Notice, "%s: triggering shutdown", DRIVER_NAME);
	shutdownPending = 1;
	SockTrigger();
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * SockReady --
 *
 *	Trigger the SockThread to indicate the server is not longer
 *	busy.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SockThread will wakeup and retry Ns_QueueConn if a connection
 *	is pending.
 *
 *----------------------------------------------------------------------
 */

static void
SockReady(void *arg)
{
    Ns_MutexLock(&lock);
    if (sockThread != NULL) {
    	Ns_Log(Notice, "%s: server ready - resuming", DRIVER_NAME);
	SockTrigger();
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * SockShutdown --
 *
 *	Wait for exit of SockThread.  This callback is invoke later by
 *	the timed shutdown thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SSL context will be destroyed.
 *
 *----------------------------------------------------------------------
 */

static void
SockShutdown(void *arg)
{
    Driver *drvPtr;

    if (sockThread != NULL) {
	Ns_ThreadJoin(&sockThread, NULL);
	sockThread = NULL;
	ns_sockclose(trigPipe[0]);
	ns_sockclose(trigPipe[1]);
    }
    drvPtr = firstDrvPtr;
    while (drvPtr != NULL) {
#ifdef SSL 
        NsSSLDestroyServer(drvPtr->dssl); 
#endif
	drvPtr = drvPtr->nextPtr;
    }
    Ns_Log(Notice, "%s: shutdown complete", DRIVER_NAME);
}


/*
 *----------------------------------------------------------------------
 *
 * SockThread --
 *
 *	Main listening socket driver thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connections are accepted on the configured listen sockets,
 *	placed on the run queue to be serviced, and gracefully
 *	closed when done.
 *
 *----------------------------------------------------------------------
 */

static int
SockQueue(Conn *connPtr)
{
    if (Ns_QueueConn(connPtr->drvPtr->driver, connPtr) != NS_OK) {
	return 0;
    }
    return 1;
}

static int
SockAccept(SOCKET lsock, SOCKET *sockPtr, struct sockaddr_in *saPtr)
{
    SOCKET sock;
    int slen;

    slen = sizeof(struct sockaddr_in);
    sock = Ns_SockAccept(lsock, (struct sockaddr *) saPtr, &slen);
    if (sock == INVALID_SOCKET) {
	return 0;
    }
    *sockPtr = sock;
    return 1;
}

static void
SockThread(void *ignored)
{
    fd_set set;
    char c;
    int n, nactive, stopping;
    Conn *connPtr, *closePtr, *nextPtr, *waitPtr;
    Driver *activeDrvPtr;
    Driver *drvPtr, *nextDrvPtr, *idleDrvPtr, *acceptDrvPtr;
    struct sockaddr_in sa;
    SOCKET max, sock;
    time_t now, timeout;
    struct timeval tv, *tvPtr;
    char drain[1024];
    
    Ns_ThreadSetName("-" DRIVER_NAME "-");
    Ns_Log(Notice, "%s: waiting for startup", DRIVER_NAME);
    Ns_WaitForStartup();
    Ns_Log(Notice, "%s: starting", DRIVER_NAME);

    /*
     * Build up the list of active drivers.
     */

    activeDrvPtr = NULL;
    drvPtr = firstDrvPtr;
    firstDrvPtr = NULL;
    while (drvPtr != NULL) {
	nextDrvPtr = drvPtr->nextPtr;
	if (drvPtr->sock != INVALID_SOCKET) {
	    drvPtr->nextPtr = activeDrvPtr;
	    activeDrvPtr = drvPtr;
	} else {
	    drvPtr->nextPtr = firstDrvPtr;
	    firstDrvPtr = drvPtr;
	}
	drvPtr = nextDrvPtr;
    }

    /*
     * Loop forever until signalled to shutdown and all
     * connections are complete and gracefully closed.
     */

    Ns_Log(Notice, "%s: accepting connections", DRIVER_NAME);
    nactive = 0;
    closePtr = waitPtr = NULL;
    time(&now);
    while (activeDrvPtr || nactive || closePtr || waitPtr) {

	/*
	 * Set trigger pipe bit.
	 */

	FD_ZERO(&set);
	FD_SET(trigPipe[0], &set);
	max = trigPipe[0];

	/*
	 * Set the bits for all active drivers if a connection
	 * isn't already pending.
	 */

	if (waitPtr == NULL) {
    	    drvPtr = activeDrvPtr;
	    while (drvPtr != NULL) {
		FD_SET(drvPtr->sock, &set);
		if (max < drvPtr->sock) {
		    max = drvPtr->sock;
		}
		drvPtr = drvPtr->nextPtr;
	    }
	}

	/*
	 * If there are any closing sockets, set the bits
	 * and determine the minimum relative timeout.
	 */

	if (closePtr == NULL) {
	    tvPtr = NULL;
	} else {
	    timeout = INT_MAX;
	    connPtr = closePtr;
	    while (connPtr != NULL) {
		FD_SET(connPtr->sock, &set);
		if (max < connPtr->sock) {
		    max = connPtr->sock;
		}
		if (timeout > connPtr->timeout) {
		    timeout = connPtr->timeout;
		}
		connPtr = connPtr->nextPtr;
	    }
	    if (timeout > now) {
		tv.tv_sec = (int) timeout - now;
	    } else {
		tv.tv_sec = 0;
	    }
	    tv.tv_usec = 0;
	    tvPtr = &tv;
	}

	/*
	 * Select and drain the trigger pipe if necessary.
	 */

	do {
	    n = select(max+1, &set, NULL, NULL, tvPtr);
	} while (n < 0  && ns_sockerrno == EINTR);
	if (n < 0) {
	    Ns_Fatal("%s: select() failed: %s", DRIVER_NAME,
		ns_sockstrerror(ns_sockerrno));
	}
	if (FD_ISSET(trigPipe[0], &set) && recv(trigPipe[0], &c, 1, 0) != 1) {
	    Ns_Fatal("%s: trigger recv() failed: %s", DRIVER_NAME,
		ns_sockstrerror(ns_sockerrno));
	}

	/*
	 * Update the current time and drain and/or release any
	 * closing sockets.
	 */

	time(&now);
	if (closePtr != NULL) {
	    connPtr = closePtr;
	    closePtr = NULL;
	    while (connPtr != NULL) {
		nextPtr = connPtr->nextPtr;
		if (FD_ISSET(connPtr->sock, &set)) {
		    n = recv(connPtr->sock, drain, sizeof(drain), 0);
		    if (n <= 0) {
			connPtr->timeout = now;
		    }
		}
		if (connPtr->timeout <= now) {
		    SockRelease(connPtr);
		} else {
		    connPtr->nextPtr = closePtr;
		    closePtr = connPtr;
		}
		connPtr = nextPtr;
	    }
	}
	
	/*
	 * Attempt to queue any pending connection.  Otherwise,
	 * accept new connections as long as they can be queued.
	 */

	if (waitPtr != NULL) {
	    if (SockQueue(waitPtr)) {
		waitPtr = NULL;
		++nactive;
	    }
	} else {
	    drvPtr = activeDrvPtr;
	    activeDrvPtr = idleDrvPtr = acceptDrvPtr = NULL;
	    while (drvPtr != NULL) {
		nextDrvPtr = drvPtr->nextPtr;
		if (waitPtr != NULL
			|| !FD_ISSET(drvPtr->sock, &set)
			|| !SockAccept(drvPtr->sock, &sock, &sa)) {
		    /*
		     * Add this port to the temporary idle list.
		     */

		    drvPtr->nextPtr = idleDrvPtr;
		    idleDrvPtr = drvPtr;
		} else {
		    /*
		     * Add this port to the temporary accepted list.
		     */

		    drvPtr->nextPtr = acceptDrvPtr;
		    acceptDrvPtr = drvPtr;

		    /*
		     * Allocate and/or initialize a connection structure.
		     */

		    connPtr = drvPtr->firstFreeConnPtr;
		    if (connPtr != NULL) {
			drvPtr->firstFreeConnPtr = connPtr->nextPtr;
		    } else {
			connPtr = ns_malloc(sizeof(Conn) + drvPtr->bufsize);
			connPtr->drvPtr = drvPtr;
		    }
		    connPtr->nextPtr = NULL;
		    connPtr->sock = sock;
		    connPtr->nrecv = connPtr->nsend = 0;
#ifdef SSL
		    connPtr->cssl = NULL; 
#else 
		    connPtr->cnt = 0;
		    connPtr->base = connPtr->buf;
#endif
		    connPtr->port = ntohs(sa.sin_port);
		    strcpy(connPtr->peer, ns_inet_ntoa(sa.sin_addr));

		    /*
		     * Even though the socket should have inherited
		     * non-blocking from the accept socket, set again
		     * just to be sure.
		     */

		    Ns_SockSetNonBlocking(connPtr->sock);

		    /*
		     * Attempt to queue the socket, holding it if
		     * necessary to try again later.
		     */

		    if (!SockQueue(connPtr)) {
			waitPtr = connPtr;
		    } else {
			++nactive;
		    }
		}
		drvPtr = nextDrvPtr;
	    }

	    /*
	     * Put the active driver list back together with the idle
	     * drivers first but otherwise in the original order.  This
	     * should ensure round-robin service of the drivers.
	     */

	    while ((drvPtr = acceptDrvPtr) != NULL) {
		acceptDrvPtr = drvPtr->nextPtr;
		drvPtr->nextPtr = activeDrvPtr;
		activeDrvPtr = drvPtr;
	    }
	    while ((drvPtr = idleDrvPtr) != NULL) {
		idleDrvPtr = drvPtr->nextPtr;
		drvPtr->nextPtr = activeDrvPtr;
		activeDrvPtr = drvPtr;
	    }
	}

	/*
	 * Check for shutdown and get the list of any closing sockets.
	 */

	Ns_MutexLock(&lock);
	connPtr = firstClosePtr;
	firstClosePtr = NULL;
	stopping = shutdownPending;
	Ns_MutexUnlock(&lock);

	/*
	 * Update the timeout for each closing socket and add to the
	 * close list if some data has been read from the socket
	 * (i.e., it's not a closing keep-alive connection).
	 */

	while (connPtr != NULL) {
	    nextPtr = connPtr->nextPtr;
	    --nactive;
	    if (connPtr->nrecv == 0 || shutdown(connPtr->sock, 1) != 0) {
		SockRelease(connPtr);
	    } else {
		connPtr->nextPtr = closePtr;
		closePtr = connPtr;
		connPtr->timeout = now + connPtr->drvPtr->closewait;
	    }
	    connPtr = nextPtr;
	}

	/*
	 * Close the active drivers if shutdown is pending.
	 */

	if (stopping) {
	    while ((drvPtr = activeDrvPtr) != NULL) {
		activeDrvPtr = drvPtr->nextPtr;
		ns_sockclose(drvPtr->sock);
		drvPtr->sock = INVALID_SOCKET;
		drvPtr->nextPtr = firstDrvPtr;
		firstDrvPtr = drvPtr;
	    }
	}
    }

    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * SockRelease --
 *
 *	Close a socket and release the connection structure for
 *	re-use.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SockRelease(Conn *connPtr)
{
    ns_sockclose(connPtr->sock);
    connPtr->sock = INVALID_SOCKET;
    connPtr->nextPtr = connPtr->drvPtr->firstFreeConnPtr;
    connPtr->drvPtr->firstFreeConnPtr = connPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SockTrigger --
 *
 *	Wakeup SockThread from blocking select().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SockThread will wakeup.
 *
 *----------------------------------------------------------------------
 */

static void
SockTrigger(void)
{
    if (send(trigPipe[1], "", 1, 0) != 1) {
	Ns_Fatal("%s: trigger send() failed: %s", DRIVER_NAME,
	    ns_sockstrerror(ns_sockerrno));
    }
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * SockInit -- 
 * 
 *      Initialize the SSL connection. 
 * 
 * Results: 
 *      NS_OK/NS_ERROR 
 * 
 * Side effects: 
 *	See NsSSLCreateConn.
 * 
 *---------------------------------------------------------------------- 
 */ 

static int 
SockInit(void *arg) 
{ 
    Conn   *connPtr = arg; 
 
#ifdef SSL 
    if (connPtr->cssl == NULL) { 
        connPtr->cssl = NsSSLCreateConn(connPtr->sock,
			    connPtr->drvPtr->timeout,
			    connPtr->drvPtr->dssl); 
        if (connPtr->cssl == NULL) { 
            return NS_ERROR; 
        } 
    } 
#endif
    return NS_OK; 
}


/* 
 *----------------------------------------------------------------------
 *
 * SockClose --
 *
 *	Return a connction to the SockThread for closing.
 *
 * Results:
 *	NS_OK
 *
 * Side effects:
 *	SSL conn will be flushed and then destroyed.
 *
 *----------------------------------------------------------------------
 */

static int
SockClose(void *arg)
{
    Conn *connPtr = arg;
    int trigger = 0;
    
#ifdef SSL 
    if (connPtr->cssl != NULL) { 
        (void) NsSSLFlush(connPtr->cssl); 
        NsSSLDestroyConn(connPtr->cssl); 
        connPtr->cssl = NULL;
    }
#endif
    Ns_MutexLock(&lock);
    if (firstClosePtr == NULL) {
	trigger = 1;
    }
    connPtr->nextPtr = firstClosePtr;
    firstClosePtr = connPtr;
    Ns_MutexUnlock(&lock);
    if (trigger) {
	SockTrigger();
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockRead --
 *
 *	Read from the socket 
 *
 *	NB: May block waiting for requested number of bytes!
 *
 * Results:
 *	Number of bytes read or -1 on timeout or error.
 *
 * Side effects:
 *	Either reads through SSL code or through socket buffer.
 *
 *----------------------------------------------------------------------
 */

static int
SockRead(void *arg, void *vbuf, int len)
{
    Conn   *connPtr = arg;
    int	    nread;

#ifdef SSL 
    nread = NsSSLRecv(connPtr->cssl, vbuf, len);
#else 
    char       *buf = (char *) vbuf;
    int         n;
    
    nread = len;
    while (len > 0) {
        if (connPtr->cnt > 0) {
	    /*
	     * Copy bytes already in read-ahead buffer.
	     */

            if (connPtr->cnt > len) {
                n = len;
            } else {
                n = connPtr->cnt;
            }
            memcpy(buf, connPtr->base, n);
            connPtr->base += n;
            connPtr->cnt -= n;
            len -= n;
            buf += n;
        }
        if (len > 0) {
	    /*
	     * Attempt to fill the read-ahead buffer.
	     */

            connPtr->base = connPtr->buf;
	    connPtr->cnt = Ns_SockRecv(connPtr->sock, connPtr->buf,
		connPtr->drvPtr->bufsize, connPtr->drvPtr->timeout);
	    if (connPtr->cnt <= 0) {
		return -1;
	    }
    	}
    }
#endif
    if (nread > 0) {
	connPtr->nrecv += nread;
    }
    return nread;
}


/*
 *----------------------------------------------------------------------
 *
 * SockWrite --
 *
 *	Writes data to a socket.
 *
 *	NB: May block waiting to send requested number of bytes!
 *
 * Results:
 *	Number of bytes written or -1 for timeout or error.
 *
 * Side effects:
 *	Bytes may be written to a socket
 *
 *----------------------------------------------------------------------
 */

static int
SockWrite(void *arg, void *vbuf, int len)
{
    Conn   *connPtr = arg;
    int	    nwrote;

#ifdef SSL 
    nwrote = NsSSLSend(connPtr->cssl, vbuf, len); 
#else
    int     n;
    char   *buf;

    nwrote = len;
    buf = vbuf;
    while (len > 0) {
	n = Ns_SockSend(connPtr->sock, buf, len,
	    connPtr->drvPtr->timeout);
	if (n < 0) {
	    return -1;
	}
	len -= n;
	buf += n;
    }
#endif
    if (nwrote > 0) {
	connPtr->nsend += nwrote;
    }
    return nwrote;
}


/*
 *----------------------------------------------------------------------
 *
 * SockHost --
 *
 *	Return the host (addr) I'm bound to 
 *
 * Results:
 *	String hostname 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static char *
SockHost(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->drvPtr->address;
}


/*
 *----------------------------------------------------------------------
 *
 * SockPort --
 *
 *	Get the port I'm listening on.
 *
 * Results:
 *	A TCP port number 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
SockPort(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->drvPtr->port;
}


/*
 *----------------------------------------------------------------------
 *
 * SockName --
 *
 *	Return the name of this driver 
 *
 * Results:
 *	nssock or nsssl
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static char *
SockName(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->drvPtr->name;
}


/*
 *----------------------------------------------------------------------
 *
 * SockPeer --
 *
 *	Return the string name of the peer address 
 *
 * Results:
 *	String peer (ip) addr 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static char *
SockPeer(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->peer;
}


/*
 *----------------------------------------------------------------------
 *
 * SockPeerPort --
 *
 *	Get the peer's originating tcp port 
 *
 * Results:
 *	A tcp port 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
SockPeerPort(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->port;
}


/*
 *----------------------------------------------------------------------
 *
 * SockLocation --
 *
 *	Returns the location, suitable for making anchors 
 *
 * Results:
 *	String location 
 *
 * Side effects:
 *	none 
 *
 *----------------------------------------------------------------------
 */

static char *
SockLocation(void *arg)
{
    Conn   *connPtr = arg;

    return connPtr->drvPtr->location;
}


/*
 *----------------------------------------------------------------------
 *
 * SockConnectionFd --
 *
 *	Get the socket fd.  This callback is used by the keep-alive
 *	code to select() for readability.
 *
 * Results:
 *	The socket fd.
 *
 * Side effects:
 *	For SSL, any pending data is flushed.
 *
 *----------------------------------------------------------------------
 */

static int
SockConnectionFd(void *arg)
{
    Conn   *connPtr = arg;

#ifdef SSL 
    if (connPtr->cssl == NULL || (NsSSLFlush(connPtr->cssl) != NS_OK)) { 
        return -1; 
    } 
#endif 
    return (int) connPtr->sock;
}


/*
 *----------------------------------------------------------------------
 *
 * SockDetach --
 *
 *	Detach the connection data.  This callback is used by the
 *	keep-alive code to enable the socket driver to reset the
 *	data for a new connection.
 *
 * Results:
 *	Pointer to connection data.
 *
 * Side effects:
 *	Connection I/O counts reset.
 *
 *----------------------------------------------------------------------
 */

static void *
SockDetach(void *arg)
{
    Conn   *connPtr = arg;

    connPtr->nrecv = connPtr->nsend = 0;
    return arg;
}


/* 
 *---------------------------------------------------------------------- 
 * 
 * NsSSLGetConn -- 
 * 
 *      Return the SSL connection.  Used by SSL Tcl. 
 * 
 * Results: 
 *      Pointer to SSL connection or NULL. 
 * 
 * Side effects: 
 *      None. 
 * 
 *---------------------------------------------------------------------- 
 */ 
#ifdef SSL 
void * 
NsSSLGetConn(Ns_Conn *conn) 
{ 
    Conn *connPtr; 
    char *name; 
 
    if (conn != NULL) { 
        name = Ns_ConnDriverName(conn); 
        if (name != NULL && STREQ(name, DRIVER_NAME)) { 
            connPtr = Ns_ConnDriverContext(conn); 
            if (connPtr != NULL) { 
                return connPtr->cssl; 
            } 
        } 
    } 
    return  NULL; 
} 
#endif 


/*
 *----------------------------------------------------------------------
 *
 * SockSendFd --
 *
 *      Sends the contents of an open fd to a socket.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#ifdef HAVE_SENDV
static int
SockSendFd(void *arg, int fd, int nsend)
{
    Conn   *connPtr = arg;
    struct sendv_iovec vec[1];
    int         n, len, off;

    /*
     * Do sendv until all data is in the pipeline
     */

    vec[0].sendv_base = NULL;
    vec[0].sendv_flags = SENDV_FD;
    vec[0].sendv_fd = fd;
    len = nsend;
    off = 0;
    while (len > 0) {
        vec[0].sendv_off = off;
        vec[0].sendv_len = len;
        n = sendv(connPtr->sock, vec, 1);
        if (n < 0 && ns_sockerrno == EWOULDBLOCK
	    && Ns_SockWait(connPtr->sock, NS_SOCK_WRITE, connPtr->drvPtr->timeout) == NS_OK) {
            n = sendv(connPtr->sock, vec, 1);
        }   
        if (n <= 0) {
	    return NS_ERROR;
	}
	len -= n;
	off += n;
    }
    return NS_OK;
}
#endif

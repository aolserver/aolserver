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
 * driver.c --
 *
 *	Connection I/O for loadable socket drivers.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/driver.c,v 1.17.2.8 2004/08/25 19:51:29 dossy Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Defines for SockRead return code.
 */

#define SOCK_READY		0
#define SOCK_MORE		1
#define SOCK_ERROR		(-1)

/*
 * Defines for SockRelease reason codes.
 *
 */

typedef enum {
    Reason_CloseTimeout,
    Reason_ReadTimeout,
    Reason_ServerReject,
    Reason_SockError,
    Reason_SockShutError
    } ReleaseReasons;

/*
 * LoggingFlag mask values
 */
#define LOGGING_READTIMEOUT   0x01
#define LOGGING_SERVERREJECT  0x02
#define LOGGING_SOCKERROR     0x04
#define LOGGING_SOCKSHUTERROR 0x08


/*
 * The following maintains Host header to server mappings.
 */

typedef struct ServerMap {
    NsServer *servPtr;
    char location[1];
} ServerMap;

/*
 * Static functions defined in this file.
 */

static Ns_ThreadProc DriverThread;
static Sock *SockAccept(Driver *drvPtr);
static void SockRelease(Sock *sockPtr, ReleaseReasons reason);
static void SockTrigger(void);
static Driver *firstDrvPtr; /* First in list of all drivers. */
static Sock *firstClosePtr; /* First conn ready for graceful close. */
static int SockRead(Sock *sockPtr);
static void SockPoll(Sock *sockPtr, Ns_Time *timeoutPtr);
static void SockTimeout(Sock *sockPtr, Ns_Time *nowPtr, int timeout);
static int  SetServer(Sock *sockPtr);

/*
 * Static variables defined in this file.
 */

static Request *firstReqPtr;/* Free list of request structures. */
static Ns_Mutex reqLock;    /* Lock around request free list. */
static Sock *firstSockPtr;  /* Free list of Sock structures. */
static int shutdownPending; /* Flag to indicate shutdown. */
static int stopped = 1;	    /* Flag to indicate running. */
static int nactive;	    /* Active sockets. */
static Ns_Thread driverThread;/* Running DriverThread. */
static SOCKET trigPipe[2];  /* Trigger to wakeup DriverThread. */
static Ns_Mutex lock;	    /* Lock around close list and shutdown flag. */
static Ns_Cond cond;	    /* Cond for stopped flag. */
static unsigned int nfds;	/* Number of Sock to poll(). */
static unsigned int maxfds;	/* Max pollfd's in pfds. */ 
static struct pollfd *pfds; /* Array of pollfds to poll(). */
static Tcl_HashTable hosts; /* Host header to server table. */
static ServerMap *defMapPtr;    /* Default server when not found in table. */


/*
 *----------------------------------------------------------------------
 *
 * NsInitDrivers --
 *
 *	Init drivers system.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsInitDrivers(void)
{
    Tcl_InitHashTable(&hosts, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DriverInit --
 *
 *	Initialize a driver.
 *
 * Results:
 *	NS_OK if initialized, NS_ERROR if config or other error.
 *
 * Side effects:
 *	Listen socket will be opened later in NsStartDrivers.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DriverInit(char *server, char *module, Ns_DriverInitData *init)
{
    char *path,*address, *host, *bindaddr, *defproto, *defserver;
    int i, n, sockwait, defport;
    ServerMap *mapPtr;
    Tcl_HashEntry *hPtr;
    Ns_DString ds;
    Ns_Set *set;
    struct in_addr  ia;
    struct hostent *he;
    Driver *drvPtr;
    NsServer *servPtr = NULL;
    int controlFlag;

    if (server != NULL && (servPtr = NsGetServer(server)) == NULL) {
	return NS_ERROR;
    }

    if (init->version != NS_DRIVER_VERSION_1) {
        Ns_Log(Error, "%s: version field of init argument is invalid: %d", 
                module, init->version);
        return NS_ERROR;
    }

    path = (init->path ? init->path : Ns_ConfigGetPath(server, module, NULL));

    /*
     * Determine the hostname used for the local address to bind
     * to and/or the HTTP location string.
     */

    host = Ns_ConfigGetValue(path, "hostname");
    bindaddr = address = Ns_ConfigGetValue(path, "address");
    defserver = Ns_ConfigGetValue(path, "defaultserver");

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
	 * address which often returns the fully qualified name.
	 *
	 * NB: This is a common but sloppy configuration for a Unix
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
            Ns_Log(Error, "%s: could not resolve %s: %s", module,
		   host ? host : Ns_InfoHostname(), strerror(errno));
	    return NS_ERROR;
	}
        if (*(he->h_addr_list) == NULL) {
            Ns_Log(Error, "%s: no addresses for %s", module, he->h_name);
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
     * If the hostname was not specified and not determined by the lookup
     * above, set it to the specified or derived IP address string.
     */

    if (host == NULL) {
	host = address;
    }

    /*
     * Set the protocol and port defaults.
     */

    if (init->opts & NS_DRIVER_SSL) {
	defproto = "https";
	defport = 443;
    } else {
	defproto = "http";
	defport = 80;
    }

    /*
     * Allocate a new driver instance and set configurable parameters.
     */

    drvPtr = ns_calloc(1, sizeof(Driver));
    drvPtr->server = server;
    drvPtr->name = init->name;
    drvPtr->proc = init->proc;
    drvPtr->arg = init->arg;
    drvPtr->opts = init->opts;
    drvPtr->servPtr = servPtr;
#define _MAX(x,y)       ((x) > (y) ? (x) : (y))
#define _MIN(x,y)       ((x) > (y) ? (y) : (x))
    if (!Ns_ConfigGetInt(path, "bufsize", &n) || n < 1) { 
        n = 16000; 	/* ~16k */
    }
    drvPtr->bufsize = n;
    if (!Ns_ConfigGetInt(path, "rcvbuf", &n)) {
	n = 0;		/* Use OS default. */
    }
    drvPtr->rcvbuf = n;
    if (!Ns_ConfigGetInt(path, "sndbuf", &n)) {
	n = 0;		/* Use OS default. */
    }
    drvPtr->sndbuf = n;
    if (!Ns_ConfigGetInt(path, "socktimeout", &n) || n < 1) {
	n = 30;		/* 30 seconds. */
    }
    sockwait = n;
    if (!Ns_ConfigGetInt(path, "sendwait", &n) || n < 1) {
	n = sockwait; /* Use previous socktimeout option. */
    }
    drvPtr->sendwait = n;
    if (!Ns_ConfigGetInt(path, "recvwait", &n) || n < 1) {
	n = sockwait; /* Use previous socktimeout option. */
    }
    drvPtr->recvwait = n;
    if (!Ns_ConfigGetInt(path, "closewait", &n) || n < 0) {
	n = 2;		/* 2 seconds */
    }
    drvPtr->closewait = n;
    if (!Ns_ConfigGetInt(path, "keepwait", &n) || n < 0) {
	n = 30;		/* 30 seconds */
    }
    drvPtr->keepwait = n;
    if (!Ns_ConfigGetInt(path, "backlog", &n) || n < 1) {
	n = 5;		/* 5 pending connections. */
    }
    drvPtr->backlog = n;
    if (!Ns_ConfigGetInt(path, "maxinput", &n) || n < 1) {
        n = 1000 * 1024;        /* 1m. */
    }
    drvPtr->maxinput = _MAX(n, 1024);

    /*
     * Allow specification of logging or not of various deep
     * socket handling errors.  These all default to Off.
     */
    drvPtr->loggingFlags = 0;
    if (Ns_ConfigGetBool(path, "readtimeoutlogging", &controlFlag)
        && controlFlag) {
        drvPtr->loggingFlags |= LOGGING_READTIMEOUT;
    }
    if (Ns_ConfigGetBool(path, "serverrejectlogging", &controlFlag)
        && controlFlag) {
        drvPtr->loggingFlags |= LOGGING_SERVERREJECT;
    }
    if (Ns_ConfigGetBool(path, "sockerrorlogging", &controlFlag)
        && controlFlag) {
        drvPtr->loggingFlags |= LOGGING_SOCKERROR;
    }
    if (Ns_ConfigGetBool(path, "sockshuterrorlogging", &controlFlag)
        && controlFlag) {
        drvPtr->loggingFlags |= LOGGING_SOCKSHUTERROR;
    }

    /*
     * Determine the port and then set the HTTP location string either
     * as specified in the config file or constructed from the
     * hostname and port.
     */

    drvPtr->bindaddr = bindaddr;
    drvPtr->address = ns_strdup(address);
    if (!Ns_ConfigGetInt(path, "port", &drvPtr->port)) {
	drvPtr->port = defport;
    }
    drvPtr->location = Ns_ConfigGetValue(path, "location");
    if (drvPtr->location != NULL) {
	drvPtr->location = ns_strdup(drvPtr->location);
    } else {
    	Ns_DStringInit(&ds);
	Ns_DStringVarAppend(&ds, defproto, "://", host, NULL);
	if (drvPtr->port != defport) {
	    Ns_DStringPrintf(&ds, ":%d", drvPtr->port);
	}
	drvPtr->location = Ns_DStringExport(&ds);
    }
    drvPtr->nextPtr = firstDrvPtr;
    firstDrvPtr = drvPtr;
    ++maxfds;

    /*
     * Map Host headers for drivers not bound to servers.
     */

    if (server == NULL) {
        if (defserver == NULL) {
            Ns_Fatal("%s: virtual servers configured,"
                    " but %s has no defaultserver defined",
                    module, path);
        }
        defMapPtr = NULL;
	path = Ns_ConfigGetPath(NULL, module, "servers", NULL);
	set = Ns_ConfigGetSection(path);
	for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	    server = Ns_SetKey(set, i);
	    host = Ns_SetValue(set, i);
	    servPtr = NsGetServer(server);
            if (servPtr == NULL) {
                Ns_Log(Error, "%s: no such server: %s", module, server);
            } else {
                hPtr = Tcl_CreateHashEntry(&hosts, host, &n);
                if (!n) {
                    Ns_Log(Error, "%s: duplicate host map: %s", module, host);
                } else {
                    Ns_DStringVarAppend(&ds, defproto, "://", host, NULL);
                    mapPtr = ns_malloc(sizeof(ServerMap) + ds.length);
                    mapPtr->servPtr  = servPtr;
                    strcpy(mapPtr->location, ds.string);
                    Ns_DStringTrunc(&ds, 0);
                    if (defMapPtr == NULL && STREQ(defserver, server)) {
                        defMapPtr = mapPtr;
                    }
                    Tcl_SetHashValue(hPtr, mapPtr);
                }
            }
	}
        if (defMapPtr == NULL) {
            Ns_Fatal("%s: default server %s not defined in %s",
                    module, path);
        }
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterDriver --
 *
 *	Register a set of communications driver procs (no longer
 *	supported).
 *
 * Results:
 *	NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterDriver(char *server, char *label, void *procs, void *drvData)
{
    Ns_Log(Error, "driver: loadable drivers no longer supported");
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetDriverContext --
 *
 *	Return the driver's context (no longer supported)
 *
 * Results:
 *	NULL. 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_GetDriverContext(Ns_Driver drv)
{
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartDrivers --
 *
 *	Listen on all driver address/ports and start the DriverThread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See DriverThread.
 *
 *----------------------------------------------------------------------
 */

void
NsStartDrivers(void)
{
    Driver *drvPtr;

    /*
     * Listen on all drivers.
     */

    drvPtr = firstDrvPtr;
    while (drvPtr != NULL) {
	drvPtr->sock = Ns_SockListenEx(drvPtr->bindaddr, drvPtr->port,
	    drvPtr->backlog);
	if (drvPtr->sock == INVALID_SOCKET) {
	    Ns_Log(Error, "%s: failed to listen on %s:%d: %s",
		drvPtr->name, drvPtr->address, drvPtr->port,
		ns_sockstrerror(ns_sockerrno));
	} else {
    	    Ns_SockSetNonBlocking(drvPtr->sock);
    	    Ns_Log(Notice, "%s: listening on %s:%d",
		drvPtr->name, drvPtr->address, drvPtr->port);
	}
	drvPtr = drvPtr->nextPtr;
    }

    /*
     * Create the socket thread.
     */

    if (ns_sockpair(trigPipe) != 0) {
	Ns_Fatal("driver: ns_sockpair() failed: %s",
	    ns_sockstrerror(ns_sockerrno));
    }
    Ns_ThreadCreate(DriverThread, NULL, 0, &driverThread);
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopDrivers --
 *
 *	Trigger the DriverThread to begin shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	DriverThread will close listen sockets and then exit after all
 *	outstanding connections are complete and closed.
 *
 *----------------------------------------------------------------------
 */

void
NsStopDrivers(void)
{
    Ns_MutexLock(&lock);
    if (!stopped && !shutdownPending) {
    	Ns_Log(Notice, "driver: triggering shutdown");
	shutdownPending = 1;
	SockTrigger();
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitDriversShutdown --
 *
 *	Wait for exit of DriverThread.  This callback is invoke later by
 *	the timed shutdown thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Driver thread is joined and trigger pipe closed.
 *
 *----------------------------------------------------------------------
 */

void
NsWaitDriversShutdown(Ns_Time *toPtr)
{
    int status = NS_OK;

    Ns_MutexLock(&lock);
    while (!stopped && status == NS_OK) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "driver: timeout waiting for shutdown");
    } else {
	Ns_Log(Notice, "driver: shutdown complete");
	driverThread = NULL;
	ns_sockclose(trigPipe[0]);
	ns_sockclose(trigPipe[1]);
    }
}


/* 
 *----------------------------------------------------------------------
 *
 * NsGetRequest --
 *
 *	Return the request buffer, reading it if necessary (i.e., if
 *	not an async read-ahead connection).  This function is called
 *	at the start of connection processing.
 *
 * Results:
 *	Pointer to Request structure or NULL on error.
 *
 * Side effects:
 *	May wait for content to arrive if necessary.
 *
 *----------------------------------------------------------------------
 */

Request *
NsGetRequest(Sock *sockPtr)
{
    Request *reqPtr;
    int status;

    if (sockPtr->reqPtr == NULL) {
	do {
	    status = SockRead(sockPtr);
	} while (status == SOCK_MORE);
	if (status != SOCK_READY) {
	    if (sockPtr->reqPtr != NULL) {
		NsFreeRequest(sockPtr->reqPtr);
	    }
	    sockPtr->reqPtr = NULL;
	}
    }
    reqPtr = sockPtr->reqPtr;
    /* NB: Sock no longer responsible for freeing request. */
    sockPtr->reqPtr = NULL;
    return reqPtr;
}


/* 
 *----------------------------------------------------------------------
 *
 * NsFreeRequest --
 *
 *	Free a connection request structure.  This routine is called
 *	at the end of connection processing or on a socket which
 *	times out during async read-ahead.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeRequest(Request *reqPtr)
{
    reqPtr->next = reqPtr->content = NULL;
    reqPtr->length = reqPtr->avail = 0;
    reqPtr->coff = reqPtr->woff = reqPtr->roff = 0;
    reqPtr->leadblanks = 0;
    Tcl_DStringFree(&reqPtr->buffer);
    Ns_SetTrunc(reqPtr->headers, 0);
    Ns_FreeRequest(reqPtr->request);
    reqPtr->request = NULL;
    Ns_MutexLock(&reqLock);
    reqPtr->nextPtr = firstReqPtr;
    firstReqPtr = reqPtr;
    Ns_MutexUnlock(&reqLock);
}


/* 
 *----------------------------------------------------------------------
 *
 * NsSockSend --
 *
 *	Send buffers via the socket's driver callback.
 *
 * Results:
 *	# of bytes sent or -1 on error.
 *
 * Side effects:
 *	Depends on driver proc.
 *
 *----------------------------------------------------------------------
 */

int
NsSockSend(Sock *sockPtr, struct iovec *bufs, int nbufs)
{
    Ns_Sock *sock = (Ns_Sock *) sockPtr;

    return (*sockPtr->drvPtr->proc)(DriverSend, sock, bufs, nbufs);
}


/* 
 *----------------------------------------------------------------------
 *
 * NsSockClose --
 *
 *	Return a connction to the DriverThread for closing or keepalive.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Socket may be reused by a keepalive connection.
 *
 *----------------------------------------------------------------------
 */

void
NsSockClose(Sock *sockPtr, int keep)
{
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    int trigger = 0;

    if (keep && (*sockPtr->drvPtr->proc)(DriverKeep, sock, NULL, 0) != 0) {
	keep = 0;
    }
    if (!keep) {
	(void) (*sockPtr->drvPtr->proc)(DriverClose, sock, NULL, 0);
    }
    sockPtr->keep = keep;
    Ns_MutexLock(&lock);
    if (firstClosePtr == NULL) {
	trigger = 1;
    }
    sockPtr->keep = keep;
    sockPtr->nextPtr = firstClosePtr;
    firstClosePtr = sockPtr;
    Ns_MutexUnlock(&lock);
    if (trigger) {
	SockTrigger();
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DriverThread --
 *
 *	Main listening socket driver thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connections are accepted on the configured listen sockets,
 *	placed on the run queue to be serviced, and gracefully
 *	closed when done.  Async sockets have the entire request read
 *	here before queuing as well.
 *
 *----------------------------------------------------------------------
 */

static void
DriverThread(void *ignored)
{
    char c;
    int n, stopping, pollto;
    Sock *sockPtr, *closePtr, *nextPtr, *waitPtr, *readPtr;
    Driver *activeDrvPtr;
    Driver *drvPtr, *nextDrvPtr, *idleDrvPtr, *acceptDrvPtr;
    char drain[1024];
    Ns_Time timeout, now, diff;
    
    Ns_ThreadSetName("-driver-");
    Ns_Log(Notice, "starting");

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

    Ns_Log(Notice, "driver: accepting connections");
    closePtr = waitPtr = readPtr = NULL;
    Ns_GetTime(&now);
    stopping = 0;
    maxfds += 100;
    pfds = ns_malloc(sizeof(struct pollfd) * maxfds);
    pfds[0].fd = trigPipe[0];
    pfds[0].events = POLLIN;

    while (!stopping || nactive) {

	/*
	 * Set the bits for all active drivers if a connection
	 * isn't already pending.
	 */

	nfds = 1;
	if (waitPtr == NULL) {
    	    drvPtr = activeDrvPtr;
	    while (drvPtr != NULL) {
		pfds[nfds].fd = drvPtr->sock;
		pfds[nfds].events = POLLIN;
		drvPtr->pidx = nfds++;
		drvPtr = drvPtr->nextPtr;
	    }
	}

	/*
	 * If there are any closing or read-ahead sockets, set the bits
	 * and determine the minimum relative timeout.
	 */

	if (readPtr == NULL && closePtr == NULL) {
	    pollto = -1;
	} else {
	    timeout.sec = INT_MAX;
	    timeout.usec = LONG_MAX;
	    sockPtr = readPtr;
	    while (sockPtr != NULL) {
		SockPoll(sockPtr, &timeout);
		sockPtr = sockPtr->nextPtr;
	    }
	    sockPtr = closePtr;
	    while (sockPtr != NULL) {
		SockPoll(sockPtr, &timeout);
		sockPtr = sockPtr->nextPtr;
	    }
	    if (Ns_DiffTime(&timeout, &now, &diff) > 0)  {
		pollto = diff.sec * 1000 + diff.usec / 1000;
	    } else {
		pollto = 0;
	    }
	}

	/*
	 * Select and drain the trigger pipe if necessary.
	 */

    	pfds[0].revents = 0;
	do {
	    n = poll(pfds, nfds, pollto);
	} while (n < 0  && errno == EINTR);
	if (n < 0) {
	    Ns_Fatal("driver: poll() failed: %s",
		ns_sockstrerror(ns_sockerrno));
	}
	if ((pfds[0].revents & POLLIN) && recv(trigPipe[0], &c, 1, 0) != 1) {
	    Ns_Fatal("driver: trigger recv() failed: %s",
            ns_sockstrerror(ns_sockerrno));
	}

	/*
	 * Update the current time and drain and/or release any
	 * closing sockets.
	 */

	Ns_GetTime(&now);
	if (closePtr != NULL) {
	    sockPtr = closePtr;
	    closePtr = NULL;
	    while (sockPtr != NULL) {
		nextPtr = sockPtr->nextPtr;
		if (pfds[sockPtr->pidx].revents & POLLIN) {
		    n = recv(sockPtr->sock, drain, sizeof(drain), 0);
		    if (n <= 0) {
			sockPtr->timeout = now;
		    }
		}
		if (Ns_DiffTime(&sockPtr->timeout, &now, &diff) <= 0) {
		    SockRelease(sockPtr, Reason_CloseTimeout);
		} else {
		    sockPtr->nextPtr = closePtr;
		    closePtr = sockPtr;
		}
		sockPtr = nextPtr;
	    }
	}

	/*
	 * Attempt read-ahead of any new connections.
	 */

	sockPtr = readPtr;
	readPtr = NULL;
	while (sockPtr != NULL) {
	    nextPtr = sockPtr->nextPtr;
	    if (!(pfds[sockPtr->pidx].revents & POLLIN)) {
		if (Ns_DiffTime(&sockPtr->timeout, &now, &diff) <= 0) {
		    SockRelease(sockPtr, Reason_ReadTimeout);
		} else {
		    sockPtr->nextPtr = readPtr;
		    readPtr = sockPtr;
		}
	    } else {
		/*
		 * If enabled, perform read-ahead now.
		 */

		sockPtr->keep = 0;
		if (sockPtr->drvPtr->opts & NS_DRIVER_ASYNC) {
		    n = SockRead(sockPtr);
		} else {
		    n = SOCK_READY;
		}

		/*
		 * Queue for connection processing if ready.
		 */

		switch (n) {
		case SOCK_MORE:
		    SockTimeout(sockPtr, &now, sockPtr->drvPtr->recvwait);
		    sockPtr->nextPtr = readPtr;
		    readPtr = sockPtr;
		    break;
		case SOCK_READY:
		    if (!SetServer(sockPtr)) {
			SockRelease(sockPtr, Reason_ServerReject);
		    } else {
		    	sockPtr->nextPtr = waitPtr;
			waitPtr = sockPtr;
		    }
		    break;
		default:
		    SockRelease(sockPtr, Reason_SockError);
		    break;
		}
	    }
	    sockPtr = nextPtr;
	}

	/*
	 * Attempt to queue any pending connection
	 * after reversing the list to ensure oldest
	 * connections are tried first.
	 */

	if (waitPtr != NULL) {
	    sockPtr = NULL;
	    while ((nextPtr = waitPtr) != NULL) {
		waitPtr = nextPtr->nextPtr;
		nextPtr->nextPtr = sockPtr;
		sockPtr = nextPtr;
	    }
	    while (sockPtr != NULL) {
		nextPtr = sockPtr->nextPtr;
		if (waitPtr != NULL || !NsQueueConn(sockPtr, &now)) {
		    sockPtr->nextPtr = waitPtr;
		    waitPtr = sockPtr;
		}
		sockPtr = nextPtr;
	    }
	}

	/*
	 * If no connections are waiting, attempt to accept more.
	 */

  	if (waitPtr == NULL) {
	    drvPtr = activeDrvPtr;
	    activeDrvPtr = idleDrvPtr = acceptDrvPtr = NULL;
	    while (drvPtr != NULL) {
		nextDrvPtr = drvPtr->nextPtr;
		if (waitPtr != NULL
	    		|| (!(pfds[drvPtr->pidx].revents & POLLIN))
			|| ((sockPtr = SockAccept(drvPtr)) == NULL)) {
		    /*
		     * Add this driver to the temporary idle list.
		     */

		    drvPtr->nextPtr = idleDrvPtr;
		    idleDrvPtr = drvPtr;
		} else {
		    /*
		     * Add this driver to the temporary accepted list.
		     */

		    drvPtr->nextPtr = acceptDrvPtr;
		    acceptDrvPtr = drvPtr;

		    /*
		     * Put the socket on the read-ahead list.
		     */

		    SockTimeout(sockPtr, &now, sockPtr->drvPtr->recvwait);
		    sockPtr->nextPtr = readPtr;
		    readPtr = sockPtr;
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
	 * Check for shutdown and get the list of any closing or
	 * keepalive sockets.
	 */

	Ns_MutexLock(&lock);
	sockPtr = firstClosePtr;
	firstClosePtr = NULL;
	stopping = shutdownPending;
	Ns_MutexUnlock(&lock);

	/*
	 * Update the timeout for each closing socket and add to the
	 * close list if some data has been read from the socket
	 * (i.e., it's not a closing keep-alive connection).
	 */

	while (sockPtr != NULL) {
	    nextPtr = sockPtr->nextPtr;
	    if (sockPtr->keep) {
		SockTimeout(sockPtr, &now, sockPtr->drvPtr->keepwait);
		sockPtr->nextPtr = readPtr;
		readPtr = sockPtr;
	    } else {
		if (shutdown(sockPtr->sock, 1) != 0) {
		    SockRelease(sockPtr, Reason_SockShutError);
		} else {
		    SockTimeout(sockPtr, &now, sockPtr->drvPtr->closewait);
		    sockPtr->nextPtr = closePtr;
		    closePtr = sockPtr;
		}
	    }
	    sockPtr = nextPtr;
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
    Ns_MutexLock(&lock);
    stopped = 1;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * SetServer --
 *
 *	Set virtual server from driver context or Host header.
 *
 * Results:
 *	1 if valid server set, 0 otherwise.
 *
 * Side effects:
 *	Will update sockPtr->servPtr.
 *
 *----------------------------------------------------------------------
 */

static int
SetServer(Sock *sockPtr)
{
    ServerMap *mapPtr = NULL;
    Tcl_HashEntry *hPtr;
    char *host;
    int status = 1;

    sockPtr->servPtr = sockPtr->drvPtr->servPtr;
    sockPtr->location = sockPtr->drvPtr->location;
    if (sockPtr->reqPtr) {
	host = Ns_SetIGet(sockPtr->reqPtr->headers, "Host");
	if (!host && sockPtr->reqPtr->request->version >= 1.1) {
	    status = 0;
	}
    }
    if (sockPtr->servPtr == NULL) {
	if (host) {
	    hPtr = Tcl_FindHashEntry(&hosts, host);
	    if (hPtr != NULL) {
		mapPtr = Tcl_GetHashValue(hPtr);
	    }
	}
	if (!mapPtr) {
	    mapPtr = defMapPtr;
	}
	if (mapPtr) {
	    sockPtr->servPtr = mapPtr->servPtr;
	    sockPtr->location = mapPtr->location;
	}
	if (sockPtr->servPtr == NULL) {
	    status = 0;
	}
    }

    if (!status && sockPtr->reqPtr) {
	ns_free(sockPtr->reqPtr->request->method);
	sockPtr->reqPtr->request->method = ns_strdup("BAD");
    }
    return 1;
    // return status;
}


/*
 *----------------------------------------------------------------------
 *
 * SockPoll --
 *
 *	Arrange for given Sock to be monitored. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sock fd will be monitored for readability on next spin of
 *	DriverThread.
 *
 *----------------------------------------------------------------------
 */

static void
SockPoll(Sock *sockPtr, Ns_Time *timeoutPtr)
{
    /*
     * Grow the pfds array if necessary.
     */

    if (nfds >= maxfds) {
	maxfds += 100;
	pfds = ns_realloc(pfds, maxfds * sizeof(struct pollfd));
    }

    /*
     * Set the next pollfd struct with this socket.
     */

    pfds[nfds].fd = sockPtr->sock;
    pfds[nfds].events = POLLIN;
    pfds[nfds].revents = 0;
    sockPtr->pidx = nfds++;

    /* 
     * Check for new minimum timeout.
     */

    if (Ns_DiffTime(&sockPtr->timeout, timeoutPtr, NULL) < 0) {
    	*timeoutPtr = sockPtr->timeout;
    }
}

static void
SockTimeout(Sock *sockPtr, Ns_Time *nowPtr, int timeout)
{
    sockPtr->timeout = *nowPtr;
    Ns_IncrTime(&sockPtr->timeout, timeout, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * SockAccept --
 *
 *	Accept and initialize a new Sock.
 *
 * Results:
 *	Pointer to Sock or NULL on error.
 *
 * Side effects:
 *	Socket buffer sizes are set as configured.
 *
 *----------------------------------------------------------------------
 */

static Sock *
SockAccept(Driver *drvPtr)
{
    Sock *sockPtr;
    int slen;

    /*
     * Allocate and/or initialize a connection structure.
     */

    sockPtr = firstSockPtr;
    if (sockPtr != NULL) {
	firstSockPtr = sockPtr->nextPtr;
    } else {
	sockPtr = ns_malloc(sizeof(Sock));
	sockPtr->reqPtr = NULL;
    }

    /*
     * Accept the new connection.
     */

    slen = sizeof(struct sockaddr_in);
    sockPtr->drvPtr = drvPtr;
    sockPtr->keep = 0;
    sockPtr->arg = NULL;
    sockPtr->sock = Ns_SockAccept(drvPtr->sock,
				  (struct sockaddr *) &sockPtr->sa, &slen);
    if (sockPtr->sock == INVALID_SOCKET) {
	/* 
	 * Accept failed - return the Sock to the free list.
	 */

	sockPtr->nextPtr = firstSockPtr;
	firstSockPtr = sockPtr;
	return NULL;
    }

    /*
     * Even though the socket should have inherited
     * non-blocking from the accept socket, set again
     * just to be sure.
     */

    Ns_SockSetNonBlocking(sockPtr->sock);

    /*
     * Set the send/recv socket bufsizes if required.
     */

    if (drvPtr->sndbuf > 0) {
	setsockopt(sockPtr->sock, SOL_SOCKET, SO_SNDBUF,
	    (char *) &drvPtr->sndbuf, sizeof(drvPtr->sndbuf));
    }
    if (drvPtr->rcvbuf > 0) {
	setsockopt(sockPtr->sock, SOL_SOCKET, SO_RCVBUF,
	    (char *) &drvPtr->rcvbuf, sizeof(drvPtr->rcvbuf));
    }
    ++nactive;
    return sockPtr;
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
SockRelease(Sock *sockPtr, ReleaseReasons reason)
{
    char *errMsg = NULL;

    switch (reason) {
    case Reason_CloseTimeout:
        /* This is normal, never log. */
        break;
    case Reason_ReadTimeout:
        /*
         * For this case, whether this is acceptable or not
         * depends upon whether this sock was a keep-alive
         * that we were allowing to 'linger'.
         */
        if (!sockPtr->keep
            && (sockPtr->drvPtr->loggingFlags & LOGGING_READTIMEOUT) ) {
            errMsg = "Timeout during read";
        }
        break;
    case Reason_ServerReject:
        if (sockPtr->drvPtr->loggingFlags & LOGGING_SERVERREJECT) {
            errMsg = "No Server found for request";
        }
        break;
    case Reason_SockError:
        if (sockPtr->drvPtr->loggingFlags & LOGGING_SOCKERROR) {
            errMsg = "Unable to read request";
        }
        break;
    case Reason_SockShutError:
        if (sockPtr->drvPtr->loggingFlags & LOGGING_SOCKSHUTERROR) {
            errMsg = "Unable to shutdown socket";
        }
        break;
    }
    if (errMsg != NULL) {
        Ns_Log( Error, "Releasing Socket; %s, Peer =  %s:%d", 
                errMsg, ns_inet_ntoa(sockPtr->sa.sin_addr),
                ntohs(sockPtr->sa.sin_port) );
    }

    --nactive;
    ns_sockclose(sockPtr->sock);
    sockPtr->sock = INVALID_SOCKET;
    if (sockPtr->reqPtr != NULL) {
	NsFreeRequest(sockPtr->reqPtr);
	sockPtr->reqPtr = NULL;
    }
    sockPtr->nextPtr = firstSockPtr;
    firstSockPtr = sockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SockTrigger --
 *
 *	Wakeup DriversThread from blocking poll().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	DriversThread will wakeup.
 *
 *----------------------------------------------------------------------
 */

static void
SockTrigger(void)
{
    if (send(trigPipe[1], "", 1, 0) != 1) {
	Ns_Fatal("driver: trigger send() failed: %s",
	    ns_sockstrerror(ns_sockerrno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SockRead --
 *
 *	Read content from the given Sock, processing the input as
 *	necessary.  This is the core callback routine designed to
 *	either be called repeatedly within the DriverThread during
 *	an async read-ahead or in a blocking loop in NsGetRequest
 *	at the start of connection processing.
 *
 * Results:
 *	SOCK_READY:	Request is ready for processing.
 *	SOCK_MORE:	More input is required.
 *	SOCK_ERROR:	Client drop or timeout.
 *
 * Side effects:
 *	The Request structure will be built up for use by the
 *	connection thread.  Also, before returning SOCK_READY,
 *	the next byte to read mark and bytes available are set
 *	to the beginning of the content, just beyond the headers.
 *
 *----------------------------------------------------------------------
 */

static int
SockRead(Sock *sockPtr)
{
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    struct iovec buf;
    Request *reqPtr;
    Tcl_DString *bufPtr;
    char *s, *e, save;
    int   cnt, len, nread, n;

    reqPtr = sockPtr->reqPtr;
    if (reqPtr == NULL) {
	Ns_MutexLock(&reqLock);
	reqPtr = firstReqPtr;
	if (reqPtr != NULL) {
	    firstReqPtr = reqPtr->nextPtr;
	}
	Ns_MutexUnlock(&reqLock);
	if (reqPtr == NULL) {
	    reqPtr = ns_malloc(sizeof(Request));
	    Tcl_DStringInit(&reqPtr->buffer);
	    reqPtr->headers = Ns_SetCreate(NULL);
	    reqPtr->request = NULL;
	    reqPtr->next = reqPtr->content = NULL;
	    reqPtr->length = reqPtr->avail = 0;
	    reqPtr->coff = reqPtr->woff = reqPtr->roff = 0;
	    reqPtr->leadblanks = 0;
	}
	sockPtr->reqPtr = reqPtr;
    	reqPtr->port = ntohs(sockPtr->sa.sin_port);
	strcpy(reqPtr->peer, ns_inet_ntoa(sockPtr->sa.sin_addr));
    }

    /*
     * On the first read, attempt to read-ahead bufsize bytes.
     * Otherwise, read only the number of bytes left in the
     * content.
     */

    bufPtr = &reqPtr->buffer;
    if (reqPtr->length == 0) {
	nread = sockPtr->drvPtr->bufsize;
    } else {
	nread = reqPtr->length - reqPtr->avail;
    }

    /*
     * Grow the buffer to include space for the next bytes.
     */

    len = bufPtr->length;
    n = len + nread;
    if (n > sockPtr->drvPtr->maxinput) {
        n = sockPtr->drvPtr->maxinput;
        nread = n - len;
        if (nread == 0) {
            return SOCK_ERROR;
        } 
    }
    Tcl_DStringSetLength(bufPtr, len + nread);
    buf.iov_base = bufPtr->string + reqPtr->woff;
    buf.iov_len = nread;
    n = (*sockPtr->drvPtr->proc)(DriverRecv, sock, &buf, 1);
    if (n <= 0) {
	return SOCK_ERROR;
    }
    Tcl_DStringSetLength(bufPtr, len + n);
    reqPtr->woff  += n;
    reqPtr->avail += n;

    /*
     * Scan lines until start of content.
     */

    while (reqPtr->coff == 0) {
	/*
	 * Find the next line.
	 */

	s = bufPtr->string + reqPtr->roff;
	e = strchr(s, '\n');
	if (e == NULL) {
	    /*
	     * Input not yet null terminated - request more.
	     */

	    return SOCK_MORE;
	}

	/*
	 * Update next read pointer to end of this line.
	 */

	cnt = e - s + 1;
	reqPtr->roff  += cnt;
	reqPtr->avail -= cnt;
	if (e > s && e[-1] == '\r') {
	    --e;
	}

	/*
	 * Check for end of headers.
	 */

	if (e == s) {
            /*
             * Look for a blank line on its own prior to any "real" 
             * data. We eat up to 2 of these before closing the
             * connection.
             */

            if (bufPtr->length == 0) {
		if (++reqPtr->leadblanks > 2) {
		    return SOCK_ERROR;
		}
		reqPtr->woff = reqPtr->roff = 0;
		Tcl_DStringSetLength(bufPtr, 0);
		return SOCK_MORE;
            }
	    reqPtr->coff = reqPtr->roff;
	    s = Ns_SetIGet(reqPtr->headers, "content-length");
	    if (s != NULL) {
		reqPtr->length = atoi(s);
                if (reqPtr->length < 0 
                    && reqPtr->length > sockPtr->drvPtr->servPtr->limits.maxpost) {
                    return SOCK_ERROR;
                }
	    }
	} else {
	    save = *e;
	    *e = '\0';
	    if (reqPtr->request == NULL) {
		reqPtr->request = Ns_ParseRequest(s);
		if (reqPtr->request == NULL) {
		    /*
		     * Invalid request.
		     */

		    return SOCK_ERROR;
		}
	    } else if (Ns_ParseHeader(reqPtr->headers, s, Preserve) != NS_OK) {
		/*
		 * Invalid header.
		 */

		return SOCK_ERROR;
	    }
	    *e = save;
	    if (reqPtr->request->version <= 0.0) {
		/*
		 * Pre-HTTP/1.0 request.
		 */

		reqPtr->coff = reqPtr->roff;
	    }
	}
    }

    /*
     * Check if all content has arrived.
     */

    if (reqPtr->coff > 0 && reqPtr->length <= reqPtr->avail) {
	reqPtr->content = bufPtr->string + reqPtr->coff;
	reqPtr->next = reqPtr->content;
	reqPtr->avail = reqPtr->length;

        /*
         * Ensure that there are no 'bonus' crlf chars left visible
         * in the buffer beyond the specified content-length.
         * This happens from some browsers on POST requests.
         */
        if (reqPtr->length > 0) {
            reqPtr->content[reqPtr->length] = '\0';
        }

	return (reqPtr->request ? SOCK_READY : SOCK_ERROR);
    }

    /*
     * Wait for more input.
     */

    return SOCK_MORE;
}

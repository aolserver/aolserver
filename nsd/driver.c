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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/driver.c,v 1.23 2004/07/16 19:49:40 dossy Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Defines for SockRead and SockRun return codes.
 */

#define SOCK_READY		0
#define SOCK_MORE		1
#define SOCK_ERROR		(-1)

/*
 * The following are valid Sock states.
 */

#define SOCK_KEEPALIVE		1
#define SOCK_QUEWAIT		2
#define SOCK_READWAIT		4
#define SOCK_CLOSEWAIT		8
#define SOCK_RUNNING		16

/*
 * The following structure defines a Host header to server mappings.
 */

typedef struct ServerMap {
    NsServer *servPtr;
    char location[8];	/* Location starting with http://. */ 
} ServerMap;

/*
 * The following strucutre defines a pre-queue callback.
 */

typedef struct PreQueue {
    struct PreQueue *nextPtr;
    Ns_PreQueueProc *proc;
    void *arg;
} PreQueue;

/*
 * The following structure defines a pending queue connection callback.
 */

typedef struct QueWait {
    struct QueWait *nextPtr;
    SOCKET sock;
    short events;
    int pidx;
    Ns_Time timeout;
    Ns_QueueWaitProc *proc;
    void *arg;
} QueWait;

/*
 * Static functions defined in this file.
 */

static Ns_ThreadProc DriverThread;
static Ns_ThreadProc ReaderThread;
static Sock *SockAccept(Driver *drvPtr);
static void SockRelease(Driver *drvPtr, Sock *sockPtr);
static void SockClose(Driver *drvPtr, Sock *sockPtr);
static void SockTrigger(Driver *drvPtr);
static int SockRead(Driver *drvPtr, Sock *sockPtr);
static void SockPoll(Driver *drvPtr, SOCKET sock, int events, int *idxPtr,
    	    	 Ns_Time *toPtr, Ns_Time *minPtr);
static int  SetServer(Conn *connPtr);
static Conn *AllocConn(Driver *drvPtr, Sock *sockPtr);
static void FreeConn(Conn *connPtr);
static int RunPreQueues(Conn *connPtr);
static int SockRun(Driver *drvPtr, Sock *sockPtr);
static void ParseAuth(Conn *connPtr, char *auth);
static void ThreadName(Driver *drvPtr, char *name);
static void SockWait(Sock *sockPtr, Ns_Time *nowPtr, int timeout,
			  Sock **listPtrPtr);
#define SockPush(s, sp)		((s)->nextPtr = *(sp), *(sp) = (s))

/*
 * Static variables defined in this file.
 */

static Driver *firstDrvPtr; /* First in list of all drivers. */
static Conn *firstConnPtr;  /* Conn free list. */
static Ns_Mutex connlock;   /* Lock around Conn free list. */
static Tcl_HashTable hosts; /* Host header to server table. */
static ServerMap *defMapPtr;	/* Default server when not found in table. */


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
    Ns_MutexSetName(&connlock, "ns:conns");
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
    char *path, *address, *host, *bindaddr, *defproto;
    int i, n, socktimeout, defport;
    ServerMap *mapPtr;
    Tcl_HashEntry *hPtr;
    Ns_DString ds;
    Ns_Set *set;
    struct in_addr  ia;
    struct hostent *he;
    Driver *drvPtr;
    NsServer *servPtr = NULL;

    if (server != NULL && (servPtr = NsGetServer(server)) == NULL) {
	Ns_Log(Error, "%s: no such server: %s", module, server);
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

    Ns_DStringInit(&ds);
    drvPtr = ns_calloc(1, sizeof(Driver));
    Ns_MutexSetName2(&drvPtr->lock, "ns:drv", module);
    if (ns_sockpair(drvPtr->trigger) != 0) {
	Ns_Fatal("ns_sockpair() failed: %s", ns_sockstrerror(ns_sockerrno));
    }
    drvPtr->server = server;
    drvPtr->module = module;
    drvPtr->name = init->name;
    drvPtr->proc = init->proc;
    drvPtr->arg = init->arg;
    drvPtr->opts = init->opts;
    drvPtr->servPtr = servPtr;
#define _MAX(x,y)	((x) > (y) ? (x) : (y))
#define _MIN(x,y)	((x) > (y) ? (y) : (x))
    if (!Ns_ConfigGetInt(path, "bufsize", &n) || n < 1) { 
        n = 16000; 	/* ~16k */
    }
    drvPtr->bufsize = _MAX(n, 1024);
    if (!Ns_ConfigGetInt(path, "rcvbuf", &n)) {
	n = 0;		/* Use OS default. */
    }
    drvPtr->rcvbuf = _MAX(n, 0);
    if (!Ns_ConfigGetInt(path, "sndbuf", &n)) {
	n = 0;		/* Use OS default. */
    }
    drvPtr->sndbuf = _MAX(n, 0);
    if (!Ns_ConfigGetInt(path, "socktimeout", &n) || n < 1) {
	n = 30;		/* 30 seconds. */
    }
    socktimeout = n;
    if (!Ns_ConfigGetInt(path, "sendwait", &n) || n < 1) {
	n = socktimeout; /* Use previous socktimeout option. */
    }
    drvPtr->sendwait = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "recvwait", &n) || n < 1) {
	n = socktimeout; /* Use previous socktimeout option. */
    }
    drvPtr->recvwait = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "closewait", &n) || n < 0) {
	n = 2;		/* 2 seconds */
    }
    drvPtr->closewait = _MAX(n, 0); /* NB: 0 for no graceful close. */
    if (!Ns_ConfigGetInt(path, "keepwait", &n) || n < 0) {
	n = 30;		/* 30 seconds */
    }
    drvPtr->keepwait = _MAX(n, 0); /* NB: 0 for no keepalive. */
    if (!Ns_ConfigGetInt(path, "backlog", &n) || n < 1) {
	n = 5;		/* 5 pending connections. */
    }
    drvPtr->backlog = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "maxinput", &n) || n < 1) {
	n = 1000 * 1024;	/* 1m. */
    }
    drvPtr->maxinput = _MAX(n, 1024);
    if (!Ns_ConfigGetInt(path, "maxsock", &n) || n < 1) {
	n = 100;		/* 100 total sockets. */
    }
    drvPtr->maxsock = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "maxreaders", &n) || n < 1) {
	n = 10;
    }
    n = _MAX(n, 1);
    n = _MIN(n, drvPtr->maxsock);
    drvPtr->readers = ns_calloc(n, sizeof(Ns_Thread));
    drvPtr->maxreaders = n;
#undef _MAX
#undef _MIN

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
	Ns_DStringVarAppend(&ds, defproto, "://", host, NULL);
	if (drvPtr->port != defport) {
	    Ns_DStringPrintf(&ds, ":%d", drvPtr->port);
	}
	drvPtr->location = Ns_DStringExport(&ds);
    }
    drvPtr->nextPtr = firstDrvPtr;
    firstDrvPtr = drvPtr;

    /*
     * Map Host headers for drivers not bound to servers.
     */

    if (server == NULL) {
	char *vhost;
	path = Ns_ConfigGetPath(NULL, module, "servers", NULL);
	set = Ns_ConfigGetSection(path);
	for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	    server = Ns_SetKey(set, i);
	    vhost = Ns_SetValue(set, i);
	    servPtr = NsGetServer(server);
	    if (servPtr == NULL) {
		Ns_Log(Error, "%s: no such server: %s", module, server);
	    } else {
		hPtr = Tcl_CreateHashEntry(&hosts, vhost, &n);
		if (!n) {
		    Ns_Log(Error, "%s: duplicate host map: %s", module, vhost);
		} else {
		    Ns_DStringVarAppend(&ds, defproto, "://", vhost, NULL);
    		    mapPtr = ns_malloc(sizeof(ServerMap) + ds.length);
    		    mapPtr->servPtr  = servPtr;
		    strcpy(mapPtr->location, ds.string);
		    Ns_DStringTrunc(&ds, 0);
		    if (!defMapPtr && STREQ(host, vhost)) {
			defMapPtr = mapPtr;
		    }
    		    Tcl_SetHashValue(hPtr, mapPtr);
		}
	    }
	}
    }
    Ns_DStringFree(&ds);
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
 * Ns_RegisterPreQueue --
 *
 *	Arrange for given callback to be invoked just before a
 *  	a connection is queued for service.
 
 * Results:
 *	None.
 *
 * Side effects:
 *	Proc will be invoked with arg as data before connection queue.
 *
 *----------------------------------------------------------------------
 */

void
Ns_RegisterPreQueue(char *server, Ns_PreQueueProc *proc, void *arg)
{
    PreQueue *preQuePtr;
    NsServer *servPtr;
    
    servPtr = NsGetServer(server);
    if (servPtr != NULL) {
	preQuePtr = ns_malloc(sizeof(PreQueue));
	preQuePtr->proc = proc;
	preQuePtr->arg = arg;
	preQuePtr->nextPtr = servPtr->firstPreQuePtr;
	servPtr->firstPreQuePtr = preQuePtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_QueueWait --
 *
 *	Arrange for connection to wait for requested I/O on given
 *  	socket.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Proc will be called with given arg and sock as arguements
 *  	when the requested I/O condition is met or the given timeout
 *  	has expired. The connection will not be queued until all
 *  	such callbacks are processed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_QueueWait(Ns_Conn *conn, SOCKET sock, Ns_QueueWaitProc *proc,
    	     void *arg, int when, Ns_Time *timePtr)
{
    Conn *connPtr = (Conn *) conn;
    Driver *drvPtr = connPtr->drvPtr;
    QueWait *queWaitPtr; 

    queWaitPtr = drvPtr->freeQueWaitPtr;
    if (queWaitPtr != NULL) {
	drvPtr->freeQueWaitPtr = queWaitPtr->nextPtr;
    } else {
	queWaitPtr = ns_malloc(sizeof(QueWait));
    }
    queWaitPtr->proc = proc;
    queWaitPtr->arg = arg;
    queWaitPtr->sock = sock;
    queWaitPtr->events = 0;
    if (when & NS_SOCK_READ) {
	queWaitPtr->events |= POLLIN;
    }
    if (when & NS_SOCK_WRITE) {
	queWaitPtr->events |= POLLOUT;
    }
    queWaitPtr->nextPtr = connPtr->queWaitPtr;
    connPtr->queWaitPtr = queWaitPtr;
    queWaitPtr->timeout = *timePtr;
}



/*
 *----------------------------------------------------------------------
 *
 * NsStartDrivers --
 *
 *	Start all driver threads.
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

    drvPtr = firstDrvPtr;
    while (drvPtr != NULL) {
    	Ns_Log(Notice, "driver: starting: %s", drvPtr->module);
	Ns_ThreadCreate(DriverThread, drvPtr, 0, &drvPtr->thread);
	drvPtr = drvPtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitDriversStartup --
 *
 *	Wait for startup of all DriverThreads.  Current behavior is to
 *	just wait until all sockets are bound to.
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
NsWaitDriversStartup(void)
{
    Driver *drvPtr = firstDrvPtr;
    int status = NS_OK;
    Ns_Time timeout;

    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, 10, 0);

    while (drvPtr != NULL) {
	Ns_MutexLock(&drvPtr->lock);
	while (!drvPtr->started && status == NS_OK) {
	    status = Ns_CondTimedWait(&drvPtr->cond, &drvPtr->lock, &timeout);
	}
	Ns_MutexUnlock(&drvPtr->lock);
	if (status != NS_OK) {
	    Ns_Log(Warning, "driver: startup timeout: %s", drvPtr->module);
	}
	drvPtr = drvPtr->nextPtr;
    }
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
    Driver *drvPtr = firstDrvPtr;
    
    while (drvPtr != NULL) {
	Ns_MutexLock(&drvPtr->lock);
    	Ns_Log(Notice, "driver: stopping: %s", drvPtr->module);
	drvPtr->shutdown = 1;
	SockTrigger(drvPtr);
	Ns_CondBroadcast(&drvPtr->cond);
	Ns_MutexUnlock(&drvPtr->lock);
	drvPtr = drvPtr->nextPtr;
    }
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
    Driver *drvPtr = firstDrvPtr;
    int status = NS_OK;

    while (drvPtr != NULL) {
	Ns_MutexLock(&drvPtr->lock);
	while (!drvPtr->stopped && status == NS_OK) {
	    status = Ns_CondTimedWait(&drvPtr->cond, &drvPtr->lock, toPtr);
	}
	Ns_MutexUnlock(&drvPtr->lock);
	if (status != NS_OK) {
	    Ns_Log(Warning, "driver: shutdown timeout: %s", drvPtr->module);
	} else {
	    Ns_Log(Notice, "driver: stopped: %s", drvPtr->module);
	    Ns_ThreadJoin(&drvPtr->thread, NULL);
	    drvPtr->thread = NULL;
	    ns_sockclose(drvPtr->trigger[0]);
	    ns_sockclose(drvPtr->trigger[1]);
	}
	drvPtr = drvPtr->nextPtr;
    }
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
    Driver *drvPtr = sockPtr->drvPtr;
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    int trigger = 0;

    if (keep && drvPtr->keepwait > 0
	    && (*drvPtr->proc)(DriverKeep, sock, NULL, 0) == 0) {
    	sockPtr->state = SOCK_KEEPALIVE;
    } else {
    	sockPtr->state = SOCK_CLOSEWAIT;
	(void) (*drvPtr->proc)(DriverClose, sock, NULL, 0);
    }
    Ns_MutexLock(&drvPtr->lock);
    if (drvPtr->firstClosePtr == NULL) {
	trigger = 1;
    }
    sockPtr->nextPtr = drvPtr->firstClosePtr;
    drvPtr->firstClosePtr = sockPtr;
    Ns_MutexUnlock(&drvPtr->lock);
    if (trigger) {
	SockTrigger(drvPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeConn --
 *
 *	Free a Conn structure after processing, called by a the
 *  	conn threads.
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
NsFreeConn(Conn *connPtr)
{
    /*
     * Close the socket if not already closed by the connection proc.
     */
     
    if (connPtr->sockPtr != NULL) {
        SockClose(connPtr->drvPtr, connPtr->sockPtr);
        connPtr->sockPtr = NULL;
    }
    
    FreeConn(connPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * DriverThread --
 *
 *	Main communication driver thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Connections are accepted and processed.
 *
 *----------------------------------------------------------------------
 */

static void
DriverThread(void *arg)
{
    Driver *drvPtr = (Driver *) arg;
    int n, stopping, pollto;
    Sock *sockPtr, *nextPtr;
    QueWait *queWaitPtr;
    Conn *connPtr;
    char drain[1024];
    Ns_Time timeout, diff, maxtimeout;
    Sock *queuePtr = NULL;	/* Sock's ready to queue for processing. */
    Sock *preqPtr = NULL;	/* Sock's ready for pre-queue callbacks. */
    Sock *readPtr = NULL;	/* Sock's ready reader thread. */
    Sock *closePtr = NULL;	/* Sock's ready for close or keepalive wait. */
    Sock *waitPtr = NULL;	/* Sock's waiting for I/O events. */

    ThreadName(drvPtr, "driver");
    
    /*
     * Create the listen socket.
     */
 
    drvPtr->sock = Ns_SockListenEx(drvPtr->bindaddr, drvPtr->port,
				   drvPtr->backlog);
    if (drvPtr->sock == INVALID_SOCKET) {
	Ns_Fatal("%s: failed to listen on %s:%d: %s", drvPtr->name,
	    drvPtr->address, drvPtr->port, ns_sockstrerror(ns_sockerrno));
    }
    Ns_SockSetNonBlocking(drvPtr->sock);
    Ns_MutexLock(&drvPtr->lock);
    drvPtr->started = 1;
    Ns_CondBroadcast(&drvPtr->cond);
    Ns_MutexUnlock(&drvPtr->lock);

    /*
     * Pre-allocate Sock structures.
     */
          
    sockPtr = ns_malloc(sizeof(Sock) * drvPtr->maxsock);
    for (n = 0; n < drvPtr->maxsock; ++n) {
        sockPtr->nextPtr = drvPtr->firstFreePtr;
        drvPtr->firstFreePtr = sockPtr;
	++sockPtr;
    }
    
    /*
     * Loop forever until signalled to shutdown and all
     * connections are complete and gracefully closed.
     */

    Ns_Log(Notice, "%s: listening on %s:%d",
		drvPtr->name, drvPtr->address, drvPtr->port);
    maxtimeout.sec  = INT_MAX;
    maxtimeout.usec = LONG_MAX;
    Ns_GetTime(&drvPtr->now);
    stopping = 0;
    drvPtr->maxfds = 100;
    drvPtr->pfds = ns_malloc(sizeof(struct pollfd) * drvPtr->maxfds);
    drvPtr->pfds[0].fd = drvPtr->trigger[0];
    drvPtr->pfds[0].events = POLLIN;
    drvPtr->pfds[1].fd = drvPtr->sock;
    
    while (!stopping || drvPtr->nactive) {

	/*
	 * Poll the trigger pipe and, if a Sock strucutre is available,
	 * the listen socket.
	 */

	drvPtr->pfds[1].events = (drvPtr->firstFreePtr ? POLLIN : 0);
	drvPtr->pfds[1].revents = drvPtr->pfds[0].revents = 0;
	drvPtr->nfds = 2;

	/*
	 * Poll waiting sockets, determining the minimum relative timeout.
	 */

	sockPtr = waitPtr;
	if (sockPtr == NULL) {
	    pollto = -1;
	} else {
	    timeout = maxtimeout;
	    while (sockPtr != NULL) {
    		SockPoll(drvPtr, sockPtr->sock, POLLIN,
		    	 &sockPtr->pidx, &sockPtr->timeout, &timeout);
		if (sockPtr->connPtr != NULL) {
		    queWaitPtr = sockPtr->connPtr->queWaitPtr;
		    while (queWaitPtr != NULL) {
		        SockPoll(drvPtr, queWaitPtr->sock, queWaitPtr->events,
			 	 &queWaitPtr->pidx, &queWaitPtr->timeout,
				 &timeout);
		    	queWaitPtr = queWaitPtr->nextPtr;
		    }
		}
		sockPtr = sockPtr->nextPtr;
	    }
	    if (Ns_DiffTime(&timeout, &drvPtr->now, &diff) > 0)  {
		pollto = diff.sec * 1000 + diff.usec / 1000;
	    } else {
		pollto = 0;
	    }
	}

	/*
	 * Poll, drain the trigger pipe if necessary, and get current time.
	 */

	do {
	    n = poll(drvPtr->pfds, drvPtr->nfds, pollto);
	} while (n < 0  && errno == EINTR);
	if (n < 0) {
	    Ns_Fatal("driver: poll() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}
	if ((drvPtr->pfds[0].revents & POLLIN)
		&& recv(drvPtr->trigger[0], drain, sizeof(drain), 0) <= 0) {
	    Ns_Fatal("driver: trigger recv() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}
	Ns_GetTime(&drvPtr->now);

	/*
	 * Update the current time and drain and/or release any
	 * closing sockets.
	 */

	sockPtr = waitPtr;
	waitPtr = NULL;
	while (sockPtr != NULL) {
	    nextPtr = sockPtr->nextPtr;
	    switch (sockPtr->state) {
	    case SOCK_CLOSEWAIT:
	    	if (drvPtr->pfds[sockPtr->pidx].revents & POLLIN) {
		    n = recv(sockPtr->sock, drain, sizeof(drain), 0);
		    if (n <= 0) {
		        /* NB: Timeout Sock on end-of-file or error. */
		        sockPtr->timeout = drvPtr->now;
		    }
	    	}
	    	if (Ns_DiffTime(&sockPtr->timeout, &drvPtr->now, NULL) <= 0) {
		    SockRelease(drvPtr, sockPtr);
	    	} else {
		    SockPush(sockPtr, &waitPtr);
	    	}
		break;

	    case SOCK_QUEWAIT:
	    	switch (SockRun(drvPtr, sockPtr)) {
	    	case SOCK_READY:
		    SockPush(sockPtr, &queuePtr);
		    break;
	    	case SOCK_MORE:
		    SockPush(sockPtr, &waitPtr);    /* Still pending. */
		    break;
	    	default:
		    SockRelease(drvPtr, sockPtr);
		    break;
		}
		break;

	    case SOCK_READWAIT:
	    	if (!(drvPtr->pfds[sockPtr->pidx].revents & POLLIN)) {
		    /* NB: Timeout or wait longer for input. */
		    if (Ns_DiffTime(&sockPtr->timeout, &drvPtr->now, NULL) <= 0) {
		    	SockRelease(drvPtr, sockPtr);
		    } else {
		    	SockPush(sockPtr, &waitPtr);
		    }
	    	} else {
		    /*
		     * With input available, read directly or queue for
		     * reader thread.
		     */

                    if (sockPtr->connPtr == NULL) {
                    	sockPtr->connPtr = AllocConn(drvPtr, sockPtr);
                    }                
		    if (!(drvPtr->opts & NS_DRIVER_ASYNC)) {
			SockPush(sockPtr, &readPtr);
		    } else {
		    	switch (SockRead(drvPtr, sockPtr)) {
		    	case SOCK_MORE:
		    	    SockWait(sockPtr, &drvPtr->now,
					  drvPtr->recvwait, &waitPtr);
		    	    break;
		    	case SOCK_READY:
			    SockPush(sockPtr, &preqPtr);
		    	    break;
		    	default:
			    /* Release socket on read error. */
		    	    SockRelease(drvPtr, sockPtr);
		    	    break;
			}
		    }
		}
		break;

	    default:
		Ns_Fatal("impossible state");
		break;
	    }
	    sockPtr = nextPtr;
	}

	Ns_MutexLock(&drvPtr->lock);

	/*
	 * Move Sock's to read queue, creating or signaling reader threads.
	 */

	n = 0;
	while ((sockPtr = readPtr) != NULL) {
	    readPtr = sockPtr->nextPtr;
	    sockPtr->nextPtr = drvPtr->firstReadPtr;
	    drvPtr->firstReadPtr = sockPtr;
	    ++n;
	}
	while (n > drvPtr->idlereaders
		&& drvPtr->nreaders < drvPtr->maxreaders) {
	    Ns_ThreadCreate(ReaderThread, drvPtr, 0,
			    &drvPtr->readers[drvPtr->nreaders]);
	    ++drvPtr->nreaders;
	    ++drvPtr->idlereaders;
	    --n;
	}
	if (n > 0) {
	    Ns_CondSignal(&drvPtr->cond);
	}

	/*
	 * Get Sock's from readers ready for pre-queue callbacks.
	 */

	while ((sockPtr = drvPtr->firstReadyPtr) != NULL) {
	    drvPtr->firstReadyPtr = sockPtr->nextPtr;
	    SockPush(sockPtr, &preqPtr);
	}

	/*
	 * Get the list of closing Sock's.
	 */

	closePtr = drvPtr->firstClosePtr;
	drvPtr->firstClosePtr = NULL;
	stopping = drvPtr->shutdown;

	Ns_MutexUnlock(&drvPtr->lock);

	/*
	 * Update the timeout for each closing socket and add to the
	 * close list if some data has been read from the socket
	 * (i.e., it's not a keep-alive connection).
	 */

	while ((sockPtr = closePtr) != NULL) {
	    closePtr = sockPtr->nextPtr;
	    switch (sockPtr->state) {
	    case SOCK_KEEPALIVE:
	    	sockPtr->state = SOCK_READWAIT;
		SockWait(sockPtr, &drvPtr->now, drvPtr->keepwait, &waitPtr);
		break;
	    case SOCK_CLOSEWAIT:
		if (drvPtr->closewait == 0 || shutdown(sockPtr->sock, 1) != 0) {
		    SockRelease(drvPtr, sockPtr);
		} else {
		    SockWait(sockPtr, &drvPtr->now, drvPtr->closewait, &waitPtr);
		}
	    }
	}

	/*
	 * Process pre-queue sockets.
	 */

	while ((sockPtr = preqPtr) != NULL) {
	    preqPtr = sockPtr->nextPtr;
	    SetServer(sockPtr->connPtr);
	    sockPtr->connPtr->times.ready = drvPtr->now;
	    if (RunPreQueues(sockPtr->connPtr)) {
		/* NB: Sock timeout now longest que wait. */
		sockPtr->timeout = maxtimeout;
		sockPtr->state = SOCK_QUEWAIT;
		SockPush(sockPtr, &waitPtr);
	    } else {
		SockPush(sockPtr, &queuePtr);
	    }
	}

	/*
	 * Queue connections now ready for processing.
	 */

	while ((sockPtr = queuePtr) != NULL) {
	    queuePtr = sockPtr->nextPtr;
	    connPtr = sockPtr->connPtr;
	    /* NB: Sock no longer responsible for Conn. */
	    sockPtr->connPtr = NULL;
	    sockPtr->state = SOCK_RUNNING;
	    connPtr->times.queue = drvPtr->now;
	    NsQueueConn(connPtr);
	}

	/*
	 * Attempt to accept new sockets.
	 */

  	if ((drvPtr->pfds[1].revents & POLLIN)
	    	&& ((sockPtr = SockAccept(drvPtr)) != NULL)) {
	    SockWait(sockPtr, &drvPtr->now, drvPtr->recvwait, &waitPtr);
	}

    }

    /*
     * Close the listen socket, wait for reader threads, and exit.
     */

    ns_sockclose(drvPtr->sock);
    drvPtr->sock = INVALID_SOCKET;
    while (drvPtr->nreaders > 0) {
    	--drvPtr->nreaders;
	Ns_ThreadJoin(&drvPtr->readers[drvPtr->nreaders], NULL);
    }
    Ns_MutexLock(&drvPtr->lock);
    drvPtr->stopped = 1;
    Ns_CondBroadcast(&drvPtr->cond);
    Ns_MutexUnlock(&drvPtr->lock);
    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * SockPoll --
 *
 *	Arrange for socket to be monitored by the given driver
 *	thread up to given timeout.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sock fd will be monitored on next spin of driver thread.
 *
 *----------------------------------------------------------------------
 */

static void
SockPoll(Driver *drvPtr, SOCKET sock, int events, int *idxPtr, Ns_Time *toPtr,
	Ns_Time *minPtr)
{
    /*
     * Grow the pfds array if necessary.
     */

    if (drvPtr->nfds >= drvPtr->maxfds) {
	drvPtr->maxfds += 100;
	drvPtr->pfds = ns_realloc(drvPtr->pfds,
				  drvPtr->maxfds * sizeof(struct pollfd));
    }

    /*
     * Set the next pollfd struct with this socket.
     */

    drvPtr->pfds[drvPtr->nfds].fd = sock;
    drvPtr->pfds[drvPtr->nfds].events = events;
    drvPtr->pfds[drvPtr->nfds].revents = 0;
    *idxPtr = drvPtr->nfds++;

    /* 
     * Check for new minimum timeout.
     */

    if (Ns_DiffTime(toPtr, minPtr, NULL) < 0) {
    	*minPtr = *toPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SockWait --
 *
 *	Update Sock timeout and queue on given list.
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
SockWait(Sock *sockPtr, Ns_Time *nowPtr, int timeout, Sock **listPtrPtr)
{
    sockPtr->timeout = *nowPtr;
    Ns_IncrTime(&sockPtr->timeout, timeout, 0);
    SockPush(sockPtr, listPtrPtr);
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
    Sock *sockPtr = drvPtr->firstFreePtr;
    int slen;

    /*
     * Accept the new connection.
     */

    slen = sizeof(struct sockaddr_in);
    sockPtr->connPtr = NULL;
    sockPtr->drvPtr = drvPtr;
    sockPtr->state = SOCK_READWAIT;
    sockPtr->arg = NULL;
    sockPtr->acceptTime = drvPtr->now;
    sockPtr->sock = Ns_SockAccept(drvPtr->sock,
				  (struct sockaddr *) &sockPtr->sa, &slen);
    if (sockPtr->sock == INVALID_SOCKET) {
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
    ++drvPtr->nactive;
    drvPtr->firstFreePtr = sockPtr->nextPtr;
    sockPtr->nextPtr = NULL;
    return sockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SockClose --
 *
 *	Close a socket.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sock structure is returned to driver free list.
 *
 *----------------------------------------------------------------------
 */

static void
SockClose(Driver *drvPtr, Sock *sockPtr)
{
    --drvPtr->nactive;
    ns_sockclose(sockPtr->sock);
    sockPtr->sock = INVALID_SOCKET;
    sockPtr->nextPtr = drvPtr->firstFreePtr;
    drvPtr->firstFreePtr = sockPtr;
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
SockRelease(Driver *drvPtr, Sock *sockPtr)
{
    if (sockPtr->connPtr != NULL) {
	FreeConn(sockPtr->connPtr);
	sockPtr->connPtr = NULL;
    }
    SockClose(drvPtr, sockPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SockTrigger --
 *
 *	Wakeup driver from blocking poll().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Given driver will wakeup.
 *
 *----------------------------------------------------------------------
 */

static void
SockTrigger(Driver *drvPtr)
{
    if (send(drvPtr->trigger[1], "", 1, 0) != 1) {
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
 *	either be called repeatedly from a driver thread during
 *	an async read-ahead or in a blocking loop in ReaderThread.
 *
 * Results:
 *	SOCK_READY:	Conn is ready for processing.
 *	SOCK_MORE:	More input is required.
 *	SOCK_ERROR:	Client drop or timeout.
 *
 * Side effects:
 *	The client request will be read for use by the
 *	connection thread.  Also, before returning SOCK_READY,
 *	the next byte to read mark and bytes available are set
 *	to the beginning of the content, just beyond the headers.
 *
 *----------------------------------------------------------------------
 */

static int
SockRead(Driver *drvPtr, Sock *sockPtr)
{
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    Ns_Request *request;
    Conn *connPtr = sockPtr->connPtr;
    struct iovec buf;
    Tcl_DString *bufPtr;
    char *s, *e, *auth, save;
    int   cnt, len, nread, n;

    /*
     * On the first read, attempt to read-ahead bufsize bytes.
     * Otherwise, read only the number of bytes left in the
     * content.
     */

    bufPtr = &connPtr->ibuf;
    if (connPtr->contentLength == 0) {
	nread = drvPtr->bufsize;
    } else {
	nread = connPtr->contentLength - connPtr->avail;
    }

    /*
     * Grow the buffer to include space for the next bytes.
     */

    len = bufPtr->length;
    n = len + nread;
    if (n > drvPtr->maxinput) {
	n = drvPtr->maxinput;
	nread = n - len;
	if (nread == 0) {
	    return SOCK_ERROR;
	}
    }
    Tcl_DStringSetLength(bufPtr, n);
    buf.iov_base = bufPtr->string + connPtr->woff;
    buf.iov_len = nread;
    n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
    if (n <= 0) {
	return SOCK_ERROR;
    }
    Tcl_DStringSetLength(bufPtr, len + n);
    connPtr->woff  += n;
    connPtr->avail += n;
    connPtr->times.read = drvPtr->now;

    /*
     * Scan lines until start of content.
     */

    while (connPtr->coff == 0) {
	/*
	 * Find the next line.
	 */

	s = bufPtr->string + connPtr->roff;
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
	connPtr->roff  += cnt;
	connPtr->avail -= cnt;
	if (e > s && e[-1] == '\r') {
	    --e;
	}

	/*
	 * Check for end of headers.
	 */

	if (e == s) {
	    connPtr->coff = connPtr->roff;
	    s = Ns_SetIGet(connPtr->headers, "content-length");
	    if (s != NULL) {
		connPtr->contentLength = atoi(s);
		if (connPtr->contentLength < 0) {
		    return SOCK_ERROR;
		}
	    }
	} else {
	    save = *e;
	    *e = '\0';
	    request = connPtr->request;
	    if (request == NULL) {
		request = connPtr->request = Ns_ParseRequest(s);
		if (request == NULL) {
		    /*
		     * Invalid request.
		     */

		    return SOCK_ERROR;
		}
	    } else if (Ns_ParseHeader(connPtr->headers, s, Preserve) != NS_OK) {
		/*
		 * Invalid header.
		 */

		return SOCK_ERROR;
	    }
	    *e = save;
	    if (request->version < 1.0) {
		/*
		 * Pre-HTTP/1.0 request.
		 */

		connPtr->coff = connPtr->roff;
	    	connPtr->flags |= NS_CONN_SKIPHDRS;
	    }
	    if (request->method && STREQ(request->method, "HEAD")) {
		connPtr->flags |= NS_CONN_SKIPBODY;
	    }
	    auth = Ns_SetIGet(connPtr->headers, "authorization");
	    if (auth != NULL) {
		ParseAuth(connPtr, auth);
	    }
	}
    }

    /*
     * Check if all content has arrived.
     */

    if (connPtr->coff > 0 && connPtr->contentLength <= connPtr->avail) {
	connPtr->content = bufPtr->string + connPtr->coff;
	connPtr->next = connPtr->content;
	connPtr->avail = connPtr->contentLength;

        /*
         * Ensure that there are no 'bonus' crlf chars left visible
         * in the buffer beyond the specified content-length.
         * This happens from some browsers on POST requests.
         */
        if (connPtr->contentLength > 0) {
            connPtr->content[connPtr->contentLength] = '\0';
        }

	return (connPtr->request ? SOCK_READY : SOCK_ERROR);
    }

    /*
     * Wait for more input.
     */

    return SOCK_MORE;
}


/*
 *----------------------------------------------------------------------
 *
 * SockRun --
 *
 *	Run Sock queue wait callbacks.
 *
 * Results:
 *	SOCK_READY:	Conn is ready for processing.
 *	SOCK_MORE:	More callbacks are pending.
 *	SOCK_ERROR:	Client drop or timeout.
 *
 * Side effects:
 *	Depends on callbacks which may, e.g., register more queue wait
 *      callbacks. 
 *
 *----------------------------------------------------------------------
 */

static int
SockRun(Driver *drvPtr, Sock *sockPtr)
{
    Conn *connPtr = sockPtr->connPtr;
    QueWait *queWaitPtr, *nextPtr;
    int revents, why, dropped;

    if ((drvPtr->pfds[sockPtr->pidx].revents & POLLIN)
	    || Ns_DiffTime(&sockPtr->timeout, &drvPtr->now, NULL) <= 0) {
	dropped = 1;
    } else {
	dropped = 0;
    }
    queWaitPtr = connPtr->queWaitPtr;
    connPtr->queWaitPtr = NULL;
    while (queWaitPtr != NULL) {
	nextPtr = queWaitPtr->nextPtr;
	if (dropped) {
	    why = NS_SOCK_DROP;
	} else {
	    why = 0;
	    revents = drvPtr->pfds[queWaitPtr->pidx].revents;
	    if (revents & POLLIN) {
	        why |= NS_SOCK_READ;
	    }
	    if (revents & POLLOUT) {
	        why |= NS_SOCK_WRITE;
	    }
	}
	if (why == 0
		&& Ns_DiffTime(&queWaitPtr->timeout, &drvPtr->now, NULL) > 0) {
	    queWaitPtr->nextPtr = connPtr->queWaitPtr;
	    connPtr->queWaitPtr = queWaitPtr;
	} else {
	    (*queWaitPtr->proc)((Ns_Conn *) connPtr, queWaitPtr->sock,
				    queWaitPtr->arg, why);
	     queWaitPtr->nextPtr = drvPtr->freeQueWaitPtr;
	     drvPtr->freeQueWaitPtr = queWaitPtr;
	}
	queWaitPtr = nextPtr;
    }
    if (dropped) {
	return SOCK_ERROR;
    } else if (connPtr->queWaitPtr != NULL) {
	return SOCK_MORE;
    } else {
    	return SOCK_READY;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RunPreQueues --
 *
 *	Invoke the pre-queue callbacks.
 *
 * Results:
 *	1 if any queue wait callbacks are set, 0 otherwise.
 *
 * Side effects:
 *	Depends on callbacks which may, for example, register one
 *      or more queue wait callbacks.
 *
 *----------------------------------------------------------------------
 */

static int
RunPreQueues(Conn *connPtr)
{
    PreQueue *preQuePtr = NULL;

    if (connPtr->servPtr != NULL) {
    	preQuePtr = connPtr->servPtr->firstPreQuePtr;
    }
    while (preQuePtr != NULL) {
	(*preQuePtr->proc)((Ns_Conn *) connPtr, preQuePtr->arg);
	preQuePtr = preQuePtr->nextPtr;
    }
    return (connPtr->queWaitPtr ? 1 : 0);
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
 *	Will update connPtr's servPtr, server, and location.
 *
 *----------------------------------------------------------------------
 */

static int
SetServer(Conn *connPtr)
{
    ServerMap *mapPtr = NULL;
    Tcl_HashEntry *hPtr;
    char *host;
    int status = 1;

    connPtr->servPtr = connPtr->drvPtr->servPtr;
    connPtr->location = connPtr->drvPtr->location;
    host = Ns_SetIGet(connPtr->headers, "Host");
    if (!host && connPtr->request->version >= 1.1) {
	status = 0;
    }
    if (connPtr->servPtr == NULL) {
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
	    connPtr->servPtr = mapPtr->servPtr;
	    connPtr->location = mapPtr->location;
	}
        if (connPtr->servPtr == NULL) {
            status = 0;
        }
    }
    connPtr->server = connPtr->servPtr->server;
    connPtr->encoding = connPtr->servPtr->encoding.outputEncoding;
    connPtr->urlEncoding = connPtr->servPtr->encoding.urlEncoding;
    if (!status) {
	ns_free(connPtr->request->method);
	connPtr->request->method = ns_strdup("BAD");
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ParseAuth --
 *
 *	Parse an HTTP authorization string.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May set the authPasswd and authUser connection pointers.
 *
 *----------------------------------------------------------------------
 */

static void
ParseAuth(Conn *connPtr, char *auth)
{
    register char *p, *q;
    int            n;
    char    	   save;
    
    p = auth;
    while (*p != '\0' && !isspace(UCHAR(*p))) {
        ++p;
    }
    if (*p != '\0') {
    	save = *p;
	*p = '\0';
        if (STRIEQ(auth, "Basic")) {
    	    q = p + 1;
            while (*q != '\0' && isspace(UCHAR(*q))) {
                ++q;
            }
	    n = strlen(q) + 3;
	    connPtr->authUser = ns_malloc((size_t) n);
            n = Ns_HtuuDecode(q, (unsigned char *) connPtr->authUser, n);
            connPtr->authUser[n] = '\0';
            q = strchr(connPtr->authUser, ':');
            if (q != NULL) {
                *q++ = '\0';
                connPtr->authPasswd = q;
            }
        }
	*p = save;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AllocConn --
 *
 *	Allocate a Conn structure and basic I/O related members. 
 *
 * Results:
 *	Pointer to new Conn. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Conn *
AllocConn(Driver *drvPtr, Sock *sockPtr)
{
    Conn *connPtr;
    static int nextid = 0;
    int id;
    
    Ns_MutexLock(&connlock);
    connPtr = firstConnPtr;
    if (connPtr != NULL) {
        firstConnPtr = connPtr->nextPtr;
    }
    id = nextid++;
    Ns_MutexUnlock(&connlock);
    if (connPtr == NULL) {
        connPtr = ns_malloc(sizeof(Conn));
    }
    memset(connPtr, 0, sizeof(Conn));
    Tcl_DStringInit(&connPtr->ibuf);
    Tcl_DStringInit(&connPtr->obuf);
    Tcl_InitHashTable(&connPtr->files, TCL_STRING_KEYS);
    connPtr->headers = Ns_SetCreate(NULL);
    connPtr->outputheaders = Ns_SetCreate(NULL);
    connPtr->id = id;
    sprintf(connPtr->idstr, "cns%d", connPtr->id);
    connPtr->port = ntohs(sockPtr->sa.sin_port);
    strcpy(connPtr->peer, ns_inet_ntoa(sockPtr->sa.sin_addr));
    connPtr->times.accept = sockPtr->acceptTime;
    connPtr->sockPtr = sockPtr;
    connPtr->drvPtr = drvPtr;
    return connPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeConn --
 *
 *	Free a Conn structure and members allocated by AllocConn.
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
FreeConn(Conn *connPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    FormFile	  *filePtr;
    
    NsClsCleanup(connPtr);
    if (connPtr->authUser != NULL) {
	ns_free(connPtr->authUser);
	connPtr->authUser = connPtr->authPasswd = NULL;
    }
    if (connPtr->request != NULL) {
    	Ns_FreeRequest(connPtr->request);
        connPtr->request = NULL;
    }
    Ns_SetFree(connPtr->headers);
    Ns_SetFree(connPtr->outputheaders);
    connPtr->headers = connPtr->outputheaders = NULL;
    if (connPtr->query != NULL) {
	Ns_SetFree(connPtr->query);
	connPtr->query = NULL;
    }
    Ns_DStringFree(&connPtr->ibuf);
    Ns_DStringFree(&connPtr->obuf);
    hPtr = Tcl_FirstHashEntry(&connPtr->files, &search);
    while (hPtr != NULL) {
	filePtr = Tcl_GetHashValue(hPtr);
	Ns_SetFree(filePtr->hdrs);
	ns_free(filePtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&connPtr->files);
    Ns_GetTime(&connPtr->times.done);
    Ns_MutexLock(&connlock);
    connPtr->nextPtr = firstConnPtr;
    firstConnPtr = connPtr;
    Ns_MutexUnlock(&connlock);
}


/*
 *----------------------------------------------------------------------
 *
 * ReaderThread --
 *
 *	Thread main for blocking connection reads.
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
ReaderThread(void *arg)
{
    Driver *drvPtr = arg;
    Sock *sockPtr;
    int status;

    ThreadName(drvPtr, "reader");
    Ns_MutexLock(&drvPtr->lock);
    while (1) {
    	while (!drvPtr->shutdown && drvPtr->firstReadPtr == NULL) {
	    Ns_CondWait(&drvPtr->cond, &drvPtr->lock);
    	}
	sockPtr = drvPtr->firstReadPtr;
	if (sockPtr == NULL) {
	    break;
	}
	drvPtr->firstReadPtr = sockPtr->nextPtr;
	if (drvPtr->firstReadPtr != NULL) {
	    Ns_CondSignal(&drvPtr->cond);
	}
	--drvPtr->idlereaders;
	Ns_MutexUnlock(&drvPtr->lock);

	/*
	 * Read the connection until complete.
	 */

	do {
	    status = SockRead(drvPtr, sockPtr);
	} while (status == SOCK_MORE);

	Ns_MutexLock(&drvPtr->lock);
	if (status == SOCK_READY) {
	    sockPtr->nextPtr = drvPtr->firstReadyPtr;
	    drvPtr->firstReadyPtr = sockPtr;
	} else {
    	    sockPtr->nextPtr = drvPtr->firstClosePtr;
    	    drvPtr->firstClosePtr = sockPtr;
	}
	SockTrigger(drvPtr);
	++drvPtr->idlereaders;
    }
    Ns_MutexUnlock(&drvPtr->lock);
    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * ThreadName --
 *
 *	Set name of driver or reader thread.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Thread name will show up in log messages.
 *
 *----------------------------------------------------------------------
 */

static void
ThreadName(Driver *drvPtr, char *name)
{
    Tcl_DString ds;

    Tcl_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "-", drvPtr->module, ":", name, "-", NULL);
    Ns_ThreadSetName(ds.string);
    Tcl_DStringFree(&ds);
    Ns_Log(Notice, "starting");
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/driver.c,v 1.26 2004/07/30 12:38:46 dossy Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following are result codes for SockRead, RunPreQueues, and RunQueWaits.
 */

enum {
    STATUS_READY,
    STATUS_PENDING,
    STATUS_ERROR
};

/*
 * The following are valid Sock states.
 */

enum {
    SOCK_READWAIT,
    SOCK_QUEWAIT,
    SOCK_RUNWAIT,
    SOCK_CLOSEWAIT,
    SOCK_PENDING,
    SOCK_READY,
    SOCK_RUNNING,
};

/*
 * The following are valid driver state flags.
 */

#define DRIVER_STARTED     1
#define DRIVER_STOPPED     2
#define DRIVER_SHUTDOWN    4
#define DRIVER_FAILED      8

/*
 * The following structure defines a Host header to server mappings.
 */

typedef struct ServerMap {
    NsServer *servPtr;
    char location[8];	/* Location starting with http://. */ 
} ServerMap;

/*
 * The following structure defines a pre-queue callback.
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
static void TriggerDriver(Driver *drvPtr);
static Sock *SockAccept(SOCKET lsock, Driver *drvPtr);
static void SockClose(Driver *drvPtr, Sock *sockPtr);
static int SockRead(Driver *drvPtr, Sock *sockPtr);
static int Poll(Driver *drvPtr, SOCKET sock, int events, Ns_Time *timeoutPtr);
static int SetupConn(Conn *connPtr);
static Conn *AllocConn(Driver *drvPtr, Sock *sockPtr);
static void FreeConn(Driver *drvPtr, Conn *connPtr);
static int RunPreQueues(Conn *connPtr);
static int RunQueWaits(Driver *drvPtr, Sock *sockPtr);
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
static Ns_Time maxtimeout = { INT_MAX, LONG_MAX };


/*
 *----------------------------------------------------------------------
 *
 * NsInitDrivers --
 *
 *      Init communication drivers data structures.
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
    Sock *sockPtr;
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
            he = gethostbyaddr(he->h_addr, (unsigned) he->h_length,
                    he->h_addrtype);
	}

	/*
	 * If the lookup suceeded, use the first address in host entry list.
	 */

        if (he == NULL || he->h_name == NULL) {
            Ns_Log(Error, "%s: could not resolve %s: %s", module,
                    host ? host : Ns_InfoHostname(),
                    ns_sockstrerror(ns_sockerrno));
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
    if (!Ns_ConfigGetInt(path, "backlog", &n) || n < 1) {
	n = 5;		/* 5 pending connections. */
    }
    drvPtr->backlog = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "maxsock", &n) || n < 1) {
        n = 100;        /* 100 total open sockets. */
    }
    drvPtr->maxsock = _MAX(n, 1);
    if (!Ns_ConfigGetInt(path, "maxline", &n) || n < 1) {
        n = 4 * 1024;   /* 4k per-line limit. */
    }
    drvPtr->maxline = _MAX(n, 256);
    if (!Ns_ConfigGetInt(path, "maxheader", &n) || n < 1) {
        n = 32 * 1024;  /* 32k total header limit. */
    }
    drvPtr->maxheader = _MAX(n, 1024);
    if (!Ns_ConfigGetInt(path, "maxinput", &n) || n < 1) {
        n = 1000 * 1024;/* 1m in-memory limit including request & headers. */
    }
    drvPtr->maxinput = _MAX(n, 2024);
    if (!Ns_ConfigGetInt(path, "closewait", &n) || n < 0) {
        n = 2;          /* 2 second wait for graceful client close. */
    }
    drvPtr->closewait = _MAX(n, 0); /* NB: 0 for no graceful close. */
    if (!Ns_ConfigGetInt(path, "keepwait", &n) || n < 0) {
        n = 30;         /* 30 seconds wait for more data in keep-alive.*/
    }
    drvPtr->keepwait = _MAX(n, 0); /* NB: 0 for no keepalive. */
    if (!Ns_ConfigGetInt(path, "maxreaders", &n) || n < 1) {
        n = 10;         /* Max of 10 threads for non-event driven I/O. */
    }
    n = _MAX(n, 1);     /* Minimum of 1 reader thread. */
    drvPtr->maxreaders = n;
    drvPtr->readers = ns_calloc((size_t) n, sizeof(Ns_Thread));
    drvPtr->maxfds = 100;
    drvPtr->pfds = ns_calloc(drvPtr->maxfds, sizeof(struct pollfd));

    /*
     * Pre-allocate Sock structures.
     */
          
    drvPtr->flags = DRIVER_STOPPED;
    drvPtr->freeSockPtr = NULL;
    sockPtr = ns_malloc(sizeof(Sock) * drvPtr->maxsock);
    for (n = 0; n < drvPtr->maxsock; ++n) {
        sockPtr->nextPtr = drvPtr->freeSockPtr;
        drvPtr->freeSockPtr = sockPtr;
        ++sockPtr;
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
                return NS_ERROR;
            }
            hPtr = Tcl_CreateHashEntry(&hosts, vhost, &n);
            if (!n) {
                Ns_Log(Error, "%s: duplicate host map: %s", module, vhost);
                return NS_ERROR;
            }
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
 *
 * Results:
 *      NS_ERROR on unknown server, NS_OK otherwise.
 *
 * Side effects:
 *	Proc will be invoked with arg as data before connection queue.
 *
 *----------------------------------------------------------------------
 */

int
Ns_RegisterPreQueue(char *server, Ns_PreQueueProc *proc, void *arg)
{
    PreQueue *preQuePtr;
    NsServer *servPtr;
    
    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
        return NS_ERROR;
    }
    preQuePtr = ns_malloc(sizeof(PreQueue));
    preQuePtr->proc = proc;
    preQuePtr->arg = arg;
    preQuePtr->nextPtr = servPtr->firstPreQuePtr;
    servPtr->firstPreQuePtr = preQuePtr;
    return NS_OK;
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
    QueWait *queWaitPtr; 

    queWaitPtr = ns_malloc(sizeof(QueWait));
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
 *      NS_OK if all drivers started, NS_ERROR otherwise.
 *
 * Side effects:
 *	See DriverThread.
 *
 *----------------------------------------------------------------------
 */

int
NsStartDrivers(void)
{
    Driver *drvPtr = firstDrvPtr;
    int status = NS_OK;

    /*
     * Signal and wait for each driver to start.
     */

    while (drvPtr != NULL) {
        Ns_Log(Notice, "driver: starting: %s", drvPtr->module);
        Ns_ThreadCreate(DriverThread, drvPtr, 0, &drvPtr->thread);
	Ns_MutexLock(&drvPtr->lock);
        while (!(drvPtr->flags & DRIVER_STARTED)) {
            Ns_CondWait(&drvPtr->cond, &drvPtr->lock);
	}
        if ((drvPtr->flags & DRIVER_FAILED)) {
            status = NS_ERROR;
	}
        Ns_MutexUnlock(&drvPtr->lock);
	drvPtr = drvPtr->nextPtr;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopDrivers --
 *
 *      Trigger driver threads to begin shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Driver threads will close listen sockets and exit after all
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
        drvPtr->flags |= DRIVER_SHUTDOWN;
	Ns_CondBroadcast(&drvPtr->cond);
	Ns_MutexUnlock(&drvPtr->lock);
        TriggerDriver(drvPtr);
	drvPtr = drvPtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitDriversShutdown --
 *
 *      Wait for exit of all driver threads.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Driver threads are joined.
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
        while (!(drvPtr->flags & DRIVER_STOPPED) && status == NS_OK) {
	    status = Ns_CondTimedWait(&drvPtr->cond, &drvPtr->lock, toPtr);
	}
	Ns_MutexUnlock(&drvPtr->lock);
	if (status != NS_OK) {
	    Ns_Log(Warning, "driver: shutdown timeout: %s", drvPtr->module);
	} else {
	    Ns_Log(Notice, "driver: stopped: %s", drvPtr->module);
	    Ns_ThreadJoin(&drvPtr->thread, NULL);
	    drvPtr->thread = NULL;
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
 *      Return a Sock to the DriverThread for closing or keepalive.
 *      Note the connection may continue to run after releasing the
 *      Sock.
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

    if (keep && drvPtr->keepwait > 0
	    && (*drvPtr->proc)(DriverKeep, sock, NULL, 0) == 0) {
        sockPtr->state = SOCK_READWAIT;
    } else {
    	sockPtr->state = SOCK_CLOSEWAIT;
	(void) (*drvPtr->proc)(DriverClose, sock, NULL, 0);
    }
    Ns_MutexLock(&drvPtr->lock);
    sockPtr->nextPtr = drvPtr->closeSockPtr;
    drvPtr->closeSockPtr = sockPtr;
    Ns_MutexUnlock(&drvPtr->lock);
    TriggerDriver(drvPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeConn --
 *
 *      Return a Conn structure to the free list after processing,
 *      called by the conn threads.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *      Will close Sock structure if still open.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeConn(Conn *connPtr)
{
    Driver *drvPtr = connPtr->drvPtr;

    /*
     * Close the Sock if still open.
     */

    if (connPtr->sockPtr != NULL) {
        NsSockClose(connPtr->sockPtr, 0);
        connPtr->sockPtr = NULL;
    }

    /*
     * Return the Conn to the driver.
     */

    Ns_MutexLock(&drvPtr->lock);
    connPtr->nextPtr = drvPtr->freeConnPtr;
    drvPtr->freeConnPtr = connPtr;
    Ns_MutexUnlock(&drvPtr->lock);
    TriggerDriver(drvPtr);
}


static void
DelinkConn(Driver *drvPtr, Conn *connPtr)
{
    if (connPtr->prevPtr != NULL) {
       connPtr->prevPtr->nextPtr = connPtr->nextPtr;
    } else {
       drvPtr->firstConnPtr = connPtr->nextPtr;
    }
    if (connPtr->nextPtr != NULL) {
       connPtr->nextPtr->prevPtr = connPtr->prevPtr;
    } else {
       drvPtr->lastConnPtr = NULL;
    }
    connPtr->prevPtr = connPtr->nextPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * DriverThread --
 *
 *      Main communication driver thread.  A driver thread is created
 *      for each loaded module.  The thread continuously loops 
 *      handling I/O events, accepting new connections, and queuing
 *      connections for reader and execution threads.  The driver will
 *      also processes any pre-queue callbacks and resulting I/O events,
 *      if any. 
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
    SOCKET lsock;
    Driver *drvPtr = (Driver *) arg;
    int n, pollto, flags;
    Sock *sockPtr, *closePtr, *nextPtr;
    QueWait *queWaitPtr;
    Conn *connPtr, *nextConnPtr, *freeConnPtr;
    Limits *limitsPtr;
    char drain[1024];
    Ns_Time diff;
    Sock *waitPtr = NULL;	/* Sock's waiting for I/O events. */
    Sock *readSockPtr = NULL;  /* Sock's to send to reader threads. */
    Sock *runSockPtr = NULL;   /* Sock's ready for pre-queue callbacks. */
    Sock *queSockPtr = NULL;    /* Sock's ready to queue. */

    ThreadName(drvPtr, "driver");
    
    /*
     * Create the listen socket.
     */
 
    flags = 0;
    lsock = Ns_SockListenEx(drvPtr->bindaddr, drvPtr->port, drvPtr->backlog);
    if (lsock == INVALID_SOCKET) {
        Ns_Log(Error, "%s: failed to listen on %s:%d: %s", drvPtr->name,
                drvPtr->address, drvPtr->port, ns_sockstrerror(ns_sockerrno));
        flags |= (DRIVER_FAILED | DRIVER_SHUTDOWN);
        goto stopped;
    }
    Ns_SockSetNonBlocking(lsock);
    Ns_Log(Notice, "%s: listening on %s:%d",
            drvPtr->name, drvPtr->address, drvPtr->port);

    /*
     * Update and signal state of driver.
     */

    flags |= DRIVER_STARTED;
    Ns_MutexLock(&drvPtr->lock);
    drvPtr->flags |= flags;
    Ns_CondBroadcast(&drvPtr->cond);
    Ns_MutexUnlock(&drvPtr->lock);
    
    /*
     * Loop forever until signalled to shutdown and all
     * connections are complete and gracefully closed.
     */

    drvPtr->pfds = NULL;
    drvPtr->nfds = 0;
    drvPtr->maxfds = 100;
    drvPtr->pfds = ns_calloc(sizeof(struct pollfd), drvPtr->maxfds);
    drvPtr->pfds[0].fd = drvPtr->trigger[0];
    drvPtr->pfds[0].events = POLLIN;
    drvPtr->pfds[1].fd = lsock;
    Ns_GetTime(&drvPtr->now);

    while (!(flags & DRIVER_SHUTDOWN) || drvPtr->nactive) {

	/*
         * Poll the trigger pipe and, if a Sock structure is available,
	 * the listen socket.
	 */

        if ((drvPtr->flags & DRIVER_SHUTDOWN) || drvPtr->freeSockPtr == NULL) {
            drvPtr->pfds[1].events = 0;
        } else {
            drvPtr->pfds[1].events = POLLIN;
        }
	drvPtr->pfds[1].revents = drvPtr->pfds[0].revents = 0;
	drvPtr->nfds = 2;

	/*
	 * Poll waiting sockets, determining the minimum relative timeout.
	 */

	sockPtr = waitPtr;
        drvPtr->timeout = maxtimeout;
        while (sockPtr != NULL) {
            sockPtr->pidx = Poll(drvPtr, sockPtr->sock, POLLIN,
                    &sockPtr->timeout);
            if (sockPtr->connPtr != NULL) {
                queWaitPtr = sockPtr->connPtr->queWaitPtr;
                while (queWaitPtr != NULL) {
                    queWaitPtr->pidx = Poll(drvPtr, queWaitPtr->sock,
                            queWaitPtr->events,
                            &queWaitPtr->timeout);
                    queWaitPtr = queWaitPtr->nextPtr;
                }
	    }
            sockPtr = sockPtr->nextPtr;
        }
        if (Ns_DiffTime(&drvPtr->timeout, &drvPtr->now, &diff) <= 0)  {
            pollto = 0;
        } else if (diff.sec > 214783) { /* NB: Avoid overflow. */
            pollto = -1;
        } else {
            pollto = diff.sec * 1000 + diff.usec / 1000;
	}

	/*
	 * Poll, drain the trigger pipe if necessary, and get current time.
	 */

	do {
	    n = poll(drvPtr->pfds, drvPtr->nfds, pollto);
        } while (n < 0  && ns_sockerrno == EINTR);
	if (n < 0) {
	    Ns_Fatal("driver: poll() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}
	if ((drvPtr->pfds[0].revents & POLLIN)
		&& recv(drvPtr->trigger[0], drain, sizeof(drain), 0) <= 0) {
	    Ns_Fatal("driver: trigger recv() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}

	/*
         * Update the time for this spin.
         */

        Ns_GetTime(&drvPtr->now);

        /*
         * Process ready sockets.
	 */

	sockPtr = waitPtr;
	waitPtr = NULL;
	while (sockPtr != NULL) {
	    nextPtr = sockPtr->nextPtr;
	    switch (sockPtr->state) {
	    case SOCK_CLOSEWAIT:
                /*
                 * Cleanup connections in graceful close.
                 */

	    	if (drvPtr->pfds[sockPtr->pidx].revents & POLLIN) {
		    n = recv(sockPtr->sock, drain, sizeof(drain), 0);
		    if (n <= 0) {
                        /* Timeout Sock on end-of-file or error. */
		        sockPtr->timeout = drvPtr->now;
		    }
	    	}
	    	if (Ns_DiffTime(&sockPtr->timeout, &drvPtr->now, NULL) <= 0) {
                    /* Close wait complete or timeout. */
                    SockClose(drvPtr, sockPtr);
	    	} else {
		    SockPush(sockPtr, &waitPtr);
	    	}
		break;

	    case SOCK_QUEWAIT:
                /*
                 * Run connections with queue-wait callbacks.
                 */

                switch (RunQueWaits(drvPtr, sockPtr)) {
                case STATUS_READY:
                    SockPush(sockPtr, &queSockPtr);    /* Ready to queue. */
		    break;
                case STATUS_PENDING:
		    SockPush(sockPtr, &waitPtr);    /* Still pending. */
		    break;
	    	default:
                    /* Failure of a queue wait callback. */
                    SockClose(drvPtr, sockPtr);
		    break;
		}
		break;

	    case SOCK_READWAIT:
                /*
                 * Read connection for more input.
                 */

	    	if (!(drvPtr->pfds[sockPtr->pidx].revents & POLLIN)) {
                    /* Timeout or wait longer for input. */
                    if (Ns_DiffTime(&sockPtr->timeout, &drvPtr->now,
                                NULL) <= 0) {
                        /* Timeout waiting for input. */
                        SockClose(drvPtr, sockPtr);
		    } else {
		    	SockPush(sockPtr, &waitPtr);
		    }
	    	} else {
                    /* Input now available */
		    if (!(drvPtr->opts & NS_DRIVER_ASYNC)) {
                        /* Queue for read by reader threads. */
                        SockPush(sockPtr, &readSockPtr);
		    } else {
                        /* Read directly. */
		    	switch (SockRead(drvPtr, sockPtr)) {
                        case STATUS_READY:
                            /* Ready for pre-queue callbacks. */
                            SockPush(sockPtr, &runSockPtr);
                            break;

                        case STATUS_PENDING:
                            /* Wait for more content. */
                            SockWait(sockPtr, &drvPtr->now,
                                    drvPtr->recvwait, &waitPtr);
                            break;

		    	default:
			    /* Release socket on read error. */
                            SockClose(drvPtr, sockPtr);
		    	    break;
			}
		    }
		}
		break;

            case SOCK_RUNWAIT:
                /*
                 * Handle connections blocked from running due to
                 * resource limits.
                 */

                if (drvPtr->pfds[sockPtr->pidx].revents & POLLIN) {
                    /* Client dropped waiting for processing. */
                    DelinkConn(drvPtr, sockPtr->connPtr);
                    SockClose(drvPtr, sockPtr);
                }
                break;
                    
	    default:
		Ns_Fatal("impossible state");
		break;
	    }
	    sockPtr = nextPtr;
	}

	/*
         * Get current flags, free conns, closing socks, and socks returning
         * from the reader threads.
	 */

        Ns_MutexLock(&drvPtr->lock);
        flags = drvPtr->flags;
        freeConnPtr = drvPtr->freeConnPtr;
        drvPtr->freeConnPtr = NULL;
        closePtr = drvPtr->closeSockPtr;
        drvPtr->closeSockPtr = NULL;
        while ((sockPtr = drvPtr->runSockPtr) != NULL) {
            drvPtr->runSockPtr = sockPtr->nextPtr;
            SockPush(sockPtr, &runSockPtr);
        }
        if (readSockPtr != NULL) {
            n = 0;
            while ((sockPtr = readSockPtr) != NULL) {
                readSockPtr = sockPtr->nextPtr;
                sockPtr->nextPtr = drvPtr->readSockPtr;
                drvPtr->readSockPtr = sockPtr;
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
	}
        Ns_MutexUnlock(&drvPtr->lock);

	/*
         * Process Sock's returned for keep-alive or close.
	 */

        while ((sockPtr = closePtr) != NULL) {
            closePtr = sockPtr->nextPtr;
            if (sockPtr->state == SOCK_READWAIT) {
                /* Allocate a new Conn for the connection. */
                sockPtr->connPtr = AllocConn(drvPtr, sockPtr);
                SockWait(sockPtr, &drvPtr->now, drvPtr->keepwait, &waitPtr);
            } else if (!drvPtr->closewait || shutdown(sockPtr->sock, 1) != 0) {
                /* Graceful close diabled or shutdown() failed. */
                SockClose(drvPtr, sockPtr);
            } else {
                SockWait(sockPtr, &drvPtr->now, drvPtr->closewait, &waitPtr);
            }
	}

	/*
         * Free connections done executing.
	 */

        while ((connPtr = freeConnPtr) != NULL) {
            freeConnPtr = connPtr->nextPtr;
            limitsPtr = connPtr->limitsPtr;
            Ns_MutexLock(&limitsPtr->lock);
            --limitsPtr->nrunning;
            Ns_MutexUnlock(&limitsPtr->lock);
            FreeConn(drvPtr, connPtr);
        }

	/*
         * Process sockets ready to run, starting with pre-queue callbacks.
	 */

        while ((sockPtr = runSockPtr) != NULL) {
            runSockPtr = sockPtr->nextPtr;
            sockPtr->connPtr->times.ready = drvPtr->now;
            switch (RunPreQueues(sockPtr->connPtr)) {
            case STATUS_PENDING:
                /* NB: Sock timeout now longest que wait. */
                sockPtr->timeout = maxtimeout;
                sockPtr->state = SOCK_QUEWAIT;
                SockPush(sockPtr, &waitPtr);
                break;

            case STATUS_READY:
                SockPush(sockPtr, &queSockPtr);
                break;

            default:
                Ns_Fatal("impossible state");
                break;
            }
	}

	/*
         * Add Sock's now ready to the queue.
	 */

        while ((sockPtr = queSockPtr) != NULL) {
            queSockPtr = sockPtr->nextPtr;
            connPtr = sockPtr->connPtr; 
            sockPtr->timeout = drvPtr->now;
            Ns_IncrTime(&sockPtr->timeout, connPtr->limitsPtr->timeout, 0);
            connPtr->prevPtr = drvPtr->lastConnPtr;
            if (drvPtr->firstConnPtr == NULL) {
                drvPtr->firstConnPtr = connPtr;
            } else {
                drvPtr->lastConnPtr->nextPtr = connPtr;
            }
            drvPtr->lastConnPtr = connPtr;
            connPtr->nextPtr = NULL;
	}

	/*
         * Attempt to queue any waiting connections.
	 */

        connPtr = drvPtr->firstConnPtr; 
        while (connPtr != NULL) {
            sockPtr = connPtr->sockPtr;
            limitsPtr = connPtr->limitsPtr;
            Ns_MutexLock(&limitsPtr->lock);
            if (limitsPtr->nrunning < limitsPtr->maxrun) {
                if (sockPtr->state == SOCK_RUNWAIT) {
                    --limitsPtr->nwaiting;
                }
                ++limitsPtr->nrunning;
                sockPtr->state = SOCK_RUNNING;
            } else if (sockPtr->state != SOCK_RUNWAIT) {
                if (limitsPtr->nwaiting < limitsPtr->maxwait) {
                    ++limitsPtr->nwaiting;
                    sockPtr->state = SOCK_RUNWAIT;
                } else {
                    Ns_Log(Warning, "%s: dropping connection, "
                            "limit %s maxwait %u reached", drvPtr->name,
                            limitsPtr->name, limitsPtr->maxwait);
                    connPtr->responseStatus = 503;
                    ++limitsPtr->nrunning;
                    sockPtr->state = SOCK_RUNNING;
                }
            }
            Ns_MutexUnlock(&limitsPtr->lock);
            if (sockPtr->state != SOCK_RUNNING) {
                connPtr = connPtr->nextPtr;
            } else {
                if (connPtr == drvPtr->firstConnPtr) {
                    drvPtr->firstConnPtr = connPtr->nextPtr;
                } else {
                    connPtr->prevPtr->nextPtr = connPtr->nextPtr;
                }
                if (connPtr == drvPtr->lastConnPtr) {
                    drvPtr->lastConnPtr = connPtr->prevPtr;
                } else {
                    connPtr->nextPtr->prevPtr = connPtr->prevPtr;
                }
                nextConnPtr = connPtr->nextPtr;
                NsQueueConn(connPtr);
                connPtr = nextConnPtr;
            }
	}

	/*
	 * Attempt to accept new sockets.
	 */

  	if ((drvPtr->pfds[1].revents & POLLIN)
                && ((sockPtr = SockAccept(lsock, drvPtr)) != NULL)) {
	    SockWait(sockPtr, &drvPtr->now, drvPtr->recvwait, &waitPtr);
	}
    }

    /*
     * TODO: Handle waiting Sock's on shutdown.
     */

    if (lsock != INVALID_SOCKET) {
        ns_sockclose(lsock);
    }
    while (drvPtr->nreaders > 0) {
    	--drvPtr->nreaders;
	Ns_ThreadJoin(&drvPtr->readers[drvPtr->nreaders], NULL);
    }

stopped:
    Ns_MutexLock(&drvPtr->lock);
    drvPtr->flags |= DRIVER_STOPPED;
    Ns_CondBroadcast(&drvPtr->cond);
    Ns_MutexUnlock(&drvPtr->lock);
    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * Poll --
 *
 *	Arrange for socket to be monitored by the given driver
 *	thread up to given timeout.
 *
 * Results:
 *      Index into poll array.
 *
 * Side effects:
 *	Sock fd will be monitored on next spin of driver thread.
 *
 *----------------------------------------------------------------------
 */

static int
Poll(Driver *drvPtr, SOCKET sock, int events, Ns_Time *toPtr)
{
    int idx;

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
    idx = drvPtr->nfds++;

    /* 
     * Check for new minimum timeout.
     */

    if (Ns_DiffTime(toPtr, &drvPtr->timeout, NULL) < 0) {
        drvPtr->timeout = *toPtr;
    }

    return idx;
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
SockAccept(SOCKET lsock, Driver *drvPtr)
{
    Sock *sockPtr = drvPtr->freeSockPtr;
    int slen;

    /*
     * Accept the new connection.
     */

    slen = sizeof(struct sockaddr_in);
    sockPtr->sock = Ns_SockAccept(lsock,
            (struct sockaddr *) &sockPtr->sa, &slen);
    if (sockPtr->sock == INVALID_SOCKET) {
	return NULL;
    }
    sockPtr->drvPtr = drvPtr;
    sockPtr->state = SOCK_READWAIT;
    sockPtr->arg = NULL;
    sockPtr->acceptTime = drvPtr->now;
    sockPtr->connPtr = AllocConn(drvPtr, sockPtr);

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
    drvPtr->freeSockPtr = sockPtr->nextPtr;
    sockPtr->nextPtr = NULL;
    return sockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SockClose --
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
SockClose(Driver *drvPtr, Sock *sockPtr)
{
    /*
     * Free the Conn if the Sock is still responsible for it.
     */
     
    if (sockPtr->connPtr != NULL) {
        FreeConn(drvPtr, sockPtr->connPtr);
	sockPtr->connPtr = NULL;
    }
    ns_sockclose(sockPtr->sock);
    sockPtr->sock = INVALID_SOCKET;
    sockPtr->nextPtr = drvPtr->freeSockPtr;
    drvPtr->freeSockPtr = sockPtr;
    --drvPtr->nactive;
}


/*
 *----------------------------------------------------------------------
 *
 * TriggerDriver --
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
TriggerDriver(Driver *drvPtr)
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
 *      STATUS_READY:   Conn is ready for processing.
 *      STATUS_PENDING: More input is required.
 *      STATUS_ERROR:   Client drop or timeout.
 *
 * Side effects:
 *	The client request will be read for use by the
 *      connection thread.  Also, before returning STATUS_READY,
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
    char *s, *e, save;
    int   len, n, max, err;

    /*
     * Update read time.
     */

    connPtr->times.read = drvPtr->now;

    /*
     * First, handle the simple case of continued content read.
     */

    if ((connPtr->flags & NS_CONN_READHDRS)) {
        buf.iov_base = connPtr->content + connPtr->avail;
        buf.iov_len = connPtr->contentLength - connPtr->avail + 2;
        n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
        if (n <= 0) {
            return STATUS_ERROR;
        }
        connPtr->avail += n;
        goto done;
    }

    /*
     * Otherwise handle the more complex read-ahead of request, headers,
     * and possibly content which may require growing the connection buffer.
     */

    bufPtr = &connPtr->ibuf;
    len = bufPtr->length;
    max = bufPtr->spaceAvl - 1;
    if (len == drvPtr->maxinput) {
        return STATUS_ERROR;
    }
    if (max == len) {
        max += drvPtr->bufsize;
        if (max > drvPtr->maxinput) {
            max = drvPtr->maxinput;
        }
    }
    Tcl_DStringSetLength(bufPtr, max);
    buf.iov_base = bufPtr->string + len;
    buf.iov_len = max - len;
    n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
    if (n <= 0) {
        return STATUS_ERROR;
    }
    len += n;
    Tcl_DStringSetLength(bufPtr, len);

    /*
     * Scan lines until start of content.
     */

    while (!(connPtr->flags & NS_CONN_READHDRS)) {
        s = bufPtr->string + connPtr->roff;
        e = strchr(s, '\n');
        if (e == NULL) {
	    /*
             * Input not yet nl-terminated, request more.
	     */

            return STATUS_PENDING;
	}

	/*
	 * Update next read pointer to end of this line.
	 */

        connPtr->roff += (e - s + 1);
	if (e > s && e[-1] == '\r') {
	    --e;
	}
        if ((e - s) > drvPtr->maxline) {
            /*
             * Exceeded maximum single line input length.
             */

            return STATUS_ERROR;
        } else if (e == s) {
            /*
             * Found end of headers, setup connection limits, etc.
             */

            connPtr->flags |= NS_CONN_READHDRS;
            if (!SetupConn(connPtr)) {
                return STATUS_ERROR;
            }
        } else if (connPtr->request == NULL) {
            /*
             * Reading first line, parse into request.
             */

	    save = *e;
	    *e = '\0';
            connPtr->request = request = Ns_ParseRequest(s);
	    *e = save;

            if (request == NULL || request->method == NULL) {
                return STATUS_ERROR;
	    }
            if (STREQ(request->method, "HEAD")) {
		connPtr->flags |= NS_CONN_SKIPBODY;
	    }
            if (request->version < 1.0) {
                connPtr->flags |= NS_CONN_SKIPHDRS;
                return (SetupConn(connPtr) ? STATUS_READY : STATUS_ERROR);
            }
        } else {
            /*
             * Parsee next HTTP header line.
             */

            save = *e;
            *e = '\0';
            err = Ns_ParseHeader(connPtr->headers, s, Preserve);
            *e = save;
            if (err != NS_OK) {
                return STATUS_ERROR;
	    }
	}
    }

done:
    if (connPtr->avail < connPtr->contentLength) {
        return STATUS_PENDING;
    }

    /*
     * Rewind the content pointer and null terminate the content.
     */ 
 
    connPtr->avail = connPtr->contentLength;
    connPtr->content[connPtr->avail] = '\0';
    return STATUS_READY;
}


/*
 *----------------------------------------------------------------------
 *
 * RunQueWaits --
 *
 *	Run Sock queue wait callbacks.
 *
 * Results:
 *      STATUS_READY:   Conn is ready for processing.
 *      STATUS_PENDING: More callbacks are pending.
 *      STATUS_ERROR:   Client drop or timeout.
 *
 * Side effects:
 *	Depends on callbacks which may, e.g., register more queue wait
 *      callbacks. 
 *
 *----------------------------------------------------------------------
 */

static int
RunQueWaits(Driver *drvPtr, Sock *sockPtr)
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
             ns_free(queWaitPtr);
	}
	queWaitPtr = nextPtr;
    }
    if (dropped) {
        return STATUS_ERROR;
    } else if (connPtr->queWaitPtr != NULL) {
        return STATUS_PENDING;
    } else {
        return STATUS_READY;
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
 *      STATUS_READY:  Connection run to run.
 *      STATUS_PENDING: One or more quewait callbacks pending.
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
    return (connPtr->queWaitPtr ? STATUS_PENDING : STATUS_READY);
}


/*
 *----------------------------------------------------------------------
 *
 * SetupConn --
 *
 *      Determine the virtual server, various request limits, and
 *      setup the content (if any) for continued read.
 *
 * Results:
 *      1 if setup correctly, 0 if request is invalid.
 *
 * Side effects:
 *      Will update several connPtr values.
 *
 *----------------------------------------------------------------------
 */

static int
SetupConn(Conn *connPtr)
{
    Tcl_DString *bufPtr = &connPtr->ibuf;
    ServerMap *mapPtr = NULL;
    Tcl_HashEntry *hPtr;
    int len, nbuf;
    char *hdr;

    /*
     * Determine the virtual server and driver location.
     */
    
    connPtr->servPtr = connPtr->drvPtr->servPtr;
    connPtr->location = connPtr->drvPtr->location;
    hdr = Ns_SetIGet(connPtr->headers, "host");
    if (hdr == NULL && connPtr->request->version >= 1.1) {
        connPtr->responseStatus = 400;
    }
    if (connPtr->servPtr == NULL) {
	if (hdr != NULL) {
	    hPtr = Tcl_FindHashEntry(&hosts, hdr);
	    if (hPtr != NULL) {
		mapPtr = Tcl_GetHashValue(hPtr);
	    }
	}
	if (mapPtr == NULL) {
	    mapPtr = defMapPtr;
	}
	if (mapPtr != NULL) {
	    connPtr->servPtr = mapPtr->servPtr;
	    connPtr->location = mapPtr->location;
	}
        if (connPtr->servPtr == NULL) {
            connPtr->responseStatus = 400;
        }
    }
    connPtr->server = connPtr->servPtr->server;

    /*
     * Setup character encodings.
     */

    connPtr->encoding = connPtr->servPtr->encoding.outputEncoding;
    connPtr->urlEncoding = connPtr->servPtr->encoding.urlEncoding;

    /*
     * Get limits and check content length.
     */
     
    connPtr->limitsPtr = NsGetLimits(connPtr->server,
            connPtr->request->method, connPtr->request->url);

    hdr = Ns_SetIGet(connPtr->headers, "content-length");
    if (hdr == NULL) {
        len = 0;
    } else if (sscanf(hdr, "%d", &len) != 1 || len < 0) {
        return 0;
    }
    if (len > connPtr->limitsPtr->maxupload &&
            len > connPtr->drvPtr->maxinput) {
        return 0;
    }
    connPtr->contentLength = len;
    if (len > connPtr->drvPtr->maxinput) {
        connPtr->flags |= NS_CONN_FILECONTENT;
    }

    /*
     * Parse authorization header.
     */

    hdr = Ns_SetIGet(connPtr->headers, "authorization");
    if (hdr != NULL) {
        char *p, *q, save;

        p = hdr;
        while (*p != '\0' && !isspace(UCHAR(*p))) {
            ++p;
        }
        if (*p != '\0') {
            save = *p;
            *p = '\0';
            if (STRIEQ(hdr, "basic")) {
                q = p + 1;
                while (*q != '\0' && isspace(UCHAR(*q))) {
                    ++q;
                }
                len = strlen(q) + 3;
                connPtr->authUser = ns_malloc((size_t) len);
                len = Ns_HtuuDecode(q, connPtr->authUser, len);
                connPtr->authUser[len] = '\0';
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
     * Setup the connection to read remaining content (if any).
     */

    connPtr->avail = bufPtr->length - connPtr->roff;
    nbuf = connPtr->roff + connPtr->contentLength;
    if (nbuf < connPtr->drvPtr->maxinput) {
        /*
         * Content will fit at end of request buffer.
         */

        Tcl_DStringSetLength(bufPtr, nbuf);
        connPtr->content = bufPtr->string + connPtr->roff;
    } else {
        /*
         * Content must overflow to a mmap'ed temp file.
         */

        connPtr->flags |= NS_CONN_FILECONTENT;
        connPtr->mlen = len + 1;
        connPtr->tfd = Ns_GetTemp();
        if (connPtr->tfd < 0) {
            return 0;
        }
        if (ftruncate(connPtr->tfd, (off_t) connPtr->mlen) < 0) {
            return 0;
        }
        connPtr->content = mmap(0, connPtr->mlen, PROT_READ|PROT_WRITE,
                MAP_SHARED, connPtr->tfd, 0);
        if (connPtr->content == MAP_FAILED) {
            return 0;
        }
        memcpy(connPtr->content, bufPtr->string + connPtr->roff,
                connPtr->avail);
        Tcl_DStringSetLength(bufPtr, connPtr->roff);
    }

    return 1;
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
    id = nextid++;
    connPtr = firstConnPtr;
    if (connPtr != NULL) {
        firstConnPtr = connPtr->nextPtr;
    }
    Ns_MutexUnlock(&connlock);
    if (connPtr == NULL) {
        connPtr = ns_calloc(1, sizeof(Conn));
        Tcl_DStringInit(&connPtr->ibuf);
        Tcl_DStringInit(&connPtr->obuf);
        Tcl_InitHashTable(&connPtr->files, TCL_STRING_KEYS);
        connPtr->headers = Ns_SetCreate(NULL);
        connPtr->outputheaders = Ns_SetCreate(NULL);
        connPtr->drvPtr = drvPtr;
        connPtr->tfd = -1;
    }
    connPtr->id = id;
    sprintf(connPtr->idstr, "cns%d", connPtr->id);
    connPtr->port = ntohs(sockPtr->sa.sin_port);
    strcpy(connPtr->peer, ns_inet_ntoa(sockPtr->sa.sin_addr));
    connPtr->times.accept = sockPtr->acceptTime;
    connPtr->responseStatus = 0;
    connPtr->sockPtr = sockPtr;
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
FreeConn(Driver *drvPtr, Conn *connPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    FormFile	  *filePtr;

    /*
     * Set the completion time and call CLS cleanups.
     */

    connPtr->times.done = drvPtr->now;    
    NsClsCleanup(connPtr);

    /*
     * Cleanup public elements.
     */

    if (connPtr->request != NULL) {
    	Ns_FreeRequest(connPtr->request);
        connPtr->request = NULL;
    }
    Ns_SetTrunc(connPtr->headers, 0);
    Ns_SetTrunc(connPtr->outputheaders, 0);
    if (connPtr->authUser != NULL) {
        ns_free(connPtr->authUser);
        connPtr->authUser = connPtr->authPasswd = NULL;
    }
    connPtr->contentLength = 0;
    connPtr->flags = 0;

    /*
     * Cleanup private elements.
     */

    if (connPtr->query != NULL) {
	Ns_SetFree(connPtr->query);
	connPtr->query = NULL;
    }
    hPtr = Tcl_FirstHashEntry(&connPtr->files, &search);
    while (hPtr != NULL) {
	filePtr = Tcl_GetHashValue(hPtr);
	Ns_SetFree(filePtr->hdrs);
	ns_free(filePtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&connPtr->files);
    Tcl_InitHashTable(&connPtr->files, TCL_STRING_KEYS);
    connPtr->responseStatus = 0;

    /*
     * Cleanup content buffers.
     */

#ifndef WIN32
    if (connPtr->mlen > 0) {
        if (munmap(connPtr->content, connPtr->mlen) != 0) {
            Ns_Fatal("FreeConn: munmap() failed: %s", strerror(errno));
        }
        connPtr->mlen = 0;
    }
#endif
    if (connPtr->tfd >= 0) {
        Ns_ReleaseTemp(connPtr->tfd);
        connPtr->tfd = -1;
    }
    connPtr->avail = 0;
    connPtr->roff = 0;
    connPtr->content = NULL;
    connPtr->next = NULL;
    Ns_DStringFree(&connPtr->ibuf);
    Ns_DStringFree(&connPtr->obuf);

    /*
     * Dump on the free list.
     */

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
        while (!(drvPtr->flags & DRIVER_SHUTDOWN)
                && drvPtr->readSockPtr == NULL) {
            Ns_CondWait(&drvPtr->cond, &drvPtr->lock);
    	}
        sockPtr = drvPtr->readSockPtr;
	if (sockPtr == NULL) {
	    break;
	}
        drvPtr->readSockPtr = sockPtr->nextPtr;
        if (drvPtr->readSockPtr != NULL) {
	    Ns_CondSignal(&drvPtr->cond);
	}
	--drvPtr->idlereaders;
	Ns_MutexUnlock(&drvPtr->lock);

	/*
	 * Read the connection until complete.
	 */

	do {
	    status = SockRead(drvPtr, sockPtr);
        } while (status == STATUS_PENDING);

	Ns_MutexLock(&drvPtr->lock);
        if (status == STATUS_READY) {
            sockPtr->nextPtr = drvPtr->runSockPtr;
            drvPtr->runSockPtr = sockPtr;
	} else {
            sockPtr->nextPtr = drvPtr->closeSockPtr;
            drvPtr->closeSockPtr = sockPtr;
	}
        TriggerDriver(drvPtr);
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

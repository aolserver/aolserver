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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/driver.c,v 1.41 2005/01/15 23:55:17 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following are valid Sock states.
 */

enum {
    SOCK_READWAIT,	/* Waiting for request or content from client. */
    SOCK_PREQUE,	/* Ready to invoke pre-queue filters. */
    SOCK_QUEWAIT,	/* Running pre-queue filters and event callbacks. */
    SOCK_RUNWAIT,	/* Ready to run when allowed by connection limits. */
    SOCK_RUNNING,	/* Running in a connection queue. */
    SOCK_CLOSEWAIT,	/* Graceful close wait draining remaining bytes .*/
    SOCK_DROPPED,	/* Client dropped connection. */
    SOCK_TIMEOUT,	/* Request timeout waiting to be queued. */
    SOCK_OVERFLOW,	/* Request was denied by connection limits. */
    SOCK_ERROR		/* Sock read error or invalid request. */
};

char *states[] = {
    "readwait",
    "preque",
    "quewait",
    "runwait",
    "running",
    "closewait",
    "dropped",
    "timeout",
    "overflow",
    "error"
};

/*
 * The following are valid driver state flags.
 */

#define DRIVER_STARTED     1
#define DRIVER_STOPPED     2
#define DRIVER_SHUTDOWN    4
#define DRIVER_FAILED      8
#define DRIVER_QUERY	  16

/*
 * The following structure manages polling.  The PollIn macro is
 * used for the common case of checking for readability.
 */

typedef struct PollData {
    int nfds;			/* Number of fd's being monitored. */
    int maxfds;			/* Max fd's (will grow as needed). */
    struct pollfd *pfds;	/* Dynamic array of poll struct's. */
    Ns_Time *timeoutPtr;	/* Min timeout, if any, for next spin. */
} PollData;

#define PollIn(ppd,i)		((ppd)->pfds[(i)].revents & POLLIN)

/*
 * The following structure defines a Host header to server mappings.
 */

typedef struct ServerMap {
    NsServer *servPtr;
    char location[8];	/* Location starting with http://. */ 
} ServerMap;

/*
 * The following structure defines a pre-queue event wait callback.
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
static void SockRead(Driver *drvPtr, Sock *sockPtr);
static int Poll(PollData *pdataPtr, SOCKET sock, int events, Ns_Time *timeoutPtr);
static int SetupConn(Conn *connPtr);
static Conn *AllocConn(Driver *drvPtr, Ns_Time *nowPtr, Sock *sockPtr);
static void FreeConn(Conn *connPtr);
static int RunQueWaits(PollData *pdataPtr, Ns_Time *nowPtr, Sock *sockPtr);
static void ThreadName(Driver *drvPtr, char *name);
static void SockWait(Sock *sockPtr, Ns_Time *nowPtr, int timeout,
			  Sock **listPtrPtr);
static void AppendConn(Driver *drvPtr, Conn *connPtr);
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
    char *path, *address, *host, *bindaddr, *defproto, *defserver;
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

    if (init->version != NS_DRIVER_VERSION_1) {
        Ns_Log(Error, "%s: version field of init argument is invalid: %d", 
                module, init->version);
        return NS_ERROR;
    }
    path = (init->path ? init->path : Ns_ConfigGetPath(server, module, NULL));
    if (server != NULL && (servPtr = NsGetServer(server)) == NULL) {
	Ns_Log(Error, "%s: no such server: %s", module, server);
	return NS_ERROR;
    }
    defserver = Ns_ConfigGetValue(path, "defaultserver");
    if (server == NULL && defserver == NULL) {
	Ns_Log(Error, "%s: no defaultserver defined: %s", module, path);
	return NS_ERROR;
    }

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
    drvPtr->flags = DRIVER_STOPPED;
    Ns_MutexSetName2(&drvPtr->lock, "ns:drv", module);
    if (ns_sockpair(drvPtr->trigger) != 0) {
	Ns_Fatal("ns_sockpair() failed: %s", ns_sockstrerror(ns_sockerrno));
    }
    Ns_DStringVarAppend(&ds, server, "/", module, NULL);
    drvPtr->fullname = Ns_DStringExport(&ds);
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

    /*
     * Pre-allocate Sock structures.
     */
          
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
        defMapPtr = NULL;
	path = Ns_ConfigGetPath(NULL, module, "servers", NULL);
	set = Ns_ConfigGetSection(path);
	for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	    server = Ns_SetKey(set, i);
	    host = Ns_SetValue(set, i);
	    servPtr = NsGetServer(server);
	    if (servPtr == NULL) {
		Ns_Log(Error, "%s: no such server: %s", module, server);
                return NS_ERROR;
            }
            hPtr = Tcl_CreateHashEntry(&hosts, host, &n);
            if (!n) {
                Ns_Log(Error, "%s: duplicate host map: %s", module, host);
                return NS_ERROR;
            }
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
        if (defMapPtr == NULL) {
            Ns_Fatal("%s: default server %s not defined in %s",
                    module, path);
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
    Driver *drvPtr;
    int status = NS_OK;

    /*
     * Signal and wait for each driver to start.
     */

    drvPtr = firstDrvPtr;
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
 * NsTclDriverObjCmd --
 *
 *      Implements ns_driver command.
 *
 * Results:
 *	Standard Tcl result..
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclDriverObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Tcl_DString ds;
    Driver *drvPtr;
    char *fullname;
    static CONST char *opts[] = {
        "list", "query", NULL
    };
    enum {
        DListIdx, DQueryIdx
    } opt;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 1,
                (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case DListIdx:
	drvPtr = firstDrvPtr;
	while (drvPtr != NULL) {
	    Tcl_AppendElement(interp, drvPtr->fullname);
	    drvPtr = drvPtr->nextPtr;
	}
	break;

    case DQueryIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "driver");
            return TCL_ERROR;
	}
	fullname = Tcl_GetString(objv[2]);
	drvPtr = firstDrvPtr;
	while (drvPtr != NULL) {
	    if (STREQ(fullname, drvPtr->fullname)) {
		break;
	    }
	    drvPtr = drvPtr->nextPtr;
	}
	if (drvPtr == NULL) {
	    Tcl_AppendResult(interp, "no such driver: ", fullname, NULL);
	    return TCL_ERROR;
	}
	Tcl_DStringInit(&ds);
    	Ns_MutexLock(&drvPtr->lock);
    	while (drvPtr->flags & DRIVER_QUERY) {
	    Ns_CondWait(&drvPtr->cond, &drvPtr->lock);
    	}
    	drvPtr->queryPtr = &ds;
    	drvPtr->flags |= DRIVER_QUERY;
    	TriggerDriver(drvPtr);
    	while (drvPtr->flags & DRIVER_QUERY) {
	    Ns_CondWait(&drvPtr->cond, &drvPtr->lock);
    	}
    	Ns_MutexUnlock(&drvPtr->lock);
	Tcl_DStringResult(interp, &ds);
	break;
    }
    return TCL_OK;
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

    ++sockPtr->nwrites;
    return (*sockPtr->drvPtr->proc)(DriverSend, sock, bufs, nbufs);
}


/* 
 *----------------------------------------------------------------------
 *
 * NsSockClose --
 *
 *      Return a Sock to its DriverThread for closing or keepalive.
 *      Note the connection may continue to run after releasing the
 *      Sock (traces, etc.).
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

    /*
     * If keepalive is requested and enable, set the read wait
     * state. Otherwise, set close wait which simply drains any
     * remaining bytes to read.
     */

    if (keep && drvPtr->keepwait > 0
	    && (*drvPtr->proc)(DriverKeep, sock, NULL, 0) == 0) {
        sockPtr->state = SOCK_READWAIT;
    } else {
    	sockPtr->state = SOCK_CLOSEWAIT;
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
 *      Return a Conn structure to the free list after processing.
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
     * Return the Conn to the driver.
     */

    Ns_MutexLock(&drvPtr->lock);
    connPtr->nextPtr = drvPtr->freeConnPtr;
    drvPtr->freeConnPtr = connPtr;
    Ns_MutexUnlock(&drvPtr->lock);
    TriggerDriver(drvPtr);
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
    int n, flags, stop, lidx, tidx;
    Sock *sockPtr, *closePtr, *nextPtr;
    QueWait *queWaitPtr;
    Conn *connPtr, *nextConnPtr, *freeConnPtr;
    PollData pdata;
    Limits *limitsPtr;
    char drain[1024];
    Ns_Time now;
    Sock *waitPtr = NULL;	/* Sock's waiting for I/O events. */
    Sock *readSockPtr = NULL;	/* Sock's to send to reader threads. */
    Sock *preqSockPtr = NULL;	/* Sock's ready for pre-queue callbacks. */
    Sock *queSockPtr = NULL;    /* Sock's ready to queue. */

    ThreadName(drvPtr, "driver");
    
    /*
     * Create the listen socket.
     */
 
    flags = DRIVER_STARTED;
    lsock = Ns_SockListenEx(drvPtr->bindaddr, drvPtr->port, drvPtr->backlog);
    if (lsock != INVALID_SOCKET) {
    	Ns_Log(Notice, "%s: listening on %s:%d", drvPtr->name,
	       drvPtr->address, drvPtr->port);
    	Ns_SockSetNonBlocking(lsock);
    } else {
        Ns_Log(Error, "%s: failed to listen on %s:%d: %s", drvPtr->name,
                drvPtr->address, drvPtr->port, ns_sockstrerror(ns_sockerrno));
        flags |= (DRIVER_FAILED | DRIVER_SHUTDOWN);
    }

    /*
     * Update and signal state of driver.
     */

    Ns_MutexLock(&drvPtr->lock);
    drvPtr->flags |= flags;
    Ns_CondBroadcast(&drvPtr->cond);
    Ns_MutexUnlock(&drvPtr->lock);
    
    /*
     * Loop forever until signalled to shutdown and all
     * connections are complete and gracefully closed.
     */

    pdata.nfds = pdata.maxfds = 0;
    pdata.pfds = NULL;
    pdata.timeoutPtr = NULL;
    stop = (flags & DRIVER_SHUTDOWN);
    while (!stop || drvPtr->nactive) {

	/*
         * Poll the trigger pipe and, if a Sock structure is available,
	 * the listen socket.
	 */

	pdata.nfds = 0;
	pdata.timeoutPtr = NULL;
	tidx = Poll(&pdata, drvPtr->trigger[0], POLLIN, NULL);
        if (!stop && drvPtr->freeSockPtr != NULL) {
	    lidx = Poll(&pdata, lsock, POLLIN, NULL);
	} else {
	    lidx = -1;
	}

	/*
	 * Poll waiting sockets, determining the minimum relative timeout.
	 */

	sockPtr = waitPtr;
        while (sockPtr != NULL) {
            if (sockPtr->state != SOCK_QUEWAIT) {
            	sockPtr->pidx = Poll(&pdata, sockPtr->sock, POLLIN,
				     &sockPtr->timeout);
	    } else {
		/* NB: No client timeout with active queue wait events. */
            	sockPtr->pidx = Poll(&pdata, sockPtr->sock, POLLIN, NULL);
                queWaitPtr = sockPtr->connPtr->queWaitPtr;
                while (queWaitPtr != NULL) {
                    queWaitPtr->pidx = Poll(&pdata, queWaitPtr->sock,
                            		    queWaitPtr->events,
					    &queWaitPtr->timeout);
                    queWaitPtr = queWaitPtr->nextPtr;
                }
	    }
            sockPtr = sockPtr->nextPtr;
        }

	/*
	 * Poll, drain the trigger pipe if necessary, and get current time.
	 */

	++drvPtr->stats.spins;
	n = NsPoll(pdata.pfds, pdata.nfds, pdata.timeoutPtr);
	if (PollIn(&pdata, tidx)
		&& recv(drvPtr->trigger[0], drain, sizeof(drain), 0) <= 0) {
	    Ns_Fatal("driver: trigger recv() failed: %s",
		     ns_sockstrerror(ns_sockerrno));
	}
        Ns_GetTime(&now);

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
            SockPush(sockPtr, &preqSockPtr);
        }
	Ns_MutexUnlock(&drvPtr->lock);

	/*
         * Free connections done executing.
	 */

        while ((connPtr = freeConnPtr) != NULL) {
            freeConnPtr = connPtr->nextPtr;
            limitsPtr = connPtr->limitsPtr;
	    if (limitsPtr != NULL) {
            	Ns_MutexLock(&limitsPtr->lock);
            	--limitsPtr->nrunning;
            	Ns_MutexUnlock(&limitsPtr->lock);
	    }
	    connPtr->times.done = now;

	    /*
	     * Add the Sock to the gracefull close list if still open.
	     */

    	    if ((sockPtr = connPtr->sockPtr) != NULL) {
        	connPtr->sockPtr = NULL;
    		sockPtr->state = SOCK_CLOSEWAIT;
		SockPush(sockPtr, &closePtr);
    	    }
            FreeConn(connPtr);
        }

        /*
         * Process ready sockets.
	 */

	stop = (flags & DRIVER_SHUTDOWN);
	sockPtr = waitPtr;
	waitPtr = NULL;
	while (sockPtr != NULL) {
	    nextPtr = sockPtr->nextPtr;
	    switch (sockPtr->state) {
	    case SOCK_CLOSEWAIT:
                /*
                 * Cleanup connections in graceful close.
                 */

	    	if (PollIn(&pdata, sockPtr->pidx)) {
		    n = recv(sockPtr->sock, drain, sizeof(drain), 0);
		    if (n <= 0) {
                        /* NB: Timeout Sock on end-of-file or error. */
		        sockPtr->timeout = now;
		    }
	    	}
	    	if (Ns_DiffTime(&sockPtr->timeout, &now, NULL) <= 0 || stop) {
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

                if (!RunQueWaits(&pdata, &now, sockPtr)) {
                    SockClose(drvPtr, sockPtr);
		} else if (sockPtr->connPtr->queWaitPtr == NULL) {
                    SockPush(sockPtr, &queSockPtr);	/* Ready to queue. */
		} else {
		    SockPush(sockPtr, &waitPtr);	/* Still pending. */
		}
		break;

	    case SOCK_READWAIT:
                /*
                 * Read connection for more input.
                 */

	    	if (!PollIn(&pdata, sockPtr->pidx)) {
                    if (Ns_DiffTime(&sockPtr->timeout, &now, NULL) <= 0
				    || stop) {
                        /* Timeout waiting for input. */
                        SockClose(drvPtr, sockPtr);
		    } else {
		    	SockPush(sockPtr, &waitPtr);
		    }
	    	} else {
                    /* Input now available */
		    if (sockPtr->connPtr->ibuf.length == 0) {
			sockPtr->connPtr->times.read = now;
		    }
		    if (!(drvPtr->opts & NS_DRIVER_ASYNC)) {
                        /* Queue for read by reader threads. */
                        SockPush(sockPtr, &readSockPtr);
		    } else {
                        /* Read directly. */
		    	SockRead(drvPtr, sockPtr);
			if (sockPtr->state == SOCK_READWAIT) {
                            SockWait(sockPtr, &now, drvPtr->recvwait, &waitPtr);
			} else {
                            SockPush(sockPtr, &preqSockPtr);
			}
		    }
		}
		break;

            case SOCK_RUNWAIT:
		/* NB: Handled below when processing Conn queue. */
                break;
                    
	    default:
		Ns_Fatal("impossible state");
		break;
	    }
	    sockPtr = nextPtr;
	}

	/*
         * Move Sock's to the reader threads if necessary.
	 */

        if (readSockPtr != NULL) {
            n = 0;
            Ns_MutexLock(&drvPtr->lock);
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
            Ns_MutexUnlock(&drvPtr->lock);
            if (n > 0) {
                Ns_CondSignal(&drvPtr->cond);
            }
	}

	/*
         * Process Sock's returned for keep-alive or close.
	 */

        while ((sockPtr = closePtr) != NULL) {
            closePtr = sockPtr->nextPtr;
            if (!stop && sockPtr->state == SOCK_READWAIT) {
		sockPtr->connPtr = AllocConn(drvPtr, &now, sockPtr);
                SockWait(sockPtr, &now, drvPtr->keepwait, &waitPtr);
            } else if (!drvPtr->closewait || shutdown(sockPtr->sock, 1) != 0) {
                /* Graceful close diabled or shutdown() failed. */
                SockClose(drvPtr, sockPtr);
            } else {
                SockWait(sockPtr, &now, drvPtr->closewait, &waitPtr);
            }
	}

	/*
         * Process sockets ready to run, starting with pre-queue callbacks.
	 */

        while ((sockPtr = preqSockPtr) != NULL) {
            preqSockPtr = sockPtr->nextPtr;
            sockPtr->connPtr->times.ready = now;
	    if (sockPtr->state != SOCK_PREQUE ||
	    	NsRunFilters((Ns_Conn *) sockPtr->connPtr,
			     NS_FILTER_PRE_QUEUE) != NS_OK) {
		SockClose(drvPtr, sockPtr);
	    } else if (sockPtr->connPtr->queWaitPtr != NULL) {
                /* NB: Sock timeout ignored during que wait. */
                sockPtr->state = SOCK_QUEWAIT;
                SockPush(sockPtr, &waitPtr);
	    } else {
                SockPush(sockPtr, &queSockPtr);
	    }
	}

	/*
         * Add Sock's now ready to the queue.
	 */

        while ((sockPtr = queSockPtr) != NULL) {
            queSockPtr = sockPtr->nextPtr;
            connPtr = sockPtr->connPtr; 
            sockPtr->timeout = connPtr->times.queue = now;
            Ns_IncrTime(&sockPtr->timeout, connPtr->limitsPtr->timeout, 0);
	    AppendConn(drvPtr, connPtr);
	}

	/*
         * Attempt to queue any waiting connections.
	 */

	connPtr = drvPtr->firstConnPtr;
	drvPtr->firstConnPtr = drvPtr->lastConnPtr = NULL;
        while (connPtr != NULL) {
	    nextConnPtr = connPtr->nextPtr;
            sockPtr = connPtr->sockPtr;
            limitsPtr = connPtr->limitsPtr;
            Ns_MutexLock(&limitsPtr->lock);
	    if (sockPtr->state == SOCK_RUNWAIT) {
		--limitsPtr->nwaiting;
                if (PollIn(&pdata, sockPtr->pidx)) {
		    ++drvPtr->stats.dropped;
		    sockPtr->state = SOCK_DROPPED;
		    goto dropped;
		}
	    }
            if (limitsPtr->nrunning < limitsPtr->maxrun) {
            	++limitsPtr->nrunning;
                sockPtr->state = SOCK_RUNNING;
	    } else if (Ns_DiffTime(&sockPtr->timeout, &now, NULL) <= 0) {
		++limitsPtr->ntimeout;
		++drvPtr->stats.timeout;
		sockPtr->state = SOCK_TIMEOUT;
	    } else if (limitsPtr->nwaiting < limitsPtr->maxwait) {
		++limitsPtr->nwaiting;
		sockPtr->state = SOCK_RUNWAIT;
	    } else {
		++limitsPtr->noverflow;
		++drvPtr->stats.overflow;
		sockPtr->state = SOCK_OVERFLOW;
	    }
dropped:
            Ns_MutexUnlock(&limitsPtr->lock);
	    switch (sockPtr->state) {
	    case SOCK_RUNWAIT:
		AppendConn(drvPtr, connPtr);
		SockPush(sockPtr, &waitPtr);
		break;

	    case SOCK_DROPPED:
		SockClose(drvPtr, sockPtr);
		break;

	    case SOCK_TIMEOUT:
		connPtr->flags |= NS_CONN_TIMEOUT;
		/* FALLTHROUGH */

	    case SOCK_OVERFLOW:
		connPtr->limitsPtr = NULL;
		connPtr->flags |= NS_CONN_OVERFLOW;
                connPtr->responseStatus = 503;
		/* FALLTHROUGH */

	    case SOCK_RUNNING:
	    	/* NB: Sock no longer responsible for Conn. */
		sockPtr->connPtr->times.run = now;
	    	sockPtr->connPtr = NULL;
	    	NsQueueConn(connPtr);
		++drvPtr->stats.queued;
		break;

	    default:
		Ns_Fatal("impossible state");
		break;
	    }
	    connPtr = nextConnPtr;
	}

	/*
	 * Attempt to accept new sockets.
	 */

  	if (!stop && lidx >= 0 && PollIn(&pdata, lidx)
                && ((sockPtr = SockAccept(lsock, drvPtr)) != NULL)) {
    	    sockPtr->acceptTime = now;
	    sockPtr->connPtr = AllocConn(drvPtr, &now, sockPtr);
	    SockWait(sockPtr, &now, drvPtr->recvwait, &waitPtr);
	    ++drvPtr->stats.accepts;
	}

	/*
	 * Copy current driver details if requested.
	 */

	if (flags & DRIVER_QUERY) {
	    Ns_MutexLock(&drvPtr->lock);
	    Tcl_DStringAppendElement(drvPtr->queryPtr, "stats");
	    Tcl_DStringStartSublist(drvPtr->queryPtr);
	    Ns_DStringPrintf(drvPtr->queryPtr,
		"time %ld:%ld "
		"spins %ld accepts %u queued %u reads %u "
		"dropped %u overflow %d timeout %d",
	    	now.sec, now.usec,
		drvPtr->stats.spins, drvPtr->stats.accepts,
		drvPtr->stats.queued, drvPtr->stats.reads,
		drvPtr->stats.dropped, drvPtr->stats.overflow,
		drvPtr->stats.timeout);
	    Tcl_DStringEndSublist(drvPtr->queryPtr);
	    Tcl_DStringAppendElement(drvPtr->queryPtr, "socks");
	    sockPtr = waitPtr;
	    Tcl_DStringStartSublist(drvPtr->queryPtr);
	    while (sockPtr != NULL) {
	    	Tcl_DStringStartSublist(drvPtr->queryPtr);
		Ns_DStringPrintf(drvPtr->queryPtr,
		    "id %u sock %d state %s idx %d events %d revents %d "
		    "accept %ld:%ld timeout %ld:%ld",
		    sockPtr->id, sockPtr->sock, states[sockPtr->state], sockPtr->pidx,
		    pdata.pfds[sockPtr->pidx].events, 
		    pdata.pfds[sockPtr->pidx].revents, 
		    sockPtr->acceptTime.sec, sockPtr->acceptTime.usec,
		    sockPtr->timeout.sec, sockPtr->timeout.usec);
	    	NsAppendConn(drvPtr->queryPtr, sockPtr->connPtr, "i/o");
	    	Tcl_DStringEndSublist(drvPtr->queryPtr);
		sockPtr = sockPtr->nextPtr;
	    }
	    Tcl_DStringEndSublist(drvPtr->queryPtr);
	    drvPtr->flags &= ~DRIVER_QUERY;
	    Ns_CondBroadcast(&drvPtr->cond);
	    Ns_MutexUnlock(&drvPtr->lock);
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
 *	Update given PollData to monitor given socket on next spin.
 *
 * Results:
 *      Index into poll array.
 *
 * Side effects:
 *	Min timeout is updated if necessary.
 *
 *----------------------------------------------------------------------
 */

static int
Poll(PollData *pdataPtr, SOCKET sock, int events, Ns_Time *timeoutPtr)
{
    int idx;

    /*
     * Allocate or grow the pfds array if necessary.
     */

    if (pdataPtr->nfds >= pdataPtr->maxfds) {
	pdataPtr->maxfds += 100;
	pdataPtr->pfds = ns_realloc(pdataPtr->pfds,
                pdataPtr->maxfds * sizeof(struct pollfd));
    }

    /*
     * Set the next pollfd struct with this socket.
     */

    pdataPtr->pfds[pdataPtr->nfds].fd = sock;
    pdataPtr->pfds[pdataPtr->nfds].events = events;
    pdataPtr->pfds[pdataPtr->nfds].revents = 0;
    idx = pdataPtr->nfds++;

    /* 
     * Check for new minimum timeout.
     */

    if (timeoutPtr != NULL && (pdataPtr->timeoutPtr == NULL
	    || (Ns_DiffTime(timeoutPtr, pdataPtr->timeoutPtr, NULL) < 0))) {
	pdataPtr->timeoutPtr = timeoutPtr;
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
 * AppendConn --
 *
 *	Append a connection to the waiting list.
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
AppendConn(Driver *drvPtr, Conn *connPtr)
{
    if ((connPtr->prevPtr = drvPtr->lastConnPtr) == NULL) {
	drvPtr->firstConnPtr = connPtr;
    } else {
	connPtr->prevPtr->nextPtr = connPtr;
    }
    drvPtr->lastConnPtr = connPtr;
    connPtr->nextPtr = NULL;
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
    sockPtr->id = drvPtr->nextid++;
    sockPtr->drvPtr = drvPtr;
    sockPtr->state = SOCK_READWAIT;
    sockPtr->arg = NULL;
    sockPtr->connPtr = NULL;

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
        FreeConn(sockPtr->connPtr);
	sockPtr->connPtr = NULL;
    }

    (void) (*drvPtr->proc)(DriverClose, (Ns_Sock *) sockPtr, NULL, 0);
    ns_sockclose(sockPtr->sock);
    sockPtr->sock = INVALID_SOCKET;
    drvPtr->stats.reads += sockPtr->nreads;
    drvPtr->stats.writes += sockPtr->nwrites;
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
 *	None.
 *
 * Side effects:
 *	Will set sockPtr->state to next state as needed. Also,
 *	the next byte to read mark and bytes available are set
 *	to the beginning of the content, just beyond the headers,
 *	when all content has been read.
 *
 *----------------------------------------------------------------------
 */

static void
SockRead(Driver *drvPtr, Sock *sockPtr)
{
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    Ns_Request *request;
    Conn *connPtr = sockPtr->connPtr;
    struct iovec buf;
    Tcl_DString *bufPtr;
    char *s, *e, save;
    int   len, n, max, err;

    ++sockPtr->nreads;

    /*
     * First, handle the simple case of continued content read.
     */

    if ((connPtr->flags & NS_CONN_READHDRS)) {
        buf.iov_base = connPtr->content + connPtr->avail;
        buf.iov_len = connPtr->contentLength - connPtr->avail + 2;
        n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
        if (n <= 0) {
	    goto fail;
        }
        connPtr->avail += n;
	return;
    }

    /*
     * Otherwise handle the more complex read-ahead of request, headers,
     * and possibly content which may require growing the connection buffer.
     */

    bufPtr = &connPtr->ibuf;
    len = bufPtr->length;
    max = bufPtr->spaceAvl - 1;
    if (len == drvPtr->maxinput) {
        goto fail;
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
        goto fail;
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

            return;
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

            goto fail;
        } else if (e == s) {
            /*
             * Found end of headers, setup connection limits, etc.
             */

            connPtr->flags |= NS_CONN_READHDRS;
            if (connPtr->request == NULL || !SetupConn(connPtr)) {
                goto fail;
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
                goto fail;
	    }
            if (STREQ(request->method, "HEAD")) {
		connPtr->flags |= NS_CONN_SKIPBODY;
	    }

	    /*
	     * Handle special case of pre-HTTP/1.0 requests without headers.
	     */

            if (request->version < 1.0) {
                connPtr->flags |= NS_CONN_SKIPHDRS;
		if (!SetupConn(connPtr)) {
		    goto fail;
		}
		sockPtr->state = SOCK_PREQUE;
		return;
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
                goto fail;
	    }
	}
    }
    if (connPtr->avail < connPtr->contentLength) {
	return;
    }

    /*
     * Rewind the content pointer and null terminate the content.
     */ 

    connPtr->avail = connPtr->contentLength;
    connPtr->content[connPtr->avail] = '\0';
    sockPtr->state = SOCK_PREQUE;
    return;

fail:
    sockPtr->state = SOCK_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * RunQueWaits --
 *
 *	Run Sock queue wait callbacks.
 *
 * Results:
 *      nsReadDone:   Conn is ready for processing.
 *      nsReadMore:   More callbacks are pending.
 *      nsReadFail:   Client drop or timeout.
 *
 * Side effects:
 *	Depends on callbacks which may, e.g., register more queue wait
 *      callbacks. 
 *
 *----------------------------------------------------------------------
 */

static int
RunQueWaits(PollData *pdataPtr, Ns_Time *nowPtr, Sock *sockPtr)
{
    Conn *connPtr = sockPtr->connPtr;
    QueWait *queWaitPtr, *nextPtr;
    int revents, why, dropped;

    if (PollIn(pdataPtr, sockPtr->pidx)) {
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
	    revents = pdataPtr->pfds[queWaitPtr->pidx].revents;
	    if (revents & POLLIN) {
	        why |= NS_SOCK_READ;
	    }
	    if (revents & POLLOUT) {
	        why |= NS_SOCK_WRITE;
	    }
	}
	if (why == 0
		&& Ns_DiffTime(&queWaitPtr->timeout, nowPtr, NULL) > 0) {
	    queWaitPtr->nextPtr = connPtr->queWaitPtr;
	    connPtr->queWaitPtr = queWaitPtr;
	} else {
            (*queWaitPtr->proc)((Ns_Conn *) connPtr, queWaitPtr->sock,
                                queWaitPtr->arg, why);
             ns_free(queWaitPtr);
	}
	queWaitPtr = nextPtr;
    }
    return (dropped ? 0 : 1);
}


/*
 *----------------------------------------------------------------------
 *
 * SetupConn --
 *
 *      Determine the virtual server, various request limits, and
 *      setup the content (if any) for continued read.  Assumes
 *      that the request has already been parsed and that
 *      connPtr->request is NOT NULL.
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
    connPtr->queryEncoding = NULL;

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
         * Content must overflow to a mmap'ed temp file if it's
         * supported, otherwise we fake it by writing to the
         * file.
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
#ifdef HAVE_MMAP
        connPtr->content = mmap(0, connPtr->mlen, PROT_READ|PROT_WRITE,
                MAP_SHARED, connPtr->tfd, 0);
        if (connPtr->content == MAP_FAILED) {
            return 0;
        }
#else
	/* TODO: Actually mmap on WIN32. */
        connPtr->content = ns_calloc(1, connPtr->mlen);
        if (write(connPtr->tfd, bufPtr->string + connPtr->roff,
                    connPtr->avail) < 0) {
            return 0;
        }
#endif
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
AllocConn(Driver *drvPtr, Ns_Time *nowPtr, Sock *sockPtr)
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
        connPtr->tfd = -1;
    }
    connPtr->drvPtr = drvPtr;
    connPtr->times.accept = *nowPtr;
    connPtr->id = id;
    sprintf(connPtr->idstr, "cns%d", connPtr->id);
    connPtr->port = ntohs(sockPtr->sa.sin_port);
    strcpy(connPtr->peer, ns_inet_ntoa(sockPtr->sa.sin_addr));
    connPtr->times.accept = sockPtr->acceptTime;
    connPtr->sockPtr = sockPtr;
    connPtr->nextPtr = connPtr->prevPtr = NULL;
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
    Ns_Conn *conn = (Ns_Conn *) connPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    FormFile	  *filePtr;

    /*
     * Call CLS cleanups.
     */

    NsClsCleanup(connPtr);

    /*
     * Cleanup public elements.
     */

    if (conn->request != NULL) {
    	Ns_FreeRequest(conn->request);
        conn->request = NULL;
    }
    Ns_SetTrunc(conn->headers, 0);
    Ns_SetTrunc(conn->outputheaders, 0);
    if (conn->authUser != NULL) {
        ns_free(conn->authUser);
        conn->authUser = conn->authPasswd = NULL;
    }
    conn->flags = 0;
    conn->contentLength = 0;

    /*
     * Cleanup private elements.
     */

    connPtr->responseStatus = 0;
    connPtr->nContentSent = 0;
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
    connPtr->nContentSent = 0;

    /*
     * Cleanup content buffers.
     */

    if (connPtr->mlen > 0) {
#ifdef HAVE_MMAP
        if (munmap(connPtr->content, connPtr->mlen) != 0) {
            Ns_Fatal("FreeConn: munmap() failed: %s", strerror(errno));
        }
#else
        ns_free(connPtr->content);
#endif
        connPtr->mlen = 0;
    }
    if (connPtr->tfd >= 0) {
        Ns_ReleaseTemp(connPtr->tfd);
        connPtr->tfd = -1;
    }
    connPtr->avail = 0;
    connPtr->roff = 0;
    connPtr->content = NULL;
    connPtr->next = NULL;
    Ns_DStringTrunc(&connPtr->ibuf, 0);
    Ns_DStringTrunc(&connPtr->obuf, 0);

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
	 * Read the connection until complete or error.
	 */

	do {
	    SockRead(drvPtr, sockPtr);
        } while (sockPtr->state == SOCK_READWAIT);

	/*
	 * Return the connection to the driver thread.
	 */

	Ns_MutexLock(&drvPtr->lock);
        sockPtr->nextPtr = drvPtr->runSockPtr;
        drvPtr->runSockPtr = sockPtr;
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

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/driver.c,v 1.59 2009/12/08 04:12:19 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following are valid Sock states.
 */

enum {
    SOCK_ACCEPT,	/* Socket has just been accepted. */
    SOCK_READWAIT,	/* Waiting for request or content from client. */
    SOCK_PREQUE,	/* Ready to invoke pre-queue filters. */
    SOCK_QUEWAIT,	/* Running pre-queue filters and event callbacks. */
    SOCK_RUNWAIT,	/* Ready to run when allowed by connection limits. */
    SOCK_RUNNING,	/* Running in a connection queue. */
    SOCK_CLOSEREQ,	/* Close request in a read or pre-queue filter. */
    SOCK_CLOSEWAIT,	/* Graceful close wait draining remaining bytes .*/
    SOCK_CLOSED,	/* Socket has been closed. */
    SOCK_DROPPED,	/* Client dropped connection. */
    SOCK_TIMEOUT,	/* Request timeout waiting to be queued. */
    SOCK_OVERFLOW,	/* Request was denied by connection limits. */
    SOCK_ERROR		/* Sock read error or invalid request. */
};

char *states[] = {
    "accept",
    "readwait",
    "preque",
    "quewait",
    "runwait",
    "running",
    "closereq",
    "closewait",
    "closed",
    "dropped",
    "timeout",
    "overflow",
    "error"
};

/*
 * The following are error codes for all possible connection faults.
 */

typedef enum {
    E_NOERROR = 0,
    E_CLOSE,
    E_RECV,
    E_FDAGAIN,
    E_FDWRITE,
    E_FDTRUNC,
    E_FDSEEK,
    E_NOHOST,
    E_NOSERV,
    E_HINVAL,
    E_RINVAL,
    E_NINVAL,
    E_LRANGE,
    E_RRANGE,
    E_CRANGE,
    E_FILTER,
    E_QUEWAIT,
} ReadErr;

/*
 * The following are valid driver state flags.
 */

#define DRIVER_STARTED     1
#define DRIVER_STOPPED     2
#define DRIVER_SHUTDOWN    4
#define DRIVER_FAILED      8
#define DRIVER_QUERY	  16
#define DRIVER_DEBUG	  32

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
static void SockClose(Sock *sockPtr);
static void SockRead(Sock *sockPtr);
static ReadErr SockReadLine(Driver *drvPtr, Ns_Sock *sock, Conn *connPtr);
static ReadErr SockReadContent(Driver *drvPtr, Ns_Sock *sock, Conn *connPtr);
static int Poll(PollData *pdataPtr, SOCKET sock, int events, Ns_Time *timeoutPtr);
static Conn *AllocConn(Driver *drvPtr, Ns_Time *nowPtr, Sock *sockPtr);
static void FreeConn(Conn *connPtr);
static int RunQueWaits(PollData *pdataPtr, Ns_Time *nowPtr, Sock *sockPtr);
static int RunFilters(Conn *connPtr, int why);
static void ThreadName(Driver *drvPtr, char *name);
static void SockState(Sock *sockPtr, int state);
static void SockWait(Sock *sockPtr, Ns_Time *nowPtr, int timeout,
			  Sock **listPtrPtr);
static void AppendConn(Driver *drvPtr, Conn *connPtr);
#define SockPush(s, sp)		((s)->nextPtr = *(sp), *(sp) = (s))
static void LogReadError(Conn *connPtr, ReadErr err);

/*
 * Static variables defined in this file.
 */

static Driver *firstDrvPtr; /* First in list of all drivers. */
static Conn *firstConnPtr;  /* Conn free list. */
static Ns_Mutex connlock;   /* Lock around Conn free list. */
static Tcl_HashTable hosts; /* Host header to server table. */
static ServerMap *defMapPtr;/* Default server when not found in table. */
static Ns_Tls drvtls;


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
    Ns_TlsAlloc(&drvtls, NULL);
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
    if (Ns_ConfigGetBool(path, "debug", &n) && n) {
	drvPtr->flags |= DRIVER_DEBUG;
    }
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
                    module, server, path);
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
 *	NS_OK if registered, NS_ERROR otherwise.
 *
 * Side effects:
 *	Proc will be called with given arg and sock as arguements
 *  	when the requested I/O condition is met or the given timeout
 *  	has expired. The connection will not be queued until all
 *  	such callbacks are processed.
 *
 *----------------------------------------------------------------------
 */

int
Ns_QueueWait(Ns_Conn *conn, SOCKET sock, Ns_QueueWaitProc *proc,
    	     void *arg, int when, Ns_Time *timePtr)
{
    Conn *connPtr = (Conn *) conn;
    QueWait *queWaitPtr; 
    Driver *drvPtr;

    drvPtr = Ns_TlsGet(&drvtls);
    if (connPtr->sockPtr == NULL || connPtr->sockPtr->drvPtr != Ns_TlsGet(&drvtls)) {
	LogReadError(connPtr, E_QUEWAIT);
	return NS_ERROR;
    }
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
    return NS_OK;
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
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
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
 * NsConnSend --
 *
 *	Send buffers via the connection's driver callback.
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
NsConnSend(Ns_Conn *conn, struct iovec *bufs, int nbufs)
{
    Conn *connPtr = (Conn *) conn;

    if (connPtr->sockPtr == NULL) {
	return -1;
    }
    ++connPtr->sockPtr->nwrites;
    return (*connPtr->sockPtr->drvPtr->proc)(DriverSend,
		(Ns_Sock *) connPtr->sockPtr, bufs, nbufs);
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
     * Defer the close if not requested from a connection thread
     * which could happen in a read or pre-queue callback.
     */

    if (sockPtr->state != SOCK_RUNNING) {
	SockState(sockPtr, SOCK_CLOSEREQ);
	return;
    }

    /*
     * If keepalive is requested and enabled, set the read wait
     * state. Otherwise, set close wait which simply drains any
     * remaining bytes to read.
     */

    if (keep && drvPtr->keepwait > 0
	    && (*drvPtr->proc)(DriverKeep, sock, NULL, 0) == 0) {
	SockState(sockPtr, SOCK_READWAIT);
    } else {
	SockState(sockPtr, SOCK_CLOSEWAIT);
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

    Ns_TlsSet(&drvtls, drvPtr);
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
		SockState(sockPtr, SOCK_CLOSEWAIT);
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
                    SockClose(sockPtr);
	    	} else {
		    SockPush(sockPtr, &waitPtr);
	    	}
		break;

	    case SOCK_QUEWAIT:
                /*
                 * Run connections with queue-wait callbacks.
                 */

                if (!RunQueWaits(&pdata, &now, sockPtr)) {
                    SockClose(sockPtr);
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
                        SockClose(sockPtr);
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
		    	SockRead(sockPtr);
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
                SockClose(sockPtr);
            } else {
                SockWait(sockPtr, &now, drvPtr->closewait, &waitPtr);
            }
	}

	/*
         * Process sockets ready to run.
	 */

        while ((sockPtr = preqSockPtr) != NULL) {
            preqSockPtr = sockPtr->nextPtr;
            sockPtr->connPtr->times.ready = now;
	    /*
	     * Invoke any pre-queue filters 
	     */

	    if (sockPtr->state == SOCK_PREQUE
		    && !RunFilters(sockPtr->connPtr, NS_FILTER_PRE_QUEUE)) {
		if (sockPtr->state != SOCK_CLOSEREQ) {
		    SockState(sockPtr, SOCK_ERROR);
		}
	    }
	    if (sockPtr->state != SOCK_PREQUE) {
		/* NB: Should be one of SOCK_ERROR or SOCK_CLOSEREQ. */
		SockClose(sockPtr);
	    } else if (sockPtr->connPtr->queWaitPtr != NULL) {
		/* NB: Sock timeout ignored during que wait. */
		SockState(sockPtr, SOCK_QUEWAIT);
		SockPush(sockPtr, &waitPtr);
	    } else {
		SockPush(sockPtr, &queSockPtr);
	    }
	}

	/*
         * Add Sock's now ready to queue, freeing any connection
	 * interp which may have been allocated for que-wait callbacks
	 * and/or pre-queue filters.
	 */

        while ((sockPtr = queSockPtr) != NULL) {
            queSockPtr = sockPtr->nextPtr;
	    connPtr = sockPtr->connPtr; 
	    NsFreeConnInterp(connPtr);
	    if (sockPtr->state == SOCK_ERROR) {
		SockClose(sockPtr);
	    } else {
		sockPtr->timeout = connPtr->times.queue = now;
		Ns_IncrTime(&sockPtr->timeout, connPtr->limitsPtr->timeout, 0);
		AppendConn(drvPtr, connPtr);
	    }
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
		    SockState(sockPtr, SOCK_DROPPED);
		    goto dropped;
		}
	    }
            if (limitsPtr->nrunning < limitsPtr->maxrun) {
            	++limitsPtr->nrunning;
		SockState(sockPtr, SOCK_RUNNING);
	    } else if (Ns_DiffTime(&sockPtr->timeout, &now, NULL) <= 0) {
		++limitsPtr->ntimeout;
		++drvPtr->stats.timeout;
		SockState(sockPtr, SOCK_TIMEOUT);
	    } else if (limitsPtr->nwaiting < limitsPtr->maxwait) {
		++limitsPtr->nwaiting;
		SockState(sockPtr, SOCK_RUNWAIT);
	    } else {
		++limitsPtr->noverflow;
		++drvPtr->stats.overflow;
		SockState(sockPtr, SOCK_OVERFLOW);
	    }
dropped:
            Ns_MutexUnlock(&limitsPtr->lock);
	    switch (sockPtr->state) {
	    case SOCK_RUNWAIT:
		AppendConn(drvPtr, connPtr);
		SockPush(sockPtr, &waitPtr);
		break;

	    case SOCK_DROPPED:
		SockClose(sockPtr);
		break;

	    case SOCK_TIMEOUT:
		connPtr->flags |= NS_CONN_TIMEOUT;
		/* FALLTHROUGH */

	    case SOCK_OVERFLOW:
		connPtr->limitsPtr = NULL;
		connPtr->flags |= NS_CONN_OVERFLOW;
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
		"spins %d accepts %u queued %u reads %u "
		"dropped %u overflow %d timeout %u",
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
		if (sockPtr->connPtr != NULL) {
		    NsAppendConn(drvPtr->queryPtr, sockPtr->connPtr, "i/o");
		} else {
		    Tcl_DStringStartSublist(drvPtr->queryPtr);
		    Tcl_DStringEndSublist(drvPtr->queryPtr);
		}
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
 * SockState --
 *
 *	Set the socket state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates sockPtr->state.
 *
 *----------------------------------------------------------------------
 */

static void
SockState(Sock *sockPtr, int state)
{
    if (sockPtr->drvPtr->flags & DRIVER_DEBUG) {
	Ns_Log(Notice, "%s[%d]: %s -> %s\n", sockPtr->drvPtr->name,
	       sockPtr->sock, states[sockPtr->state], states[state]);
    }
    sockPtr->state = state;
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
    sockPtr->state = SOCK_ACCEPT;
    SockState(sockPtr, SOCK_READWAIT);
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
SockClose(Sock *sockPtr)
{
    Driver *drvPtr = sockPtr->drvPtr;

    /*
     * Free the Conn if the Sock is still responsible for it.
     */
     
    if (sockPtr->connPtr != NULL) {
	NsFreeConnInterp(sockPtr->connPtr);
        FreeConn(sockPtr->connPtr);
	sockPtr->connPtr = NULL;
    }

    (void) (*drvPtr->proc)(DriverClose, (Ns_Sock *) sockPtr, NULL, 0);
    ns_sockclose(sockPtr->sock);
    SockState(sockPtr, SOCK_CLOSED);
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
 *	Will set sockPtr->state to next state as needed and rewind
 *	the content (in-memory or in temp file) when ready for
 *	processing.
 *
 *----------------------------------------------------------------------
 */

static void
SockRead(Sock *sockPtr)
{
    Driver *drvPtr = sockPtr->drvPtr;
    Conn *connPtr = sockPtr->connPtr;
    Ns_Sock *sock = (Ns_Sock *) sockPtr;
    ReadErr err;

    /*
     * Read any waiting request+headers and/or content.
     */

    ++sockPtr->nreads;
    if ((connPtr->flags & NS_CONN_READHDRS)) {
	err = SockReadContent(drvPtr, sock, connPtr);
    } else {
	err = SockReadLine(drvPtr, sock, connPtr);
    }

    /*
     * If the request+headers have been received, check that all content
     * has been received (truncating any extra \r\n and rewinding
     * file-based content) and/or invoke read filter callbacks.
     */

    if (!err && (connPtr->flags & NS_CONN_READHDRS)) {
    	if (!RunFilters(connPtr, NS_FILTER_READ)) {
	    err = E_FILTER;
	} else if (connPtr->avail >= (size_t) connPtr->contentLength) {
	    connPtr->avail = connPtr->contentLength;
	    if (!(connPtr->flags & NS_CONN_FILECONTENT)) {
		connPtr->content[connPtr->avail] = '\0';
	    } else {
		if (ftruncate(connPtr->tfd, connPtr->avail) != 0) {
		    err = E_FDTRUNC;
		} else if (lseek(connPtr->tfd, (off_t) 0, SEEK_SET) != 0) {
		    err = E_FDSEEK;
		}
	    }
	    if (!err) {
		SockState(sockPtr, SOCK_PREQUE);
	    }
	}
    }

    if (err) {
	if (sockPtr->state != SOCK_CLOSEREQ) {
	    SockState(sockPtr, SOCK_ERROR);
	}
	LogReadError(connPtr, err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SockReadLine --
 *
 *	Read the next line of content from the connection, used during
 *	request and header processing.
 *
 * Results:
 *	0 if ok or an I/O error code.
 *
 * Side effects:
 *	When request and headers are read, will setup the connection for
 *	reading remaining content (if any) and for connection processing
 *	based on the cooresponding limits.
 *
 *----------------------------------------------------------------------
 */

static ReadErr
SockReadLine(Driver *drvPtr, Ns_Sock *sock, Conn *connPtr)
{
    Tcl_DString *bufPtr;
    NsServer *servPtr;
    Ns_Request *request;
    ServerMap *mapPtr;
    Tcl_HashEntry *hPtr;
    struct iovec buf;
    char *s, *e, *hdr, save;
    int len, n, max;

    /*
     * Setup the request buffer and read more input.
     */

    bufPtr = &connPtr->ibuf;
    len = bufPtr->length;
    if (len >= drvPtr->maxinput) {
	return E_RRANGE;
    }
    max = bufPtr->spaceAvl - 1;
    if (max < drvPtr->bufsize) {
        max += drvPtr->bufsize;
        if (max > drvPtr->maxinput) {
            max = drvPtr->maxinput;
        }
    }
    Tcl_DStringSetLength(bufPtr, max);
    buf.iov_base = bufPtr->string + len;
    buf.iov_len = max - len;
    n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
    if (n < 0) {
	return E_RECV;
    } else if (n == 0) {
	return E_CLOSE;
    }
    len += n;
    Tcl_DStringSetLength(bufPtr, len);

    /*
     * Scan available content for lines until end-of-headers.
     */

    while (!(connPtr->flags & NS_CONN_READHDRS)) {
	/*
	 * Look for a newline past the current read offset. If the buffer
	 * does not include a full line, return now to request more input.
	 */

        s = bufPtr->string + connPtr->roff;
        e = strchr(s, '\n');
        if (e == NULL) {
            return E_NOERROR;
	}

	/*
	 * Check for max single line overflow.
	 */

	if ((e - s) > drvPtr->maxline) {
	    return E_LRANGE;
	}

	/*
	 * Update next read pointer to end of this line, trim any
	 * expected \r before the \n, and temporarily null-terminate
	 * the line.
	 */

        connPtr->roff += (e - s + 1);
	if (e > s && e[-1] == '\r') {
	    --e;
	}
        save = *e;
        *e = '\0';

	/*
 	 * On the first line, save the offsets for later request parsing,
	 * checking for pre-HTTP/1.0 requests.  Otherwise, parse the line
	 * as the next header.
	 */

        if (connPtr->rstart == NULL) {
	    connPtr->rstart = s;
	    connPtr->rend = e;
	    if (NsFindVersion(s, &connPtr->major, &connPtr->minor) == NULL
		    || connPtr->major < 1) {
        	connPtr->flags |= (NS_CONN_SKIPHDRS | NS_CONN_READHDRS);
	    }
	} else if (e > s) {
            if (Ns_ParseHeader(connPtr->headers, s, Preserve) != NS_OK) {
		return E_HINVAL;
	    }
	}

	/*
	 * Restore the line and check for end-of-headers.
	 */

        *e = save;
	if (e == s) {
            connPtr->flags |= NS_CONN_READHDRS;
	}
    }

    /*
     * With the request and headers read, setup the connection.  First,  
     * determine the virtual server and driver location, handling
     * Host: header based multi-server drivers.
     */
    
    servPtr = connPtr->drvPtr->servPtr;
    if (servPtr != NULL) {
    	connPtr->location = connPtr->drvPtr->location;
    } else {
    	hdr = Ns_SetIGet(connPtr->headers, "host");
	if (hdr == NULL) {
	    return E_NOHOST;
	}
	hPtr = Tcl_FindHashEntry(&hosts, hdr);
	if (hPtr == NULL) {
	    mapPtr = defMapPtr;
	} else {
	    mapPtr = Tcl_GetHashValue(hPtr);
	}
	if (mapPtr == NULL) {
	    return E_NOSERV;
	}
	servPtr = mapPtr->servPtr;
	connPtr->location = mapPtr->location;
    }
    connPtr->servPtr = servPtr;
    connPtr->server = servPtr->server;

    /*
     * Next, parse the request using the server-default URL encoding.
     */

    save = *connPtr->rend;
    *connPtr->rend = '\0';
    connPtr->request = request = Ns_ParseRequestEx(connPtr->rstart,
						   servPtr->urlEncoding);
    *connPtr->rend = save;
    if (request == NULL || request->method == NULL) {
	return E_RINVAL;
    }
    if (STREQ(request->method, "HEAD")) {
	connPtr->flags |= NS_CONN_SKIPBODY;
    }

    /*
     * Parse authorization header, if any.
     */

    hdr = Ns_SetIGet(connPtr->headers, "authorization");
    if (hdr != NULL) {
        s = hdr;
        while (*s != '\0' && !isspace(UCHAR(*s))) {
            ++s;
        }
        if (*s != '\0') {
            save = *s;
            *s = '\0';
            if (STRIEQ(hdr, "basic")) {
                e = s + 1;
                while (*e != '\0' && isspace(UCHAR(*e))) {
                    ++e;
                }
                len = strlen(e) + 3;
                connPtr->authUser = ns_malloc((size_t) len);
                len = Ns_HtuuDecode(e, (unsigned char *) connPtr->authUser, len);
                connPtr->authUser[len] = '\0';
                e = strchr(connPtr->authUser, ':');
                if (e != NULL) {
                    *e++ = '\0';
                    connPtr->authPasswd = e;
                }
            }
            *s = save;
        }
    }

    /*
     * Get limits and content length, checking for overflow.
     */
     
    connPtr->limitsPtr = NsGetRequestLimits(connPtr->server,
            				    request->method, request->url);
    hdr = Ns_SetIGet(connPtr->headers, "content-length");
    if (hdr == NULL) {
        len = 0;
    } else if (sscanf(hdr, "%d", &len) != 1 || len < 0) {
        return E_NINVAL;
    }
    if (len > (int) connPtr->limitsPtr->maxupload) {
        return E_CRANGE;
    }
    connPtr->contentLength = len;

    /*
     * Setup the connection to read remaining content (if any).
     */

    connPtr->avail = bufPtr->length - connPtr->roff;
    max = connPtr->roff + connPtr->contentLength + 2;	/* NB: Space for \r\n if present. */
    if (max < connPtr->drvPtr->maxinput) {
        /*
         * Content will fit at end of request buffer.
         */

        Tcl_DStringSetLength(bufPtr, max);
        connPtr->content = bufPtr->string + connPtr->roff;
    } else {
        /*
         * Content must overflow to a temp file.
         */

        connPtr->flags |= NS_CONN_FILECONTENT;
        connPtr->tfd = Ns_GetTemp();
        if (connPtr->tfd < 0) {
	    return E_FDAGAIN;
	}
	if (write(connPtr->tfd, bufPtr->string + connPtr->roff,
                    connPtr->avail) != connPtr->avail) {
            return E_FDWRITE;
        }
        Tcl_DStringSetLength(bufPtr, connPtr->roff);
    }
    return E_NOERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * SockReadContent --
 *
 *	Attempt to read available content into the pre-sized connection
 *	dstring buffer or a temp file for large requests.
 *
 * Results:
 *	0 if ok or an I/O error code.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static ReadErr
SockReadContent(Driver *drvPtr, Ns_Sock *sock, Conn *connPtr)
{
    struct iovec buf;
    char fbuf[4096];
    int n;

    /*
     * When reading content, allow for 2 bytes more then the expected
     * content to absorb the extra \r\n, if any, at the end of a POST
     * request.  Note this isn't guaranteed to work in the case the two
     * bytes would have arrived in the next packet.
     */

    buf.iov_len = connPtr->contentLength - connPtr->avail + 2;
    if (!(connPtr->flags & NS_CONN_FILECONTENT)) {
        buf.iov_base = connPtr->content + connPtr->avail;
    } else {
        buf.iov_base = fbuf;
	if (buf.iov_len > sizeof(fbuf)) {
	    buf.iov_len = sizeof(fbuf);
	}
    }
    n = (*drvPtr->proc)(DriverRecv, sock, &buf, 1);
    if (n < 0) {
	return E_RECV;
    } else if (n == 0) {
	return E_CLOSE;
    }
    if ((connPtr->flags & NS_CONN_FILECONTENT)
	    && write(connPtr->tfd, fbuf, n) != n) {
	return E_FDWRITE;
    }
    connPtr->avail += n;
    return E_NOERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnContent --
 *
 *	Return the connction content buffer, mapping the temp file
 *	if necessary.
 *
 *	NB: It may make better sense to update the various content
 *	reading/parsing code to handle true incremental reads from
 *	an open file instead of risking a potential mapping failure.
 *	The current approach keeps the code simple and flexible.
 *
 * Results:
 *	Pointer to start of content or NULL if mapping failed.
 *
 * Side effects:
 *	If nextPtr and/or availPtr are not NULL, they are updated
 *	with the next byte to read and remaining content available.
 *
 *----------------------------------------------------------------------
 */

char *
NsConnContent(Ns_Conn *conn, char **nextPtr, int *availPtr)
{
    Conn *connPtr = (Conn *) conn;

    if (connPtr->next == NULL) {
	if (connPtr->content == NULL && (conn->flags & NS_CONN_FILECONTENT)) {
	    connPtr->map = NsMap(connPtr->tfd, 0, conn->contentLength, 1,
				 &connPtr->maparg);
	    if (connPtr->map != NULL) {
	        connPtr->content = connPtr->map;
	    }
	}
	connPtr->next = connPtr->content;
    }
    if (connPtr->next != NULL) {
	if (nextPtr != NULL) {
	    *nextPtr = connPtr->next;
	}
	if (availPtr != NULL) {
	    *availPtr = connPtr->avail;
	}
    }
    return connPtr->content;
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnSeek --
 *
 *	Update the next read and available content counter.
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
NsConnSeek(Ns_Conn *conn, int count)
{
    Conn *connPtr = (Conn *) conn;
    
    connPtr->avail -= count;
    connPtr->next  += count;
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
    static unsigned int nextid = 0;
    Conn *connPtr;
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
    }

    /*
     * All elements of the Conn are zero'ed at the start of a connection
     * except the I/O buffers and the items set below.
     */

    connPtr->tfd = -1;
    connPtr->headers = Ns_SetCreate(NULL);
    connPtr->outputheaders = Ns_SetCreate(NULL);
    Tcl_InitHashTable(&connPtr->files, TCL_STRING_KEYS);
    connPtr->drvPtr = drvPtr;
    connPtr->times.accept = *nowPtr;
    connPtr->id = id;
    sprintf(connPtr->idstr, "cns%u", connPtr->id);
    connPtr->port = ntohs(sockPtr->sa.sin_port);
    strcpy(connPtr->peer, ns_inet_ntoa(sockPtr->sa.sin_addr));
    connPtr->times.accept = sockPtr->acceptTime;
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
FreeConn(Conn *connPtr)
{
    size_t zlen;
    Ns_Conn *conn = (Ns_Conn *) connPtr;

    /*
     * Call CLS cleanups which may access other conn data.
     */

    NsClsCleanup(connPtr);

    /*
     * Free resources which may have been allocated during the request.
     */

    if (connPtr->type != NULL) {
        Ns_ConnSetType(conn, NULL);
    }
    if (connPtr->query != NULL) {
        Ns_ConnClearQuery(conn);
    }
    if (conn->request != NULL) {
    	Ns_FreeRequest(conn->request);
    }
    if (connPtr->map != NULL) {
	NsUnMap(connPtr->map, connPtr->maparg);
    }
    if (connPtr->tfd != -1) {
        Ns_ReleaseTemp(connPtr->tfd);
    }
    if (connPtr->authUser != NULL) {
        ns_free(connPtr->authUser);
    }
    Ns_SetFree(connPtr->headers);
    Ns_SetFree(connPtr->outputheaders);

    /*
     * Truncate the I/O buffers, zero remaining elements of the Conn,
     * and return the Conn to the free list.
     *
     */

    Ns_DStringTrunc(&connPtr->obuf, 0);
    Ns_DStringTrunc(&connPtr->ibuf, 0);
    zlen = (size_t) ((char *) &connPtr->ibuf - (char *) connPtr);
    memset(connPtr, 0, zlen);

    Ns_MutexLock(&connlock);
    connPtr->nextPtr = firstConnPtr;
    firstConnPtr = connPtr;
    Ns_MutexUnlock(&connlock);
}


/*
 *----------------------------------------------------------------------
 *
 * RunPreQueues --
 *
 *	Execute any pre-queue callbacks, freeing any allocated interp.
 *
 * Results:
 *	1 if ok, 0 otherwise.
 *
 * Side effects:
 *	Depends on callbacks, if any.
 *
 *----------------------------------------------------------------------
 */

static int
RunFilters(Conn *connPtr, int why)
{
    int status;

    status = NsRunFilters((Ns_Conn *) connPtr, why);
    return (status == NS_OK ? 1 : 0);
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
	    SockRead(sockPtr);
        } while (sockPtr->state == SOCK_READWAIT);

	/*
	 * Return the connection to the driver thread, freeing
	 * any connection interp which may have been allocated
	 * by read filter callbacks.
	 */

	NsFreeConnInterp(sockPtr->connPtr);
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


/*
 *----------------------------------------------------------------------
 *
 * LogReadError --
 *
 *	Log an extended error message on unusual I/O conditions.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Message to server log.
 *
 *----------------------------------------------------------------------
 */

static void
LogReadError(Conn *connPtr, ReadErr err)
{
    Sock *sockPtr = connPtr->sockPtr;
    char *msg, *fmt;

    if (!(sockPtr->drvPtr->flags & DRIVER_DEBUG)) {
	return;
    }
    switch (err) {
    case E_NOERROR:		
	msg = "no error";
	break;
    case E_CLOSE:		
	msg = "client close";
	break;
    case E_RECV:		
	msg = "recv failed";
	break;
    case E_FDAGAIN:		
	msg = "fd unavailable";
	break;
    case E_FDWRITE:		
	msg = "fd write failed";
	break;
    case E_FDTRUNC:		
	msg = "fd truncate failed";
	break;
    case E_FDSEEK:		
	msg = "fd seek failed";
	break;
    case E_NOHOST:		
	msg = "no host header";
	break;
    case E_NOSERV:		
	msg = "no such host";
	break;
    case E_HINVAL:		
	msg = "invalid header";
	break;
    case E_RINVAL:		
	msg = "invalid request";
	break;
    case E_NINVAL:		
	msg = "invalid content-length";
	break;
    case E_LRANGE:		
	msg = "max line exceeded";
	break;
    case E_RRANGE:		
	msg = "max request exceeded";
	break;
    case E_CRANGE:		
	msg = "max content exceeded";
	break;
    case E_FILTER:		
	msg = "filter error or abort result";
	break;
    case E_QUEWAIT:		
	msg = "attempt to register quewait outside driver thread";
	break;
    default:
	msg = "unknown error";
    }
    switch (err) {
    case E_RECV:		
    case E_FDWRITE:		
    case E_FDTRUNC:		
    case E_FDSEEK:		
	fmt = "conn[%d]: %s: %s";
	break;
    default:
	fmt = "conn[%d]: %s";
	break;
    }
    Ns_Log(Error, fmt, connPtr->id, msg, strerror(errno));
}

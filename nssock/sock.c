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
 * nssock.c --
 *
 *	Routines for the nssock and nsssl socket drivers.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nssock/Attic/sock.c,v 1.6 2000/09/29 13:42:39 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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

#define BUSY \
	"HTTP/1.0 503 Server Busy\r\n"\
	"Content-type: text/html\r\n"\
	"\r\n"\
	"<html>\n"\
	"<head><title>Server Busy</title></head>\n"\
	"<body>\n"\
	"<h2>Server Busy</h2>\n"\
	"The server is temporarily too busy to process your request. "\
        "Please try again later.\n"\
	"</body>\n"\
	"</html>\n"

struct ConnData;

typedef struct SockDrv {
    struct SockDrv *nextPtr;
    struct ConnData *firstFreePtr;
    Ns_Mutex	 lock;
    int		 refcnt;
    Ns_Driver	 driver;
    char        *name;
    char        *location;
    char        *address;
    char        *bindaddr;
    int          port;
    int     	 bufsize;
    int     	 timeout;
    int     	 closewait;
    SOCKET       lsock;
#ifdef SSL
    void        *server;
#endif
} SockDrv;

typedef struct ConnData {
    struct ConnData *nextPtr;
    struct SockDrv  *sdPtr;
    SOCKET	sock;
    char	peer[16];
    int		port;
    int		nrecv;
    int		nsend;
#ifdef SSL
    void       *conn;
#else
    int		cnt;
    char       *base;
    char	buf[1];
#endif
} ConnData;

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc SockThread;
static void SockFreeConn(SockDrv *sdPtr, ConnData *cdPtr);
static SockDrv *firstSockDrvPtr;
static Ns_Thread sockThread;
static SOCKET trigPipe[2];

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
#ifdef SSL 
static Ns_ConnInitProc SockInit; 
#endif 
#ifdef HAVE_SENDV
static Ns_ConnSendFdProc SockSendFd;
#endif

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
#ifdef SSL 
    {Ns_DrvIdInit,         (void *) SockInit}, 
#endif 
#ifdef HAVE_SENDV
    {Ns_DrvIdSendFd,       (void *) SockSendFd},
#endif
    {0,                    NULL}
};

NS_EXPORT int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Sock module init routine.
 *
 * Results:
 *	NS_OK if initialized ok, NS_ERROR otherwise.
 *
 * Side effects:
 *	Calls Ns_RegisterLocation as specified by this instance
 *	in the config file.
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
    SockDrv *sdPtr;

#ifdef SSL 
    char *cert, *key; 
#endif 
    
    /*
     * Determine the hostname used for the local address to bind
     * to and/or the HTTP location string.
     */

    path = Ns_ConfigGetPath(server, name, NULL);

#ifdef SSL 
    if (NsSSLInitialize(server, name) != NS_OK) { 
        Ns_Log(Error, "%s: failed to initialize ssl driver", DRIVER_NAME);
        return NS_ERROR; 
    } 
    cert = Ns_ConfigGet(path, "certfile"); 
    if (cert == NULL) { 
        Ns_Log(Warning, "%s: certfile not specified", DRIVER_NAME); 
        return NS_OK; 
    } 
#endif 
    
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
     * Determine the port and then set the HTTP location string either
     * as specified in the config file or constructed from the
     * hostname and port.
     */

    sdPtr = ns_calloc(1, sizeof(SockDrv));

#ifdef SSL 
    key = Ns_ConfigGet(path, "keyfile"); 
    sdPtr->server = NsSSLCreateServer(cert, key); 
    if (sdPtr->server == NULL) { 
        ns_free(sdPtr); 
        return NS_ERROR; 
    } 
    sdPtr->bufsize = 0; 
#else 
    if (!Ns_ConfigGetInt(path, "bufsize", &n) || n < 1) { 
        n = 16000; 
    } 
    sdPtr->bufsize = n; 
#endif 
    Ns_MutexSetName2(&sdPtr->lock, DRIVER_NAME, name);
    sdPtr->refcnt = 1;
    sdPtr->lsock = INVALID_SOCKET;
    sdPtr->name = name;
    sdPtr->bindaddr = bindaddr;
    sdPtr->address = ns_strdup(address);
    if (!Ns_ConfigGetInt(path, "port", &sdPtr->port)) {
	sdPtr->port = DEFAULT_PORT;
    }
    sdPtr->location = Ns_ConfigGet(path, "location");
    if (sdPtr->location != NULL) {
	sdPtr->location = ns_strdup(sdPtr->location);
    } else {
    	Ns_DStringInit(&ds);
	Ns_DStringVarAppend(&ds, DEFAULT_PROTOCOL "://", host, NULL);
	if (sdPtr->port != DEFAULT_PORT) {
	    Ns_DStringPrintf(&ds, ":%d", sdPtr->port);
	}
	sdPtr->location = Ns_DStringExport(&ds);
    }
    if (!Ns_ConfigGetInt(path, "socktimeout", &n) || n < 1) {
	n = 30;
    }
    sdPtr->timeout = n;
    if (!Ns_ConfigGetInt(path, "closewait", &n)) {
	n = 2000;		/* 2k milliseconds */
    }
    sdPtr->closewait = n * 1000;
    sdPtr->driver = Ns_RegisterDriver(server, name, sockProcs, sdPtr);
    if (sdPtr->driver == NULL) {
	SockFreeConn(sdPtr, NULL);
	return NS_ERROR;
    }
    sdPtr->nextPtr = firstSockDrvPtr;
    firstSockDrvPtr = sdPtr;

#ifdef SSL
#ifdef SSL_EXPORT 
    Ns_Log(Notice, "%s: initialized with 40-bit export encryption",
	   DRIVER_NAME);
#else 
    Ns_Log(Notice, "%s: initialized with 128-bit domestic encryption",
	   DRIVER_NAME);
#endif 
#endif

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockStart --
 *
 *	Configure and then start the SockThread servicing new
 *	connections.  This is the final initializiation routine
 *	called from main().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SockThread is created.
 *
 *----------------------------------------------------------------------
 */

static int
SockStart(char *server, char *label, void **drvDataPtr)
{
    SockDrv *sdPtr = *((SockDrv **) drvDataPtr);
    
    sdPtr->lsock = Ns_SockListen(sdPtr->bindaddr, sdPtr->port);
    if (sdPtr->lsock == INVALID_SOCKET) {
	Ns_Log(Error, "%s: failed to listen on %s:%d: '%s'",
	       sdPtr->name, sdPtr->address ? sdPtr->address : "*",
	       sdPtr->port, ns_sockstrerror(ns_sockerrno));
	return NS_ERROR;
    }
    if (sockThread == NULL) {
	if (ns_sockpair(trigPipe) != 0) {
	    Ns_Fatal("%s: ns_sockpair() failed: '%s'",
		     DRIVER_NAME, ns_sockstrerror(ns_sockerrno));
	}
	Ns_ThreadCreate(SockThread, NULL, 0, &sockThread);
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SockFreeConn --
 *
 *	Return a conneciton to the free list, decrement the driver
 *	refcnt, and free the driver if no longer in use.
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
SockFreeConn(SockDrv *sdPtr, ConnData *cdPtr)
{
    int refcnt;

    Ns_MutexLock(&sdPtr->lock);
    if (cdPtr != NULL) {
	cdPtr->nextPtr = sdPtr->firstFreePtr;
	sdPtr->firstFreePtr = cdPtr;
    }
    refcnt = --sdPtr->refcnt;
    Ns_MutexUnlock(&sdPtr->lock);

    if (refcnt == 0) {
    	ns_free(sdPtr->location);
    	ns_free(sdPtr->address);
	while ((cdPtr = sdPtr->firstFreePtr) != NULL) {
	    sdPtr->firstFreePtr = cdPtr->nextPtr;
	    ns_free(cdPtr);
	}

#ifdef SSL 
        NsSSLDestroyServer(sdPtr->server); 
#endif 
	Ns_MutexDestroy(&sdPtr->lock);
    	ns_free(sdPtr);
    }
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
 *	Connections are accepted on the configured listen sockets
 *	and placed on the run queue to be serviced.
 *
 *----------------------------------------------------------------------
 */

static void
SockThread(void *ignored)
{
    fd_set set;
    char c;
    int slen, n, stop;
    SockDrv *sdPtr, *nextPtr;
    ConnData *cdPtr;
    struct sockaddr_in sa;
    SOCKET max, sock;
    
    Ns_ThreadSetName("-" DRIVER_NAME "-");
    Ns_Log(Notice, "%s: waiting for startup", DRIVER_NAME);
    Ns_WaitForStartup();
    Ns_Log(Notice, "%s: starting", DRIVER_NAME);
    
    sdPtr = firstSockDrvPtr;
    firstSockDrvPtr = NULL;
    while (sdPtr != NULL) {
	nextPtr = sdPtr->nextPtr;
	if (sdPtr->lsock != INVALID_SOCKET) {
    	    Ns_Log(Notice, "%s: listening on %s (%s:%d)",
			    sdPtr->name, sdPtr->location,
	      		    sdPtr->address ? sdPtr->address : "*",
			    sdPtr->port);
    	    Ns_SockSetNonBlocking(sdPtr->lsock);
	    sdPtr->nextPtr = firstSockDrvPtr;
	    firstSockDrvPtr = sdPtr;
	}
	sdPtr = nextPtr;
    }

    Ns_Log(Notice, "%s: accepting connections", DRIVER_NAME);

    stop = 0;
    do {
	FD_ZERO(&set);
	FD_SET(trigPipe[0], &set);
	max = trigPipe[0];
    	sdPtr = firstSockDrvPtr;
	while (sdPtr != NULL) {
	    FD_SET(sdPtr->lsock, &set);
	    if (max < sdPtr->lsock) {
	        max = sdPtr->lsock;
	    }
	    sdPtr = sdPtr->nextPtr;
	}
	do {
	    n = select(max+1, &set, NULL, NULL, NULL);
	} while (n < 0  && ns_sockerrno == EINTR);
	if (n < 0) {
	    Ns_Fatal("%s: select() failed: '%s'",
		     DRIVER_NAME, ns_sockstrerror(ns_sockerrno));
	} else if (FD_ISSET(trigPipe[0], &set)) {
	    if (recv(trigPipe[0], &c, 1, 0) != 1) {
	    	Ns_Fatal("%s: trigger recv() failed: '%s'",
			 DRIVER_NAME, ns_sockstrerror(ns_sockerrno));
	    }
	    stop = 1;
	    --n;
	}
	
	sdPtr = firstSockDrvPtr;
	while (n > 0 && sdPtr != NULL) {
	    if (FD_ISSET(sdPtr->lsock, &set)) {
		--n;
    		slen = sizeof(sa);
    		sock = Ns_SockAccept(sdPtr->lsock, (struct sockaddr *) &sa, &slen);
		if (sock != INVALID_SOCKET) {
    	    	    Ns_SockSetNonBlocking(sock);
		    Ns_MutexLock(&sdPtr->lock);
		    ++sdPtr->refcnt;
		    cdPtr = sdPtr->firstFreePtr;
		    if (cdPtr != NULL) {
			sdPtr->firstFreePtr = cdPtr->nextPtr;
		    }
		    Ns_MutexUnlock(&sdPtr->lock);
		    if (cdPtr == NULL) {
			cdPtr = ns_malloc(sizeof(ConnData) + sdPtr->bufsize);
		    }
		    cdPtr->sdPtr = sdPtr;
		    cdPtr->sock = sock;
		    cdPtr->port = ntohs(sa.sin_port);
#ifdef SSL 
                    cdPtr->conn = NULL; 
#else 
		    cdPtr->cnt = cdPtr->nrecv = cdPtr->nsend = 0;
		    cdPtr->base = cdPtr->buf;
#endif
		    strcpy(cdPtr->peer, ns_inet_ntoa(sa.sin_addr));
		    if (Ns_QueueConn(sdPtr->driver, cdPtr) != NS_OK) {
#ifndef SSL
			(void) send(sock, BUSY, sizeof(BUSY), 0);
			Ns_Log(Warning, "%s: server too busy: "
			       "request failed for peer %s",
			       DRIVER_NAME, cdPtr->peer);
#endif
			(void) SockClose(cdPtr);
		    }
	    	}
	    }
	    sdPtr = sdPtr->nextPtr;
	}
    } while (!stop);

    while ((sdPtr = firstSockDrvPtr) != NULL) {
	firstSockDrvPtr = sdPtr->nextPtr;
	Ns_Log(Notice, "%s: closing %s", sdPtr->name, sdPtr->location);
	ns_sockclose(sdPtr->lsock);
	SockFreeConn(sdPtr, NULL);
    }

    ns_sockclose(trigPipe[0]);
    ns_sockclose(trigPipe[1]);
}


/*
*----------------------------------------------------------------------
 *
 * SockStop --
 *
 *	Trigger the SockThread to shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SockThread will close ports.
 *
 *----------------------------------------------------------------------
 */
static void
SockStop(void *arg)
{
SockDrv *sdPtr = (SockDrv *) arg;
    
    if (sockThread != NULL) {
    	Ns_Log(Notice, "%s: exiting: triggering shutdown", DRIVER_NAME);
	if (send(trigPipe[1], "", 1, 0) != 1) {
	    Ns_Fatal("%s: trigger send() failed: %s",
		     DRIVER_NAME, ns_sockstrerror(ns_sockerrno));
	}
	Ns_ThreadJoin(&sockThread, NULL);
	sockThread = NULL;
    	Ns_Log(Notice, "%s: exiting: shutdown complete", DRIVER_NAME);
	}
    }


/*
 *----------------------------------------------------------------------
 *
 * SockClose --
 *
 *	Close the socket 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Socket will be closed and buffer returned to free list.
 *
 *----------------------------------------------------------------------
 */

static int
SockClose(void *arg)
{
    ConnData *cdPtr = arg;
    SockDrv *sdPtr = cdPtr->sdPtr;
    
    if (cdPtr->sock != INVALID_SOCKET) {
	
#ifdef SSL 
        if (cdPtr->conn != NULL) { 
            (void) NsSSLFlush(cdPtr->conn); 
            NsSSLDestroyConn(cdPtr->conn); 
            cdPtr->conn = NULL; 
        } 
#endif 

	/*
	 * Some clients will have trouble if we simply
	 * close the socket here so instead a shutdown()
	 * followed by draining any unacknowledge data
	 * is used.
	 */

	if (cdPtr->nrecv > 0
	    && cdPtr->sdPtr->closewait > 0
	    && shutdown(cdPtr->sock, 1) == 0) {
	    char drain[1024];
	    Ns_Time now, wait, diff;
	    int n;
	    fd_set set;
	    struct timeval tv;

	    Ns_GetTime(&wait);
	    Ns_IncrTime(&wait, 0, cdPtr->sdPtr->closewait);
	    do {
		Ns_GetTime(&now);
		Ns_DiffTime(&wait, &now, &diff);
		if (diff.sec < 0) {
		    break;
		}
		tv.tv_sec = diff.sec;
		tv.tv_usec = diff.usec;
		FD_ZERO(&set);
		FD_SET(cdPtr->sock, &set);
		n = select(cdPtr->sock + 1, &set, NULL, NULL, &tv);
	    } while (n == 1 && recv(cdPtr->sock, drain, sizeof(drain), 0) > 0);
	}
	ns_sockclose(cdPtr->sock);
	cdPtr->sock = INVALID_SOCKET;
    }
    SockFreeConn(cdPtr->sdPtr, cdPtr);
    return NS_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * SockRead --
 *
 *	Read from the socket 
 *
 * Results:
 *	# bytes read 
 *
 * Side effects:
 *	Will read from socket 
 *
 *----------------------------------------------------------------------
 */

static int
SockRead(void *arg, void *vbuf, int toread)
{
    ConnData   *cdPtr = arg;

#ifdef SSL 
    /* 
     * SSL returns immediately. 
     */ 
    return NsSSLRecv(cdPtr->conn, vbuf, toread); 
#else 
    
    char       *buf = (char *) vbuf;
    int		nread;
    int         tocopy;
    
    nread = 0;
    while (toread > 0) {
        if (cdPtr->cnt > 0) {
            if (cdPtr->cnt > toread) {
                tocopy = toread;
            } else {
                tocopy = cdPtr->cnt;
            }
            memcpy(buf, cdPtr->base, tocopy);
            cdPtr->base += tocopy;
            cdPtr->cnt -= tocopy;
            toread -= tocopy;
            nread += tocopy;
            buf += tocopy;
        }
        if (toread > 0) {
            cdPtr->base = cdPtr->buf;
	    cdPtr->cnt = Ns_SockRecv(cdPtr->sock, cdPtr->buf,
		cdPtr->sdPtr->bufsize, cdPtr->sdPtr->timeout);
	    if (cdPtr->cnt <= 0) {
		return -1;
	    }
	    cdPtr->nrecv += cdPtr->cnt;
    	}
    }
    return nread;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * SockWrite --
 *
 *	Writes data to a socket.
 *	NOTE: This may not write all of the data you send it!
 *
 * Results:
 *	Number of bytes written, -1 for error 
 *
 * Side effects:
 *	Bytes may be written to a socket
 *
 *----------------------------------------------------------------------
 */

static int
SockWrite(void *arg, void *buf, int towrite)
{
    ConnData   *cdPtr = arg;
    int nsend;

#ifdef SSL 
    nsend = NsSSLSend(cdPtr->conn, buf, towrite); 
#else
    nsend = Ns_SockSend(cdPtr->sock, buf, towrite, cdPtr->sdPtr->timeout);
#endif

    if (nsend > 0) {
	cdPtr->nsend += nsend;
    }
    return nsend;
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
    ConnData   *cdPtr = arg;

    return cdPtr->sdPtr->address;
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
    ConnData   *cdPtr = arg;

    return cdPtr->sdPtr->port;
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
    ConnData   *cdPtr = arg;

    return cdPtr->sdPtr->name;
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
    ConnData   *cdPtr = arg;

    return cdPtr->peer;
}


/*
 *----------------------------------------------------------------------
 *
 * SockConnectionFd --
 *
 *	Get the socket fd 
 *
 * Results:
 *	The socket fd 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
SockConnectionFd(void *arg)
{
    ConnData   *cdPtr = arg;

#ifdef SSL 
    if (cdPtr->conn == NULL || (NsSSLFlush(cdPtr->conn) != NS_OK)) { 
        return -1; 
    } 
#endif 

    return (int) cdPtr->sock;
}


/*
 *----------------------------------------------------------------------
 *
 * SockDetach --
 *
 *	Detach the connection data from this conneciton for keep-alive.
 *
 * Results:
 *	Pointer to connection data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void *
SockDetach(void *arg)
{
    ConnData   *cdPtr = arg;

    cdPtr->nrecv = cdPtr->nsend = 0;
    return arg;
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
    ConnData   *cdPtr = arg;

    return cdPtr->port;
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
    ConnData   *cdPtr = arg;

    return cdPtr->sdPtr->location;
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
 *      Data may be written to a socket. 
 * 
 *      NOTE: This is currently only implemented for 
 *      UnixWare. 
 * 
 *---------------------------------------------------------------------- 
 */ 
#ifdef SSL 
static int 
SockInit(void *arg) 
{ 
    ConnData   *cdPtr = arg; 
 
    if (cdPtr->conn == NULL) { 
        cdPtr->conn = NsSSLCreateConn(cdPtr->sock, cdPtr->sdPtr->timeout, 
                                      cdPtr->sdPtr->server); 
        if (cdPtr->conn == NULL) { 
            return NS_ERROR; 
        } 
    } 
    return NS_OK; 
} 
#endif 



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
    ConnData *cdPtr; 
    char *name; 
 
    if (conn != NULL) { 
        name = Ns_ConnDriverName(conn); 
        if (name != NULL && STREQ(name, DRIVER_NAME)) { 
            cdPtr = Ns_ConnDriverContext(conn); 
            if (cdPtr != NULL) { 
                return cdPtr->conn; 
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
 *      Sends the contents of a file to a socket.
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	Stuff may be written to a socket.
 *
 *      NOTE: This is currently only implemented for
 *      UnixWare.
 *
 *----------------------------------------------------------------------
 */

#ifdef HAVE_SENDV
static int
SockSendFd(void *arg, int fd, int nsend)
{
    ConnData   *cdPtr = arg;
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
        n = sendv(cdPtr->sock, vec, 1);
        if (n < 0 && ns_sockerrno == EWOULDBLOCK
	    && Ns_SockWait(cdPtr->sock, NS_SOCK_WRITE, cdPtr->sdPtr->timeout) == NS_OK) {
            n = sendv(cdPtr->sock, vec, 1);
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

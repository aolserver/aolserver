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
 * nsssl.c --
 *
 *	Call internal Ns_DriverInit.
 *
 */

#include "ns.h"
#include "ssl.h"

#ifdef SSL_EXPORT 
#define DRIVER_NAME "nsssle"
#else
#define DRIVER_NAME "nsssl"
#endif

static Ns_DriverProc SSLProc;

NS_EXPORT int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Initialize the SSL driver.
 *
 * Results:
 *	NS_OK if initialized ok, NS_ERROR otherwise.
 *
 * Side effects:
 *	See Ns_DriverInit.
 *
 *----------------------------------------------------------------------
 */

NS_EXPORT int
Ns_ModuleInit(char *server, char *module)
{
    Ns_DriverInitData *init;
    char *cert, *key, *path;
    void *dssl;
    int n;

    init = ns_calloc(1, sizeof(Ns_DriverInitData));
    if (init == NULL) {
        Ns_Log(Error, "%s: Memory allocation failure in Ns_ModuleInit",
                module);
        return NS_ERROR;
    }

    /*
     * Initialize the global and per-driver SSL.
     */

    path = Ns_ConfigGetPath(server, module, NULL);
    if (NsSSLInitialize(server, module) != NS_OK) { 
        Ns_Log(Error, "%s: failed to initialize ssl driver", module);
        return NS_ERROR; 
    } 
    cert = Ns_ConfigGet(path, "certfile"); 

    if (cert == NULL) { 
        Ns_Log(Warning, "%s: certfile not specified", module); 
        return NS_OK; 
    } 
#ifdef SSL_EXPORT 
    n = 40;
#else
    n = 128;
#endif
    Ns_Log(Notice, "%s: initialized with %d-bit encryption", module, n);

#ifdef HAVE_SWIFT
    Ns_Log(Notice, "%s: SUPPORTS RAINBOW CRYPTOSWIFT HARDWARE SSL ACCELERATOR",
	   module);
#endif
    key = Ns_ConfigGet(path, "keyfile"); 
    dssl = NsSSLCreateServer(cert, key);
    if (dssl == NULL) {
        return NS_ERROR;
    }

    /*
     * Initialize the driver without the async option so all I/O
     * happens in the connection thread, avoiding any possible
     * blocking in the driver thread due to SSL overhead.  Set
     * the SSL option to use the SSL port and protocol defaults.
     */

    init->version = NS_DRIVER_VERSION_1;
    init->name = DRIVER_NAME;
    init->proc = SSLProc;
    init->arg = dssl;
    init->opts = NS_DRIVER_SSL;
    init->path = NULL;

    return Ns_DriverInit(server, module, init);
}


/*
 *----------------------------------------------------------------------
 *
 * SSLProc --
 *
 *	SSL driver callback proc.  This driver performs the necessary
 *	handshake and encryption of SSL.
 *
 * Results:
 *	For close, always 0.  For keep, 0 if connection could be
 *	properly flushed, -1 otherwise.  For send and recv, # of bytes
 *	processed or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
SSLProc(Ns_DriverCmd cmd, Ns_Sock *sock, struct iovec *bufs, int nbufs)
{
    Ns_Driver *driver = sock->driver;
    int n, total;

    switch (cmd) {
    case DriverRecv:
    case DriverSend:
	/*
	 * On first I/O, initialize the connection context.
	 */

	if (sock->arg == NULL) { 
	    n = driver->recvwait;
	    if (n > driver->sendwait) {
		n = driver->sendwait;
	    }
	    sock->arg = NsSSLCreateConn(sock->sock, n, driver->arg);
	    if (sock->arg == NULL) { 
		return -1;
	    } 
	}

	/*
	 * Process each buffer one at a time.
	 */

	total = 0;
	do {
	    if (cmd == DriverSend) {
		n = NsSSLSend(sock->arg, bufs->iov_base, bufs->iov_len);
	    } else {
		n = NsSSLRecv(sock->arg, bufs->iov_base, bufs->iov_len);
	    }
	    if (n < 0 && total > 0) {
		/* NB: Mask error if some bytes were read. */
		n = 0;
	    }
	    ++bufs;
	    total += n;
	} while (n > 0 && --nbufs > 0);
	n = total;
	break;

    case DriverKeep:
	if (sock->arg != NULL && NsSSLFlush(sock->arg) == NS_OK) {
	    n = 0;
	} else {
	    n = -1;
	}
	break;

    case DriverClose:
	if (sock->arg != NULL) { 
	    (void) NsSSLFlush(sock->arg); 
	    NsSSLDestroyConn(sock->arg); 
	    sock->arg = NULL;
	}
	n = 0;
	break;

    default:
	/* Unsupported command. */
	n = -1;
	break;
    }
    return n;
}

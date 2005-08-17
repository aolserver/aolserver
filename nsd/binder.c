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
 *Support for pre-bound privileged ports.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/binder.c,v 1.18 2005/08/17 21:18:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Locals defined in this file
 */

static void PreBind(char *line);
static Tcl_HashTable prebound;
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * NsSockGetBound --
 *
 *	Get a pre-bound socket if available.
 *
 * Results:
 *	Socket descriptor or INVALID_SOCKET on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
NsSockGetBound(struct sockaddr_in *saPtr)
{
    SOCKET sock = INVALID_SOCKET;
    Tcl_HashEntry *hPtr;

    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&prebound, (char *) saPtr);
    if (hPtr != NULL) {
	sock = (int) Tcl_GetHashValue(hPtr);
	Tcl_DeleteHashEntry(hPtr);
    }
    Ns_MutexUnlock(&lock);
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitBinder --
 *
 *	Initialize the pre-bind table.
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
NsInitBinder(void)
{
    Ns_MutexSetName(&lock, "ns:binder");
    Tcl_InitHashTable(&prebound, sizeof(struct sockaddr_in)/sizeof(int));
}


/*
 *----------------------------------------------------------------------
 *
 * NsPreBind --
 *
 *	Pre-bind any requested ports, called from Ns_Main at startup.
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
NsPreBind(char *args, char *file)
{
    char line[1024];
    FILE *fp;

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
 * NsClosePreBound --
 *
 *	Close any remaining pre-bound sockets.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pre-bound sockets closed.
 *
 *----------------------------------------------------------------------
 */

void
NsClosePreBound(void)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *addr;
    int port, sock;
    struct sockaddr_in *saPtr;

    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&prebound, &search);
    while (hPtr != NULL) {
	saPtr = (struct sockaddr_in *) Tcl_GetHashKey(&prebound, hPtr);
	addr = ns_inet_ntoa(saPtr->sin_addr);
	port = htons(saPtr->sin_port);
	sock = (int) Tcl_GetHashValue(hPtr);
	Ns_Log(Warning, "prebind: closed unused: %s:%d = %d", addr, port, sock);
	close(sock);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&prebound);
    Tcl_InitHashTable(&prebound, sizeof(struct sockaddr_in)/sizeof(int));
    Ns_MutexUnlock(&lock);
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
    Tcl_HashEntry *hPtr;
    int new, sock, port;
    struct sockaddr_in sa;
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
	    hPtr = Tcl_CreateHashEntry(&prebound, (char *) &sa, &new);
	    if (!new) {
		err = "duplicate entry";
	    } else if ((sock = Ns_SockBind(&sa)) == -1) {
		Tcl_DeleteHashEntry(hPtr);
	    	err = strerror(errno);
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
	    Ns_Log(Error, "prebind: invalid entry: %s: %s", ent, err);
	} else {
	    Ns_Log(Notice, "prebind: bound: %s", ent);
	}
	ent = p;
    } while (ent != NULL);
}

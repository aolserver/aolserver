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
 * listen.c --
 *
 *	Listen on sockets and register callbacks for incoming 
 *	connections. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/listen.c,v 1.9 2005/07/18 23:32:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * This the context used by the socket callback.
 */

typedef struct ListenData {
    Ns_SockProc *proc;
    void        *arg;
} ListenData;

/*
 * Local functions defined in this file
 */

static Ns_SockProc  ListenProc; 

/*
 * Static variables defined in this file
 */

static Tcl_HashTable portsTable;      /* Table of per-port data. */
static Ns_Mutex      lock;            /* Lock around portsTable. */


/*
 *----------------------------------------------------------------------
 *
 * NsInitListen --
 *
 *	Initialize listen callback API.
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
NsInitListen(void)
{
    Ns_MutexInit(&lock);
    Ns_MutexSetName(&lock, "ns:listencallbacks");
    Tcl_InitHashTable(&portsTable, TCL_ONE_WORD_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockListenCallback --
 *
 *	Listen on an address/port and register a callback to be run 
 *	when connections come in on it. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockListenCallback(char *addr, int port, Ns_SockProc *proc, void *arg)
{
    Tcl_HashTable      *tablePtr = NULL;
    Tcl_HashEntry      *hPtr;
    ListenData         *ldPtr;
    SOCKET              new, sock;
    int                 status;
    struct sockaddr_in  sa;

    if (Ns_GetSockAddr(&sa, addr, port) != NS_OK) {
        return NS_ERROR;
    }
    if (addr != NULL) {
        /*
	 * Make sure we can bind to the specified interface.
	 */
	
        sa.sin_port = 0;
        sock = Ns_SockBind(&sa);
        if (sock == INVALID_SOCKET) {
            return NS_ERROR;
        }
        ns_sockclose(sock);
    }
    status = NS_OK;
    Ns_MutexLock(&lock);

    /*
     * Update the global hash table that keeps track of which ports
     * we're listening on.
     */
  
    hPtr = Tcl_CreateHashEntry(&portsTable, (char *) port, &new);
    if (new == 0) {
        tablePtr = Tcl_GetHashValue(hPtr);
    } else {
        sock = Ns_SockListen(NULL, port);
        if (sock == INVALID_SOCKET) {
            Tcl_DeleteHashEntry(hPtr);
            status = NS_ERROR;
        } else {
            Ns_SockSetNonBlocking(sock);
            tablePtr = ns_malloc(sizeof(Tcl_HashTable));
            Tcl_InitHashTable(tablePtr, TCL_ONE_WORD_KEYS);
            Tcl_SetHashValue(hPtr, tablePtr);
            Ns_SockCallback(sock, ListenProc, tablePtr,
			    NS_SOCK_READ | NS_SOCK_EXIT);
        }
    }
    if (status == NS_OK) {
        hPtr = Tcl_CreateHashEntry(tablePtr, (char *) sa.sin_addr.s_addr, &new);
        if (!new) {
            status = NS_ERROR;
        } else {
            ldPtr = ns_malloc(sizeof(ListenData));
            ldPtr->proc = proc;
            ldPtr->arg = arg;
            Tcl_SetHashValue(hPtr, ldPtr);
        }
    }
    Ns_MutexUnlock(&lock);
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SockPortBound --
 *
 *	Determine if we're already listening on a given port on any 
 *	address. 
 *
 * Results:
 *	Boolean: true=yes, false=no. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_SockPortBound(int port)
{
    Tcl_HashEntry  *hPtr;

    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&portsTable, (char *) port);
    Ns_MutexUnlock(&lock);
    return (hPtr != NULL ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * ListenProc --
 *
 *	This is a wrapper callback that runs the user's callback iff 
 *	a valid socket exists. 
 *
 * Results:
 *	NS_TRUE or NS_FALSE 
 *
 * Side effects:
 *	May close the socket if no user context can be found. 
 *
 *----------------------------------------------------------------------
 */

static int
ListenProc(SOCKET sock, void *arg, int why)
{
    struct sockaddr_in  sa;
    int                 len;
    Tcl_HashTable      *tablePtr;
    Tcl_HashEntry      *hPtr;
    SOCKET              new;
    ListenData          *ldPtr;

    tablePtr = arg;
    if (why == NS_SOCK_EXIT) {
        ns_sockclose(sock);
        return NS_FALSE;
    }
    new = Ns_SockAccept(sock, NULL, NULL);
    if (new != INVALID_SOCKET) {
        Ns_SockSetBlocking(new);
        len = sizeof(sa);
        getsockname(new, (struct sockaddr *) &sa, &len);
        ldPtr = NULL;
        Ns_MutexLock(&lock);
        hPtr = Tcl_FindHashEntry(tablePtr, (char *) sa.sin_addr.s_addr);
        if (hPtr == NULL) {
            hPtr = Tcl_FindHashEntry(tablePtr, (char *) INADDR_ANY);
        }
        if (hPtr != NULL) {
            ldPtr = Tcl_GetHashValue(hPtr);
        }
        Ns_MutexUnlock(&lock);
        if (ldPtr == NULL) {
            ns_sockclose(new);
        } else {
            (*ldPtr->proc) (new, ldPtr->arg, why);
        }
    }
    return NS_TRUE;
}

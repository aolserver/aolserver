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
 * dns.c --
 *
 *      DNS lookup routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/dns.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#ifndef INADDR_NONE
#define INADDR_NONE (-1)
#endif

#ifndef NETDB_INTERNAL
#  ifdef h_NETDB_INTERNAL
#  define NETDB_INTERNAL h_NETDB_INTERNAL
#  endif
#endif

/*
 * The following structure maintains a cached lookup value.
 */

typedef struct Value {
    time_t      expires;
    char	value[1];
} Value;

typedef int (GetProc)(Ns_DString *dsPtr, char *key);

/*
 * Static variables defined in this file
 */

static Ns_Cache *hostCache;
static Ns_Cache *addrCache;

/*
 * Static functions defined in this file
 */

static GetProc GetAddr;
static GetProc GetHost;
static int DnsGet(GetProc *getProc, Ns_DString *dsPtr, Ns_Cache *cachePtr, char *key);
static void LogError(char *func);


/*
 *----------------------------------------------------------------------
 * Ns_GetHostByAddr, Ns_GetAddrByHost --
 *
 *      Convert an IP address to a hostname or vice versa.
 *
 * Results:
 *	See DnsGet().
 *
 * Side effects:
 *      A new entry is entered into the hash table.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetHostByAddr(Ns_DString *dsPtr, char *addr)
{
    return DnsGet(GetHost, dsPtr, hostCache, addr);
}


int
Ns_GetAddrByHost(Ns_DString *dsPtr, char *host)
{
    return DnsGet(GetAddr, dsPtr, addrCache, host);
}


static int
DnsGet(GetProc *getProc, Ns_DString *dsPtr, Ns_Cache *cachePtr, char *key)
{
    int             status, new;
    Value   	   *vPtr;
    Ns_Entry       *ePtr;

    if (cachePtr == NULL) {
        status = (*getProc)(dsPtr, key);
    } else {
	Ns_CacheLock(cachePtr);
	ePtr = Ns_CacheCreateEntry(cachePtr, key, &new);
	if (!new) {
	    while (ePtr != NULL &&
		    (vPtr = Ns_CacheGetValue(ePtr)) == NULL) {
		Ns_CacheWait(cachePtr);
		ePtr = Ns_CacheFindEntry(cachePtr, key);
	    }
	    if (ePtr == NULL) {
	        status = NS_FALSE;
	    } else if (vPtr->expires < time(NULL)) {
		Ns_CacheUnsetValue(ePtr);
		new = 1;
	    } else {
		Ns_DStringAppend(dsPtr, vPtr->value);
		status = NS_TRUE;
	    }
	}
	if (new) {
	    Ns_CacheUnlock(cachePtr);
	    status = (*getProc)(dsPtr, key);
	    Ns_CacheLock(cachePtr);
	    if (status != NS_TRUE) {
		Ns_CacheDeleteEntry(ePtr);
	    } else {
		vPtr = ns_malloc(sizeof(Value) + dsPtr->length);
		vPtr->expires = time(NULL) + nsconf.dns.timeout;
		strcpy(vPtr->value, dsPtr->string);
		Ns_CacheSetValue(ePtr, vPtr);
	    }
	    Ns_CacheBroadcast(cachePtr);
	}
	Ns_CacheUnlock(cachePtr);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 * NsDNSInit --
 *
 *      Initialize the static hash table
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsDNSInit(void)
{
    if (nsconf.dns.cache == NS_TRUE) {
	hostCache = Ns_CacheCreate("ns_dnshost", TCL_STRING_KEYS, nsconf.dns.timeout, ns_free);
	addrCache = Ns_CacheCreate("ns_dnsaddr", TCL_STRING_KEYS, nsconf.dns.timeout, ns_free);
    }
}


/*
 *----------------------------------------------------------------------
 * GetHost, GetAddr --
 *
 *      Perform the actual lookup by host or address.
 *
 * Results:
 *      If a name can be found, the function returns NS_TRUE; otherwise, 
 *	it returns NS_FALSE.
 *
 * Side effects:
 *      Result is appended to dsPtr.
 *
 *----------------------------------------------------------------------
 */

static int
GetHost(Ns_DString *dsPtr, char *addr)
{
    struct hostent *he;
    struct sockaddr_in sa;
    static Ns_Cs cs;
    int status = NS_FALSE;

    sa.sin_addr.s_addr = inet_addr(addr);
    if (sa.sin_addr.s_addr != INADDR_NONE) {
	Ns_CsEnter(&cs);
        he = gethostbyaddr((char *) &sa.sin_addr,
			   sizeof(struct in_addr), AF_INET);
	if (he == NULL) {
	    LogError("gethostbyaddr");
	} else if (he->h_name != NULL) {
	    Ns_DStringAppend(dsPtr, he->h_name);
	    status = NS_TRUE;
	}
	Ns_CsLeave(&cs);
    }
    return status;
}

static int
GetAddr(Ns_DString *dsPtr, char *host)
{
    struct hostent *he;
    struct in_addr ia;
    static Ns_Cs cs;
    int status = NS_FALSE;

    Ns_CsEnter(&cs);
    he = gethostbyname(host);
    if (he == NULL) {
	LogError("gethostbyname");
    } else if (he->h_addr != NULL) {
        ia.s_addr = ((struct in_addr *) he->h_addr)->s_addr;
	Ns_DStringAppend(dsPtr, ns_inet_ntoa(ia));
	status = NS_TRUE;
    }
    Ns_CsLeave(&cs);
    return status;
}


/*
 *----------------------------------------------------------------------
 * LogError -
 *
 *      Log errors which may indicate a failure in the underlying
 *	resolver.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
LogError(char *func)
{
    int i;
    char *h, *e, buf[20];
#ifdef NEED_HERRNO
    extern int h_errno;
#endif

    i = h_errno;
    e = NULL;
    switch (i) {
	case HOST_NOT_FOUND:
	    /* Log nothing. */
	    return;
	    break;

	case TRY_AGAIN:
	    h = "temporary error - try again";
	    break;

	case NO_RECOVERY:
	    h = "unexpected server failure";
	    break;

	case NO_DATA:
	    h = "no valid IP address";
	    break;

#ifdef NETDB_INTERNAL
	case NETDB_INTERNAL:
	    h = "netdb internal error: ";
	    e = strerror(errno);
	    break;
#endif

	default:
	    sprintf(buf, "unknown error #%d", i);
	    h = buf;
    }

    Ns_Log(Error, "dns: %s failed: %s%s", func, h, e ? e : "");
}

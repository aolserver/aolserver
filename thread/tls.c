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
 * tls.c --
 *
 *	Thread local storage support for nsthreads.  Note that the nsthread
 *	library handles thread local storage directly.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/tls.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following global variable specifies the maximum TLS id.  Modifying
 * this value has no effect.
 */

int nsThreadMaxTls = NS_THREAD_MAXTLS;

/*
 * The following static variable and lock is used when allocating
 * tls ids.  The first valid id starts at 1 to detect use of an
 * unallocated id initialized to zero as a static variable.
 */

static int      nextId = 1;
static Ns_Mutex idLock;

/* 
 * Static functions defined in this file.
 */

static Ns_TlsCleanup *cleanupProcs[NS_THREAD_MAXTLS];

#define GETKEY(tlsPtr)	((int) *(tlsPtr))
#define GETSLOT(tlsPtr, slotPtr) \
	if (GETKEY(tlsPtr) < 1 || GETKEY(tlsPtr) >= NS_THREAD_MAXTLS) \
	    NsThreadAbort("Ns_Tls: invalid key: %d: should be between 1 and %d",\
		 GETKEY(tlsPtr), NS_THREAD_MAXTLS);\
	(slotPtr) = &((NsGetThread())->tlsPtr[GETKEY(tlsPtr)]);


/*
 *----------------------------------------------------------------------
 *
 * Ns_TlsAlloc --
 *
 *	Allocate the next tls id.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Id is set in given tlsPtr.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TlsAlloc(Ns_Tls *tlsPtr, Ns_TlsCleanup *cleanup)
{
    int id;

    Ns_MutexLock(&idLock);
    if (nextId == NS_THREAD_MAXTLS) {
	NsThreadAbort("Ns_TlsAlloc: exceded max tls: %d", NS_THREAD_MAXTLS);
    }
    id = nextId++;
    cleanupProcs[id] = cleanup;
    Ns_MutexUnlock(&idLock);
    if (id == 1) {
	Ns_MutexSetName(&idLock, "nsthread:tls");
    }
    *tlsPtr = (void *) id;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TlsSet --
 *
 *	Set the value for a threads tls slot.
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
Ns_TlsSet(Ns_Tls *tlsPtr, void *value)
{
    register void **slotPtr;

    GETSLOT(tlsPtr, slotPtr);
    *slotPtr = value;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TlsGet --
 *
 *	Get this thread's value in a tls slot.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	valuePtr is updated with value in this thread's slot.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_TlsGet(Ns_Tls *tlsPtr)
{
    register void **slotPtr;

    GETSLOT(tlsPtr, slotPtr);
    return *slotPtr;
}



/*
 *----------------------------------------------------------------------
 *
 * NsCleanupTls --
 *
 *	Cleanup thread local storage in LIFO order for an exiting thread.
 *	Note the careful use of the counters to keep iterating over the
 *	list, up to 5 times, until all TLS values are NULL.  This emulates
 *	the Pthread TLS behavior which catches a destructor inadvertantly
 *	calling a library which resets a TLS value after it's been destroyed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cleanup procs are invoked for non-null values.
 *
 *----------------------------------------------------------------------
 */

void
NsCleanupTls(Thread *thrPtr)
{
    int i, trys, retry;
    void *arg;

    trys = 0;
    do {
	retry = 0;
    	i = NS_THREAD_MAXTLS;
    	while (i-- > 0) {
	    if (cleanupProcs[i] != NULL && thrPtr->tlsPtr[i] != NULL) {
	    	arg = thrPtr->tlsPtr[i];
	    	thrPtr->tlsPtr[i] = NULL;
	    	(*cleanupProcs[i])(arg);
		retry = 1;
	    }
	}
    } while (retry && trys++ < 5);
}


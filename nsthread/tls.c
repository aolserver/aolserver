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
 *	Thread local storage.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/tls.c,v 1.1.2.1 2002/09/17 23:05:26 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"


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
    pthread_key_t key;
    int err;

    err = pthread_key_create(&key, cleanup);
    if (err != 0) {
	NsThreadFatal("Ns_TlsAlloc", "pthread_key_create", err);
    }
    *tlsPtr = (Ns_Tls) key;
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
    pthread_key_t key = (pthread_key_t) *tlsPtr;
    int err;

    err = pthread_setspecific(key, value);
    if (err != 0) {
	NsThreadFatal("Ns_TlsSet", "pthread_setspecific", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TlsGet --
 *
 *	Get this thread's value in a tls slot.
 *
 * Results:
 *	Pointer in slot.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_TlsGet(Ns_Tls *tlsPtr)
{
    pthread_key_t key = (pthread_key_t) *tlsPtr;

    return pthread_getspecific(key);
}

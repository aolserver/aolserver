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
 *	Routines for managing the NsTls structure.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/tls.c,v 1.3 2001/05/02 15:50:34 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static Ns_TlsCleanup FreeTls;


/*
 *----------------------------------------------------------------------
 *
 * NsGetTls --
 *
 *      Return the NsTls structure for this thread.
 *
 * Results:
 *      Pointer to NsTls.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NsTls *
NsGetTls(void)
{
    static Ns_Tls tls;
    NsTls *tlsPtr;

    if (tls == NULL) {
	/* NB: Safe via single threaded Tcl init. */
	Ns_TlsAlloc(&tls, FreeTls);
    }
    tlsPtr = Ns_TlsGet(&tls);
    if (tlsPtr == NULL) {
	tlsPtr = ns_malloc(sizeof(NsTls));
	Tcl_InitHashTable(&tlsPtr->db.owned, TCL_STRING_KEYS);
	Tcl_InitHashTable(&tlsPtr->tcl.interps, TCL_STRING_KEYS);
	tlsPtr->tfmt.gtime = tlsPtr->tfmt.ltime = 0;
	tlsPtr->tfmt.gbuf[0] = tlsPtr->tfmt.lbuf[0] = '\0';
	Ns_TlsSet(&tls, tlsPtr);
    }
    return tlsPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeTls --
 *
 *      Cleanup the NsTls at thread exit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	See Tcl_FinalizeThread().
 *
 *----------------------------------------------------------------------
 */

static void
FreeTls(void *arg)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_Interp *interp;
    NsTls *tlsPtr = arg;

    hPtr = Tcl_FirstHashEntry(&tlsPtr->tcl.interps, &search);
    while (hPtr != NULL) {
	interp = Tcl_GetHashValue(hPtr);
	Tcl_DeleteInterp(interp);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&tlsPtr->tcl.interps);
    Tcl_DeleteHashTable(&tlsPtr->db.owned);
    ns_free(tlsPtr);
    Tcl_FinalizeThread();
}

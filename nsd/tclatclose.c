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
 * tclatclose --
 *
 *	Routines for the ns_atclose command.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/tclatclose.c,v 1.8 2003/10/28 08:29:29 vasiljevic Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains script to execute when the
 * connection is closed.
 */

typedef struct AtClose {
    struct AtClose *nextPtr;
    char script[1];
} AtClose;

static void RunAtClose(NsInterp *itPtr, int run);


/*
 *----------------------------------------------------------------------
 *
 * NsTclAtCloseCmd --
 *
 *	Implements ns_atclose. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclAtCloseCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    NsInterp *itPtr = arg;
    char    *script;
    AtClose *atPtr;

    if (argc < 2 || argc > 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " script ?arg?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
	script = (char*)argv[1];
    } else {
	script = (char*)Tcl_Concat(2, (CONST char**)(argv+1));
    }

    /*
     * Push the script onto the head of the atclose list so scripts
     * will be called in reversed order when invoked.
     */

    atPtr = ns_malloc(sizeof(AtClose) + strlen(script));
    strcpy(atPtr->script, script);
    atPtr->nextPtr = itPtr->firstAtClosePtr;
    itPtr->firstAtClosePtr = atPtr;
    if (script != argv[1]) {
	ckfree(script);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsRunAtClose, NsFreeAtClose --
 *
 *	Run and/or free the registered at-close scripts. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will free the atclose scripts after execution. 
 *
 *----------------------------------------------------------------------
 */

void
NsFreeAtClose(NsInterp *itPtr)
{
    RunAtClose(itPtr, 0);
}

void
NsRunAtClose(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterp(interp);

    RunAtClose(itPtr, 1);
}

static void
RunAtClose(NsInterp *itPtr, int run)
{
    Tcl_Interp    *interp = itPtr->interp;
    AtClose       *atPtr;

    while ((atPtr = itPtr->firstAtClosePtr) != NULL) {
	itPtr->firstAtClosePtr = atPtr->nextPtr;
	if (run && Tcl_GlobalEval(interp, atPtr->script) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
	ns_free(atPtr);
    }
}

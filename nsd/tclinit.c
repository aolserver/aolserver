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
 * tclinit.c --
 *
 *	Initialization routines for Tcl.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.15 2001/04/12 17:53:29 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static functions defined in this file.
 */

static Tcl_InterpDeleteProc FreeData;

/*
 * Static variables defined in this file.
 */

static char initServer[] = "_ns_initserver";
static char cleanupInterp[] = "ns_cleanup";


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclEval --
 *
 *	Execute a tcl script for the given server.
 *
 * Results:
 *	Tcl return code. 
 *
 * Side effects:
 *	String results or error are placed in dsPtr if not NULL.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclEval(Ns_DString *dsPtr, char *server, char *script)
{
    int         retcode;
    Tcl_Interp *interp;
    char       *result;

    retcode = NS_ERROR;
    interp = Ns_TclAllocateInterp(server);
    if (interp != NULL) {
        if (Tcl_GlobalEval(interp, script) != TCL_OK) {
	    result = Ns_TclLogError(interp);
        } else {
	    result = interp->result;
            retcode = NS_OK;
	}
	if (dsPtr != NULL) {
            Ns_DStringAppend(dsPtr, result);
        }
        Ns_TclDeAllocateInterp(interp);
    }
    return retcode;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitInterps --
 *
 *	Register a user-supplied Tcl initialization procedure to be
 *	called when interps are created.
 *
 * Results:
 *	NS_OK
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitInterps(char *server, Ns_TclInterpInitProc *proc, void *arg)
{
    NsServer *servPtr = NsGetServer(server);
    Init *initPtr, **firstPtrPtr;

    initPtr = ns_malloc(sizeof(Init));
    initPtr->nextPtr = NULL;
    initPtr->proc = proc; 
    initPtr->arg = arg;
    firstPtrPtr = &servPtr->tcl.firstInitPtr;
    while (*firstPtrPtr != NULL) {
	firstPtrPtr = &((*firstPtrPtr)->nextPtr);
    }
    *firstPtrPtr = initPtr;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclMarkForDelete --
 *
 *	Mark the interp to be deleted after next cleanup.  This routine
 *	is useful for destory interps after they've been modified in
 *	weird ways, e.g., by the TclPro debugger.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Interp will be deleted on next de-allocate.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclMarkForDelete(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterp(interp);
    
    itPtr->delete = 1;
}
    

/*
 *----------------------------------------------------------------------
 *
 * Ns_TclAllocateInterp --
 *
 *	Allocate an interpreter, or if one is already associated with 
 *	this thread and server, return that one. 
 *
 * Results:
 *	Pointer to Tcl_Interp.
 *
 * Side effects:
 *	On create, depends on registered init procs.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclAllocateInterp(char *server)
{
    NsTls *tlsPtr = NsGetTls();
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp;
    Init *initPtr;
    NsInterp *itPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&tlsPtr->tcl.interps, server, &new);
    interp = Tcl_GetHashValue(hPtr);
    if (interp != NULL) {
	Tcl_SetHashValue(hPtr, NULL);
    } else {
	interp = Tcl_CreateInterp();
	itPtr = ns_calloc(1, sizeof(NsInterp));
	itPtr->interp = interp;
	itPtr->servPtr = NsGetServer(server);
	itPtr->hPtr = hPtr;
	Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
	Tcl_InitHashTable(&itPtr->dbs, TCL_STRING_KEYS);	
	Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);	
	Tcl_SetAssocData(interp, "ns:data", FreeData, itPtr);
	NsTclAddCmds(itPtr, interp);
	initPtr = itPtr->servPtr->tcl.firstInitPtr;
	while (initPtr != NULL) {
	    if (((*initPtr->proc)(interp, initPtr->arg)) != TCL_OK) {
		Ns_TclLogError(interp);
	    }
	    initPtr = initPtr->nextPtr;
	}
	if (Tcl_EvalFile(interp, itPtr->servPtr->tcl.initfile) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
    }
    return interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDeAllocateInterp --
 *
 *	Evalute the ns_cleanup proc and, if marked, delete the interp.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	See ns_cleanup proc.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclDeAllocateInterp(Tcl_Interp *interp)
{
    NsInterp	*itPtr = NsGetInterp(interp);
    AtCleanup	*cleanupPtr;

    /*
     * Invoke the cleanup callbacks if any.
     */

    if (itPtr != NULL) {
    	while ((cleanupPtr = itPtr->firstAtCleanupPtr) != NULL) {
	    itPtr->firstAtCleanupPtr = cleanupPtr->nextPtr;
	    (*cleanupPtr->procPtr)(interp, cleanupPtr->arg);
	    ns_free(cleanupPtr);
	}
    }

    /*
     * Evaluate the cleanup proc.
     */

    if (Tcl_Eval(interp, cleanupInterp) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_ResetResult(interp);    

    /*
     * Free up any remaining resources and put
     * this interp back in the table if another
     * is not already there.
     */

    if (itPtr != NULL) {
    	NsFreeAtClose(itPtr);
        itPtr->conn = NULL;
	if (Tcl_GetHashValue(itPtr->hPtr) != NULL) {
	    itPtr->delete = 1;
	}
	if (itPtr->delete) {
	    Tcl_DeleteInterp(interp);
	} else {
	    Tcl_SetHashValue(itPtr->hPtr, interp);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetInterp --
 *
 *	Return the interp's NsInterp structure from assoc data.
 *	This routine is used when the NsInterp is needed and
 *	not available as command ClientData.
 *
 * Results:
 *	Pointer to NsInterp or NULL if none.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NsInterp *
NsGetInterp(Tcl_Interp *interp)
{
    return (NsInterp *) Tcl_GetAssocData(interp, "ns:data", NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMarkForDeleteCmd --
 *
 *	Implements ns_markfordelete. 
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
NsTclMarkForDeleteCmd(ClientData arg, Tcl_Interp *interp, int argc,
		      char **argv)
{
    NsInterp *itPtr = arg;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    itPtr->delete = 1;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetConnInterp --
 *
 *	Get an interp for use in a connection thread.  Using this
 *	interface will ensure an automatic call to
 *	Ns_TclDeAllocateInterp() at the end of the connection.
 *
 * Results:
 *	See Ns_TclAllocateInterp().
 *
 * Side effects:
 *	Interp will be deallocated when connection is complete.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_GetConnInterp(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    NsInterp *itPtr;

    if (connPtr->interp == NULL) {
	connPtr->interp = Ns_TclAllocateInterp(connPtr->servPtr->server);
	itPtr = NsGetInterp(connPtr->interp);
	itPtr->conn = conn;
	itPtr->nsconn.flags = 0;
    }
    return connPtr->interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetConn --
 *
 *	Get the Ns_Conn structure associated with this tcl interp.
 *
 * Results:
 *	An Ns_Conn. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Conn *
Ns_TclGetConn(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterp(interp);

    return (itPtr ? itPtr->conn : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclLibrary --
 *
 *	Return the name of the private tcl lib 
 *
 * Results:
 *	Tcl lib name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclLibrary(char *server)
{
    NsServer *servPtr;

    if (server == NULL) {
	return nsconf.tcl.sharedlibrary;
    }
    servPtr = NsGetServer(server);
    return servPtr->tcl.library;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInterpServer --
 *
 *	Return the name of the server. 
 *
 * Results:
 *	Server name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclInterpServer(Tcl_Interp *interp)
{
    NsInterp *itPtr = NsGetInterp(interp);
    
    return (itPtr ? itPtr->servPtr->server : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitServer --
 *
 *	Evaluate the server init script at startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See _ns_initserver proc.
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitServer(char *server)
{
    (void) Ns_TclEval(NULL, server, initServer);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclEval --
 *
 *      Wraps Tcl_EvalEx() to avoid byte code compiling.
 *
 * Results:
 *      See Tcl_Eval
 *
 * Side effects:
 *	See Tcl_Eval
 *
 *----------------------------------------------------------------------
 */

int
NsTclEval(Tcl_Interp *interp, char *script)
{
    int status;

    /* 
     * Eval without the byte code compiler, and ensure that we
     * have a string result so old code can reference interp->result.
     */

    status = Tcl_EvalEx(interp, script, strlen(script), TCL_EVAL_DIRECT);
    Tcl_SetResult(interp, Tcl_GetString(Tcl_GetObjResult(interp)),
		  TCL_VOLATILE);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclLogError --
 *
 *	Log the global errorInfo variable to the server log. 
 *
 * Results:
 *	Returns a pointer to the errorInfo. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclLogError(Tcl_Interp *interp)
{
    char *errorInfo;

    errorInfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo == NULL) {
        errorInfo = "";
    }
    Ns_Log(Error, "%s\n%s", interp->result, errorInfo);
    return errorInfo;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeData --
 *
 *      Destroy the per-interp NsInterp structure at
 *	interp delete time.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeData(ClientData arg, Tcl_Interp *interp)
{
    NsInterp *itPtr = arg;

    /*
     * Clear the hash value for this server if still
     * pointing to this interp.
     */

    if (Tcl_GetHashValue(itPtr->hPtr) == interp) {
	Tcl_SetHashValue(itPtr->hPtr, NULL);
    }

    /*
     * Free the NsInterp resources.
     */

    NsFreeSets(itPtr);
    NsFreeDbs(itPtr);
    NsFreeAdp(itPtr);
    Tcl_DeleteHashTable(&itPtr->chans);
    ns_free(itPtr);
}

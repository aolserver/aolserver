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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.26 2002/08/25 22:06:40 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains new interp init callbacks.
 */

typedef struct Init {
    struct Init *nextPtr;
    Ns_TclInterpInitProc *proc;
    void *arg;  
} Init;

/*
 * The following structure maintains procs to call at interp
 * deallocation time.
 */

typedef struct Defer {
    struct Defer *nextPtr;
    Ns_TclDeferProc *proc;
    void *arg;
} Defer;

/*
 * Static functions defined in this file.
 */

static Tcl_InterpDeleteProc FreeData;
static Ns_TlsCleanup DeleteInterps;
static Tcl_Interp *CreateInterp(NsInterp *itPtr);

/*
 * Static variables defined in this file.
 */

static Ns_Tls tls;


/*
 *----------------------------------------------------------------------
 *
 * NsInitTcl --
 *
 *	Initialize the Tcl interp interface.
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
NsInitTcl(void)
{
    Ns_TlsAlloc(&tls, DeleteInterps);
}


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
    CONST char *result;

    retcode = NS_ERROR;
    interp = Ns_TclAllocateInterp(server);
    if (interp != NULL) {
        if (Tcl_EvalEx(interp, script, -1, 0) != TCL_OK) {
	    result = Ns_TclLogError(interp);
        } else {
	    result = Tcl_GetStringResult(interp);
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
    
    if (itPtr != NULL) {
	itPtr->delete = 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclCreateInterp --
 *
 *      Create a new interp.
 *
 * Results:
 *      Pointer to new interp.
 *
 * Side effects:
 *	See CreateInterp.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclCreateInterp(void)
{
    return CreateInterp(NULL);
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
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp;
    NsInterp *itPtr;
    NsServer *servPtr = NsGetServer(server);
    Init *initPtr;
    int new;

    if (servPtr == NULL) {
	return NULL;
    }
    tablePtr = Ns_TlsGet(&tls);
    if (tablePtr == NULL) {
	tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr, TCL_ONE_WORD_KEYS);
	Ns_TlsSet(&tls, tablePtr);
    }
    hPtr = Tcl_CreateHashEntry(tablePtr, (char *) servPtr, &new);
    interp = Tcl_GetHashValue(hPtr);
    if (interp != NULL) {
	itPtr = NsGetInterp(interp);
	Tcl_SetHashValue(hPtr, NULL);
    } else {
	itPtr = ns_calloc(1, sizeof(NsInterp));
	itPtr->hPtr = hPtr;
	itPtr->servPtr = servPtr;
	Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
	Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);	
	Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);	
	itPtr->interp = interp = CreateInterp(itPtr);
	Tcl_SetAssocData(interp, "ns:data", FreeData, itPtr);
	initPtr = itPtr->servPtr->tcl.firstInitPtr;
	while (initPtr != NULL) {
	    if (((*initPtr->proc)(interp, initPtr->arg)) != TCL_OK) {
		Ns_TclLogError(interp);
	    }
	    initPtr = initPtr->nextPtr;
	}
    }
    if (Tcl_EvalEx(interp, "ns_init update", -1, 0) != TCL_OK) {
	Ns_TclLogError(interp);
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
    Defer    	*deferPtr;

    if (itPtr == NULL) {
	Tcl_DeleteInterp(interp);
    } else {
    	/*
     	 * Invoke the deferred callbacks if any.
     	 */

    	while ((deferPtr = itPtr->firstDeferPtr) != NULL) {
	    itPtr->firstDeferPtr = deferPtr->nextPtr;
	    (*deferPtr->proc)(interp, deferPtr->arg);
	    ns_free(deferPtr);
	}

    	/*
     	 * Evaluate the cleanup proc.
     	 */

    	if (Tcl_EvalEx(interp, "ns_cleanup", -1, 0) != TCL_OK) {
	    Ns_TclLogError(interp);
    	}
    	Tcl_ResetResult(interp);    

    	/*
     	 * Free up any remaining resources and put
     	 * this interp back in the table if another
     	 * is not already there.
     	 */
	 
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
 * Ns_TclRegisterDeferred --
 *
 *	Register a procedure to be called when the interp is deallocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Procedure will be called later.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclRegisterDeferred(Tcl_Interp *interp, Ns_TclDeferProc *proc,
	void *arg)
{
    NsInterp   *itPtr = NsGetInterp(interp);
    Defer      *deferPtr, **nextPtrPtr;

    deferPtr = ns_malloc(sizeof(Defer));
    deferPtr->proc = proc;
    deferPtr->arg = arg;
    deferPtr->nextPtr = NULL;
    nextPtrPtr = &itPtr->firstDeferPtr;
    while (*nextPtrPtr != NULL) {
	nextPtrPtr = &((*nextPtrPtr)->nextPtr);
    }
    *nextPtrPtr = deferPtr;
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
 * NsTclMarkForDeleteObjCmd --
 *
 *	Implements ns_markfordelete as obj command. 
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
NsTclMarkForDeleteObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
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
    NsServer *servPtr = NsGetServer(server);

    return (servPtr ? servPtr->tcl.library : nsconf.tcl.sharedlibrary);
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
 *	Evaluate server initialization script at startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See init script (normally init.tcl).
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitServer(char *server)
{
    NsServer *servPtr = NsGetServer(server);
    Tcl_Interp *interp;

    if (servPtr != NULL) {
	interp = Ns_TclAllocateInterp(server);
	if (Tcl_EvalFile(interp, servPtr->tcl.initfile) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
    	Ns_TclDeAllocateInterp(interp);
    }
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
    CONST char *errorInfo;

    errorInfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo == NULL) {
        errorInfo = "";
    }
    Ns_Log(Error, "%s\n%s", Tcl_GetStringResult(interp), errorInfo);
    return (char *) errorInfo;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitObjCmd --
 *
 *      Implements ns_init command.
 *
 * Results:
 *      Standar Tcl result.
 *
 * Side effects:
 *	May update current saved server Tcl state.
 *
 *----------------------------------------------------------------------
 */


int
NsTclInitObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    static CONST char *opts[] = {
	"epoch", "get", "save", "update", NULL
    };
    enum {
	IEpochIdx, IGetIdx, ISaveIdx, IUpdateIdx
    } opt;
    char *script;
    int length, result;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }

    result = TCL_OK;
    switch (opt) {
    case IGetIdx:
	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Ns_RWLockRdLock(&itPtr->servPtr->tcl.lock);
	Tcl_SetResult(interp, itPtr->servPtr->tcl.script, TCL_VOLATILE);
	Ns_RWLockUnlock(&itPtr->servPtr->tcl.lock);
	break;

    case IEpochIdx:
	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Ns_RWLockRdLock(&itPtr->servPtr->tcl.lock);
	Tcl_SetIntObj(Tcl_GetObjResult(interp), itPtr->servPtr->tcl.epoch);
	Ns_RWLockUnlock(&itPtr->servPtr->tcl.lock);
	break;
   
    case ISaveIdx:
	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
	    return TCL_ERROR;
    	}
	script = ns_strdup(Tcl_GetStringFromObj(objv[2], &length));
	Ns_RWLockWrLock(&itPtr->servPtr->tcl.lock);
	ns_free(itPtr->servPtr->tcl.script);
	itPtr->servPtr->tcl.script = script;
	itPtr->servPtr->tcl.length = length;
	if (++itPtr->servPtr->tcl.epoch == 0) {
	    /* NB: Epoch zero reserved for new interps. */
	    ++itPtr->servPtr->tcl.epoch;
	}
	Ns_RWLockUnlock(&itPtr->servPtr->tcl.lock);
	break;

    case IUpdateIdx:
	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
    	Ns_RWLockRdLock(&itPtr->servPtr->tcl.lock);
    	if (itPtr->epoch != itPtr->servPtr->tcl.epoch) {
	    result = Tcl_EvalEx(itPtr->interp, itPtr->servPtr->tcl.script,
			    itPtr->servPtr->tcl.length, TCL_EVAL_GLOBAL);
	    itPtr->epoch = itPtr->servPtr->tcl.epoch;
        }
    	Ns_RWLockUnlock(&itPtr->servPtr->tcl.lock);
	break;
    }

    return result;
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

    NsFreeAdp(itPtr);
    Tcl_DeleteHashTable(&itPtr->sets);
    Tcl_DeleteHashTable(&itPtr->chans);
    Tcl_DeleteHashTable(&itPtr->https);
    ns_free(itPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * DeleteInterps --
 *
 *      Delete all per-thread interps.
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
DeleteInterps(void *arg)
{
    Tcl_HashTable *tablePtr = arg;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_Interp *interp;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
	interp = Tcl_GetHashValue(hPtr);
	if (interp != NULL) {
	    Tcl_DeleteInterp(interp);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}

static Tcl_Interp *
CreateInterp(NsInterp *itPtr)
{
    Tcl_Interp *interp;

    interp = Tcl_CreateInterp();
    if (interp == NULL) {
	Ns_Fatal("could not create interp");
    }
    if (Tcl_Init(interp) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_InitMemory(interp);
    NsTclAddCmds(interp, itPtr);
    return interp;
}

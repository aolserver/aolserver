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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.38 2003/07/18 18:01:16 elizthom Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains interp callback traces.
 */

typedef struct Trace {
    struct Trace *nextPtr;
    Ns_TclInterpInitProc *proc;
    void *arg;  
} Trace;

#define TRACE_INIT	0
#define TRACE_CREATE	1
#define TRACE_CLEANUP	2
#define TRACE_DELETE	3

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
static int InitInterp(Tcl_Interp *interp, NsServer *servPtr, NsInterp **itPtrPtr);
static int UpdateInterp(NsInterp *itPtr);
static Tcl_HashEntry *GetHashEntry(NsServer *servPtr);
static int RegisterTrace(NsServer *servPtr, int idx,
			 Ns_TclTraceProc *proc, void *arg);
static void RunTraces(NsInterp *itPtr, int idx);
static int TclScriptTraceCB(Tcl_Interp *interp, void *arg);
static int TclInitScriptCB(Tcl_Interp *interp, void *arg);

/*
 * Static variables defined in this file.
 */

static Ns_Tls tls;
static Ns_Mutex initLock;


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
    Ns_MutexInit(&initLock);
    Ns_MutexSetName(&initLock, "ns:interp");

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
    return RegisterTrace(NsGetServer(server), TRACE_INIT, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterAtCreate, Ns_TclRegisterAtCleanup,
 * Ns_TclRegisterAtDelete --
 *
 *  	Register callbacks for interp create, cleanup, and delete.
 *
 * Results:
 *	NS_ERROR if no initializing server, NS_OK otherwise.
 *
 * Side effects:
 *	Callback will be invoke at requested time.
 *
 *----------------------------------------------------------------------
 */
 
int
Ns_TclRegisterAtCreate(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(NsGetInitServer(), TRACE_CREATE, proc, arg);
}

int
Ns_TclRegisterAtCleanup(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(NsGetInitServer(), TRACE_CLEANUP, proc, arg);
}

int
Ns_TclRegisterAtDelete(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(NsGetInitServer(), TRACE_DELETE, proc, arg);
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
 * Ns_TclInit --
 *
 *      Initialize an interp with AOLserver commands.
 *
 * Results:
 *      TCL_OK or TCL_ERROR on init error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInit(Tcl_Interp *interp)
{
    return InitInterp(interp, NULL, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Nsd_Init --
 *
 *      Init routine for loading libnsd as a tclsh module.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *	See Ns_TclInit.
 *
 *----------------------------------------------------------------------
 */

int
Nsd_Init(Tcl_Interp *interp)
{
    return Ns_TclInit(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclCreateInterp --
 *
 *      Create a new interp with AOLserver commands.
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
    Tcl_Interp *interp;

    interp = Tcl_CreateInterp();
    if (Ns_TclInit(interp) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    return interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDestroyInterp --
 *
 *      Delete an interp.
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
Ns_TclDestroyInterp(Tcl_Interp *interp)
{
    Tcl_DeleteInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclAllocateInterp --
 *
 *	Allocate an interpreter from the per-thread list.
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
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp;
    NsInterp *itPtr;
    NsServer *servPtr;

    /*
     * Verify the server.  NULL (i.e., no server) is valid but
     * a non-null, unknown server is an error.
     */

    if (server == NULL) {
	servPtr = NULL;
    } else {
	servPtr = NsGetServer(server);
	if (servPtr == NULL) {
	    return NULL;
	}
    }

    /*
     * Pop the first interp off the list or create a new one.
     */

    hPtr = GetHashEntry(servPtr);
    itPtr = Tcl_GetHashValue(hPtr);
    if (itPtr != NULL) {
	Tcl_SetHashValue(hPtr, itPtr->nextPtr);
    } else {
	(void) InitInterp(Tcl_CreateInterp(), servPtr, &itPtr);
    }
    interp = itPtr->interp;
    itPtr->nextPtr = NULL;

    /*
     * Evaluate the ns_init proc which by default updates the
     * interp state with ns_ictl.
     */

    if (Tcl_EvalEx(interp, "ns_init", -1, 0) != TCL_OK) {
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
    Tcl_HashEntry *hPtr;

    if (itPtr == NULL) {
	/*
	 * Not an AOLserver interp.
	 */

	Tcl_DeleteInterp(interp);
	return;
    }

    if (itPtr->conn != NULL) {
	/*
	 * The current connection still has a reference to this
	 * interp so do nothing.  Ns_FreeConnInterp() must be
	 * used instead.
	 */

	return;
    }

    /*
     * Evaluate the cleanup script to perform various garbage collection
     * and then either delete the interp or push it back on the
     * per-thread list.
     */

    if (Tcl_EvalEx(interp, "ns_cleanup", -1, 0) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    if (itPtr->delete) {
	Tcl_DeleteInterp(interp);
    } else {
	Tcl_ResetResult(interp);
	hPtr = GetHashEntry(itPtr->servPtr);
	itPtr->nextPtr = Tcl_GetHashValue(hPtr);
	Tcl_SetHashValue(hPtr, itPtr);
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

    if (itPtr == NULL) {
        return;
    }

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
    if (interp == NULL) {
	Ns_Log(Warning, "NsGetInterp: Invalid Tcl_Interp == NULL");
        return NULL;
    } else {
        return (NsInterp *) Tcl_GetAssocData(interp, "ns:data", NULL);
    }
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
	connPtr->interp = Ns_TclAllocateInterp(connPtr->server);
	itPtr = NsGetInterp(connPtr->interp);
	itPtr->conn = conn;
	itPtr->nsconn.flags = 0;
    }
    return connPtr->interp;
}

void
Ns_FreeConnInterp(Ns_Conn *conn)
{
    NsInterp *itPtr;
    Conn *connPtr = (Conn *) conn;

    if (connPtr->interp != NULL) {
	itPtr = NsGetInterp(connPtr->interp);
	itPtr->conn = NULL;
	itPtr->nsconn.flags = 0;
	Ns_TclDeAllocateInterp(connPtr->interp);
	connPtr->interp = NULL;
    }
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
    NsInterp *itPtr;

    if (interp == NULL) {
        Ns_Log(Warning, "Ns_TclGetConn: interp == NULL; Valid interp value required." );
        return NULL;
    }

    itPtr = NsGetInterp(interp);
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
    NsInterp *itPtr;
    
    if (interp == NULL) {
        Ns_Log(Warning, "Ns_TclInterpServer: interp == NULL; Valid interp value required." );
        return NULL;
    }

    itPtr = NsGetInterp(interp);
    if (itPtr == NULL || itPtr->servPtr == NULL) {
	return NULL;
    }
    return itPtr->servPtr->server;
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
 * Ns_TclLogErrorRequest --
 *
 *	Log both errorInfo and info about the HTTP request that led 
 *	to it. 
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
Ns_TclLogErrorRequest(Tcl_Interp *interp, Ns_Conn *conn)
{
    char *agent;
    CONST char *errorInfo;

    errorInfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo == NULL) {
        errorInfo = Tcl_GetStringResult(interp);
    }
    agent = Ns_SetIGet(conn->headers, "user-agent");
    if (agent == NULL) {
	agent = "?";
    }
    Ns_Log(Error, "error for %s %s, "
           "User-Agent: %s, PeerAddress: %s\n%s", 
           conn->request->method, conn->request->url,
	   agent, Ns_ConnPeer(conn), errorInfo);
    return (char*)errorInfo;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitModule --
 *
 *      Add a module name to the init list.
 *
 * Results:
 *      TCL_ERROR if no such server, TCL_OK otherwise.
 *
 * Side effects:
 *	Module will be initialized by the init script later.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitModule(char *server, char *module)
{
    NsServer *servPtr;

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
	return NS_ERROR;
    }
    (void) Tcl_ListObjAppendElement(NULL, servPtr->tcl.modules,
			     	    Tcl_NewStringObj(module, -1));
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclDummyObjCmd --
 *
 *      Dummy command for ns_init and ns_cleanup normally replaced
 *	with procs by server init scripts.
 *
 * Results:
 *      TCL_OK.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclDummyObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclICtlObjCmd --
 *
 *      Implements ns_ictl command to control interp internals.
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
NsTclICtlObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Defer    	*deferPtr;
    NsInterp *itPtr = arg;
    static CONST char *opts[] = {
	"addmodule", "cleanup", "epoch", "get", "getmodules", "save",
	"update", "oncreate", "oncleanup", "oninit", NULL
    };
    enum {
	IAddModuleIdx, ICleanupIdx, IEpochIdx, IGetIdx, IGetModulesIdx,
	ISaveIdx, IUpdateIdx, IOnCreateIdx, IOnCleanupIdx, IOnInitIdx
    } opt;
    char *script;
    int length, result;
    int status;
    Tcl_Obj   *objPtr;

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
    case IAddModuleIdx:
	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "module");
	    return TCL_ERROR;
	}
	if (Tcl_ListObjAppendElement(interp, itPtr->servPtr->tcl.modules,
				     objv[2]) != TCL_OK) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, itPtr->servPtr->tcl.modules);
	break;

    case IGetModulesIdx:
	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, itPtr->servPtr->tcl.modules);
	break;

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
	result = UpdateInterp(itPtr);
	break;

    case ICleanupIdx:
	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	RunTraces(itPtr, TRACE_CLEANUP);
    	while ((deferPtr = itPtr->firstDeferPtr) != NULL) {
	    itPtr->firstDeferPtr = deferPtr->nextPtr;
	    (*deferPtr->proc)(interp, deferPtr->arg);
	    ns_free(deferPtr);
	}
    	NsFreeAtClose(itPtr);
	break;

    case IOnCreateIdx:
    case IOnCleanupIdx:
    case IOnInitIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 1, objv, "when script");
	    return TCL_ERROR;
        }
        objPtr = objv[2];
        Tcl_IncrRefCount(objPtr);
        switch (opt) {
        case IOnCreateIdx:
            status = Ns_TclRegisterAtCreate(TclScriptTraceCB, objPtr);
            break;
        case IOnCleanupIdx:
            status = Ns_TclRegisterAtCreate(TclScriptTraceCB, objPtr);
            break;
        case IOnInitIdx:
            status = Ns_TclInitInterps(itPtr->servPtr->server, 
                                       TclInitScriptCB, objPtr);
            break;
        default:
            status = NS_ERROR;
            break;
        }
        if( status != NS_OK ) {
            Tcl_AppendResult(interp, "Failed ", opts[opt], "-time registration", NULL );
            /*
             * There is a restriction imposed from the Ns_TclRegisterAtxx
             * funcs that this can only be called during server init.
             * Check for this case, so that I can produce a reasonable 
             * diagnostic.
             */
            if( (opt != IOnInitIdx) && (NsGetInitServer() == NULL) ) {
                Tcl_AppendResult(interp,
                                 ", this can only be used during server init.",
                                 NULL );
            }
            Tcl_DecrRefCount(objPtr);
            result = TCL_ERROR;
        } else {
            result = TCL_OK;
        }
        break;
    }

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * TclScriptTraceCB --
 *
 *      Eval the given script from being called as a trace callback.
 *
 * Results:
 *      Status from script eval.
 *
 * Side effects:
 *	Depends on Tcl init script sources by Tcl_Init.
 *
 *----------------------------------------------------------------------
 */

static int
TclScriptTraceCB(Tcl_Interp *interp, void *arg)
{
    Tcl_Obj *objPtr = (Tcl_Obj *)arg;
    int status = TCL_OK;

    if( objPtr != NULL ) {
        status = Tcl_EvalObjEx(interp, objPtr, 0);
    }
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * TclInitScriptCB --
 *
 *      Eval the given script from being called as a Tcl Init callback.
 *
 * Results:
 *      Status from script eval.
 *
 * Side effects:
 *	Depends on Tcl init script sources by Tcl_Init.
 *
 *----------------------------------------------------------------------
 */

static int
TclInitScriptCB(Tcl_Interp *interp, void *arg)
{
    Tcl_Obj *objPtr = (Tcl_Obj *)arg;
    int status = NS_OK;

    /*
     * Note we are inhibiting the bytecode compiler in this case.  By their
     * nature, these scripts are run just once during an interp's init.
     * Tcl will not reuse the bytecode produced within one interp
     * in another interp; it will discard the previous and recompile.
     * So, better just to interpret the script.
     */
    if( (objPtr != NULL) &&
        (Tcl_EvalObjEx(interp, objPtr, 
                       TCL_EVAL_DIRECT | TCL_EVAL_GLOBAL) != TCL_OK) ) {
        status = NS_ERROR;
    }
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * InitInterp --
 *
 *      Initialize an interp with standard Tcl and AOLserver commands.
 *	If servPtr is not NULL, virtual server commands will be added
 *	as well.
 *
 * Results:
 *      Pointer to new NsInterp structure.
 *
 * Side effects:
 *	Depends on Tcl init script sources by Tcl_Init.
 *
 *----------------------------------------------------------------------
 */

static int
InitInterp(Tcl_Interp *interp, NsServer *servPtr, NsInterp **itPtrPtr)
{
    static volatile int initialized = 0;
    NsInterp *itPtr;
    int result = TCL_OK;
    int updateResult = TCL_OK;

    /*
     * Basic Tcl initialization.
     */

    if (Tcl_Init(interp) != TCL_OK) {
	Ns_TclLogError(interp);
	result = TCL_ERROR;
    }
    Tcl_InitMemory(interp);

    /*
     * Core AOLserver initialization.
     */

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    NsTclInitQueueType();
	    NsTclInitAddrType();
	    NsTclInitTimeType();
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }
    itPtr = ns_calloc(1, sizeof(NsInterp));
    itPtr->interp = interp;
    itPtr->servPtr = servPtr;
    Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
    Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);	
    Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);	
    Tcl_SetAssocData(interp, "ns:data", FreeData, itPtr);
    NsTclAddCmds(interp, itPtr);

    /*
     * Virtual-server Tcl initialization.
     */

    if (servPtr != NULL) {

	NsTclAddServerCmds(interp, itPtr);

	/*
	 * Call traces registered by Ns_TclInitInterps which
	 * generally create commands for loadable module.
	 */

	RunTraces(itPtr, TRACE_INIT);

	/*
	 * Call traces registered by Ns_RegisterAtCreate.
	 * This was necessary in prior releases because
	 * Ns_TclInitInterp traces where actually only 
	 * called in the initial startup interp.
	 */

	RunTraces(itPtr, TRACE_CREATE);

	/*
	 * Update the interp state which should define ns_init.
	 */

	if (nsconf.tcl.lockoninit) {
            /* optionally serialize interp initialization as 
               the resulting malloc lock contention can be far worse */
       	    Ns_MutexLock(&initLock);
        }
        updateResult = UpdateInterp(itPtr);
	if (nsconf.tcl.lockoninit) {
            Ns_MutexUnlock(&initLock); 
	}
	if (updateResult != TCL_OK) {
	    Ns_TclLogError(interp);
	    result = TCL_ERROR;
    	}
    }

    if (itPtrPtr != NULL) {
	*itPtrPtr = itPtr;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateInterp --
 *
 *      Update the state (procs, namespaces) of an interp.  Called
 *	directly to bootstrap new interps and then through
 *	"ns_ictl update" during regular cleanup.
 *
 * Results:
 *      Tcl result from update script.
 *
 * Side effects:
 *	Will update procs when epoch changes.
 *
 *----------------------------------------------------------------------
 */

static int
UpdateInterp(NsInterp *itPtr)
{
    int result = TCL_OK;

    Ns_RWLockRdLock(&itPtr->servPtr->tcl.lock);
    if (itPtr->epoch != itPtr->servPtr->tcl.epoch) {
	result = Tcl_EvalEx(itPtr->interp, itPtr->servPtr->tcl.script,
		    	    itPtr->servPtr->tcl.length, TCL_EVAL_GLOBAL);
	itPtr->epoch = itPtr->servPtr->tcl.epoch;
    }
    Ns_RWLockUnlock(&itPtr->servPtr->tcl.lock);
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

    RunTraces(itPtr, TRACE_DELETE);
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
 *      Delete all per-thread interps at thread exit time.
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
    NsInterp *itPtr;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
	while ((itPtr = Tcl_GetHashValue(hPtr)) != NULL) {
	    Tcl_SetHashValue(hPtr, itPtr->nextPtr);
	    Tcl_DeleteInterp(itPtr->interp);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);

    Tcl_FinalizeThread(); /* To call registered thread-exit handlers */
}


/*
 *----------------------------------------------------------------------
 *
 * GetHashEntry --
 *
 *      Get/Create hash entry for per-thread list of interps for given
 *	server.
 *
 * Results:
 *      Pointer to hash entry.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashEntry *
GetHashEntry(NsServer *servPtr)
{
    Tcl_HashTable *tablePtr;
    int ignored;

    tablePtr = Ns_TlsGet(&tls);
    if (tablePtr == NULL) {
	tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr, TCL_ONE_WORD_KEYS);
	Ns_TlsSet(&tls, tablePtr);
    }
    return Tcl_CreateHashEntry(tablePtr, (char *) servPtr, &ignored);
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterTrace --
 *
 *  	Add a trace to one of the server trace lists.  Traces are
 *	added in FIFO order which (aside from init's) may not
 *	be quite right.
 *
 * Results:
 *	NS_OK if called with a non-NULL server, NS_ERROR otherwise.
 *
 * Side effects:
 *	Will modify server trace list.
 *
 *----------------------------------------------------------------------
 */
 
static int
RegisterTrace(NsServer *servPtr, int idx, Ns_TclTraceProc *proc, void *arg)
{
    Trace *tracePtr, **firstPtrPtr;

    if (servPtr == NULL) {
	return NS_ERROR;
    }
    tracePtr = ns_malloc(sizeof(Trace));
    tracePtr->proc = proc;
    tracePtr->arg = arg;
    tracePtr->nextPtr = NULL;
    firstPtrPtr = &servPtr->tcl.traces[idx];
    while (*firstPtrPtr != NULL) {
	firstPtrPtr = &((*firstPtrPtr)->nextPtr);
    }
    *firstPtrPtr = tracePtr;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * RunTraces --
 *
 *  	Execute trace callbacks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depeneds on callbacks.
 *
 *----------------------------------------------------------------------
 */
 
static void
RunTraces(NsInterp *itPtr, int idx)
{
    Trace *tracePtr;

    if (itPtr->servPtr != NULL) {
    	tracePtr = itPtr->servPtr->tcl.traces[idx];
    	while (tracePtr != NULL) {
	    if ((*tracePtr->proc)(itPtr->interp, tracePtr->arg) != TCL_OK) {
	        Ns_TclLogError(itPtr->interp);
	    }
	    tracePtr = tracePtr->nextPtr;
	}
    }
}

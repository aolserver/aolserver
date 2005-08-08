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
 *	Initialization and resource management routines for Tcl.  This
 *	code provides for three types of AOLserver extended Tcl
 *	interps:
 *
 *	1. First, basic interps created with Ns_TclCreateInterp or
 *	initialized with Ns_TclInit.  These interps include "core" Tcl
 *	and AOLserver commands and are normally used simply to
 *	evaluate the config file.
 *
 *	2. Next, virtual server interps allocated from per-thread
 *	caches using Ns_TclAllocateInterp and returned via
 *	Ns_TclDeAllocateInterp.  These interps include all the
 *	commands of the basic interp, commands useful for virtual
 *	server environments, and any commands added as a result of
 *	loadable module initialization callbacks.  These interps are
 *	normally used during connection processing but can also be
 *	used outside a connection, e.g., in a scheduled procedure or
 *	detached background thread.
 *
 *	3. Finally, connection interps accessed  with
 *	Ns_TclGetConnInterp.  These interps are virtual server interps
 *	but managed along with a connection, having access to
 *	connection specific data structures (e.g., via ns_conn) and
 *	released automatically during connection cleanup.  This type
 *	of interp is used for ns_register_filter and ns_register_proc
 *	callback scripts as well as for ADP pages.  The same interp is
 *	used throughout the connection, for all filters, procs, and/or
 *	ADP pages.
 *
 *	Note the need to initialize Tcl state (i.e., procs, vars,
 *	packages, etc.) specific to a virtual server, later to copy
 *	this state to new interps when created, and garbage collection
 *	and end-of-connection cleanup facilities add quite a bit of
 *	complexity and confusion to the code.  See the comments in
 *	Ns_TclRegisterTrace, Ns_TclAllocateInterp, and NsTclICtlObjCmd
 *	and review the code in init.tcl for more details.
 *
 *	Note also the role of the NsInterp structure.  This single
 *	structure provides storage necessary for all AOLserver
 *	commands, core or virtual-server.  The structure is allocated
 *	for each new interp and passed as the ClientData argument to
 *	all AOLserver commands (see NewInterpData and NsTclAddCmds in
 *	tclcmds.c for details).  Both for cases where the ClientData
 *	isn't available and to ensure proper cleanup of the structure
 *	when the interp is deleted, the NsInterp is managed by the
 *	interp via the Tcl assoc data interface under the name
 *	"ns:data" and accessible by NsGetInterpData.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.49 2005/08/08 11:29:58 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains per-thread context to support Tcl
 * including a table of cached interps by server and a shared async
 * cancel object.
 */

typedef struct TclData {
    Tcl_AsyncHandler cancel;
    Tcl_HashEntry *hPtr;
    Tcl_HashTable interps;
} TclData;

/*
 * The following structure maintains interp callback traces. The traces
 * must be registered during server startup and invoked later for
 * virutal server interps at specific points in the lifetime of the
 * interp.  The callbacks are invoked in in LIFO order.  A common trace
 * would be an "on create" trace to add commands in a loadable C module,
 * e.g., the "ns_accesslog" command in nslog.
 */

typedef struct TclTrace {
    struct TclTrace	 *nextPtr;
    Ns_TclTraceProc      *proc;
    void		 *arg;  
    int			  when;
} TclTrace;

/*
 * The following structure maintains Tcl-script based traces.
 */

typedef struct ScriptTrace {
    int  length;
    char script[1];
} ScriptTrace;

/*
 * The following structure maintains procs to call during interp garbage
 * collection.  Unlike traces, these callbacks are one-shot events
 * registered during normal Tcl script evaluation. The callbacks are
 * invoked in FIFO order (LIFO would probably have been better). In
 * practice this API is rarely used. Instead, more specific garbage
 * collection schemes are used; see the "ns_cleanup" script in init.tcl
 * for examples.
 */

typedef struct Defer {
    struct Defer *nextPtr;
    Ns_TclDeferProc *proc;
    void *arg;
} Defer;

/*
 * The following structure maintains scripts to execute when the
 * connection is closed.  The scripts are invoked in LIFO order.
 */

typedef struct AtClose {
    struct AtClose *nextPtr;
    Tcl_Obj *objPtr;
} AtClose;

/*
 * Static functions defined in this file.
 */

static TclData *GetData(void);
static Ns_TlsCleanup DeleteData;
static Tcl_Interp *CreateInterp(NsInterp **itPtrPtr, char *server);
static NsInterp *NewInterpData(Tcl_Interp *interp, char *server);
static Tcl_InterpDeleteProc FreeInterpData;
static NsInterp *PopInterp(char *server);
static void PushInterp(NsInterp *itPtr);
static Tcl_HashEntry *GetCacheEntry(NsServer *servPtr);
static int UpdateInterp(NsInterp *itPtr);
static void RunTclTraces(NsInterp *itPtr, int why);
static int EvalTrace(Tcl_Interp *interp, void *arg);
static int RegisterAt(Ns_TclTraceProc *proc, void *arg, int when);
static Tcl_AsyncProc AsyncCancel;

/*
 * Static variables defined in this file.
 */

static Ns_Tls tls;		/* Slot for per-thread Tcl interp cache. */
static Tcl_HashTable threads;	/* Table of threads with nsd-based interps. */
static Ns_Mutex lock;		/* Lock around threads table. */


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
    /*
     * Allocate the thread storage slot for the table of interps
     * per-thread. At thread exit, DeleteData will be called
     * to free any interps remaining on the thread cache
     * and remove the async cancel handler.
     */

    Ns_TlsAlloc(&tls, DeleteData);

    /*
     * Initialize the table of all threads with active TclData.
     */

    Tcl_InitHashTable(&threads, TCL_ONE_WORD_KEYS);
    Ns_MutexSetName(&lock, "ns:threads");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclCreateInterp --
 *
 *      Create a new interp with basic AOLserver commands. 
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
    return CreateInterp(NULL, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInit --
 *
 *      Initialize an interp with basic AOLserver commands.
 *
 * Results:
 *      TCL_OK or TCL_ERROR on init error.
 *
 * Side effects:
 *	See InitInterp.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInit(Tcl_Interp *interp)
{
    /*
     * Initialize libnsd if not already initialized at library load
     * time.
     */

    Ns_LibInit();

    NewInterpData(interp, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Nsd_Init --
 *
 *      Init routine called when libnsd is loaded via the Tcl
 *	load command. This simply calls Ns_TclInit.
 *
 * Results:
 *      See Ns_TclInit.
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
 * Ns_TclEval --
 *
 *	Execute a Tcl script in the context of the the given server.
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
 * Ns_TclAllocateInterp --
 *
 *	Allocate an interpreter from the per-thread list.  Note that a
 *	single thread can have multiple interps for multiple virtual
 *	servers.
 *
 * Results:
 *	Pointer to Tcl_Interp.
 *
 * Side effects:
 *	See PopInterp for details on various traces which may be
 *	called.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclAllocateInterp(char *server)
{
    NsInterp *itPtr;

    itPtr = PopInterp(server);
    return (itPtr ? itPtr->interp : NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDeAllocateInterp --
 *
 *	Get the NsInterp for the given interp and return the interp to
 *	the per-thread cache.  If the interp is associated with a
 *	connection, silently do nothing as cleanup will occur later
 *	with connection cleanup.  Also, if the interp is not actually
 *	an AOLserver interp, i.e., missing the NsInterp structure,
 *	simply delete the interp directly (this is suspect).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See notes on garbage collection in PushInterp.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TclDeAllocateInterp(Tcl_Interp *interp)
{
    NsInterp *itPtr;

    itPtr = NsGetInterpData(interp);
    if (itPtr == NULL) {
	Tcl_DeleteInterp(interp);
    } else if (itPtr->conn == NULL) {
    	PushInterp(itPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetConnInterp --
 *
 *	Get the interp for the given connection.  When first called
 *	for a connection, the interp data is allocated and associated
 *	with the given connection.  The interp will be automatically
 *	cleaned up at the end of the connection via a call to via
 *	NsFreeConnInterp().
 *
 * Results:
 *	Pointer to Tcl interp data initialized for given connection.
 *
 * Side effects:
 *	See NsGetInputEncodings for details on connection encoding setup
 *	required to ensure proper UTF-8 input and output.
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_GetConnInterp(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;
    NsInterp *itPtr;

    if (connPtr->itPtr == NULL) {
	itPtr = PopInterp(connPtr->server);
	itPtr->conn = conn;
	itPtr->nsconn.flags = 0;
	connPtr->itPtr = itPtr;
    	Tcl_SetVar2(itPtr->interp, "conn", NULL, connPtr->idstr,
		    TCL_GLOBAL_ONLY);
	RunTclTraces(itPtr, NS_TCL_TRACE_GETCONN);
    }
    return connPtr->itPtr->interp;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_FreeConnInterp --
 *
 *	Release and cleanup the interp associated with given
 *	connection.  This routine no longer does actual cleanup.  The
 *	connection cleanup code will call NsFreeConnInterp if needed.
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
Ns_FreeConnInterp(Ns_Conn *conn)
{
    return;
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
    NsInterp *itPtr = NsGetInterpData(interp);

    return (itPtr ? itPtr->conn : NULL);
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
    NsInterp *itPtr = NsGetInterpData(interp);

    /*
     * If this is an AOLserver interp, invoke the delete traces.
     */

    if (itPtr != NULL) {
	RunTclTraces(itPtr, NS_TCL_TRACE_DELETE);
    }

    /*
     * All other cleanup, including the NsInterp data, if any, will
     * be handled by Tcl's normal delete mechanisms.
     */

    Tcl_DeleteInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclMarkForDelete --
 *
 *	Mark the interp to be deleted at the  next deallocation.  This
 *	routine is useful to destory interps after they've been
 *	modified in weird ways, e.g., by the TclPro debugger.
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
    NsInterp *itPtr = NsGetInterpData(interp);
    
    if (itPtr != NULL) {
	itPtr->delete = 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterTrace --
 *
 *  	Add a Tcl trace.  Traces are called in FIFO order.
 *
 * Results:
 *	NS_OK if called with a non-NULL server, NS_ERROR otherwise.
 *
 * Side effects:
 *	Will modify server trace list.
 *
 *----------------------------------------------------------------------
 */
 
int
Ns_TclRegisterTrace(char *server, Ns_TclTraceProc *proc, void *arg, int when)
{
    TclTrace *tracePtr, **firstPtrPtr;
    NsServer *servPtr;

    if (Ns_InfoStarted()) {
	return NS_ERROR;
    }
    servPtr = NsGetServer(server);
    tracePtr = ns_malloc(sizeof(TclTrace));
    tracePtr->proc = proc;
    tracePtr->arg = arg;
    tracePtr->when = when;
    tracePtr->nextPtr = NULL;
    firstPtrPtr = &servPtr->tcl.firstTracePtr;
    while (*firstPtrPtr != NULL) {
	firstPtrPtr = &((*firstPtrPtr)->nextPtr);
    }
    *firstPtrPtr = tracePtr;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterAtCreate, Ns_TclRegisterAtCleanup,
 * Ns_TclRegisterAtDelete --
 *
 *	Register callbacks for interp create, cleanup, and delete at
 *	startup.  These routines are deprecated in favor of the more
 *	general Ns_TclRegisterTrace. In particular, they do not take a
 *	virtual server argument so must assume the currently
 *	initializing server is the intended server.
 *
 * Results:
 *	See Ns_TclRegisterTrace.
 *
 * Side effects:
 *	See Ns_TclRegisterTrace.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclRegisterAtCreate(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_CREATE);
}

int
Ns_TclRegisterAtCleanup(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_DEALLOCATE);
}

int
Ns_TclRegisterAtDelete(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterAt(proc, arg, NS_TCL_TRACE_DELETE);
}

static int
RegisterAt(Ns_TclTraceProc *proc, void *arg, int when)
{
    NsServer *servPtr;

    servPtr = NsGetInitServer();
    if (servPtr == NULL) {
	return NS_ERROR;
    }
    return Ns_TclRegisterTrace(servPtr->server, proc, arg, when);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitInterps --
 *
 *	Arrange for the given proc to be called on newly created
 *	interps.  This routine now simply uses the more general Tcl
 *	interp tracing facility.  Earlier versions of AOLserver would
 *	invoke the given proc immediately on each interp in a shared
 *	pool which explains this otherwise misnamed API.
 *
 * Results:
 *	See Ns_TclRegisterTrace.
 *
 * Side effects:
 *	See Ns_TclRegisterTrace.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitInterps(char *server, Ns_TclInterpInitProc *proc, void *arg)
{
    return Ns_TclRegisterTrace(server, proc, arg, NS_TCL_TRACE_CREATE);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterDeferred --
 *
 *	Register a procedure to be called when the interp is deallocated.
 *	This is on-shot FIFO order callback mechanism which is seldom
 *	used.
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
Ns_TclRegisterDeferred(Tcl_Interp *interp, Ns_TclDeferProc *proc, void *arg)
{
    NsInterp   *itPtr = NsGetInterpData(interp);
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

    return (servPtr->tcl.library);
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
    NsInterp *itPtr = NsGetInterpData(interp);
    
    return (itPtr ? itPtr->servPtr->server : NULL);
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
        errorInfo = Tcl_GetStringResult(interp);
    }
    Ns_Log(Error, "Tcl exception:\n%s", errorInfo);
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
 *      Always TCL_OK.
 *
 * Side effects:
 *	Module will be initialized by the init script later.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitModule(char *server, char *module)
{
    NsServer *servPtr = NsGetServer(server);

    Tcl_DStringAppendElement(&servPtr->tcl.modules, module);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetInterpServer --
 *
 *      Get server for given interp.
 *
 * Results:
 *      TCL_OK if interp has a server, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Given serverPtr will be updated with pointer to server string.
 *
 *----------------------------------------------------------------------
 */

int
NsTclGetServer(NsInterp *itPtr, char **serverPtr)
{
    if (itPtr->servPtr->server == NULL) {
	Tcl_SetResult(itPtr->interp, "no server", TCL_STATIC);
	return TCL_ERROR;
    }
    *serverPtr = itPtr->servPtr->server;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclICtlObjCmd --
 *
 *      Implements ns_ictl command to control interp state for
 *	virtual server interps.  This command provide internal control
 *	functions required by the init.tcl script and is not intended
 *	to be called by a user directly.  It supports four activities:
 *	1. Managing the list of "modules" to initialize.
 *	2. Saving the init script for evaluation with new interps.
 *	3. Checking for change of the init script.
 *	4. Register script-level traces.
 *
 *	See init.tcl for details.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *	May update current saved server Tcl state.
 *
 *----------------------------------------------------------------------
 */

int
NsTclICtlObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Defer    *deferPtr;
    ScriptTrace *stPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    TclData *dataPtr;
    Tcl_Obj *objPtr, *listPtr;
    int	      when, length, result, tid;
    char     *script;
    static CONST char *opts[] = {
	"addmodule", "cleanup", "epoch", "get", "getmodules", "save",
	"update", "oncreate", "oncleanup", "oninit", "ondelete", "trace",
	"threads", "cancel",
	NULL
    };
    enum {
	IAddModuleIdx, ICleanupIdx, IEpochIdx, IGetIdx, IGetModulesIdx,
	ISaveIdx, IUpdateIdx, IOnCreateIdx, IOnCleanupIdx, IOnInitIdx,
        IOnDeleteIdx, ITraceIdx, IThreadsIdx, ICancelIdx
    } opt;
    static CONST char *topts[] = {
	"create", "delete", "allocate", "deallocate", "getconn", "freeconn",
	NULL
    };
    enum {
	TCreateIdx, TDeleteIdx, TAllocateIdx, TDeAllocateIdx,
	TGetConnIdx, TFreeConnIdx
    } topt;
    static int twhen[] = {
	NS_TCL_TRACE_CREATE, NS_TCL_TRACE_DELETE,
	NS_TCL_TRACE_ALLOCATE, NS_TCL_TRACE_DEALLOCATE,
	NS_TCL_TRACE_GETCONN, NS_TCL_TRACE_FREECONN
    };

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
	/*
	 * Add a Tcl module to the list of for later initialization.
	 */

	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "module");
	    return TCL_ERROR;
	}
	Ns_TclInitModule(servPtr->server, Tcl_GetString(objv[2]));
	break;

    case IGetModulesIdx:
	/*
	 * Get the list of modules for initialization.  See init.tcl
	 * for expected use.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Tcl_SetResult(interp, servPtr->tcl.modules.string, TCL_VOLATILE);
	break;

    case IGetIdx:
	/*
	 * Get the current init script to evaluate in new interps.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Ns_RWLockRdLock(&servPtr->tcl.lock);
	Tcl_SetResult(interp, servPtr->tcl.script, TCL_VOLATILE);
	Ns_RWLockUnlock(&servPtr->tcl.lock);
	break;

    case IEpochIdx:
	/*
	 * Check the version of this interp against current init script.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Ns_RWLockRdLock(&servPtr->tcl.lock);
	Tcl_SetIntObj(Tcl_GetObjResult(interp), servPtr->tcl.epoch);
	Ns_RWLockUnlock(&servPtr->tcl.lock);
	break;
   
    case ISaveIdx:
	/*
	 * Save the init script.
	 */

	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
	    return TCL_ERROR;
    	}
	script = ns_strdup(Tcl_GetStringFromObj(objv[2], &length));
	Ns_RWLockWrLock(&servPtr->tcl.lock);
	ns_free(servPtr->tcl.script);
	servPtr->tcl.script = script;
	servPtr->tcl.length = length;
	if (++servPtr->tcl.epoch == 0) {
	    /* NB: Epoch zero reserved for new interps. */
	    ++servPtr->tcl.epoch;
	}
	Ns_RWLockUnlock(&servPtr->tcl.lock);
	break;

    case IUpdateIdx:
	/*
	 * Check for and process possible change in the init script.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	result = UpdateInterp(itPtr);
	break;

    case ICleanupIdx:
	/*
	 * Invoke the legacy defer callbacks.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
    	while ((deferPtr = itPtr->firstDeferPtr) != NULL) {
	    itPtr->firstDeferPtr = deferPtr->nextPtr;
	    (*deferPtr->proc)(interp, deferPtr->arg);
	    ns_free(deferPtr);
	}
	break;

    case IOnInitIdx:
    case IOnCreateIdx:
    case IOnCleanupIdx:
    case IOnDeleteIdx:
	/*
	 * Register script-level interp traces.
	 */

        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "script");
	    return TCL_ERROR;
        }
	switch (opt) {
	case IOnInitIdx:
	case IOnCreateIdx:
	    when = NS_TCL_TRACE_CREATE;
	    break;
	case IOnCleanupIdx:
	    when = NS_TCL_TRACE_DEALLOCATE;
	    break;
	case IOnDeleteIdx:
	    when = NS_TCL_TRACE_DELETE;
	    break;
	default:
	    /* NB: Silence compiler. */
	    break;
	}
	goto trace;
	break;

    case ITraceIdx:
        if (objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "when script");
	    return TCL_ERROR;
        }
    	if (Tcl_GetIndexFromObj(interp, objv[2], topts, "when", 0,
			    (int *) &topt) != TCL_OK) {
	    return TCL_ERROR;
    	}
	when = twhen[topt];
    trace:
	if (servPtr != NsGetInitServer()) {
	    Tcl_AppendResult(interp, "attempt to call ", opts[opt],
		" after startup", NULL);
	    return TCL_ERROR;
	}
	script = Tcl_GetString(objv[objc-1]);
	length = strlen(script);
	stPtr = ns_malloc(sizeof(ScriptTrace) + length);
	stPtr->length = length;
	strcpy(stPtr->script,  script);
	(void) Ns_TclRegisterTrace(servPtr->server, EvalTrace, stPtr, when);
	break;

    case IThreadsIdx:
        if (objc > 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
        }
	listPtr = Tcl_NewObj();
	Ns_MutexLock(&lock);
	hPtr = Tcl_FirstHashEntry(&threads, &search);
	while (hPtr != NULL) {
	    tid = (int) Tcl_GetHashKey(&threads, hPtr);
	    objPtr = Tcl_NewIntObj(tid);
	    Tcl_ListObjAppendElement(interp, listPtr, objPtr);
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Ns_MutexUnlock(&lock);
	Tcl_SetObjResult(interp, listPtr);
	break;

    case ICancelIdx:
	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "tid");
	    return TCL_ERROR;
        }
	if (Tcl_GetIntFromObj(interp, objv[2], &tid) != TCL_OK) {
	    return TCL_ERROR;
	}
	Ns_MutexLock(&lock);
	hPtr = Tcl_FindHashEntry(&threads, (char *) tid);
	if (hPtr != NULL) {
	    dataPtr = Tcl_GetHashValue(hPtr);
	    Tcl_AsyncMark(dataPtr->cancel);
	}
	Ns_MutexUnlock(&lock);
	if (hPtr == NULL) {
	    Tcl_AppendResult(interp, "no such active thread: ",
			     Tcl_GetString(objv[2]), NULL);
	    return TCL_ERROR;
	}
	break;
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAtCloseObjCmd --
 *
 *	Implements ns_atclose. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Script will be invoked when the connection is closed.  Note
 *	the connection may continue execution, e.g., with continued
 *	ADP code, traces, etc.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAtCloseObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   CONST Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    AtClose *atPtr;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "script ?args?");
	return TCL_ERROR;
    }
    if (NsTclGetConn(itPtr, NULL) != TCL_OK) {
	return TCL_ERROR;
    }
    atPtr = ns_malloc(sizeof(AtClose));
    atPtr->nextPtr = itPtr->firstAtClosePtr;
    itPtr->firstAtClosePtr = atPtr;
    atPtr->objPtr = Tcl_ConcatObj(objc-1, objv+1);
    Tcl_IncrRefCount(atPtr->objPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMarkForDeleteObjCmd --
 *
 *	Implements ns_markfordelete. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See Ns_TclMarkForDelete. 
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
 * NsTclDummyObjCmd --
 *
 *      Dummy command for ns_init and ns_cleanup.  The default
 *	init.tcl script will re-define these commands with proper
 *	startup initialization and deallocation scripts.
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
 * NsGetInterpData --
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
NsGetInterpData(Tcl_Interp *interp)
{
    return (interp ? Tcl_GetAssocData(interp, "ns:data", NULL) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeConnInterp --
 *
 *	Free the interp data, if any, for given connection.  This
 *	routine is called at the end of connection processing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See PushInterp.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeConnInterp(Conn *connPtr)
{
    NsInterp *itPtr = connPtr->itPtr;

    if (itPtr != NULL) {
    	RunTclTraces(itPtr, NS_TCL_TRACE_FREECONN);
    	itPtr->conn = NULL;
    	itPtr->nsconn.flags = 0;
    	PushInterp(itPtr);
    	connPtr->itPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRunAtClose --
 *
 *	Run any registered connection at-close scripts. 
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
NsTclRunAtClose(NsInterp *itPtr)
{
    Tcl_Interp    *interp = itPtr->interp;
    AtClose       *atPtr;

    while ((atPtr = itPtr->firstAtClosePtr) != NULL) {
	itPtr->firstAtClosePtr = atPtr->nextPtr;
	if (Tcl_EvalObjEx(interp, atPtr->objPtr, TCL_EVAL_DIRECT) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
	Tcl_DecrRefCount(atPtr->objPtr);
	ns_free(atPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PopInterp --
 *
 *	Pop next avaialble virtual-server interp from the per-thread
 *	cache, allocating a new interp if necessary.
 *
 * Results:
 *	Pointer to next available NsInterp.
 *
 * Side effects:
 *	Will invoke alloc traces and, if the interp is new, create
 *	traces.
 *
 *----------------------------------------------------------------------
 */

static NsInterp *
PopInterp(char *server)
{
    static Ns_Cs lock;
    NsServer *servPtr;
    NsInterp *itPtr;
    Tcl_HashEntry *hPtr;
    Tcl_Interp *interp;

    /*
     * Verify the server.  NULL (i.e., no server) is valid but
     * a non-null, unknown server is an error.
     */

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
	return NULL;
    }

    /*
     * Pop the first interp off the list of availabe interps for
     * the given virtual server on this thread.  If none exists,
     * create and initialize a new interp.
     */

    hPtr = GetCacheEntry(servPtr);
    itPtr = Tcl_GetHashValue(hPtr);
    if (itPtr != NULL) {
	Tcl_SetHashValue(hPtr, itPtr->nextPtr);
    } else {
    	if (nsconf.tcl.lockoninit) {
	    Ns_CsEnter(&lock);
    	}
	(void) CreateInterp(&itPtr, server);
    	RunTclTraces(itPtr, NS_TCL_TRACE_CREATE);
    	if (UpdateInterp(itPtr) != TCL_OK) {
	    Ns_TclLogError(itPtr->interp);
	}
    	if (nsconf.tcl.lockoninit) {
	    Ns_CsLeave(&lock);
    	}
    }
    itPtr->nextPtr = NULL;
    interp = itPtr->interp;

    /*
     * Clear any pending async cancel message.
     */

    (void) Tcl_AsyncInvoke(interp, TCL_OK);
    Tcl_ResetResult(interp);

    /*
     * Run allocation traces and evaluate the ns_init proc which by
     * default updates the interp state with ns_ictl if necessary.
     */

    RunTclTraces(itPtr, NS_TCL_TRACE_ALLOCATE);
    if (Tcl_EvalEx(interp, "ns_init", -1, 0) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_ResetResult(interp);
    return itPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * PushInterp --
 *
 *	Return a virtual-server interp to the per-thread interp
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will invoke de-alloc traces.
 *
 *----------------------------------------------------------------------
 */

static void
PushInterp(NsInterp *itPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    Tcl_HashEntry *hPtr;

    /*
     * Evaluate the cleanup script to perform various garbage collection
     * and then either delete the interp or push it back on the
     * per-thread list.
     */

    RunTclTraces(itPtr, NS_TCL_TRACE_DEALLOCATE);
    if (Tcl_EvalEx(interp, "ns_cleanup", -1, 0) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    if (itPtr->delete) {
	Ns_TclDestroyInterp(interp);
    } else {
	Tcl_ResetResult(interp);
	hPtr = GetCacheEntry(itPtr->servPtr);
	itPtr->nextPtr = Tcl_GetHashValue(hPtr);
	Tcl_SetHashValue(hPtr, itPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetCacheEntry --
 *
 *      Get hash entry in per-thread interp cache for given virtual
 *	 server
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
GetCacheEntry(NsServer *servPtr)
{
    TclData *dataPtr = GetData();
    int new;

    return Tcl_CreateHashEntry(&dataPtr->interps, (char *) servPtr, &new);
}


/*
 *----------------------------------------------------------------------
 *
 * EvalTrace --
 *
 *      Eval the given script from being called as a Tcl Init callback.
 *
 * Results:
 *      Status from script eval.
 *
 * Side effects:
 *	Depends on script.
 *
 *----------------------------------------------------------------------
 */

static int
EvalTrace(Tcl_Interp *interp, void *arg)
{
    ScriptTrace *stPtr = arg;

    return Tcl_EvalEx(interp, stPtr->script, stPtr->length, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * CreateInterp --
 *
 *      Create a new AOLserver interp.
 *
 * Results:
 *      Pointer to new NsInterp.
 *
 * Side effects:
 *	NsInterp structure for new interp will be stored in itPtrPt
 *	if not null.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Interp *
CreateInterp(NsInterp **itPtrPtr, char *server)
{
    NsInterp *itPtr;
    Tcl_Interp *interp;

    /*
     * Create and initialize a basic Tcl interp.
     */

    interp = Tcl_CreateInterp();
    Tcl_InitMemory(interp);
    if (Tcl_Init(interp) != TCL_OK) {
	Ns_TclLogError(interp);
    }

    /*
     * Allocate and associate a new NsInterp struct for the interp.
     */

    itPtr = NewInterpData(interp, server);
    if (itPtrPtr != NULL) {
	*itPtrPtr = itPtr;
    }
    return interp;
}


/*
 *----------------------------------------------------------------------
 *
 * NewInterpData --
 *
 *      Create a new NsInterp struct for the given interp, adding
 *	core AOLserver commands and associating it with the interp.
 *
 * Results:
 *      Pointer to new NsInterp struct.
 *
 * Side effects:
 *	Will add core AOLserver commands to given interp.
 *
 *----------------------------------------------------------------------
 */

static NsInterp *
NewInterpData(Tcl_Interp *interp, char *server)
{
    static volatile int initialized = 0;
    NsInterp *itPtr;

    /*
     * Core one-time AOLserver initialization to add a few Tcl_Obj
     * types.  These calls cannot be in NsTclInit above because
     * Tcl is not fully initialized at libnsd load time.
     */

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    NsTclInitQueueType();
	    NsTclInitAddrType();
	    NsTclInitTimeType();
	    NsTclInitCacheType();
	    NsTclInitKeylistType();
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }

    /*
     * Allocate and initialize a new NsInterp struct.
     */

    itPtr = ns_calloc(1, sizeof(NsInterp));
    itPtr->interp = interp;
    itPtr->servPtr = NsGetServer(server);
    Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
    Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);	
    Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);	
    NsAdpInit(itPtr);
    itPtr->adp.cwd = Ns_PageRoot(server);

    /*
     * Associate the new NsInterp with this interp.  At interp delete
     * time, Tcl will call FreeInterpData to cleanup the struct.
     */

    Tcl_SetAssocData(interp, "ns:data", FreeInterpData, itPtr);

    /*
     * Ensure the per-thread data with async cancel handle is allocated.
     */

    (void) GetData();

    /*
     * Add core AOLserver commands.
     */

    NsTclAddCmds(interp, itPtr);
    Tcl_PkgProvide(interp, "Nsd", NS_VERSION);
    return itPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateServerInterpData --
 *
 *      Update the state (procs, namespaces) of an interp.  The current
 *      state of interps for a virtual server is defined by a script
 *      used to add any needed procs, vars, package calls, etc.  By
 *	default, the state script for a virtual server is created at
 *	startup by an interp which sources all cooresponding
 *	configuration, saves the resulting definitions as a script via
 *	"ns_ictl save" (see nsd/init.tcl for details), and marks
 *	itself for deletion as it's only purpose was to create the
 *	initial state.  To detect possible change of the script at
 *	runtime through other interps (e.g., through a call to
 *	ns_eval), an epoch counter is maintained and updated each time
 *	a new script is saved.  New interps are initialized with epoch
 *	0 and always detect the need to evaluate the state script
 *	below.  Existing interps can call UpdateInterp to detect
 *	runtime config changes via "ns_ictl update" which, by default,
 *	is called in the "ns_cleanup" garbage collection script called
 *	at interp deallocation time.
 *
 * Results:
 *      Tcl result from update script if invoked, TCL_OK otherwise.
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

    /*
     * A reader-writer lock is used on the assumption updates are
     * rare and likley expensive to evaluate if the virtual server
     * contains significant state.
     */

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
 * FreeInterpData --
 *
 *      Tcl assoc data callback to destroy the per-interp NsInterp
 *      structure at interp delete time.
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
FreeInterpData(ClientData arg, Tcl_Interp *interp)
{
    NsInterp *itPtr = arg;

    NsAdpFree(itPtr);
    Tcl_DeleteHashTable(&itPtr->sets);
    Tcl_DeleteHashTable(&itPtr->chans);
    Tcl_DeleteHashTable(&itPtr->https);
    ns_free(itPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetData --
 *
 *	Return the per-thread Tcl data structure for current thread.
 *
 * Results:
 *	Pointer to TclData structure.
 *
 * Side effects:
 *	Will allocate and initialize TclData struct if necessary.
 *
 *----------------------------------------------------------------------
 */

static TclData *
GetData(void)
{
    TclData *dataPtr;
    int tid, new;

    dataPtr = Ns_TlsGet(&tls);
    if (dataPtr == NULL) {
	dataPtr = ns_malloc(sizeof(TclData));
	dataPtr->cancel = Tcl_AsyncCreate(AsyncCancel, NULL);
	Tcl_InitHashTable(&dataPtr->interps, TCL_ONE_WORD_KEYS);
	tid = Ns_ThreadId();
	Ns_MutexLock(&lock);
	dataPtr->hPtr = Tcl_CreateHashEntry(&threads, (char *) tid, &new);
	Tcl_SetHashValue(dataPtr->hPtr, dataPtr);
	Ns_MutexUnlock(&lock);
	Ns_TlsSet(&tls, dataPtr);
    }
    return dataPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * DeleteData --
 *
 *      Delete all per-thread data at thread exit time.
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
DeleteData(void *arg)
{
    TclData *dataPtr = arg;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    NsInterp *itPtr;

    Ns_MutexLock(&lock);
    Tcl_DeleteHashEntry(dataPtr->hPtr);
    Ns_MutexUnlock(&lock);
    hPtr = Tcl_FirstHashEntry(&dataPtr->interps, &search);
    while (hPtr != NULL) {
	while ((itPtr = Tcl_GetHashValue(hPtr)) != NULL) {
	    Tcl_SetHashValue(hPtr, itPtr->nextPtr);
	    Ns_TclDestroyInterp(itPtr->interp);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&dataPtr->interps);
    Tcl_AsyncDelete(dataPtr->cancel);
    ns_free(dataPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * RunTclTraces --
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
RunTclTraces(NsInterp *itPtr, int why)
{
    TclTrace *tracePtr;

    if (itPtr->servPtr != NULL) {
    	tracePtr = itPtr->servPtr->tcl.firstTracePtr;
    	while (tracePtr != NULL) {
	    if ((tracePtr->when & why)) {
		if ((*tracePtr->proc)(itPtr->interp, tracePtr->arg) != TCL_OK) {
	            Ns_TclLogError(itPtr->interp);
		}
	    }
	    tracePtr = tracePtr->nextPtr;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AsyncCancel --
 *
 *	Callback which cancels Tcl execution in the given thread.
 *
 * Results:
 *	TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
AsyncCancel(ClientData ignored, Tcl_Interp *interp, int code)
{
    Tcl_ResetResult(interp);
    Tcl_SetResult(interp, "async cancel", TCL_STATIC);
    return TCL_ERROR;
}

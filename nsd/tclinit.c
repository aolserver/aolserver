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
 *	all AOLserver commands (see InitData and NsTclAddCmds in
 *	tclcmds.c for details).  Both for cases where the ClientData
 *	isn't available and to ensure proper cleanup of the structure
 *	when the interp is deleted, the NsInterp is managed by the
 *	interp via the Tcl assoc data interface under the name
 *	"ns:data" and accessible by NsGetInterpData.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.52 2006/06/02 18:51:49 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
 * are normally registered during server startup and invoked later for
 * virutal server interps at specific points in the lifetime of the
 * interp.  Initialization callbacks (create, alloc, getconn) are called
 * in FIFO order while finalization callbacks (freeconn, dealloc,
 * delete) are called in LIFO order. In addition, script callbacks are
 * invoked after non-script callbacks.  A
 * common trace would be an "create" trace to add commands in a loadable
 * C module, e.g., the "ns_accesslog" command in nslog.
 */

typedef struct TclTrace {
    struct TclTrace	 *prevPtr;
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
 * The following structure maintains callbacks registered with the
 * Ns_TclRegisterDeffered rouinte to invoke during interp deallocation
 * Unlike traces, these callbacks are one-shot events and invoked in
 * FIFO order (LIFO would probably have been better). In practice this
 * API is rarely used. Instead, more specific garbage collection schemes
 * should be used with "ns_ictl trace deallocate".
 */

typedef struct Defer {
    struct Defer *nextPtr;
    Ns_TclDeferProc *proc;
    void *arg;
} Defer;

/*
 * The following structure maintains scripts registered via ns_atclose
 * to invoke when the connection is closed.  The scripts are invoked in
 * LIFO order.  As with the Ns_TclRegisteredDeferred callbacks, this
 * interface is rarely used.
 */

typedef struct AtClose {
    struct AtClose *nextPtr;
    Tcl_Obj *objPtr;
} AtClose;

/*
 * The following defines a multi-thread aware Tcl package.
 */

typedef struct Package {
    char *name;
    int exact;
    char version[1];
} Package;

/*
 * Static functions defined in this file.
 */

static TclData *GetData(void);
static Ns_TlsCleanup DeleteData;
static Tcl_Interp *CreateInterp(NsServer *server);
static int InitData(Tcl_Interp *interp, NsServer *servPtr);
static Tcl_InterpDeleteProc FreeData;
static NsInterp *PopInterp(char *server);
static void PushInterp(NsInterp *itPtr);
static Tcl_HashEntry *GetCacheEntry(NsServer *servPtr);
static void RunTraces(NsInterp *itPtr, int why);
static void ForeachTrace(NsInterp *itPtr, int why, int append);
static void DoTrace(Tcl_Interp *interp, TclTrace *tracePtr, int append);
static int EvalTrace(Tcl_Interp *interp, void *arg);
static int RegisterAt(Ns_TclTraceProc *proc, void *arg, int when);
static Tcl_AsyncProc AsyncCancel;
static Ns_TclTraceProc PkgRequire;

/*
 * Static variables defined in this file.
 */

static Ns_Tls tls;		/* Slot for per-thread Tcl interp cache. */
static Tcl_HashTable threads;	/* Table of threads with nsd-based interps. */
static Ns_Mutex tlock;		/* Lock around threads table. */


/*
 *----------------------------------------------------------------------
 *
 * NsInitTcl --
 *
 *	Initialize the Nsd Tcl package.
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
     * Initialize the table of all threads with active TclData
     * and the one-time init table.
     */

    Tcl_InitHashTable(&threads, TCL_ONE_WORD_KEYS);
    Ns_MutexSetName(&tlock, "ns:threads");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclCreateInterp --
 *
 *      Create a new interp with basic Nsd package.
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
    return Ns_TclAllocateInterp(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInit --
 *
 *      Initialize an interp with the global server context.
 *
 * Results:
 *      TCL_OK or TCL_ERROR on init error.
 *
 * Side effects:
 *	See InitData.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInit(Tcl_Interp *interp)
{
    NsServer *servPtr = NsGetServer(NULL);

    return InitData(interp, servPtr);
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
 *	simply delete the interp directly.
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
	RunTraces(itPtr, NS_TCL_TRACE_GETCONN);
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

    if (itPtr != NULL) {
    	RunTraces(itPtr, NS_TCL_TRACE_DELETE);
    }
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
    TclTrace *tracePtr;
    NsServer *servPtr;

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
	return NS_ERROR;
    }
    tracePtr = ns_malloc(sizeof(TclTrace));
    tracePtr->proc = proc;
    tracePtr->arg = arg;
    tracePtr->when = when;
    tracePtr->nextPtr = NULL;
    Ns_RWLockWrLock(&servPtr->tcl.tlock);
    tracePtr->prevPtr = servPtr->tcl.lastTracePtr;
    servPtr->tcl.lastTracePtr = tracePtr;
    if (tracePtr->prevPtr != NULL) {
	tracePtr->prevPtr->nextPtr = tracePtr;
    } else {
	servPtr->tcl.firstTracePtr = tracePtr;
    }
    Ns_RWLockUnlock(&servPtr->tcl.tlock);
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

    return (servPtr ? servPtr->tcl.library : NULL);
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

    if (servPtr == NULL) {
	return NS_ERROR;
    }
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
    Package *pkgPtr;
    int	      when, length, result, tid, new, exact;
    char     *script, *name, *pattern, *version;
    static CONST char *opts[] = {
	"addmodule", "cleanup", "epoch", "get", "getmodules", "save",
	"update", "oncreate", "oncleanup", "oninit", "ondelete", "trace",
	"threads", "cancel", "runtraces", "gettraces", "package", "once",
	NULL
    };
    enum {
	IAddModuleIdx, ICleanupIdx, IEpochIdx, IGetIdx, IGetModulesIdx,
	ISaveIdx, IUpdateIdx, IOnCreateIdx, IOnCleanupIdx, IOnInitIdx,
        IOnDeleteIdx, ITraceIdx, IThreadsIdx, ICancelIdx, IRunIdx, 
	IGetTracesIdx, IPackageIdx, IOnceIdx
    } opt;
    static CONST char *popts[] = {
	"require", "names", NULL
    };
    enum {
	PRequireIdx, PNamesIdx
    } _nsmayalias popt;
    static CONST char *topts[] = {
	"create", "delete", "allocate",
	"deallocate", "getconn", "freeconn", NULL
    };
    static int twhen[] = {
    	NS_TCL_TRACE_CREATE, NS_TCL_TRACE_DELETE, NS_TCL_TRACE_ALLOCATE,
    	NS_TCL_TRACE_DEALLOCATE, NS_TCL_TRACE_GETCONN, NS_TCL_TRACE_FREECONN
    };
    enum {
    	TCreateIdx, TDeleteIdx, TAllocateIdx,
	TDeAllocateIdx, TGetConnIdx, TFreeConnIdx
    } _nsmayalias topt;

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
	Ns_RWLockRdLock(&servPtr->tcl.slock);
	objPtr = Tcl_NewStringObj(servPtr->tcl.script, servPtr->tcl.length);
	Ns_RWLockUnlock(&servPtr->tcl.slock);
	Tcl_SetObjResult(interp, objPtr);
	break;

    case IEpochIdx:
	/*
	 * Check the version of this interp against current init script.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	Ns_RWLockRdLock(&servPtr->tcl.slock);
	Tcl_SetIntObj(Tcl_GetObjResult(interp), servPtr->tcl.epoch);
	Ns_RWLockUnlock(&servPtr->tcl.slock);
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
	Ns_RWLockWrLock(&servPtr->tcl.slock);
	ns_free(servPtr->tcl.script);
	servPtr->tcl.script = script;
	servPtr->tcl.length = length;
	if (++servPtr->tcl.epoch == 0) {
	    /* NB: Epoch zero reserved for new interps. */
	    ++servPtr->tcl.epoch;
	}
	Ns_RWLockUnlock(&servPtr->tcl.slock);
	break;

    case IUpdateIdx:
	/*
	 * Check for and process possible change in the init script.
	 */

	if (objc != 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
    	Ns_RWLockRdLock(&servPtr->tcl.slock);
    	if (itPtr->epoch != servPtr->tcl.epoch) {
	    result = Tcl_EvalEx(itPtr->interp, servPtr->tcl.script,
		    	    	servPtr->tcl.length, TCL_EVAL_GLOBAL);
	    itPtr->epoch = servPtr->tcl.epoch;
    	}
    	Ns_RWLockUnlock(&servPtr->tcl.slock);
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

    case IPackageIdx:
        if (objc < 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "option ?args?");
	    return TCL_ERROR;
        }
    	if (Tcl_GetIndexFromObj(interp, objv[2], popts, "option", 0,
			    (int *) &popt) != TCL_OK) {
	    return TCL_ERROR;
    	}
	switch (popt) {
	case PNamesIdx:
	    if (objc > 3) {
		pattern = Tcl_GetString(objv[3]);
	    } else {
		pattern = NULL;
	    }
	    listPtr = Tcl_NewObj();
	    Ns_MutexLock(&servPtr->tcl.plock);
	    hPtr = Tcl_FirstHashEntry(&servPtr->tcl.packages, &search);
	    while (hPtr != NULL) {
		name = Tcl_GetHashKey(&servPtr->tcl.packages, hPtr);
		if (pattern == NULL || Tcl_StringMatch(name, pattern)) {
		    Tcl_ListObjAppendElement(interp, listPtr,
					     Tcl_NewStringObj(name, -1));
		}
		hPtr = Tcl_NextHashEntry(&search);
	    }
	    Ns_MutexUnlock(&servPtr->tcl.plock);
	    Tcl_SetObjResult(interp, listPtr);
	    break;

	case PRequireIdx:
	    if (objc < 4 || objc > 6) {
badargs:
            	Tcl_WrongNumArgs(interp, 3, objv, "?-exact? package ?version?");
		return TCL_ERROR;
	    }
	    exact = 0;
	    name = Tcl_GetString(objv[3]);
	    if (STREQ(name, "-exact")) {
		if (objc < 5) {
		    goto badargs;
		}
		--objc;
		++objv;
		exact = 1;
		name = Tcl_GetString(objv[3]);
	    }
	    if (objc < 5) {
		version = NULL;
	    } else {
		version = Tcl_GetString(objv[4]);
	    }

	    /*
	     * Confirm the package can be loaded and determine version.
	     */

	    version = Tcl_PkgRequire(interp, name, version, exact);
	    if (version == NULL) {
		return TCL_ERROR;
	    }
	    Ns_MutexLock(&servPtr->tcl.plock);
	    hPtr = Tcl_CreateHashEntry(&servPtr->tcl.packages, name, &new);
	    if (!new) {
		/*
		 * Confirm current registered package is the same version.
		 */

		pkgPtr = Tcl_GetHashValue(hPtr);
		if (!STREQ(pkgPtr->version, version)) {
		    Tcl_AppendResult(interp, "version conflict for package \"",
			name, "\": have ", pkgPtr->version, ", need ",
			version, NULL);
		    pkgPtr = NULL;
		}
	    } else {
		/*
		 * Register new package.
		 */
	    	pkgPtr = ns_malloc(sizeof(Package) + strlen(version));
		strcpy(pkgPtr->version, version);
	    	pkgPtr->name = Tcl_GetHashKey(&servPtr->tcl.packages, hPtr);
	    	pkgPtr->exact = exact;
	    	Tcl_SetHashValue(hPtr, pkgPtr);
		Ns_TclRegisterTrace(servPtr->server, PkgRequire, pkgPtr,
				    NS_TCL_TRACE_ALLOCATE);
	    }
	    Ns_MutexUnlock(&servPtr->tcl.plock);
	    if (pkgPtr == NULL) {
		return TCL_ERROR;
	    }
	    Tcl_SetResult(interp, pkgPtr->version, TCL_STATIC);
	    break;
	}
	break;

    case IOnceIdx:
	if (objc < 4) {
            Tcl_WrongNumArgs(interp, 3, objv, "name script");
	    return TCL_ERROR;
	}
	name = Tcl_GetString(objv[2]);
	Ns_CsEnter(&servPtr->tcl.olock);
	hPtr = Tcl_CreateHashEntry(&servPtr->tcl.once, name, &new);
	if (new) {
	    result = Tcl_EvalObjEx(interp, objv[3], TCL_EVAL_DIRECT);
	    if (result != TCL_OK) {
		Tcl_DeleteHashEntry(hPtr);
	    }
	}
	Ns_CsLeave(&servPtr->tcl.olock);
	if (result != TCL_OK) {
	    return TCL_ERROR;
	}
	Tcl_SetBooleanObj(Tcl_GetObjResult(interp), new);
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
	    when = 0;
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
	script = Tcl_GetString(objv[objc-1]);
	length = strlen(script);
	stPtr = ns_malloc(sizeof(ScriptTrace) + length);
	stPtr->length = length;
	strcpy(stPtr->script,  script);
	(void) Ns_TclRegisterTrace(servPtr->server, EvalTrace, stPtr, when);
	break;

    case IGetTracesIdx:
    case IRunIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "which");
	    return TCL_ERROR;
        }
    	if (Tcl_GetIndexFromObj(interp, objv[2], topts, "traces", 0,
			    (int *) &topt) != TCL_OK) {
	    return TCL_ERROR;
    	}
	ForeachTrace(itPtr, twhen[topt], (opt == IGetTracesIdx));
	break;

    case IThreadsIdx:
        if (objc > 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
        }
	listPtr = Tcl_NewObj();
	Ns_MutexLock(&tlock);
	hPtr = Tcl_FirstHashEntry(&threads, &search);
	while (hPtr != NULL) {
	    tid = (int) Tcl_GetHashKey(&threads, hPtr);
	    objPtr = Tcl_NewIntObj(tid);
	    Tcl_ListObjAppendElement(interp, listPtr, objPtr);
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Ns_MutexUnlock(&tlock);
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
	Ns_MutexLock(&tlock);
	hPtr = Tcl_FindHashEntry(&threads, (char *) tid);
	if (hPtr != NULL) {
	    dataPtr = Tcl_GetHashValue(hPtr);
	    Tcl_AsyncMark(dataPtr->cancel);
	}
	Ns_MutexUnlock(&tlock);
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
    	RunTraces(itPtr, NS_TCL_TRACE_FREECONN);
    	itPtr->conn = NULL;
    	itPtr->nsconn.flags = 0;
    	connPtr->itPtr = NULL;
    	PushInterp(itPtr);
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
    int epoch;

    /*
     * Verify the server.  NULL (i.e., no server) is valid but
     * a non-null, unknown server is an error and get the current
     * epoch.
     */

    servPtr = NsGetServer(server);
    if (servPtr == NULL) {
	return NULL;
    }
    Ns_RWLockRdLock(&servPtr->tcl.slock);
    epoch = servPtr->tcl.epoch;
    Ns_RWLockUnlock(&servPtr->tcl.slock);

    /*
     * Dump any interps with an invalid epoch and then pop the first
     * available interp or create a new interp.
     */

    hPtr = GetCacheEntry(servPtr);
    if (epoch == 0) {
	/* NB: Epoch 0 indicates legacy module config disabled. */
	itPtr = Tcl_GetHashValue(hPtr);
    } else {
    	NsInterp *validPtr = NULL;
    	while ((itPtr = Tcl_GetHashValue(hPtr)) != NULL) {
	    Tcl_SetHashValue(hPtr, itPtr->nextPtr);
	    if (itPtr->epoch != epoch) {
	        Ns_TclDestroyInterp(itPtr->interp);
	    } else {
	        itPtr->nextPtr = validPtr;
	    	validPtr = itPtr;
	    }
	}
    	itPtr = validPtr;
    }
    if (itPtr != NULL) {
	Tcl_SetHashValue(hPtr, itPtr->nextPtr);
    } else {
    	if (nsconf.tcl.lockoninit) {
	    Ns_CsEnter(&lock);
    	}
	interp = CreateInterp(servPtr);
	itPtr = NsGetInterpData(interp);
    	RunTraces(itPtr, NS_TCL_TRACE_CREATE);
    	if (nsconf.tcl.lockoninit) {
	    Ns_CsLeave(&lock);
    	}
    }
    itPtr->nextPtr = NULL;
    interp = itPtr->interp;

    /*
     * Clear any pending async cancel message, run the traces and
     * set the epoch if a create and/or allocate traces hasn't
     * already done so.
     */

    (void) Tcl_AsyncInvoke(interp, TCL_OK);
    RunTraces(itPtr, NS_TCL_TRACE_ALLOCATE);
    if (itPtr->epoch != epoch) {
	itPtr->epoch = epoch;
    }
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

    RunTraces(itPtr, NS_TCL_TRACE_DEALLOCATE);
    if (itPtr->delete) {
	Ns_TclDestroyInterp(interp);
    } else {
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
 *      Create a new interp with the Nsd package.
 *
 * Results:
 *      Pointer to new Tcl_Interp.
 *
 * Side effects:
 *	Will log an error message on core Tcl and/or Nsd package init
 *	failure.
 *
 *----------------------------------------------------------------------
 */

static Tcl_Interp *
CreateInterp(NsServer *servPtr)
{
    Tcl_Interp *interp;

    interp = Tcl_CreateInterp();
    Tcl_InitMemory(interp);
    if (Tcl_Init(interp) != TCL_OK || InitData(interp, servPtr) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    return interp;
}


/*
 *----------------------------------------------------------------------
 *
 * InitData --
 *
 *      Initialize and provide the Nsd package for given interp,
 * 	associating it with a specific virtual server, if any.
 *
 * Results:
 *      Return code of Tcl_PkgProvide.
 *
 * Side effects:
 *	Will add Nsd package commands to given interp.
 *
 *----------------------------------------------------------------------
 */

static int
InitData(Tcl_Interp *interp, NsServer *servPtr)
{
    static volatile int initialized = 0;
    NsInterp *itPtr;

    /*
     * Core one-time AOLserver initialization to add a few Tcl_Obj
     * types.  These calls cannot be in NsTclInit above because
     * Tcl is not fully initialized at libnsd load time.
     */

    if (!initialized) {
    	Ns_LibInit();
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
    itPtr->servPtr = servPtr;
    Tcl_InitHashTable(&itPtr->sets, TCL_STRING_KEYS);
    Tcl_InitHashTable(&itPtr->chans, TCL_STRING_KEYS);	
    Tcl_InitHashTable(&itPtr->https, TCL_STRING_KEYS);	
    NsAdpInit(itPtr);
    itPtr->adp.cwd = Ns_PageRoot(servPtr->server);

    /*
     * Associate the new NsInterp with this interp.  At interp delete
     * time, Tcl will call FreeData to cleanup the struct.
     */

    Tcl_SetAssocData(interp, "ns:data", FreeData, itPtr);

    /*
     * Ensure the per-thread data with async cancel handle is allocated.
     */

    (void) GetData();

    /*
     * Add core AOLserver commands.
     */

    NsTclAddCmds(interp, itPtr);
    return Tcl_PkgProvide(interp, "Nsd", NS_VERSION);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeData --
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
FreeData(ClientData arg, Tcl_Interp *interp)
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
	Ns_MutexLock(&tlock);
	dataPtr->hPtr = Tcl_CreateHashEntry(&threads, (char *) tid, &new);
	Tcl_SetHashValue(dataPtr->hPtr, dataPtr);
	Ns_MutexUnlock(&tlock);
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

    Ns_MutexLock(&tlock);
    Tcl_DeleteHashEntry(dataPtr->hPtr);
    Ns_MutexUnlock(&tlock);
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
 * RunTraces --
 *
 *  	Execute script and C-level trace callbacks
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
RunTraces(NsInterp *itPtr, int why)
{
    ForeachTrace(itPtr, why, 0);
}

static void
ForeachTrace(NsInterp *itPtr, int why, int append)
{
    Tcl_Interp *interp = itPtr->interp;
    TclTrace *tracePtr;

    /*
     * Finalization traces are invoked in LIFO order with script-traces
     * before C-level traces.  Otherwise, traces are invoked in FIFO
     * order, with C-level traces before script-traces.
     */

    Tcl_ResetResult(interp);
    Ns_RWLockRdLock(&itPtr->servPtr->tcl.tlock);
    switch (why) {
    case NS_TCL_TRACE_FREECONN:
    case NS_TCL_TRACE_DEALLOCATE:
    case NS_TCL_TRACE_DELETE:
    	tracePtr = itPtr->servPtr->tcl.lastTracePtr;
	while (tracePtr != NULL) {
	    if (tracePtr->proc == EvalTrace && (tracePtr->when & why)) {
		DoTrace(interp, tracePtr, append);
	    }
	    tracePtr = tracePtr->prevPtr;
	}
    	tracePtr = itPtr->servPtr->tcl.lastTracePtr;
	while (tracePtr != NULL) {
	    if (tracePtr->proc != EvalTrace && (tracePtr->when & why)) {
		DoTrace(interp, tracePtr, append);
	    }
	    tracePtr = tracePtr->prevPtr;
	}
	break;

    default:
    	tracePtr = itPtr->servPtr->tcl.firstTracePtr;
    	while (tracePtr != NULL) {
	    if (tracePtr->proc != EvalTrace && (tracePtr->when & why)) {
		DoTrace(interp, tracePtr, append);
	    }
	    tracePtr = tracePtr->nextPtr;
	}
    	tracePtr = itPtr->servPtr->tcl.firstTracePtr;
    	while (tracePtr != NULL) {
	    if (tracePtr->proc == EvalTrace && (tracePtr->when & why)) {
		DoTrace(interp, tracePtr, append);
	    }
	    tracePtr = tracePtr->nextPtr;
	}
	break;
    }
    Ns_RWLockUnlock(&itPtr->servPtr->tcl.tlock);
    if (!append) {
	Tcl_ResetResult(interp);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DoTrace --
 *
 *  	Invoke or append a trace, logging any error message.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
static void
DoTrace(Tcl_Interp *interp, TclTrace *tracePtr, int append)
{
    Tcl_Obj *procPtr;
    ScriptTrace *stPtr;
    char buf[100];
    int result;

    if (!append) {
    	result = (*tracePtr->proc)(interp, tracePtr->arg);
    	if (result != TCL_OK) {
	    Ns_TclLogError(interp);
    	}
    } else {
    	if (tracePtr->proc == EvalTrace) {
	    stPtr = tracePtr->arg;
	    procPtr = Tcl_NewStringObj(stPtr->script, stPtr->length);
    	} else {
	    sprintf(buf, "C {p:%p a:%p}", tracePtr->proc, tracePtr->arg);
	    procPtr = Tcl_NewStringObj(buf, -1);
    	}
    	Tcl_ListObjAppendElement(NULL, Tcl_GetObjResult(interp), procPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PkgRequire --
 *
 *	Trace callback to add a registered package to given interp.
 *
 * Results:
 *	TCL_OK if package added, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Depends on package, typically new namespaces and/or commands.
 *
 *----------------------------------------------------------------------
 */

static int
PkgRequire(Tcl_Interp *interp, void *arg)
{
    Package *pkgPtr = arg;

    if (Tcl_PkgRequire(interp, pkgPtr->name, pkgPtr->version,
			 pkgPtr->exact) == NULL) {
	return TCL_ERROR;
    }
    return TCL_OK;
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

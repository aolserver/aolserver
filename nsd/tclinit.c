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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclinit.c,v 1.7 2000/10/20 21:53:07 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define CLEANUP_PROC 	"_ns_cleanupinterp"
#define GETCMDS_PROC 	"_ns_getcmds"
#define GETINIT_PROC 	"_ns_getinit"

static char getCmds[] = GETCMDS_PROC;
static char getInit[] = GETINIT_PROC;

static char createProcs[] = 
	"proc " CLEANUP_PROC " {{autoclose 0}} {\n"
	"    foreach g [info globals] {\n"
	"	if {![string match tcl* $g]\n"
	"		&& ![string match error* $g]\n"
	"		&& [string compare env $g] != 0} {\n"
	"	    upvar #0 $g gv\n"
	"           if [info exists gv] {\n"
	"               unset gv\n"
	"           }\n"
	"	}\n"
	"    }\n"
	"    if $autoclose {\n"
	"	foreach f [ns_getchannels] {\n"
	"	    if {![string match std* $f]} {\n"
	"		catch {close $f}\n"
	"	    }\n"
	"	}\n"
	"    }\n"
	"}\n"
	"proc " GETCMDS_PROC " {} {\n"
	"    set cmds {}\n"
	"    foreach p [info procs] {\n"
	"        set procs($p) 1\n"
	"    }\n"
	"    foreach c [info commands] {\n"
	"        if {![info exist procs($c)]} {\n"
	"            lappend cmds $c\n"
	"        }\n"
	"    }\n"
	"    return $cmds\n"
	"}\n"
	"proc " GETINIT_PROC " {} {\n"
	"    set script {}\n"
	"    foreach p [info procs] {\n"
	"        set args {}\n"
	"        foreach a [info args $p] {\n"
	"            if [info default $p $a def] {\n"
	"                set a [list $a $def]\n"
	"	     }\n"
	"	     lappend args $a\n"
	"        }\n"
	"        append script [list proc $p $args [info body $p]]\n"
	"        append script \\n\n"
	"    }\n"
	"    return $script\n"
	"}\n";
	
/*
 * The following structure is used to keep track of procedures
 * to call when interps, created, cleaned up, or destroyed for
 * a thread.
 */
 
typedef struct TclTrace {
    struct TclTrace *nextPtr;
    Ns_TclInterpInitProc *proc;
    void *arg;
} TclTrace;

/*
 * The following structure maintains callbacks to registered
 * to be executed at interp deallocate time.
 */
 
typedef struct Defer {
    struct Defer    *nextPtr;
    Ns_TclDeferProc *procPtr;
    void 	    *clientData;
} Defer;

/*
 * The following structure maintain the list of commands
 * and procedures for interp cloning.
 */
 
typedef struct InitData {
    TclCmdInfo  *firstCmdPtr;
    char	*procsInit;
    int		 refCnt;
} InitData;

/*
 * The following structure maintains a list of scripts
 * to evaluate in the next run of NsTclRunInits.
 */

typedef struct InitScript {
    struct InitScript *nextPtr;
    char script[1];
} InitScript;

/*
 * Local procedures defined in this file
 */
 
static void     CleanupTable(Tcl_HashTable *tablePtr,
			     Ns_Callback *cleanupProc);
static void	CleanupData(TclData *tdPtr);
static Ns_Callback FreeData;
static int CheckStarting(char *funcName);
static void CreateInterp(TclData *tdPtr);
static void DeleteInterp(TclData *tdPtr);
static int StrSort(const void *p1, const void *p2);
static int SourceDirs(Ns_DString *privatePtr, Ns_DString *sharedPtr);
static void SourceDirFile(Ns_DString *dsPtr, char *file);
static void SourceModules(void);
static void DecrData(InitData *iPtr);
static int RegisterTrace(TclTrace **firstPtrPtr, Ns_TclTraceProc *proc, void *arg);
static void RunTraces(TclData *tdPtr, TclTrace *firstPtr);
static int GetCmds(Tcl_Interp *interp, int *cargcPtr, char ***cargvPtr);
static int QueueInit(Tcl_Interp *interp, char *script);

/*
 * Static variables defined in this file
 */

static TclTrace   *firstAtCreatePtr;
static TclTrace   *firstAtCleanupPtr;
static TclTrace   *firstAtDeletePtr;
static Ns_DString  modlist;
static int         nmodules;
static int	   currentEpoch;
static InitScript *firstInitPtr;
static InitData   *currentInitPtr;
static Ns_Mutex    lock;
static Tcl_HashTable builtinCmds;

static int         initTid;
static Tcl_Interp *initInterp;



/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitInterps --
 *
 *	Call a user-supplied Tcl initialization procedure in the parent
 *  	interp.
 *
 *  	NOTE:  This routine only allows execution in the initial thread
 *  	during startup.
 *
 * Results:
 *	Tcl result from user's procedure.
 *
 * Side effects:
 *	Parent interp used as a template for all other per-thread interps
 *	is created.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitInterps(char *hServer, Ns_TclInterpInitProc *initProc, void *arg)
{
    Tcl_Interp *interp;
    int code;

    if (CheckStarting("Ns_TclInitInterps") == NS_FALSE) {
	return TCL_ERROR;
    }

    interp = Ns_TclAllocateInterp(NULL);
    code = ((*initProc) (interp, arg));
    Ns_TclDeAllocateInterp(interp);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclInitModule --
 *
 *	Source the Tcl script library files for a module during startup.
 *
 *  	NOTE:  This routine only allows execution in the initial thread
 *  	during startup.
 *
 * Results:
 *	Tcl result from user's procedure.
 *
 * Side effects:
 *	Parent interp used as a template for all other per-thread interps
 *	is created.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclInitModule(char *server, char *module)
{
    Ns_DStringAppendArg(&modlist, module);
    nmodules++;

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterAtCreate --
 *
 *  	Register a procedure to be called when an interp is first
 *  	created for a thread.
 *
 *  	NOTE:  This routine only allows execution in the initial thread
 *  	during startup.  Also, the procedure will NOT be run on the
 *  	parent interp - caller would have to call Ns_TclInitInterps()
 *  	as well.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Procedure will be called inside Ns_TclAllocateInterp().
 *
 *----------------------------------------------------------------------
 */
 
int
Ns_TclRegisterAtCreate(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(&firstAtCreatePtr, proc, arg);
}

int
Ns_TclRegisterAtCleanup(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(&firstAtCleanupPtr, proc, arg);
}

int
Ns_TclRegisterAtDelete(Ns_TclTraceProc *proc, void *arg)
{
    return RegisterTrace(&firstAtDeletePtr, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRegisterDeferred --
 *
 *	Register a procedure to be called when the interp is cleaned up.
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
Ns_TclRegisterDeferred(Tcl_Interp *interp, Ns_TclDeferProc *procPtr,
	void *clientData)
{
    Defer   *deferPtr;
    TclData *tdPtr;

    tdPtr = NsTclGetData(interp);
    deferPtr = ns_malloc(sizeof(Defer));
    deferPtr->procPtr = procPtr;
    deferPtr->clientData = clientData;
    deferPtr->nextPtr = tdPtr->firstDeferPtr;
    tdPtr->firstDeferPtr = deferPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclMarkForDelete --
 *
 *	Mark an interpeter for deletion when it is next deallocated. 
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
Ns_TclMarkForDelete(Tcl_Interp *interp)
{
    TclData *tdPtr;

    tdPtr = NsTclGetData(NULL);
    tdPtr->deleteInterp = 1;
}
    

/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDestroyInterp --
 *
 *	Immediately delete a Tcl interp and its associated data. 
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
Ns_TclDestroyInterp(Tcl_Interp *interp)
{
    Ns_TclMarkForDelete(interp);
    Ns_TclDeAllocateInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclAllocateInterp --
 *
 *	Allocate an interpreter, or if one is already associated with 
 *	this thread, return that one. 
 *
 * Results:
 *	This thread's Tcl interp. 
 *
 * Side effects:
 *	A new interp may be created from scratch, if needed. 
 *
 *----------------------------------------------------------------------
 */

Tcl_Interp *
Ns_TclAllocateInterp(char *ignored)
{
    TclData *tdPtr;

    tdPtr = NsTclGetData(NULL);
    if (tdPtr->interp != NULL && tdPtr->interp != initInterp) {
	Ns_MutexLock(&lock);
	if (tdPtr->lastEpoch != currentEpoch) {
	    tdPtr->deleteInterp = 1;
	}
	Ns_MutexUnlock(&lock);
	if (tdPtr->deleteInterp) {
	    DeleteInterp(tdPtr);
	}
    }
    if (tdPtr->interp == NULL) {
    	Ns_WaitForStartup();
	CreateInterp(tdPtr);
    }
    return tdPtr->interp;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDeAllocateInterp --
 *
 *	Free up an interpreter's AOLserver-specific data. 
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
Ns_TclDeAllocateInterp(Tcl_Interp *ignored)
{
    TclData *tdPtr;

    tdPtr = NsTclGetData(NULL);
    if (tdPtr->interp != NULL) {
	RunTraces(tdPtr, firstAtCleanupPtr);
    	CleanupData(tdPtr);
    }
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
Ns_TclLibrary(void)
{
    return nsconf.tcl.library;
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
Ns_TclInterpServer(Tcl_Interp *ignored)
{
    return nsServer;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetConn --
 *
 *	Get the Ns_Conn structure associated with this tcl interp 
 *	(actually implemented by tls, not interp). 
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
Ns_TclGetConn(Tcl_Interp *ignored)
{
    return Ns_GetConn();
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInit --
 *
 *	Get the directory for private tcl files, defaulting to 
 *	servers/servername/modules/tcl, then execute both private and 
 *	shared tcl files with private ones masking shared ones. Next,
 *	execute module tcl files in private/shared dirs.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Parent interp which is used as a template for all other
 *	per-thread interps is created and saved in a global variable.
 *
 *----------------------------------------------------------------------
 */

void
NsTclInit(void)
{
    TclData	   *tdPtr;
    int     	    cargc, new;
    char    	  **cargv;
    Tcl_Interp     *interp;

    /*
     * Initialize the Tcl core.
     */

    Ns_DStringInit(&modlist);
    Ns_MutexSetName2(&lock, "ns", "tclinterp");

    /*
     * Create the parent interp and the cleanup script.
     */

    Tcl_InitHashTable(&builtinCmds, TCL_STRING_KEYS);
    initTid = Ns_ThreadId();
    interp = Tcl_CreateInterp();
    if (NsTclEval(interp, createProcs) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    if (GetCmds(interp, &cargc, &cargv)) {
    	while (--cargc >= 0) {
	    Tcl_CreateHashEntry(&builtinCmds, cargv[cargc], &new);
	}
	ckfree((char *) cargv);
    }
    tdPtr = NsTclGetData(NULL);
    tdPtr->interp = initInterp = interp;
    NsTclCreateCmds(interp);
    NsTclStatsInit();
    Ns_TclDeAllocateInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * QueueInit --
 *
 *  	Evaluate a script and, if successful, queue it for the next
 *  	iteration of NsTclRunInits.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	This of commands in the parent interp is updated.
 *
 *----------------------------------------------------------------------
 */

static int
QueueInit(Tcl_Interp *interp, char *script)
{
    InitScript *initPtr, **nextPtrPtr;

    /*
     * Verify the script will execute.
     */
     	
    if (NsTclEval(interp, script) != TCL_OK) {
    	Ns_TclMarkForDelete(interp);
	Ns_TclLogError(interp);
	return TCL_ERROR;
    }

    /*
     * Queue the script and trigger the main thread if
     * necessary.
     */
     
    if (interp == initInterp) {
	initPtr = NULL;	 /* Update already done. */
    } else {
    	initPtr = ns_malloc(sizeof(InitScript) + strlen(script));
    	strcpy(initPtr->script, script);
    	initPtr->nextPtr = NULL;
    }

    Ns_MutexLock(&lock);
    if (firstInitPtr == NULL) {
	NsSendSignal(NS_SIGTCL);
    }
    if (initPtr != NULL) {
    	nextPtrPtr = &firstInitPtr;
    	while (*nextPtrPtr != NULL) {
	    nextPtrPtr = &((*nextPtrPtr)->nextPtr);
    	}
    	*nextPtrPtr = initPtr;
    }
    Ns_MutexUnlock(&lock);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRunInits --
 *
 *  	Run pending init scripts (if any) and regenerate the list of
 *  	commands for initializing new interps.
 *
 *  	NOTE:  This routine can only be called in the main thread
 *  	when signaled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	This of commands in the parent interp is updated.
 *
 *----------------------------------------------------------------------
 */

void
NsTclRunInits(void)
{
    InitScript *initPtr, *nextPtr;
    InitData *iPtr;
    TclCmdInfo    *cmdPtr;
    Tcl_Interp *interp;
    int     cargc;
    char  **cargv;

    Ns_MutexLock(&lock);
    initPtr = firstInitPtr;
    firstInitPtr = NULL;
    Ns_MutexUnlock(&lock);
    
    if (initPtr != NULL) {
	Ns_Log(Notice, "tclinit: re-initalizing tcl"); 
	while (initPtr != NULL) {
	    nextPtr = initPtr->nextPtr;
	    interp = Ns_TclAllocateInterp(NULL);
       	    if (NsTclEval(interp, initPtr->script) != TCL_OK) {
		Ns_TclLogError(interp);
	    }
	    Ns_TclDeAllocateInterp(interp);
	    ns_free(initPtr);
	    initPtr = nextPtr;
	}
    }

    /*
     * Create a linked list of command info to create new commands
     * for all non-builtin commands.
     */
     
    interp = Ns_TclAllocateInterp(NULL);
    iPtr = ns_calloc(1, sizeof(InitData));
    if (!GetCmds(interp, &cargc, &cargv)) {
	Ns_Fatal("tclinit: failed to get get list of tcl commands");
    }
    while (--cargc >= 0) {
    	if (Tcl_FindHashEntry(&builtinCmds, cargv[cargc]) == NULL) {
    	    cmdPtr = NsTclGetCmdInfo(interp, cargv[cargc]);
	    cmdPtr->nextPtr = iPtr->firstCmdPtr;
	    iPtr->firstCmdPtr = cmdPtr;
	}
    }
    ckfree((char *) cargv);

    /*
     * Evaluate the script to create one large script which will define
     * all the procedures in the new interp.
     */

    if (NsTclEval(interp, getInit) != TCL_OK) {
    	Ns_TclLogError(interp);
	Ns_Fatal("tclinit: failed to copy procs");
    }
    iPtr->procsInit = ns_strdup(interp->result);
    iPtr->refCnt = 1;
    Ns_TclDeAllocateInterp(interp);
    
    /*
     * Swap in the new init data.
     */
     
    Ns_MutexLock(&lock);
    if (currentInitPtr != NULL) {
    	DecrData(currentInitPtr);
    }
    currentInitPtr = iPtr;
    ++currentEpoch;
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitScripts --
 *
 *	Source initialization scripts; first shared and private 
 *	scripts, then those for all loaded modules. init.tcl files 
 *	are run first (possibly both the private and shared ones) and 
 *	then, alphabetically, the rest of the scripts, with private 
 *	versions overriding shared ones. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Scripts sourced. 
 *
 *----------------------------------------------------------------------
 */

void
NsTclInitScripts(void)
{
    Ns_DString priv, shared;

    Ns_DStringInit(&priv);
    Ns_DStringInit(&shared);
    Ns_DStringAppend(&priv, nsconf.tcl.library);
    Ns_DStringAppend(&shared, nsconf.tcl.sharedlibrary);
    SourceDirs(&priv, &shared);
    Ns_DStringFree(&priv);
    Ns_DStringFree(&shared);
    SourceModules();
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetData --
 *
 *	Get the data associate with a tcl interp, using TLS. 
 *
 * Results:
 *	A TclData pointer. 
 *
 * Side effects:
 *	A new TclData will be allocated if one did not exist. 
 *
 *----------------------------------------------------------------------
 */

TclData *
NsTclGetData(Tcl_Interp *ignored)
{
    TclData *tdPtr;
    static Ns_Tls tls;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, FreeData);
	}
	Ns_MasterUnlock();
    }
    tdPtr = (TclData *) Ns_TlsGet(&tls);
    if (tdPtr == NULL) {
    	tdPtr = ns_calloc(sizeof(TclData), 1);
    	Tcl_InitHashTable(&tdPtr->sets, TCL_STRING_KEYS);
    	Tcl_InitHashTable(&tdPtr->dbs, TCL_STRING_KEYS);
    	Ns_TlsSet(&tls, tdPtr);
    }
    return tdPtr;
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
NsTclMarkForDeleteCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		      char **argv)
{
    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    Ns_TclMarkForDelete(interp);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SourceModules --
 *
 *	Loop over all the modules in the global list and source the 
 *	tcl files in the private and shared directories (with usual 
 *	alphabetical, init.tcl, and precedence rules). 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Tcl files will be sourced 
 *
 *----------------------------------------------------------------------
 */

static void
SourceModules(void)
{
    Ns_DString  private, shared;
    char       *p;
    int         i;
    
    Ns_DStringInit(&private);
    Ns_DStringInit(&shared);
    p = modlist.string;
    for (i=0; i < nmodules; i++) {
	Ns_MakePath(&private, nsconf.tcl.library, p, NULL);
	Ns_HomePath(&shared, "modules", "tcl", p, NULL);
	SourceDirs(&private, &shared);
	Ns_DStringTrunc(&private, 0);
	Ns_DStringTrunc(&shared, 0);
	p += strlen(p)+1;
    }
    Ns_DStringFree(&shared);
    Ns_DStringFree(&private);
}


/*
 *----------------------------------------------------------------------
 *
 * SourceDirs --
 *
 *	Gets a list of *.tcl files in the private and shared 
 *	directory. Sort by name. Any files in private override those 
 *	in shared. First source init.tcl, then everything else. 
 *
 * Results:
 *	NS_OK 
 *
 * Side effects:
 *	Many files will be sourced 
 *
 *----------------------------------------------------------------------
 */

static int
SourceDirs(Ns_DString *privatePtr, Ns_DString *sharedPtr)
{
    DIR *pPtr,    *sPtr;
    struct dirent *entryPtr;
    Ns_DString     sf, pf, files, temp;
    int            sfiles, pfiles, i;
    char          *p, **filesArrayPtr;
    struct stat    statbuf;
    
    Ns_DStringInit(&files);
    Ns_DStringInit(&sf);
    Ns_DStringInit(&pf);
    Ns_DStringInit(&temp);

    /*
     * Create a list of shared files to source, excluding private
     * files by the same name.
     */
    
    sPtr = opendir(sharedPtr->string);
    sfiles = 0;
    if (sPtr != NULL) {
	while ((entryPtr = ns_readdir(sPtr)) != NULL) {
	    if (STREQ(entryPtr->d_name, "init.tcl")) {
		SourceDirFile(sharedPtr, "init.tcl");
	    } else if ((p = strstr(entryPtr->d_name, ".tcl")) != NULL &&
		p[4] == '\0') {
		/*
		 * We found a tcl file that exists in the shared
		 * directory. If there is no such file in the
		 * private directory, then add it to the list.
		 */
		Ns_DStringVarAppend(&temp, privatePtr->string, "/",
				    entryPtr->d_name, NULL);
		if (!((stat(temp.string, &statbuf) == 0) &&
		      S_ISREG(statbuf.st_mode))) {
		    /*
		     * There is no private file by the same name, so
		     * it is safe to append this file to the list.
		     */
		    Ns_DStringAppendArg(&sf, entryPtr->d_name);
		    ++sfiles;
		}
		Ns_DStringTrunc(&temp, 0);
	    }
	}
	closedir(sPtr);
    }

    /*
     * Grab all files in the private directory. If there's an init.tcl
     * source it right away.
     */
    
    pPtr = opendir(privatePtr->string);
    pfiles = 0;
    if (pPtr != NULL) {
	while ((entryPtr = ns_readdir(pPtr)) != NULL) {
	    if (STREQ(entryPtr->d_name, "init.tcl")) {
		SourceDirFile(privatePtr, "init.tcl");
	    } else if ((p = strstr(entryPtr->d_name, ".tcl")) != NULL &&
		p[4] == '\0') {
		/*
		 * We found a tcl file that exists in the private
		 * directory. Always append it to the list.
		 */

		Ns_DStringAppendArg(&pf, entryPtr->d_name);
		++pfiles;
	    }
	}
	closedir(pPtr);
    }


    /*
     * Source shared files
     */
    
    if (sfiles > 0) {
	filesArrayPtr = (char **) ns_malloc(sizeof(char *) * sfiles);
	p = sf.string;
	for (i=0; i < sfiles; i++) {
	    filesArrayPtr[i] = p;
	    p += strlen(p) + 1;
	}
	qsort(filesArrayPtr, sfiles, sizeof(char *), StrSort);
	for (i=0; i < sfiles; i++) {
	    SourceDirFile(sharedPtr, filesArrayPtr[i]);
	}
	ns_free(filesArrayPtr);
    }
    
    /*
     * Source private files
     */
    
    if (pfiles > 0) {
	filesArrayPtr = (char **) ns_malloc(sizeof(char *) * pfiles);
	p = pf.string;
	for (i=0; i < pfiles; i++) {
	    filesArrayPtr[i] = p;
	    p += strlen(p) + 1;
	}
	qsort(filesArrayPtr, pfiles, sizeof(char *), StrSort);
	for (i=0; i < pfiles; i++) {
	    SourceDirFile(privatePtr, filesArrayPtr[i]);
	}
	ns_free(filesArrayPtr);
    }

    Ns_DStringInit(&temp);
    Ns_DStringInit(&pf);
    Ns_DStringInit(&sf);
    Ns_DStringInit(&files);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CheckStarting --
 *
 *	Log an error if called after startup or from a child thread; 
 *	returns 1 if all is ok or 0 if not. 
 *
 * Results:
 *	1 if command is allowed, 0 if not. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CheckStarting(char *funcName)
{
    if (Ns_ThreadId() != initTid) {
    	Ns_Log(Error, "tclinit: cannot call NsTcl%s in a child thread", funcName);
    	return NS_FALSE;
    }
    if (Ns_InfoServersStarted()) {
	Ns_Log(Error, "tclinit: cannot call NsTcl%s after startup", funcName);
	return NS_FALSE;
    }

    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * StrSort --
 *
 *	Comparison function for qsort. 
 *
 * Results:
 *	see strcmp(*p1, *p2) 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
StrSort(const void *p1, const void *p2)
{
    char *s1 = *((char **) p1);
    char *s2 = *((char **) p2);
    return strcmp(s1, s2);
}


/*
 *----------------------------------------------------------------------
 *
 * SourceDirFile --
 *
 *	Source a Tcl file in a library directory during startup. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will add temporary data to dsPtr and then remove it. 
 *
 *----------------------------------------------------------------------
 */

static void
SourceDirFile(Ns_DString *dsPtr, char *file)
{
    Tcl_Interp *interp;	
    struct stat st;
    int len;

    len = dsPtr->length;
    file = Ns_DStringVarAppend(dsPtr, "/", file, NULL);
    if (stat(file, &st) != 0) {
	Ns_Log(Warning, "tclinit: failed to access '%s': '%s'",
	       file, strerror(errno));
    } else if (!S_ISREG(st.st_mode)) {
	Ns_Log(Warning, "tclinit: failed to access '%s': not an ordinary file",
	       file);
    } else if (access(file, R_OK) != 0) {
	Ns_Log(Warning, "tclinit: failed to access '%s': '%s'",
	       file, strerror(errno));
    } else {
    	interp = Ns_TclAllocateInterp(NULL);
	if (!nsConfQuiet) Ns_Log(Notice, "tclinit: sourcing '%s'", file);
	if (Tcl_EvalFile(interp, file) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
    	Ns_TclDeAllocateInterp(interp);
    }
    Ns_DStringTrunc(dsPtr, len);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeData --
 *
 *	Will free a TclData structure and its associated data. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will delete the interp if one is pointed to by the TclData.
 *
 *----------------------------------------------------------------------
 */

static void
FreeData(void *arg)
{
    TclData *tdPtr = (TclData *) arg;

    if (tdPtr->interp != NULL) {
	CleanupData(tdPtr);
    	DeleteInterp(tdPtr);
    }
    Tcl_DeleteHashTable(&tdPtr->sets);
    Tcl_DeleteHashTable(&tdPtr->dbs);
    ns_free(tdPtr);
}


static void
DeleteInterp(TclData *tdPtr)
{
    Tcl_DeleteInterp(tdPtr->interp);
    tdPtr->deleteInterp = 0;
    tdPtr->interp = NULL;
}



/*
 *----------------------------------------------------------------------
 *
 * CleanupData --
 *
 *	Run deferred procedures in reverse order, then cleanup all 
 *	the data associated with a TclData structure. Delete the 
 *	interp if it was marked destroyed. 
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
CleanupData(TclData *tdPtr)
{
    Defer       *deferPtr, *nextDeferPtr;
    Tcl_Interp  *interp = tdPtr->interp;
    Tcl_DString  script;

    Tcl_ResetResult(interp);

    /*
     * Run any deferred procedures in reverse order.
     * First reverse the linked list.
     */

    deferPtr = tdPtr->firstDeferPtr;
    tdPtr->firstDeferPtr = NULL;
    while (deferPtr != NULL) {
	nextDeferPtr = deferPtr->nextPtr;
	deferPtr->nextPtr = tdPtr->firstDeferPtr;
	tdPtr->firstDeferPtr = deferPtr;
	deferPtr = nextDeferPtr;
    }

    /*
     * Now walk the linked list and run all the deferred procs.
     */
    
    deferPtr = tdPtr->firstDeferPtr;
    tdPtr->firstDeferPtr = NULL;
    while (deferPtr != NULL) {
	nextDeferPtr = deferPtr->nextPtr;
        (*(deferPtr->procPtr)) (interp, deferPtr->clientData);
	ns_free(deferPtr);
	deferPtr = nextDeferPtr;
    }

    /*
     * Dump any AtClose callbacks never run.
     */

    if (tdPtr->firstAtClosePtr != NULL) {
    	NsTclFreeAtClose(tdPtr->firstAtClosePtr);
    	tdPtr->firstAtClosePtr = NULL;
    }

    /*
     * Evaluate the cleanup script.
     */

    Tcl_DStringInit(&script);
    Tcl_DStringAppendElement(&script, CLEANUP_PROC);
    Tcl_DStringAppendElement(&script, nsconf.tcl.autoclose ? "1" : "0");
    if (NsTclEval(interp, script.string) != TCL_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_DStringFree(&script);

    /*
     * Cleanup the rest of the interp data.
     */

    CleanupTable(&tdPtr->sets, NsCleanupTclSet);
    CleanupTable(&tdPtr->dbs, NsCleanupTclDb);
    tdPtr->setNum = 0;
    tdPtr->dbNum = 0;

    /*
     * Delete the interp if it was marked destroyed.
     */

    if (!tdPtr->deleteInterp) {
    	Tcl_ResetResult(interp);
    } else {
	DeleteInterp(tdPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CleanupTable --
 *
 *	Delete all the hash entries in a hash table. 
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
CleanupTable(Tcl_HashTable *tablePtr, Ns_Callback *cleanupProc)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
        (*cleanupProc) (Tcl_GetHashValue(hPtr));
	Tcl_DeleteHashEntry(hPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CreateInterp --
 *
 *	Create a new Tcl interp for this thread, initializing it with
 *  	the current list of commands.
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
CreateInterp(TclData *tdPtr)
{
    Tcl_Interp *interp;
    InitData *iPtr;
    TclCmdInfo *cmdPtr;
    char *init;
    static int initialized;

    interp = Tcl_CreateInterp();
    tdPtr->interp = interp;

    /*
     * Aquire the current InitData, triggering update
     * if necessary.
     */
         
    Ns_MutexLock(&lock);
    iPtr = currentInitPtr;
    ++iPtr->refCnt;
    tdPtr->lastEpoch = currentEpoch;
    Ns_MutexUnlock(&lock);
    
    /*
     * Create a command in interp for each entry in the linked list
     * of command info structures.
     */

    cmdPtr = iPtr->firstCmdPtr;
    while (cmdPtr != NULL) {
    	NsTclCreateCommand(interp, cmdPtr);
	cmdPtr = cmdPtr->nextPtr;
    }

    /*
     * Evaluate the script to create all procs.
     */

    init = ns_strdup(iPtr->procsInit);
    if (NsTclEval(interp, init) != TCL_OK) {
	Ns_TclLogError(interp);
	Ns_Fatal("tclinit: failed to create procs");
    }
    ns_free(init);
    Tcl_ResetResult(interp);

    /*
     * Release the InitData.
     */

    Ns_MutexLock(&lock);
    DecrData(iPtr);
    Ns_MutexUnlock(&lock);

    /*
     * Execute any AtCreate callbacks.
     */

    if (firstAtCreatePtr != NULL) {
    	RunTraces(tdPtr, firstAtCreatePtr);
    	CleanupData(tdPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DecrData --
 *
 *  	Decrement the initialization data refcnt, freeing it if no
 *  	longer used.
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
DecrData(InitData *iPtr)
{
    TclCmdInfo *cmdPtr;
    
    if (--iPtr->refCnt == 0) {
    	while (iPtr->firstCmdPtr != NULL) {
	    cmdPtr = iPtr->firstCmdPtr;
	    iPtr->firstCmdPtr = cmdPtr->nextPtr;
	    ns_free(cmdPtr);
	}
	ns_free(iPtr->procsInit);
	ns_free(iPtr);
    }
}

static int
RegisterTrace(TclTrace **firstPtrPtr, Ns_TclTraceProc *proc, void *arg)
{
    TclTrace *tPtr;

    if (CheckStarting("Ns_TclRegisterAt") == NS_FALSE) {
	return NS_ERROR;
    }
    tPtr = ns_malloc(sizeof(TclTrace));
    tPtr->proc = proc;
    tPtr->arg = arg;
    tPtr->nextPtr = NULL;
    while (*firstPtrPtr != NULL) {
	firstPtrPtr = &((*firstPtrPtr)->nextPtr);
    }
    *firstPtrPtr = tPtr;
    return NS_OK;
}

static void
RunTraces(TclData *tdPtr, TclTrace *firstPtr)
{
    while (firstPtr != NULL) {
	if ((*firstPtr->proc)(tdPtr->interp, firstPtr->arg) != TCL_OK) {
	    Ns_TclLogError(tdPtr->interp);
	}
	firstPtr = firstPtr->nextPtr;
    }
}

static int
GetCmds(Tcl_Interp *interp, int *cargcPtr, char ***cargvPtr)
{
    if (NsTclEval(interp, getCmds) != TCL_OK ||
    	Tcl_SplitList(interp, interp->result, cargcPtr, cargvPtr) != TCL_OK) {
	Ns_TclLogError(interp);
	return 0;
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclEvalCmd --
 *
 *	Implements ns_eval.
 *
 * Results:
 *	The tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclEvalCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *script;
    int   retcode;

    if (!nsconf.tcl.nseval) {
	Tcl_SetResult(interp, "ns_eval not enabled", TCL_STATIC);
	return TCL_ERROR;
    }
    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " arg ?arg? ?arg?", NULL);
        return TCL_ERROR;
    }
    if (argc == 2) {
        script = argv[1];
    } else {
        script = Tcl_Concat(argc - 1, argv + 1);
    }
    retcode = QueueInit(interp, script);
    if (script != argv[1]) {
        ckfree(script);
    }
    return retcode;
}

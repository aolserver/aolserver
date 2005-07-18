/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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
 * callbacks.c --
 *
 *	Support for Callbacks
 *
 * 	These functions allow the registration of callbacks
 *	that are run at various points during the server's execution.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/callbacks.c,v 1.7 2005/07/18 23:32:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * This structure is used as nodes in a linked list of callbacks.
 */

typedef struct Callback {
    struct Callback *nextPtr;
    Ns_Callback     *proc;
    void            *arg;
} Callback;

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc RunThread;
static void     RunCallbacks(Callback *firstPtr);
static void 	RunStart(Callback **firstPtrPtr, Ns_Thread *threadPtr);
static void 	RunWait(Callback **firstPtrPtr, Ns_Thread *threadPtr, Ns_Time *toPtr);
static void    *RegisterCallback(Callback **firstPtrPtr, Ns_Callback *proc, void *arg);

/*
 * Static variables defined in this file
 */

static Callback *firstPreStartup;
static Callback *firstStartup;
static Callback *firstSignal;
static Callback *firstServerShutdown;
static Callback *firstShutdown;
static Callback *firstExit;
static Callback *firstReady;
static Ns_Mutex  lock;
static Ns_Cond   cond;
static int shutdownPending;
static Ns_Thread serverShutdownThread;

void *
Ns_RegisterAtReady(Ns_Callback *proc, void *arg)
{
    return RegisterCallback(&firstReady, proc, arg);
}

void
NsRunAtReadyProcs(void)
{
    RunCallbacks(firstReady);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtStartup --
 *
 *	Register a callback to run at server startup 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	The callback will be registered 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtStartup(Ns_Callback *proc, void *arg)
{
    return RegisterCallback(&firstStartup, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtPreStartup --
 *
 *	Register a callback to run at pre-server startup 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	The callback will be registered 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtPreStartup(Ns_Callback *proc, void *arg)
{
    return RegisterCallback(&firstPreStartup, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtSignal --
 *
 *	Register a callback to run when a signal arrives 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	The callback will be registered
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtSignal(Ns_Callback * proc, void *arg)
{
    return RegisterCallback(&firstSignal, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtServerShutdown --
 *
 *	Register a callback to run at server shutdown. This is
 *	identical to Ns_RegisterShutdown and only exists for
 *	historical reasons.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	The callback will be registered 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtServerShutdown(Ns_Callback *proc, void *arg)
{
    return RegisterCallback(&firstServerShutdown, proc, arg);
}

void *
Ns_RegisterServerShutdown(char *ignored, Ns_Callback *proc, void *arg)
{
    return Ns_RegisterAtServerShutdown(proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtShutdown --
 *
 *	Register a callback to run at server shutdown. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	The callback will be registered. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtShutdown(Ns_Callback *proc, void *arg)
{
    return RegisterCallback(&firstShutdown, proc, arg);
}

void *
Ns_RegisterShutdown(Ns_Callback *proc, void *arg)
{
    return Ns_RegisterAtShutdown(proc, arg);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterAtExit --
 *
 *	Register a callback to be run at server exit. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	The callback will be registerd. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_RegisterAtExit(Ns_Callback * proc, void *arg)
{
    return RegisterCallback(&firstExit, proc, arg);
}


/*
 *----------------------------------------------------------------------
 *
 * NsRunStartupProcs --
 *
 *	Run any callbacks registered for server startup. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Callbacks called back. 
 *
 *----------------------------------------------------------------------
 */

void
NsRunStartupProcs(void)
{
    RunCallbacks(firstStartup);
}


/*
 *----------------------------------------------------------------------
 *
 * NsRunPreStartupProcs --
 *
 *	Run any callbacks registered for pre-server startup. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Callbacks called back. 
 *
 *----------------------------------------------------------------------
 */

void
NsRunPreStartupProcs(void)
{
    RunCallbacks(firstPreStartup);
}


/*
 *----------------------------------------------------------------------
 *
 * NsRunSignalProcs --
 *
 *	Run any callbacks registered for when a signal arrives 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Callbacks called back. 
 *
 *----------------------------------------------------------------------
 */

void
NsRunSignalProcs(void)
{
    RunCallbacks(firstSignal);
}


/*
 *----------------------------------------------------------------------
 *
 * NsRunExitProcs --
 *
 *	Run any callbacks registered for server startup, then 
 *	shutdown, then exit. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Callbacks called back. 
 *
 *----------------------------------------------------------------------
 */

void
NsStartShutdownProcs(void)
{
    Ns_MutexLock(&lock);
    shutdownPending = 1;
    Ns_MutexUnlock(&lock);
    RunStart(&firstServerShutdown, &serverShutdownThread);
}
    
void
NsWaitShutdownProcs(Ns_Time *toPtr)
{
    Ns_Thread thread;

    RunWait(&firstServerShutdown, &serverShutdownThread, toPtr);
    RunStart(&firstShutdown, &thread);
    RunWait(&firstShutdown, &thread, toPtr);
}

void
NsRunAtExitProcs(void)
{
    RunCallbacks(firstExit);
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterCallback --
 *
 *	A generic function that registers callbacks for any event 
 *
 * Results:
 *	A pointer to the newly-allocated Callback structure 
 *
 * Side effects:
 *	A Callback struct will be alloacated and put in the linked list. 
 *
 *----------------------------------------------------------------------
 */

static void *
RegisterCallback(Callback **firstPtrPtr, Ns_Callback *proc, void *arg)
{
    Callback       *cbPtr;
    static int first = 1;

    cbPtr = ns_malloc(sizeof(Callback));
    cbPtr->proc = proc;
    cbPtr->arg = arg;
    Ns_MutexLock(&lock);
    if (first) {
	Ns_MutexSetName(&lock, "ns:callbacks");
	first = 0;
    }
    if (shutdownPending) {
    	ns_free(cbPtr);
	cbPtr = NULL;
    } else {
	cbPtr->nextPtr = *firstPtrPtr;
	*firstPtrPtr = cbPtr;
    }
    Ns_MutexUnlock(&lock);
    return (void *) cbPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * RunCallbacks --
 *
 *	Run all callbacks in the passed-in linked list 
 *
 * Results:
 *	None 
 *
 * Side effects:
 *	Callbacks called back. 
 *
 *----------------------------------------------------------------------
 */

static void
RunCallbacks(Callback *cbPtr)
{
    while (cbPtr != NULL) {
        (*cbPtr->proc) (cbPtr->arg);
        cbPtr = cbPtr->nextPtr;
    }
}

static void
RunStart(Callback **firstPtrPtr, Ns_Thread *threadPtr)
{
    Ns_MutexLock(&lock);
    if (*firstPtrPtr != NULL) {
	Ns_ThreadCreate(RunThread, firstPtrPtr, 0, threadPtr);
    } else {
    	*threadPtr = NULL;
    }
    Ns_MutexUnlock(&lock);
}


static void
RunWait(Callback **firstPtrPtr, Ns_Thread *threadPtr, Ns_Time *toPtr)
{
    int status;

    status = NS_OK;
    Ns_MutexLock(&lock);
    while (status == NS_OK && *firstPtrPtr != NULL) {
	status = Ns_CondTimedWait(&cond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "callbacks: timeout waiting for shutdown procs");
    } else if (*threadPtr != NULL) {
	Ns_ThreadJoin(threadPtr, NULL);
    }
}


static void
RunThread(void *arg)
{
    Callback **firstPtrPtr = arg;
    Callback *firstPtr;

    Ns_ThreadSetName("-shutdown-");
    Ns_MutexLock(&lock);
    firstPtr = *firstPtrPtr;
    Ns_MutexUnlock(&lock);
    
    RunCallbacks(firstPtr);

    Ns_MutexLock(&lock);
    while (*firstPtrPtr != NULL) {
	firstPtr = *firstPtrPtr;
	*firstPtrPtr = firstPtr->nextPtr;
	ns_free(firstPtr);
    }
    Ns_CondSignal(&cond);
    Ns_MutexUnlock(&lock);
}


static void
AppendList(Tcl_DString *dsPtr, char *list, Callback *firstPtr)
{
    Callback *cbPtr;

    cbPtr = firstPtr;
    while (cbPtr != NULL) {
	Tcl_DStringStartSublist(dsPtr);
	Tcl_DStringAppendElement(dsPtr, list);
	Ns_GetProcInfo(dsPtr, (void *) cbPtr->proc, cbPtr->arg);
	Tcl_DStringEndSublist(dsPtr);
	cbPtr = cbPtr->nextPtr;
    }
}


void
NsGetCallbacks(Tcl_DString *dsPtr)
{
    Ns_MutexLock(&lock);
    AppendList(dsPtr, "prestartup", firstPreStartup);
    AppendList(dsPtr, "startup", firstStartup);
    AppendList(dsPtr, "signal", firstSignal);
    AppendList(dsPtr, "servershutdown", firstServerShutdown);
    AppendList(dsPtr, "shutdown", firstShutdown);
    AppendList(dsPtr, "exit", firstExit);
    Ns_MutexUnlock(&lock);
}

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
 * thread.c --
 *
 *	Routines for creating, exiting, and joining threads.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/thread.c,v 1.2 2002/06/11 01:36:41 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"
#include <sched.h>		/* sched_yield() */

/*
 * The following constants define the default and minimum stack
 * sizes for new threads.
 */

#define STACK_DEFAULT	65536	/* 64k */
#define STACK_MIN	16384	/* 16k */

/*
 * The following structure maintains all state for a thread
 * including thread local storage slots.
 */

typedef struct Thread {
    struct Thread  *nextPtr;	/* Next in list of all threads. */
    time_t	    ctime;	/* Thread structure create time. */
    int		    flags;	/* Detached, joined, etc. */
    Ns_ThreadProc  *proc;	/* Thread startup routine. */ 
    void           *arg;	/* Argument to startup proc. */
    int		    tid;        /* Id set by thread for logging. */
    char	    name[NS_THREAD_NAMESIZE+1]; /* Thread name. */
    char	    parent[NS_THREAD_NAMESIZE+1]; /* Parent name. */
}               Thread;

static void *ThreadMain(void *arg);
static Thread *NewThread(void);
static Thread *GetThread(void);
static void SetThread(Thread *thrPtr);
static void CleanupThread(void *arg);

/*
 * The following pointer maintains a linked list of all threads.
 */

static Thread *firstThreadPtr;

/*
 * The following maintains the tls key for the thread context.
 */

static Ns_Tls key;
static long stacksize = STACK_DEFAULT;


/*
 *----------------------------------------------------------------------
 *
 * NsthreadsInit --
 *
 *	Initialize threads interface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates pthread_key_t for thread context.
 *
 *----------------------------------------------------------------------
 */

void
NsthreadsInit(void)
{
    static int once = 0;

    if (!once) {
	once = 1;
    	NsInitMaster();
    	NsInitReentrant();
    	Ns_TlsAlloc(&key, CleanupThread);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadCreate --
 *
 *	Create a new thread thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A new thread is allocated and started.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadCreate(Ns_ThreadProc *proc, void *arg, long stack,
    	    	Ns_Thread *resultPtr)
{
    static char *func = "Ns_ThreadCreate";
    pthread_attr_t attr;
    pthread_t pid;
    Thread *thrPtr;
    int err;

    Ns_MasterLock();

    /*
     * Determine the stack size and impose a 16k minimum.
     */

    if (stack <= 0) {
	stack = stacksize;
    }
    if (stack < STACK_MIN) {
	stack = STACK_MIN;
    }

    /*
     * Allocate a new thread structure and update values
     * which are known for threads created here.
     */

    thrPtr = NewThread();
    thrPtr->proc = proc;
    thrPtr->arg = arg;
    if (resultPtr == NULL) {
    	thrPtr->flags = NS_THREAD_DETACHED;
    }
    strcpy(thrPtr->parent, Ns_ThreadGetName());
    Ns_MasterUnlock();

    /*
     * Set the stack size.  It could be smarter to leave the default 
     * on platforms which map large stacks with guard zones
     * (e.g., Solaris and Linux).
     */

    err = pthread_attr_init(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_init", err);
    }
    err = pthread_attr_setstacksize(&attr, stack); 
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_setstacksize", err);
    }

    /*
     * System scope threads are used by default on Solaris and UnixWare.
     * Other platforms are either already system scope (Linux, HP11), 
     * support only process scope (SGI), user-level thread libraries
     * anyway (FreeBSD), or found to be unstable with this setting (OSF).
     */

#ifdef USE_PTHREAD_SYSSCOPE
    err = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_setscope",err);
    }
#endif
    err = pthread_create(&pid, &attr, ThreadMain, thrPtr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_create", err);
    }
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_destroy", err);
    }
    if (resultPtr != NULL) {
	*resultPtr = (Ns_Thread) pid;
    } else {
    	err = pthread_detach(pid);
        if (err != 0) {
            NsThreadFatal(func, "pthread_detach", err);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadExit --
 *
 *	Exit the thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Exit arg will be passed through pthread_exit.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadExit(void *arg)
{
    pthread_exit(arg);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadJoin --
 *
 *	Wait for exit of a non-detached thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Requested thread is destroyed after join.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadJoin(Ns_Thread *threadPtr, void **argPtr)
{
    pthread_t pid = (pthread_t) *threadPtr;
    int err;

    err = pthread_join(pid, argPtr);
    if (err != 0) {
	NsThreadFatal("Ns_ThreadJoin", "pthread_join", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadStackSize --
 *
 *	Set default stack size.
 *
 * Results:
 *	Previous stack size.
 *
 * Side effects:
 *	New threads will use default size.
 *
 *----------------------------------------------------------------------
 */

long
Ns_ThreadStackSize(long size)
{
    long prev;

    Ns_MasterLock();
    prev = stacksize;
    if (size > 0) {
	stacksize = size;
    }
    Ns_MasterUnlock();
    return prev;
}


/*
 *----------------------------------------------------------------------
 *
 * ThreadMain --
 *
 *	Thread startup routine.  Sets the given pre-allocated thread
 *	structure and calls the user specified procedure.
 *
 * Results:
 *	None.  Will call Ns_ThreadExit if not called by the
 *	user code.
 *
 * Side effects:
 *	See SetThread.
 *
 *----------------------------------------------------------------------
 */

static void *
ThreadMain(void *arg)
{
    Thread      *thrPtr = (Thread *) arg;
    char	 name[NS_THREAD_NAMESIZE];

    SetThread(thrPtr);
    sprintf(name, "-thread%d-", thrPtr->tid);
    Ns_ThreadSetName(name);
    (*thrPtr->proc) (thrPtr->arg);
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadId --
 *
 *	Return the numeric thread id for calling thread.
 *
 * Results:
 *	Integer thread id.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ThreadId(void)
{
    return (int) pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadSelf --
 *
 *	Return opaque handle to thread's data structure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Value at thrPtr is updated with thread's data structure pointer.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadSelf(Ns_Thread *threadPtr)
{
    *threadPtr = (Ns_Thread) pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadGetName --
 *
 *	Return a pointer to calling thread's string name.
 *
 * Results:
 *	Pointer to thread name string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ThreadGetName(void)
{
    Thread *thisPtr = GetThread();

    return thisPtr->name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadSetName --
 *
 *	Set the name of the calling thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	String is copied to thread data structure.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadSetName(char *name)
{
    Thread *thisPtr = GetThread();

    Ns_MasterLock();
    strncpy(thisPtr->name, name, NS_THREAD_NAMESIZE);
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadGetParent --
 *
 *	Return a pointer to calling thread's parent name.
 *
 * Results:
 *	Pointer to thread parent name string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ThreadGetParent(void)
{
    Thread *thisPtr = GetThread();

    return thisPtr->parent;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadEnum --
 *
 *	Append info for each thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadEnum(Tcl_DString *dsPtr, Ns_ThreadEnumProc *proc)
{
    Thread *thrPtr;
    char buf[100];

    Ns_MasterLock();
    thrPtr = firstThreadPtr;
    while (thrPtr != NULL) {
	Tcl_DStringStartSublist(dsPtr);
	Tcl_DStringAppendElement(dsPtr, thrPtr->name);
	Tcl_DStringAppendElement(dsPtr, thrPtr->parent);
	sprintf(buf, " %d %d %ld", thrPtr->tid, thrPtr->flags, thrPtr->ctime);
	Tcl_DStringAppend(dsPtr, buf, -1);
	(*proc)(dsPtr, (void *) thrPtr->proc, thrPtr->arg);
	Tcl_DStringEndSublist(dsPtr);
	thrPtr = thrPtr->nextPtr;
    }
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * NewThread --
 *
 *	Allocate a new thread data structure and add it to the list
 *	of all threads.  The new thread is suitable for a detached,
 *	unknown thread such as the initial thread but Ns_ThreadCreate
 *	will update as necessary before creating the new threads.
 *
 * Results:
 *	Pointer to new Thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Thread *
NewThread(void)
{
    Thread *thrPtr;

    thrPtr = ns_calloc(1, sizeof(Thread));
    thrPtr->ctime = time(NULL);
    Ns_MasterLock();
    thrPtr->nextPtr = firstThreadPtr;
    firstThreadPtr = thrPtr;
    Ns_MasterUnlock();
    return thrPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SetThread --
 *
 *	Pthread routine to set this thread's nsthread data structure
 *	and update the tid.
 *
 * Results:
 *	None
 *
 * Side effects:
 *	Key is allocated the first time.
 *
 *----------------------------------------------------------------------
 */

static void
SetThread(Thread *thrPtr)
{
    thrPtr->tid = (int) pthread_self();
    Ns_TlsSet(&key, thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetThread --
 *
 *	Return this thread's nsthread data structure, initializing
 *	it if necessary, normally for the first thread but also
 *	for threads created without Ns_ThreadCreate.
 *
 * Results:
 *	Pointer to per-thread data structure.
 *
 * Side effects:
 *	Key is allocated the first time.
 *
 *----------------------------------------------------------------------
 */

static Thread      *
GetThread(void)
{
    Thread *thisPtr;

    thisPtr = Ns_TlsGet(&key);
    if (thisPtr == NULL) {
	thisPtr = NewThread();
    	thisPtr->flags = NS_THREAD_DETACHED;
	SetThread(thisPtr);
    }
    return thisPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *	Yield the cpu to another thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See sched_yield().
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadYield(void)
{
    sched_yield();
}


/*
 *----------------------------------------------------------------------
 *
 * CleanupThread --
 *
 *	TLS cleanup for the nsthread context. 
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
CleanupThread(void *arg)
{
    Thread **thrPtrPtr;
    Thread *thrPtr = arg;

    Ns_MasterLock();
    thrPtrPtr = &firstThreadPtr;
    while (*thrPtrPtr != thrPtr) {
	thrPtrPtr = &(*thrPtrPtr)->nextPtr;
    }
    *thrPtrPtr = thrPtr->nextPtr;
    thrPtr->nextPtr = NULL;
    Ns_MasterUnlock();
    ns_free(thrPtr);
}

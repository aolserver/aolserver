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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/thread.c,v 1.11 2005/08/08 11:30:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * Private flags for managing threads.
 */

#define FLAG_DETACHED	1
#define FLAG_HAVESTACK	2
#define FLAG_STACKDOWN	4

/*
 * The following structure is used as the startup arg for new threads.
 */

typedef struct ThreadArg {
    Ns_ThreadProc  *proc;	/* Thread startup routine. */ 
    void           *arg;	/* Argument to startup proc. */
    int		    flags;	/* Thread is detached. */
    char	    parent[NS_THREAD_NAMESIZE];
} ThreadArg;

/*
 * The following structure maintains all state for a thread
 * including thread local storage slots.
 */

typedef struct Thread {
    struct Thread  *nextPtr;	/* Next in list of all threads. */
    Ns_Time	    ctime;	/* Thread structure create time. */
    int		    flags;	/* Detached, joined, etc. */
    Ns_ThreadProc  *proc;	/* Thread startup routine. */ 
    void           *arg;	/* Argument to startup proc. */
    int		    tid;        /* Id set by thread for logging. */
    void	   *stackaddr;	/* Thread stack address. */
    size_t	    stacksize;	/* Thread stack size. */
    char	    name[NS_THREAD_NAMESIZE+1]; /* Thread name. */
    char	    parent[NS_THREAD_NAMESIZE+1]; /* Parent name. */
} Thread;

/*
 * Static functions defined in this file.
 */

static Thread *NewThread(ThreadArg *argPtr);
static Thread *GetThread(void);
static void CleanupThread(void *arg);

/*
 * The following pointer and lock maintain a linked list of all threads.
 */

static Thread *firstThreadPtr;
static Ns_Mutex threadlock;

/*
 * The following int and lock maintain the default stack size which can
 * be changed dynamically at runtime.
 */

static int stackdef;
static int stackmin;
static Ns_Mutex sizelock;

/*
 * The following maintains the tls key for the thread context.
 */

static Ns_Tls key;


/*
 *----------------------------------------------------------------------
 *
 * NsThreads_LibInit --
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
NsThreads_LibInit(void)
{
    static int once = 0;

    if (!once) {
	once = 1;
	NsInitThreads();
    	NsInitMaster();
    	NsInitReentrant();
	Ns_MutexSetName(&threadlock, "ns:threads");
	Ns_MutexSetName(&sizelock, "ns:stacksize");
    	Ns_TlsAlloc(&key, CleanupThread);
	stackdef = 64 * 1024;
	stackmin = 16 * 1024;
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
Ns_ThreadCreate(Ns_ThreadProc *proc, void *arg, long stacksize,
    	    	Ns_Thread *resultPtr)
{
    ThreadArg *argPtr;

    /*
     * Determine the stack size and add the guard.
     */

    if (stacksize <= 0) {
    	stacksize = Ns_ThreadStackSize(0);
    }
    if (stacksize < stackmin) {
	stacksize = stackmin;
    }

    /*
     * Create the thread.
     */

    argPtr = ns_malloc(sizeof(ThreadArg));
    argPtr->proc = proc;
    argPtr->arg = arg;
    argPtr->flags = resultPtr ? 0 : FLAG_DETACHED;
    strcpy(argPtr->parent, Ns_ThreadGetName());
    NsCreateThread(argPtr, stacksize, resultPtr);
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
Ns_ThreadStackSize(long stacksize)
{
    long prev;

    Ns_MutexLock(&sizelock);
    prev = stackdef;
    if (stacksize > 0) {
	stackdef = stacksize;
    }
    Ns_MutexUnlock(&sizelock);
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
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadMain(void *arg)
{
    ThreadArg   *argPtr = arg;
    Thread      *thrPtr;

    thrPtr = NewThread(argPtr);
    ns_free(argPtr);
    (*thrPtr->proc) (thrPtr->arg);
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
    Thread *thrPtr = GetThread();

    return thrPtr->name;
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
    Thread *thrPtr = GetThread();

    Ns_MutexLock(&threadlock);
    strncpy(thrPtr->name, name, NS_THREAD_NAMESIZE);
    Ns_MutexUnlock(&threadlock);
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
    Thread *thrPtr = GetThread();

    return thrPtr->parent;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadList --
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
Ns_ThreadList(Tcl_DString *dsPtr, Ns_ThreadArgProc *proc)
{
    Thread *thrPtr;
    char buf[100];

    Ns_MutexLock(&threadlock);
    thrPtr = firstThreadPtr;
    while (thrPtr != NULL) {
	Tcl_DStringStartSublist(dsPtr);
	Tcl_DStringAppendElement(dsPtr, thrPtr->name);
	Tcl_DStringAppendElement(dsPtr, thrPtr->parent);
	sprintf(buf, " %d %d %ld", thrPtr->tid,
		(thrPtr->flags & FLAG_DETACHED) ? NS_THREAD_DETACHED : 0,
		thrPtr->ctime.sec);
	Tcl_DStringAppend(dsPtr, buf, -1);
	if (proc != NULL) {
	    (*proc)(dsPtr, (void *) thrPtr->proc, thrPtr->arg);
	} else {
	    sprintf(buf, " %p %p", thrPtr->proc, thrPtr->arg);
	    Tcl_DStringAppend(dsPtr, buf, -1);
	}
	Tcl_DStringEndSublist(dsPtr);
	thrPtr = thrPtr->nextPtr;
    }
    Ns_MutexUnlock(&threadlock);
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
NewThread(ThreadArg *argPtr)
{
    Thread *thrPtr;
    int stack;

    thrPtr = ns_calloc(1, sizeof(Thread));
    Ns_GetTime(&thrPtr->ctime);
    thrPtr->tid = Ns_ThreadId();
    sprintf(thrPtr->name, "-thread%d-", thrPtr->tid);
    if (argPtr == NULL) {
    	thrPtr->flags = FLAG_DETACHED;
    } else {
	thrPtr->flags = argPtr->flags;
	thrPtr->proc = argPtr->proc;
	thrPtr->arg = argPtr->arg;
	strcpy(thrPtr->parent, argPtr->parent);
    }
    stack = NsGetStack(&thrPtr->stackaddr, &thrPtr->stacksize);
    if (stack) {
	thrPtr->flags |= FLAG_HAVESTACK;
	if (stack < 0) {
	    thrPtr->flags |= FLAG_STACKDOWN;
	}
    }
    Ns_TlsSet(&key, thrPtr);
    Ns_MutexLock(&threadlock);
    thrPtr->nextPtr = firstThreadPtr;
    firstThreadPtr = thrPtr;
    Ns_MutexUnlock(&threadlock);
    return thrPtr;
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

static Thread *
GetThread(void)
{
    Thread *thrPtr;

    thrPtr = Ns_TlsGet(&key);
    if (thrPtr == NULL) {
	thrPtr = NewThread(NULL);
    }
    return thrPtr;
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

    Ns_MutexLock(&threadlock);
    thrPtrPtr = &firstThreadPtr;
    while (*thrPtrPtr != thrPtr) {
	thrPtrPtr = &(*thrPtrPtr)->nextPtr;
    }
    *thrPtrPtr = thrPtr->nextPtr;
    thrPtr->nextPtr = NULL;
    Ns_MutexUnlock(&threadlock);
    ns_free(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CheckStack --
 *
 *	Check a thread stack for overflow.
 *
 * Results:
 *	NS_OK:	   Stack appears ok.
 *	NS_BREAK:  Overflow appears likely.
 *	NS_ERROR:  Stack address/size unknown.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CheckStack(void)
{
    Thread *thrPtr = GetThread();
    caddr_t limit;

    /*
     * Return error on no stack.
     */

    if (!(thrPtr->flags & FLAG_HAVESTACK)) {
	return NS_ERROR;
    }

    /*
     * Check if the stack has grown into or beyond the guard.
     */

    if (thrPtr->flags & FLAG_STACKDOWN) {
	limit = (caddr_t) thrPtr->stackaddr - thrPtr->stacksize;
	if ((caddr_t) &limit < limit) {
	    return NS_BREAK;
	}
    } else {
	limit = (caddr_t) thrPtr->stackaddr + thrPtr->stacksize;
	if ((caddr_t) &limit > limit) {
	    return NS_BREAK;
	}
    }
    return NS_OK;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/thread.c,v 1.4 2000/08/08 20:49:59 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following global specifies the maximum stack size.  This value is
 * set directly by AOLserver at startup.
 */

long nsThreadStackSize = 65536;

/*
 * The following pointer, lock, and condition maintain a linked list
 * of joinable threads, running or exited.
 */

static void FreeThread(Thread *thrPtr);
static Ns_Mutex lock;
static Ns_Cond cond;
static Thread *firstPtr;
static Thread *lastPtr;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadCreate, Ns_ThreadCreate2 --
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
Ns_ThreadCreate(Ns_ThreadProc *proc, void *arg, long stackSize, Ns_Thread *resultPtr)
{
    int flags;

    flags = resultPtr ? 0 : NS_THREAD_DETACHED;
    return Ns_ThreadCreate2(proc, arg, stackSize, flags, resultPtr);
}

void
Ns_ThreadCreate2(Ns_ThreadProc *proc, void *arg, long stackSize,
	int flags, Ns_Thread *resultPtr)
{
    Thread *thrPtr;

    /*
     * Initialize the memory pools if enabled.
     */

    if (nsMemPools) {
    	NsInitPools();
    }

    /*
     * Determine the stack size and impose a 16k minimum.
     */

    if (stackSize == 0) {
	stackSize = nsThreadStackSize;
    }
    if (stackSize < 16384) {
	stackSize = 16384;
    }

    /*
     * Create the new thread structure.
     */

    thrPtr = NsNewThread(proc, arg, stackSize, flags);
    if (resultPtr != NULL) {
	*resultPtr = (Ns_Thread) thrPtr;
    }

    /*
     * Call the interface specific create routine.
     */

    NsThreadCreate(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadExit --
 *
 *	Cleanup the thread's tls and memory pool and either frees the
 *	thread if it's detached or marks the thread as exited which
 *	allows it to be joined.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Joinable threads condition may be broadcast.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadExit(void *arg)
{
    Thread *thisPtr = NsGetThread();

    NsCleanupTls(thisPtr);
    if (thisPtr->pool != NULL) {
	Ns_PoolDestroy(thisPtr->pool);
	thisPtr->pool = NULL;
    }
    if (nsMemPools) {
        NsFlushPool(&thisPtr->memPool);
    }
    Ns_MutexLock(&lock);
    if (thisPtr->flags & NS_THREAD_DETACHED) {
	FreeThread(thisPtr);
    } else {
    	thisPtr->exitarg = arg;
	thisPtr->flags |= NS_THREAD_EXITED;
	Ns_CondBroadcast(&cond);
    }
    Ns_MutexUnlock(&lock);
    NsThreadExit();
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
 *	The calling thread may wait on the joinable thread condition
 * 	if the thread to be joined is still running.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadJoin(Ns_Thread *threadPtr, void **argPtr)
{
    Thread *thrPtr, *thisPtr;

    thisPtr = NsGetThread();
    thrPtr = (Thread *) *threadPtr;
    if (thrPtr == thisPtr) {
	NsThreadAbort("attempt to join self: %d", thrPtr->tid);
    }
    Ns_MutexLock(&lock);
    if ((thrPtr->flags & NS_THREAD_DETACHED)) {
	NsThreadAbort("attempt to join detached thread:  %d", thrPtr->tid);
    }
    if ((thrPtr->flags & NS_THREAD_JOINED)) {
	NsThreadAbort("thread %d already being joined", thrPtr->tid);
    }
    thrPtr->flags |= NS_THREAD_JOINED;
    while (!(thrPtr->flags & NS_THREAD_EXITED)) {
	Ns_CondWait(&cond, &lock);
    }
    if (argPtr != NULL) {
	*argPtr = thrPtr->exitarg;
    }
    FreeThread(thrPtr);
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadMain --
 *
 *	Thread startup routine called by interface specific
 *	NsCreateThread.
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
NsThreadMain(void *arg)
{
    Thread      *thrPtr = (Thread *) arg;

    NsSetThread(thrPtr);
    (*thrPtr->proc) (thrPtr->arg);
    Ns_ThreadExit(0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsNewThread --
 *
 *	Allocate a new thread data structure.  Note that the Thread
 *	must be allocated directly, bypassing ns_calloc, as the
 *	per-thread memory pool resides in the Thread.  Also, the
 *	careful lock bypassing is needed to avoid endless recursion
 *	at startup.
 *
 * Results:
 *	Pointer to new Thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Thread *
NsNewThread(Ns_ThreadProc *proc, void *arg, long stackSize, int flags)
{
    Thread *thrPtr, *thisPtr;
    static int state;

    thisPtr = NsGetThread();
    thrPtr = calloc(1, sizeof(Thread));
    if (thrPtr == NULL) {
	NsThreadFatal("NsNewThread", "calloc", ENOMEM);
    }
    thrPtr->proc = proc;
    thrPtr->arg = arg;
    thrPtr->stackSize = stackSize;
    thrPtr->ctime = time(NULL);
    if (state != 0) {
	Ns_MutexLock(&lock);
    }
    strcpy(thrPtr->parent, thisPtr ? thisPtr->name : "");
    thrPtr->flags = (flags & NS_THREAD_DETACHED);
    thrPtr->nextPtr = NULL;
    thrPtr->prevPtr = lastPtr;
    lastPtr = thrPtr;
    if (thrPtr->prevPtr != NULL) {
	thrPtr->prevPtr->nextPtr = thrPtr;
    }
    if (firstPtr == NULL) {
	firstPtr = thrPtr;
    }
    if (state == 0) {
	++state;
    } else {
	if (state == 1) {
	    Ns_MutexSetName2(&lock, "nsthread", "new");
	    ++state;
	}
	Ns_MutexUnlock(&lock);
    }
    return thrPtr;
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
    Thread *thisPtr = NsGetThread();

    Ns_MutexLock(&lock);
    strncpy(thisPtr->name, name, NS_THREAD_NAMESIZE);
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadEnum --
 *
 *	Enumerate all current threads.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Given callback is invoked with info for each thread.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadEnum(Ns_ThreadInfoProc *proc, void *arg)
{
    Thread *thrPtr;
    Ns_ThreadInfo info;

    thrPtr = firstPtr;
    Ns_MutexLock(&lock);
    while (thrPtr != NULL) {
	info.thread = (Ns_Thread *) thrPtr;
	info.tid = thrPtr->tid;
	info.ctime = thrPtr->ctime;
	info.name = thrPtr->name;
	info.parent = thrPtr->parent;
	info.flags = thrPtr->flags;
	info.proc = thrPtr->proc;
	info.arg = thrPtr->arg;
	(*proc)(&info, arg);
	thrPtr = thrPtr->nextPtr;
    }
    Ns_MutexUnlock(&lock);
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
    Thread *thisPtr = NsGetThread();

    return thisPtr->tid;
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
Ns_ThreadSelf(Ns_Thread *thrPtr)
{
    *thrPtr = (Ns_Thread) NsGetThread();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CheckStack --
 *
 *	Check for possible thread stack overflow.
 *
 * Results:
 *	NS_OK if stack appears ok, otherwise NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CheckStack(void)
{
    Thread *thisPtr = NsGetThread();
    
    /* 
     * Check to see if the thread may be about to grow beyond it's allocated
     * stack.  Currently, this function is only called in Tcl_Eval where
     * it traps the common case of infinite Tcl recursion.  The 1k slop
     * trys to take into account the stack used before ThreadMain is called
     * and the stack used by Tcl_Eval and Ns_CheckStack.  It's just a very
     * conservative guess.
     */

    if (abs(((long) thisPtr->stackBase) - ((long) &thisPtr)) >
	    (thisPtr->stackSize - 1024)) {
	return NS_ERROR;
    }
    return NS_OK;
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
    Thread *thisPtr = NsGetThread();

    return thisPtr->name;
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
    Thread *thisPtr = NsGetThread();

    return thisPtr->parent;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeThread --
 *
 *	Deallocate a thread data structure.
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
FreeThread(Thread *thrPtr)
{
    if (firstPtr == thrPtr) {
	firstPtr = thrPtr->nextPtr;
    } else {
	thrPtr->prevPtr->nextPtr = thrPtr->nextPtr;
    }
    if (lastPtr == thrPtr) {
	lastPtr = thrPtr->prevPtr;
    } else {
	thrPtr->nextPtr->prevPtr = thrPtr->prevPtr;
    }
    thrPtr->nextPtr = thrPtr->prevPtr = NULL;
    free(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsSetThread2 --
 *
 *	Setup the thread data structure for the calling thread.
 *	Called from NsSetThread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread name is initialized to a simple numbered string name.
 *
 *----------------------------------------------------------------------
 */

void
NsSetThread2(Thread *thisPtr)
{
    thisPtr->stackBase = &thisPtr;
    sprintf(thisPtr->name, "-t%d-", thisPtr->tid);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread2 --
 *
 *	Get a new thread data structure for an unknown thread, called
 *	from NsGetThread normally just once, the first time in the
 *	initial thread.
 *
 * Results:
 *	Pointer to new Thread structure.
 *
 * Side effects:
 *	Allocates and sets a new Thread.
 *
 *----------------------------------------------------------------------
 */

Thread *
NsGetThread2(void)
{
    Thread *thrPtr, temp;

    /*
     * Set a temporary thread to avoid possible endless recursion
     * in NsNewThread.
     */

    memset(&temp, 0, sizeof(temp));
    NsSetThread(&temp);

    thrPtr = NsNewThread(NULL, NULL, nsThreadStackSize, NS_THREAD_DETACHED);
    NsSetThread(thrPtr);
    return thrPtr;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/thread.c,v 1.12 2000/11/06 17:53:50 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
static Thread *firstThreadPtr;


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
Ns_ThreadCreate(Ns_ThreadProc *proc, void *arg, long stack,
    	    	Ns_Thread *resultPtr)
{
    int flags;

    flags = resultPtr ? 0 : NS_THREAD_DETACHED;
    Ns_ThreadCreate2(proc, arg, stack, flags, resultPtr);
}

void
Ns_ThreadCreate2(Ns_ThreadProc *proc, void *arg, long stack,
	    	 int flags, Ns_Thread *resultPtr)
{
    Thread *thrPtr;

    /*
     * Determine the stack size and impose a 16k minimum.
     */

    if (stack == 0) {
	stack = nsThreadStackSize;
    }
    if (stack < 16384) {
	stack = 16384;
    }

    /*
     * Allocate a new thread structure and update values
     * which are known for threads created here.
     */

    Ns_MasterLock();
    thrPtr = NsNewThread();
    thrPtr->proc = proc;
    thrPtr->arg = arg;
    thrPtr->stackSize = stack;
    thrPtr->flags = flags;
    strcpy(thrPtr->parent, Ns_ThreadGetName());
    Ns_MasterUnlock();
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
 *	Set the exitarg and call the interface exit routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Interface is responsible for calling NsCleanupThread.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadExit(void *arg)
{
    Thread *thisPtr = NsGetThread();

    thisPtr->exitarg = arg;
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
 *	The calling thread will wait on the joinable thread condition
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
    Ns_MutexUnlock(&lock);
    FreeThread(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadMain --
 *
 *	Thread startup routine called by interface specific
 *	NsCreateThread.  Sets the given pre-allocated thread
 *	structure and calls the user specified procedure.
 *
 * Results:
 *	None.  Will call Ns_ThreadExit if not called by the
 *	user code.
 *
 * Side effects:
 *	See NsSetThread.
 *
 *----------------------------------------------------------------------
 */

void 
NsThreadMain(void *arg)
{
    Thread      *thrPtr = (Thread *) arg;

    NsSetThread(thrPtr);
    thrPtr->stackBase = &arg;
    (*thrPtr->proc) (thrPtr->arg);
    Ns_ThreadExit(0);
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
    long size, base, here;
    
    /* 
     * Check to see if the thread may be about to grow beyond it's allocated
     * stack.  Currently, this function is only called in Tcl_Eval where
     * it traps the common case of infinite Tcl recursion.  The 1024
     * slop is just a conservative guess.
     */

    size = thisPtr->stackSize - 1024;
    base = (long) thisPtr->stackBase;
    here = (long) &thisPtr;
    if (size > 0 && abs(base - here) > size) {
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
    Thread *thisPtr = NsGetThread();

    return thisPtr->parent;
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

    Ns_MasterLock();
    thrPtr = firstThreadPtr;
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
    Ns_MasterUnlock();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadMemStats --
 *
 *	Write pool stats for all threads and the shared pool to open
 *	file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Output to open file or stderr.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadMemStats(FILE *fp)
{
    Thread *thrPtr;

    if (fp == NULL) {
	fp = stderr;
    }
    Ns_MasterLock();
    thrPtr = firstThreadPtr;
    while (thrPtr != NULL) {
	if (thrPtr->pool != NULL) {
    	    fprintf(fp, "[%d:%s]:", thrPtr->tid, thrPtr->name);
	    Ns_PoolStats(thrPtr->pool, fp);
	}
	thrPtr = thrPtr->nextPtr;
    }
    Ns_MasterUnlock();
    fprintf(fp, "[shared]:");
    Ns_PoolStats(NULL, fp);
    fflush(fp);
}


/*
 *----------------------------------------------------------------------
 *
 * NsNewThread --
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

Thread *
NsNewThread(void)
{
    Thread *thrPtr;

    thrPtr = NsAlloc(sizeof(Thread));
    thrPtr->ctime = time(NULL);
    thrPtr->flags = NS_THREAD_DETACHED;
    Ns_MasterLock();
    thrPtr->nextPtr = firstThreadPtr;
    firstThreadPtr = thrPtr;
    Ns_MasterUnlock();
    return thrPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeThread --
 *
 *	Deallocate a thread data structure and remove from the linked
 *  	list.
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
    Thread **thrPtrPtr;

    Ns_MasterLock();
    thrPtrPtr = &firstThreadPtr;
    while (*thrPtrPtr != thrPtr) {
	thrPtrPtr = &(*thrPtrPtr)->nextPtr;
    }
    *thrPtrPtr = thrPtr->nextPtr;
    thrPtr->nextPtr = NULL;
    Ns_MasterUnlock();
    NsFree(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsCleanupThread --
 *
 *	Cleanup the thread's tls and memory pool and either free the
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
NsCleanupThread(Thread *thrPtr)
{
    /*
     * Invoke the TLS cleanup callbacks.
     */

    NsCleanupTls(thrPtr);

    /*
     * Destroy the per-thread memory pool now that the
     * TLS callbacks are complete.
     */

    if (thrPtr->pool != NULL) {
	Ns_PoolDestroy(thrPtr->pool);
	thrPtr->pool = NULL;
    }

    /*
     * Signal any potential joiners.
     */

    Ns_MutexLock(&lock);
    if (!(thrPtr->flags & NS_THREAD_DETACHED)) {
    	thrPtr->etime = time(NULL);
	thrPtr->arg = NULL;
	thrPtr->flags |= NS_THREAD_EXITED;
	thrPtr = NULL;
	Ns_CondBroadcast(&cond);
    }
    Ns_MutexUnlock(&lock);

    /*
     * If the thread isn't joinable, free the context now.
     */

    if (thrPtr != NULL) {
	FreeThread(thrPtr);
    }
}

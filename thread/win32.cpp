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
 * win32.c --
 *
 *	Interface routines for nsthreads using WIN32 functions.
 *
 */


#include "thread.h"
#include <io.h>

/*
 * The following structure maintains the Win32-specific state of a
 * process thread including queue pointers for startup, exit, and
 * condition wait and the process id.
 */

typedef struct WinThread {
    HANDLE event;
    unsigned tid;
    struct WinThread *prevPtr;
    struct WinThread *nextPtr;
    struct Thread *thrPtr;
    enum {
	WinThreadRunning,
	WinThreadCondWait,
    } state;
} WinThread;

/*
 * The following structure defines a queue of threads in a condition wait.
 */

typedef struct {
    Ns_Mutex      lock;
    WinThread      *firstPtr;
    WinThread      *lastPtr;
} Cond;

typedef struct WinMutex {
    LONG spinlock;
    int locked;
    int nwait;
    HANDLE event;
} WinMutex;
  
/*
 * Static functions defined in this file.
 */

static void	WinThreadMain(void *arg);
static void	CondWakeup(Cond *cPtr);
static Cond    *GetCond(Ns_Cond *condPtr);
static WinThread *GetWinThread(void);
static DWORD	tlskey;


/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *	Thread library DLL main.  Allocates the single TLS key at
 *	process attached and then allocates/frees the per-thread
 *	WinThread strcuture on thread attach/detach.
 *
 * Results:
 *	TRUE or FALSE.
 *
 * Side effects:
 *	On error will abort process.
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD why, LPVOID lpReserved)
{
    WinThread *wPtr;

    switch (why) {
    case DLL_PROCESS_ATTACH:
	tlskey = TlsAlloc();
	if (tlskey == 0xFFFFFFFF) {
	    return FALSE;
	}
	/* FALLTHROUGH */

    case DLL_THREAD_ATTACH:
	/* NB: Can't use ns_calloc yet. */
	wPtr = calloc(1, sizeof(WinThread));
	if (wPtr == NULL) {
	    NsThreadAbort("calloc() failed");
	}
	wPtr->state = WinThreadRunning;
	wPtr->tid = GetCurrentThreadId();
	wPtr->event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (wPtr->event == NULL) {
	    NsThreadFatal("DllMain", "CreateEvent", GetLastError());
	}
	if (!TlsSetValue(tlskey, wPtr)) {
	    NsThreadFatal("DllMain", "TlsSetValue", GetLastError());
	}
	break;

    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
	wPtr = TlsGetValue(tlskey);
	if (wPtr != NULL) {
	    if (!CloseHandle(wPtr->event)) {
		NsThreadFatal("DllMain", "CloseHandle", GetLastError());
	    }
	    if (!TlsSetValue(tlskey, NULL)) {
		NsThreadFatal("DllMain", "TlsSetValue", GetLastError());
	    }
	    free(wPtr);
	}
	break;
    }
    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexInit --
 *
 *	Allocate and initialize a mutex.  Note this function
 *	is rarley called directly as mutexes are now self initalized
 *	when first locked.
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
NsMutexInit(void **lockPtr)
{
    WinMutex *wmPtr;

    wmPtr = ns_calloc(1, sizeof(WinMutex));
    wmPtr->event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (wmPtr->event == NULL) {
	NsThreadFatal("NsMutexInit", "CreateEvent", GetLastError());
    }
    *lockPtr = wmPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexDestroy --
 *
 *	Destroy a mutex.  Note this function is almost never
 *	used as mutexes typically exists until the process exists.
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
NsMutexDestroy(void **lockPtr)
{
    WinMutex *wmPtr;

    wmPtr = (WinMutex *) *lockPtr;
    if (!CloseHandle(wmPtr->event)) {
	NsThreadFatal("NsMutexDestroy", "CloseHandle", GetLastError());
    }
    ns_free(wmPtr);
    *lockPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexLock --
 *
 *	Lock a mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Some other blocked process may be resumed.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexLock(void **lockPtr)
{
    WinMutex *wmPtr = (WinMutex *) *lockPtr;
    int locked;

    do {
	while(InterlockedExchange(&wmPtr->spinlock, 1)) {
	   Ns_ThreadYield();
	}
	locked = wmPtr->locked;
	if (!locked) {
	    wmPtr->locked = 1;
	} else {
	    ResetEvent(wmPtr->event);
	    ++wmPtr->nwait;
	}
	InterlockedExchange(&wmPtr->spinlock, 0);
	if (locked && WaitForSingleObject(wmPtr->event, INFINITE) != WAIT_OBJECT_0) {
	    NsThreadFatal("NsMutexLock", "WaitForSingleObject", GetLastError());
	}
    } while (locked);
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexTryLock --
 *
 *	Mutex non-blocking lock.
 *
 * Results:
 *	NS_OK if locked, NS_TIMEOUT if lock already held.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
NsMutexTryLock(void **lockPtr)
{
    WinMutex *wmPtr = (WinMutex *) *lockPtr;
    int locked;

    while(InterlockedExchange(&wmPtr->spinlock, 1)) {
       Ns_ThreadYield();
    }
    locked = wmPtr->locked;
    if (!locked) {
	wmPtr->locked = 1;
    }
    InterlockedExchange(&wmPtr->spinlock, 0);
    if (locked) {
	return NS_TIMEOUT;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexUnlock --
 *
 *	Unlock a mutex.
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
NsMutexUnlock(void **lockPtr)
{
    WinMutex *wmPtr = (WinMutex *) *lockPtr;
    int nwait;

    while (InterlockedExchange(&wmPtr->spinlock, 1)) {
       Ns_ThreadYield();
    }
    wmPtr->locked = 0;
    nwait = wmPtr->nwait;
    if (nwait > 0) {
	--wmPtr->nwait;
    }
    InterlockedExchange(&wmPtr->spinlock, 0);
    if (nwait > 0 && !SetEvent(wmPtr->event)) {
	NsThreadFatal("NsMutexUnlock", "SetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Initialize a condition variable.  Note that this function is rarely
 *	called directly as condition variables are now self initialized
 *	when first accessed.
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
Ns_CondInit(Ns_Cond *condPtr)
{
    Cond          *cPtr;

    cPtr = ns_malloc(sizeof(Cond));
    cPtr->firstPtr = cPtr->lastPtr = NULL;
    Ns_MutexInit2(&cPtr->lock, "nsthread:cond");
    *condPtr = (Ns_Cond) cPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	Destroy a previously initialized condition variable.  Note this
 *	function is almost never called as condition variables
 *	normally exist until the process exits.
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
Ns_CondDestroy(Ns_Cond *condPtr)
{
    if (*condPtr != NULL) {
    	Cond *cPtr = (Cond *) *condPtr;

    	Ns_MutexDestroy(&cPtr->lock);
    	ns_free(cPtr);
    	*condPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *	Signal a condition variable, releasing a single thread if one
 *	is waiting.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A single waiting thread may be resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *condPtr)
{
    Cond         *cPtr = GETCOND(condPtr);

    Ns_MutexLock(&cPtr->lock);
    if (cPtr->firstPtr != NULL) {
	CondWakeup(cPtr);
    }
    Ns_MutexUnlock(&cPtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Broadcast a condition, resuming all waiting threads, if any.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	One or more waiting threads may be resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *condPtr)
{
    Cond         *cPtr = GETCOND(condPtr);

    Ns_MutexLock(&cPtr->lock);
    while (cPtr->firstPtr != NULL) {
	CondWakeup(cPtr);
    }
    Ns_MutexUnlock(&cPtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *	Wait indefinitely for a condition to be signaled.
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
Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *lockPtr)
{
    (void) Ns_CondTimedWait(condPtr, lockPtr, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *	Wait for a condition to be signaled up to a given absolute time
 *	out.  This code is very tricky to avoid the race condition between
 *	locking and unlocking the coordinating mutex and catching a
 *	a wakeup signal.  Be sure you understand how condition variables
 *	work before screwing around with this code.
 *
 * Results:
 *	NS_OK on signal being received within the timeout period, otherwise
 *	NS_TIMEOUT.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *lockPtr, Ns_Time *timePtr)
{
    int status;
    Cond           *cPtr;
    WinThread	   *wPtr;
    Ns_Time 	    now, wait;
    DWORD	    msec, w;

    /*
     * Convert to relative wait time and verify.
     */

    if (timePtr == NULL) {
	msec = INFINITE;
    } else {
	Ns_GetTime(&now);
	Ns_DiffTime(timePtr, &now, &wait);
	wait.usec /= 1000;
	if (wait.sec < 0 || (wait.sec == 0 && wait.usec <= 0)) {
	    return NS_TIMEOUT;
	}
	msec = wait.sec * 1000 + wait.usec;
    }

    /*
     * Lock the condition, queue this thread, and wait for wakeup
     * or timeout.
     */

    cPtr = GETCOND(condPtr);
    wPtr = GetWinThread();
    wPtr->state = WinThreadCondWait;
    wPtr->nextPtr = NULL;
    ResetEvent(wPtr->event);

    Ns_MutexLock(&cPtr->lock);
    wPtr->prevPtr = cPtr->lastPtr;
    cPtr->lastPtr = wPtr;
    if (wPtr->prevPtr != NULL) {
        wPtr->prevPtr->nextPtr = wPtr;
    }
    if (cPtr->firstPtr == NULL) {
        cPtr->firstPtr = wPtr;
    }
    Ns_MutexUnlock(lockPtr);
    Ns_MutexUnlock(&cPtr->lock);
    w = WaitForSingleObject(wPtr->event, msec);
    Ns_MutexLock(&cPtr->lock);
    if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) {
	NsThreadFatal("Ns_CondTimedWait", "WaitForSingleObject", GetLastError());
    }
    if (wPtr->state == WinThreadRunning) {
	status = NS_OK;
    } else {
	status = NS_TIMEOUT;
        if (cPtr->firstPtr == wPtr) {
            cPtr->firstPtr = wPtr->nextPtr;
        } else {
            wPtr->prevPtr->nextPtr = wPtr->nextPtr;
        }
        if (cPtr->lastPtr == wPtr) {
            cPtr->lastPtr = wPtr->prevPtr;
        } else {
            wPtr->nextPtr->prevPtr = wPtr->prevPtr;
        }
	wPtr->state = WinThreadRunning;
    }
    Ns_MutexUnlock(&cPtr->lock);
    Ns_MutexLock(lockPtr);

    return status;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *	WinThread specific thread yield.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Process may yield cpu to another process. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadYield(void)
{
    Sleep(0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadLibName --
 *
 *	Return the string name of the thread library.
 *
 * Results:
 *	Pointer to static string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
NsThreadLibName(void)
{
    return "win32";
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadCreate --
 *
 *	WinThread specific thread create function called by Ns_ThreadCreate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New process is WinThread'ed by manager thread.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    if (_beginthread(WinThreadMain, thrPtr->stackSize, thrPtr) == 0) {
	NsThreadFatal("NsThreadCreate", "CreateThread", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *	Terminate a WinThread processes after adding the process id to the
 *	list of processes to be reaped by the manager process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Manager process will later reap the process status.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadExit(void)
{
    _endthread();
}



/*
 *----------------------------------------------------------------------
 *
 * NsSetThread --
 *
 *	WinThread specific routine for setting a thread's data structure.
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
NsSetThread(Thread *thisPtr)
{
    WinThread *wPtr = GetWinThread();

    thisPtr->tid = (int) wPtr->tid;
    wPtr->thrPtr = thisPtr;
    NsSetThread2(thisPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread --
 *
 *	WinThread specific routine for getting a thread's structure. 
 *
 * Results:
 *	Pointer to this thread's structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Thread      *
NsGetThread(void)
{
    WinThread *wPtr = GetWinThread();
    
    return (wPtr->thrPtr ? wPtr->thrPtr : NsGetThread2());
}


/*
 *----------------------------------------------------------------------
 *
 * CondWakeup --
 *
 *	Wakeup a process sleeping on a condition variable queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	First thread is removed from the queue, marked running and
 *	signaled with SetEvent().
 *
 *----------------------------------------------------------------------
 */

static void
CondWakeup(Cond *cPtr)
{
    WinThread *wPtr;

    wPtr = cPtr->firstPtr;
    cPtr->firstPtr = wPtr->nextPtr;
    if (cPtr->lastPtr == wPtr) {
	cPtr->lastPtr = NULL;
    }
    wPtr->state = WinThreadRunning;
    wPtr->nextPtr = NULL;
    if (!SetEvent(wPtr->event)) {
	NsThreadFatal("CondWakeup", "SetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * WinThreadMain --
 *
 *	Startup routine for WinThread process threads.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Start condition may be signaled.
 *
 *----------------------------------------------------------------------
 */

static void
WinThreadMain(void *arg)
{
    Thread *thrPtr = arg;
    WinThread *wPtr = GetWinThread();

    wPtr->thrPtr = thrPtr;
    NsThreadMain(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetWinThread --
 *
 *	Retrieve this threads WinThread which was allocated and
 *	set in DllMain.
 *
 * Results:
 *	Pointer to WinThread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static WinThread *
GetWinThread(void)
{
    WinThread *wPtr;

    wPtr = TlsGetValue(tlskey);
    if (wPtr == NULL) {
	NsThreadFatal("GetWinThread", "TlsGetValue", GetLastError());
    }
    return wPtr;
}

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
 * process thread for mutex and condition waits.
 */

typedef struct WinThread {
    HANDLE event;
    struct WinThread *nextPtr;
    struct Thread *thrPtr;
    enum {
	WinThreadRunning,
	WinThreadCondWait,
    } state;
} WinThread;

/*
 * The following structure defines a condition variable as
 * a mutex and wait queue.
 */

typedef struct {
    LONG	  lock;
    WinThread	 *waitPtr;
} Cond;

/*
 * The following structure defines a mutex as a spinlock and
 * wait queue.  The custom lock code provides the speed of
 * a Win32 CriticalSection with the TryLock of a Mutex handle
 * (TryEnterCriticalSection is not yet available on all Win32
 * platforms).
 */

typedef struct WinMutex {
    LONG spinlock;
    int locked;
    WinThread *waitPtr;
} WinMutex;
  
/*
 * Static functions defined in this file.
 */

static void	WinThreadMain(void *arg);
static WinThread *GetWinThread(void);
static void	CondWakeup(Cond *cPtr);
static void	Wakeup(WinThread *wPtr, char *func);
static void	Queue(WinThread **waitPtrPtr, WinThread *wPtr);

#define SPINLOCK(lPtr) \
    while(InterlockedExchange(lPtr, 1)) Sleep(0)
#define SPINUNLOCK(lPtr) \
    InterlockedExchange(lPtr, 0)

/*
 * The following single Tls key is used to store the nsthread
 * structure.  It's initialized in DllMain.
 */

static DWORD		tlskey;


/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *	Thread library DLL main.  Allocates the single TLS key at
 *	process attached and frees the per-thread WinThread strcuture
 *	on thread detach if allocated.
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
	break;

    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
	wPtr = TlsGetValue(tlskey);
	if (wPtr != NULL) {
	    if (wPtr->thrPtr != NULL) {
		NsCleanupThread(wPtr->thrPtr);
		wPtr->thrPtr = NULL;
	    }
	    if (!CloseHandle(wPtr->event)) {
		NsThreadFatal("DllMain", "CloseHandle", GetLastError());
	    }
	    if (!TlsSetValue(tlskey, NULL)) {
		NsThreadFatal("DllMain", "TlsSetValue", GetLastError());
	    }
	    free(wPtr);
	}
	if (why == DLL_PROCESS_DETACH && !TlsFree(tlskey)) {
	    NsThreadFatal("DllMain", "TlsFree", GetLastError());
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
    WinMutex *mutexPtr;

    mutexPtr = ns_calloc(1, sizeof(WinMutex));
    *lockPtr = mutexPtr;
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
    WinMutex *mutexPtr;

    mutexPtr = (WinMutex *) *lockPtr;
    ns_free(mutexPtr);
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
    WinThread *wPtr = NULL;
    WinMutex *mutexPtr = (WinMutex *) *lockPtr;
    int locked;

    do {
	SPINLOCK(&mutexPtr->spinlock);
	locked = mutexPtr->locked;
	if (!locked) {
	    mutexPtr->locked = 1;
	} else {
	    if (wPtr == NULL) {
		wPtr = GetWinThread();
	    }
	    Queue(&mutexPtr->waitPtr, wPtr);
	}
	SPINUNLOCK(&mutexPtr->spinlock);
	if (locked && WaitForSingleObject(wPtr->event, INFINITE) != WAIT_OBJECT_0) {
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
    WinMutex *mutexPtr = (WinMutex *) *lockPtr;
    int locked;

    SPINLOCK(&mutexPtr->spinlock);
    locked = mutexPtr->locked;
    if (!locked) {
	mutexPtr->locked = 1;
    }
    SPINUNLOCK(&mutexPtr->spinlock);
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
    WinMutex *mutexPtr = (WinMutex *) *lockPtr;
    WinThread *wPtr;

    SPINLOCK(&mutexPtr->spinlock);
    mutexPtr->locked = 0;
    wPtr = mutexPtr->waitPtr;
    if (wPtr != NULL) {
	mutexPtr->waitPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
    }
    SPINUNLOCK(&mutexPtr->spinlock);
    if (wPtr != NULL) {
	Wakeup(wPtr, "NsMutexUnlock");
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

    cPtr = ns_calloc(1, sizeof(Cond));
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

    SPINLOCK(&cPtr->lock);
    if (cPtr->waitPtr != NULL) {
	CondWakeup(cPtr);
    }
    SPINUNLOCK(&cPtr->lock);
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

    SPINLOCK(&cPtr->lock);
    while (cPtr->waitPtr != NULL) {
	CondWakeup(cPtr);
    }
    SPINUNLOCK(&cPtr->lock);
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
    WinThread	   *wPtr, **waitPtrPtr;
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
     * Lock the condition and add this thread to the end of
     * the wait list.
     */

    cPtr = GETCOND(condPtr);
    wPtr = GetWinThread();
    SPINLOCK(&cPtr->lock);
    wPtr->state = WinThreadCondWait;
    Queue(&cPtr->waitPtr, wPtr);
    SPINUNLOCK(&cPtr->lock);

    /*
     * Release the outer mutex and wait for the signal to arrive
     * or timeout.
     */

    Ns_MutexUnlock(lockPtr);
    w = WaitForSingleObject(wPtr->event, msec);
    if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) {
	NsThreadFatal("Ns_CondTimedWait", "WaitForSingleObject", GetLastError());
    }

    /*
     * Lock the condition and check if wakeup was signalled.  Note
     * that the signal may have arrived as the event was timing
     * out so the return of WaitForSingleObject can't be relied on.
     * If there was no wakeup, remove this thread from the list.
     */

    SPINLOCK(&cPtr->lock);
    if (wPtr->state == WinThreadRunning) {
	status = NS_OK;
    } else {
	status = NS_TIMEOUT;
	waitPtrPtr = &cPtr->waitPtr;
	while (*waitPtrPtr != wPtr) {
	    waitPtrPtr = &(*waitPtrPtr)->nextPtr;
	}
	*waitPtrPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
	wPtr->state = WinThreadRunning;
    }
    SPINUNLOCK(&cPtr->lock);

    /*
     * Re-aquire the outer lock and return.
     */

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
    if (_beginthread(NsThreadMain, thrPtr->stackSize, thrPtr) == 0) {
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
NsSetThread(Thread *thrPtr)
{
    WinThread *wPtr = GetWinThread();

    wPtr->thrPtr = thrPtr;
    NsInitThread(thrPtr, GetCurrentThreadId());
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
    Thread    *thrPtr;
    
    thrPtr = wPtr->thrPtr;
    if (thrPtr == NULL) {
	thrPtr = NsNewThread();
	NsSetThread(thrPtr);
    }
    return thrPtr;
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
	/* NB: Can't use ns_calloc yet. */
	wPtr = calloc(1, sizeof(WinThread));
	if (wPtr == NULL) {
	    NsThreadAbort("calloc() failed");
	}
	wPtr->state = WinThreadRunning;
	wPtr->event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (wPtr->event == NULL) {
	    NsThreadFatal("GetWinThread", "CreateEvent", GetLastError());
	}
	if (!TlsSetValue(tlskey, wPtr)) {
	    NsThreadFatal("GetWinThread", "TlsSetValue", GetLastError());
	}
    }
    return wPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Queue --
 *
 *	Add a thread on a mutex or condition wait queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread wakeup event is reset in case it's holding
 *	a lingering wakeup.
 *
 *----------------------------------------------------------------------
 */

static void
Queue(WinThread **waitPtrPtr, WinThread *wPtr)
{
    while (*waitPtrPtr != NULL) {
	waitPtrPtr = &(*waitPtrPtr)->nextPtr;
    }
    *waitPtrPtr = wPtr;
    wPtr->nextPtr = NULL;
    if (!ResetEvent(wPtr->event)) {
	NsThreadFatal("Queue", "ResetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Wakeup, CondWakeup --
 *
 *	Wakeup a thread waiting on a mutex or condition queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread wakeup event is set and state is set to running
 *	for condition waits.
 *
 *----------------------------------------------------------------------
 */

static void
Wakeup(WinThread *wPtr, char *func)
{
    if (!SetEvent(wPtr->event)) {
	NsThreadFatal(func, "SetEvent", GetLastError());
    }
}

static void
CondWakeup(Cond *cPtr)
{
    WinThread *wPtr;

    wPtr = cPtr->waitPtr;
    cPtr->waitPtr = wPtr->nextPtr;
    wPtr->nextPtr = NULL;
    wPtr->state = WinThreadRunning;
    Wakeup(wPtr, "Ns_CondBroadcast");
}

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
 * process thread and mutex and condition waits pointers.
 */

typedef struct WinThread {
    struct WinThread *nextPtr;
    struct WinThread *wakeupPtr;
    struct Thread    *thrPtr;
    HANDLE	      event;
    int		      condwait;
} WinThread;

/*
 * The following structure defines a mutex as a spinlock and
 * wait queue.  The custom lock code provides the speed of
 * a Win32 CriticalSection with the TryLock of a Mutex handle
 * (TryEnterCriticalSection is not yet available on all Win32
 * platforms).
 */

typedef struct {
    int		locked;
    LONG	spinlock;
    WinThread  *waitPtr;
} Lock;
  
/*
 * The following structure defines a condition variable as
 * a spinlock and wait queue.
 */

typedef struct {
    LONG	  spinlock;
    WinThread	 *waitPtr;
} Cond;

/*
 * Static functions defined in this file.
 */

static WinThread *GetWinThread(void);
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
 * The following critical section is used for the nsthreads
 * master lock.
 */

static CRITICAL_SECTION masterLock;


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
	InitializeCriticalSection(&masterLock);
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
	    NsFree(wPtr);
	}
	if (why == DLL_PROCESS_DETACH) {
	    if (!TlsFree(tlskey)) {
		NsThreadFatal("DllMain", "TlsFree", GetLastError());
	    }
	    DeleteCriticalSection(&masterLock);
	}
	break;
    }
    return TRUE;
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
 * Ns_MasterLock, Ns_MasterUnlock --
 *
 *	Lock/unlock the global master critical section lock using
 *	the CRITICAL_SECTION initializes in DllMain.  Sometimes
 *	things are easier on Win32.
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
Ns_MasterLock(void)
{
    EnterCriticalSection(&masterLock);
}

void
Ns_MasterUnlock(void)
{
    LeaveCriticalSection(&masterLock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockCreate --
 *
 *	Allocate and initialize a mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsLockAlloc(void)
{
    return NsAlloc(sizeof(Lock));
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockFree --
 *
 *	Free a mutex lock.
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
NsLockFree(void *lock)
{
    NsFree(lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockSet --
 *
 *	Set a mutex lock.
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
NsLockSet(void *lock)
{
    WinThread *wPtr = NULL;
    Lock *lockPtr = lock;
    int locked;

    do {
	SPINLOCK(&lockPtr->spinlock);
	locked = lockPtr->locked;
	if (!locked) {
	    lockPtr->locked = 1;
	} else {
	    if (wPtr == NULL) {
		wPtr = GetWinThread();
	    }
	    Queue(&lockPtr->waitPtr, wPtr);
	}
	SPINUNLOCK(&lockPtr->spinlock);
	if (locked && WaitForSingleObject(wPtr->event, INFINITE) != WAIT_OBJECT_0) {
	    NsThreadFatal("NsLockTry", "WaitForSingleObject", GetLastError());
	}
    } while (locked);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockTry --
 *
 *	Try to set a mutex lock once.
 *
 * Results:
 *	NS_OK if set, NS_TIMEOUT if lock already set.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(Lock *lock)
{
    Lock *lockPtr = lock;
    int busy;

    SPINLOCK(&lockPtr->spinlock);
    busy = lockPtr->locked;
    if (!busy) {
	lockPtr->locked = 1;
    }
    SPINUNLOCK(&lockPtr->spinlock);
    if (busy) {
	return 0;
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockUnset --
 *
 *	Unset a mutex lock.
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
NsLockUnset(Lock *lock)
{
    Lock *lockPtr = lock;
    WinThread *wPtr;

    SPINLOCK(&lockPtr->spinlock);
    lockPtr->locked = 0;
    wPtr = lockPtr->waitPtr;
    if (wPtr != NULL) {
	lockPtr->waitPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
    }
    SPINUNLOCK(&lockPtr->spinlock);

    /*
     * NB: It's safe to send the Wakeup() signal after spin unlock
     * because the waiting thread is in an infiniate wait.
     */

    if (wPtr != NULL) {
	Wakeup(wPtr, "NsLockUnset");
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

    cPtr = NsAlloc(sizeof(Cond));
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

    	NsFree(cPtr);
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
    WinThread	 *wPtr;

    SPINLOCK(&cPtr->spinlock);
    wPtr = cPtr->waitPtr;
    if (wPtr != NULL) {
	cPtr->waitPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
	wPtr->condwait = 0;

	/*
	 * NB: Unlike with MutexUnlock, the Wakeup() must be done
	 * before the spin unlock as the other thread may have 
	 * been in a timed wait which timed out.
	 */

	Wakeup(wPtr, "Ns_CondSignal");
    }
    SPINUNLOCK(&cPtr->spinlock);
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
    WinThread	 *wPtr;

    SPINLOCK(&cPtr->spinlock);

    /*
     * Set each thread to wake up the next thread on the
     * waiting list.  This results in a rolling wakeup
     * which should reduce lock contention as the threads
     * are awoken.
     */

    wPtr = cPtr->waitPtr;
    while (wPtr != NULL) {
	wPtr->wakeupPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
	wPtr->condwait = 0;
	wPtr = wPtr->wakeupPtr;
    }

    /*
     * Wake up the first thread to start the rolling 
     * wakeup.
     */

    wPtr = cPtr->waitPtr;
    if (wPtr != NULL) {
	cPtr->waitPtr = NULL;
	/* NB: See Wakeup() comment in Ns_CondSignal(). */
	Wakeup(wPtr, "Ns_CondBroadcast");
    }

    SPINUNLOCK(&cPtr->spinlock);
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
    SPINLOCK(&cPtr->spinlock);
    wPtr->condwait = 1;
    Queue(&cPtr->waitPtr, wPtr);
    SPINUNLOCK(&cPtr->spinlock);

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

    SPINLOCK(&cPtr->spinlock);
    if (!wPtr->condwait) {
	status = NS_OK;
    } else {
	status = NS_TIMEOUT;
	waitPtrPtr = &cPtr->waitPtr;
	while (*waitPtrPtr != wPtr) {
	    waitPtrPtr = &(*waitPtrPtr)->nextPtr;
	}
	*waitPtrPtr = wPtr->nextPtr;
	wPtr->nextPtr = NULL;
	wPtr->condwait = 0;
    }

    /*
     * Wakeup the next thread in a rolling broadcast if necessary.
     */

    if (wPtr->wakeupPtr != NULL) {
	Wakeup(wPtr->wakeupPtr, "Ns_CondTimedWait");
	wPtr->wakeupPtr = NULL;
    }
    SPINUNLOCK(&cPtr->spinlock);

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
    thrPtr->tid = GetCurrentThreadId();
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
	wPtr = NsAlloc(sizeof(WinThread));
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
    wPtr->nextPtr = wPtr->wakeupPtr = NULL;
    if (!ResetEvent(wPtr->event)) {
	NsThreadFatal("Queue", "ResetEvent", GetLastError());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Wakeup --
 *
 *	Wakeup a thread waiting on a mutex or condition queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread wakeup event is set.
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

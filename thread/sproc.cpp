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
 * sproc.c --
 *
 *	Interface routines for nsthreads using native SGI multiprocessing
 *	functions.  Basically an arena is created to hold locks,
 *  	threads are created using sproc(2) through a dedicated manager
 *  	process, conditions are signaled via a Unix signal, and a pointer
 *  	to the thread's context is stored in a portion of the PRDA, the
 *  	process data area.  This code is highly SGI specific although
 *  	the implementation and strategy is very similar to the Pthreads
 *  	interface on Linux (also known as LinuxThreads) where clone(2)
 *  	replaces the sproc(2) system call.  For background information see:
 *
 *  	sproc(2)   		Create new process
 *  	prctl(2)    		Process control
 *  	usinit(3P)  		Initialize shared arena
 *  	usnewlock(3P)  		Create new arena lock
 *	sigtimedwait(2)		Timed wait for signal
 *  	<sys/prcntl.h>		Process data area (PRDA) definition
 *
 */


#include "thread.h"
#include <sys/prctl.h>		/* prctl(), PRDA definition. */
#include <ulocks.h>		/* Arena locks. */
#include <mutex.h>		/* test_then_add. */
#include <sys/ioctl.h>
#include <sys/wait.h>

extern void	__exit(int);

/*
 * The following structure maintains the sproc-specific state of a
 * process thread including pointers for the run and condition
 * queues and a wakeup pointer for "rolling" condition broadcast.
 */

typedef struct Sproc {
    int     	   pid;     	/* Thread initialized pid. */
    struct Sproc  *nextRunPtr; 	/* Next starting, exiting, or running Sproc */
    struct Sproc  *nextWaitPtr;	/* Next Sproc in CondWait. */
    struct Sproc  *wakeupPtr;	/* Next Sproc to wakeup from CondWait. */
    struct Thread *thrPtr;  	/* Pointer to NsThread structure. */
    enum {  	    	    	/* State of sproc structure as follows: */
	SprocRunning,	    	/* Sproc is running freely. */
	SprocCondWait,	    	/* Sproc is in a condition wait. */
	SprocExited 	    	/* Sproc has exited and to be reaped. */
    } state;
} Sproc;

/*
 * The following structure defines a queue of threads in a condition wait.
 */

typedef struct {
    void   *lock;	/* Lock around Cond structure. */
    Sproc  *waitPtr;	/* First waiting Sproc or NULL. */
} Cond;

/*
 * The following structure defines the single critical section lock.
 */

struct {
    void    *lock;	/* Lock around structure. */
    int      owner;	/* Current owner. */
    int	     count;	/* Recursive lock depth. */
    usema_t *sema;	/* Semaphore to wakeup waiters. */
    int      nwait;	/* # of waiters. */
} master;

/*
 * The prdaPtrPtr pointer is declared static but is not actually shared by
 * all threads.  Instead, it's a pointer to a virtual address which always
 * points to the "per-process data area" (see <sys/prctl.h> for details). The
 * address of the current thread's Sproc process is stored at this location
 * at thread startup in SprocMain() and accessed via NsGetThread().
 * If you're interested in how per-thread context management would
 * be done on other platforms check out the LinuxThreads source
 * code.  For example, on Sparc Linux (and Solaris) a pointer to thread
 * context is stored in CPU register g7.
 */

static Sproc  **prdaPtrPtr = (Sproc **) (&((PRDA)->usr2_prda));

static Sproc   *firstStartPtr;	/* List of sprocs to be started. */
static int      mgrPipe[2];	/* Trigger pipe to wakeup manager. */
static int      mgrPid = -1;	/* Manager pid, -1 until first thread. */
static int      initPid = -1;	/* Initial thread pid, -1 until first thread. */

/*
 * Static functions defined in this file.
 */

static void     MgrThread(void *arg);
static void     MgrTrigger(void);
static void	SprocMain(void *arg, size_t stacksize);
static void     CatchCLD(int signal);
static void	CatchHUP(void);
static void	CheckHUP(void);
static usptr_t *GetArena(void);
static int      StartSproc(Sproc *sPtr);
static int	GetWakeup(Sproc *sPtr);
static void	SendWakeup(int pid);
static Sproc   *InitSproc(void);
static int      shutdownPending;

#define GETSPROC()	(*prdaPtrPtr ? *prdaPtrPtr : InitSproc())


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
    return "sproc";
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterLock --
 *
 *	Enter the single master critical section, initializing
 *	it the first time.
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
    static int initialized;
    int self = getpid();

    /*
     * Initialize the critical section on first use.  This is safe
     * because the first new thread can't be created without entering 
     * the master lock once.
     */

    if (!initialized) {
	master.lock = NsLockAlloc();
	master.sema = usnewsema(GetArena(), 0);
	if (master.sema == NULL) {
    	    NsThreadFatal("Ns_MasterLock", "usnewsema", errno);
	}
	master.count = 0;
	master.owner = -1;
	initialized = 1;
    }

    /*
     * Enter the critical section, waiting if necessary.
     */

    NsLockSet(master.lock);
    while (master.owner != self && master.count > 0) {
	++master.nwait;
    	NsLockUnset(master.lock);
	if (uspsema(master.sema) != 1) {
    	    NsThreadFatal("Ns_MasterUnlock", "uspsema", errno);
	}
    	NsLockSet(master.lock);
    }
    master.owner = self;
    ++master.count;
    NsLockUnset(master.lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterUnlock --
 *
 *	Leave the single master critical section.
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
Ns_MasterUnlock(void)
{
    int self = getpid();

    /*
     * Leave the critical section, waking up one waiter if necessary.
     */

    NsLockSet(master.lock);
    if (master.owner == self && --master.count == 0) {
	master.owner = -1;
	if (master.nwait > 0) {
	    --master.nwait;
	    if (usvsema(master.sema) != 0) {
    	    	NsThreadFatal("Ns_MasterUnlock", "usvsema", errno);
	    }
	}
    }
    NsLockUnset(master.lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockAlloc --
 *
 *	Allocate and initialize a mutex lock in the arena.
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
    ulock_t         lock;

    lock = usnewlock(GetArena());
    if (lock == NULL) {
    	NsThreadFatal("NsLockAlloc", "usnewlock", errno);
    }
    usinitlock(lock);
    return lock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockFree --
 *
 *	Free a mutex lock in the arena.
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
    usfreelock((ulock_t) lock, GetArena());
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
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
NsLockSet(void *lock)
{
    if (ussetlock((ulock_t) lock) == -1) {
	NsThreadFatal("NsLockSet", "ussetlock", errno);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockTry --
 *
 *	Try once to set a mutex lock.
 *
 * Results:
 *	1 if locked, 0 otherwise.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(void *lock)
{
    int locked;

    locked = uscsetlock((ulock_t) lock, 1);
    if (locked == -1) {
    	NsThreadFatal("NsLockTry", "uscsetlock", errno);
    }
    return locked;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockUnset --
 *
 *	Unlock a mutex in the arena.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Some other thread may resume.
 *
 *----------------------------------------------------------------------
 */

void
NsLockUnset(void *lock)
{
    if (usunsetlock((ulock_t) lock) != 0) {
	NsThreadFatal("NsLockUnset", "usunsetlock", errno);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Initialize a condition variable.  Note that this function is rarely
 *	called directly as static NULL condition variables are now self 
 *	initialized when first accessed.
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
    cPtr->lock = NsLockAlloc();
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
    Cond *cPtr = (Cond *) *condPtr;

    if (cPtr != NULL) {
    	NsLockFree(cPtr->lock);
	cPtr->waitPtr = NULL;
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
    Sproc   	 *sPtr;
    Cond         *cPtr = GETCOND(condPtr);
    int		  wakeup;

    NsLockSet(cPtr->lock);
    sPtr = cPtr->waitPtr;
    if (sPtr != NULL) {
	cPtr->waitPtr = sPtr->nextWaitPtr;
	wakeup = GetWakeup(sPtr);
    }
    NsLockUnset(cPtr->lock);
    if (sPtr != NULL) {
	SendWakeup(wakeup);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Broadcast a condition by resuming the first waiting thread.
 *	The first thread will then signal the next thread in when
 *	exiting Ns_CondTimedWait which is signal the next and so
 *	on resulting an a rolling wakeup which avoids lock contention
 *	from all thread waking up at once.
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
    Sproc   	  *sPtr;
    Cond          *cPtr = GETCOND(condPtr);
    int            wakeup;

    /*
     * Mark each thread to wakeup the next thread on the queue.
     */

    NsLockSet(cPtr->lock);
    sPtr = cPtr->waitPtr;
    while (sPtr != NULL) {
	sPtr->wakeupPtr = sPtr->nextWaitPtr;
	sPtr = sPtr->nextWaitPtr;
    }
    sPtr = cPtr->waitPtr;
    if (sPtr != NULL) {
	cPtr->waitPtr = NULL;
	wakeup = GetWakeup(sPtr);
    }
    NsLockUnset(cPtr->lock);
    if (sPtr != NULL) {
	SendWakeup(wakeup);
    }
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
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr, Ns_Time *timePtr)
{
    int status, wakeup;
    struct timespec ts;
    Cond           *cPtr;
    sigset_t        set;
    Sproc	   *sPtr, *wakeupPtr, **waitPtrPtr;
    Ns_Time 	    now, wait;

    /*
     * Block SIGHUP which is used as the sigtimedwait() signal.
     */
 
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigprocmask(SIG_BLOCK, &set, NULL);

    /*
     * Lock the condition and queue this thread for wakeup.
     */

    sPtr = GETSPROC();
    cPtr = GETCOND(condPtr);
    NsLockSet(cPtr->lock);
    waitPtrPtr = &cPtr->waitPtr;
    while (*waitPtrPtr != NULL) {
	waitPtrPtr = &(*waitPtrPtr)->nextWaitPtr;
    }
    *waitPtrPtr = sPtr;
    sPtr->nextWaitPtr = NULL;

    /*
     * Unlock the associated mutex and wait for a wakeup signal
     * or timeout.  Note that because the state of the sproc is check
     * and the relative timeout is recalculated on each interation of
     * the loop it's safe to receive the wakeup signal multiple times,
     * perhaps from a previously missed wakeup.
     */

    Ns_MutexUnlock(mutexPtr);

    sPtr->state = SprocCondWait;
    status = NS_OK;
    while (status == NS_OK && sPtr->state == SprocCondWait) {
        NsLockUnset(cPtr->lock);
        if (timePtr != NULL) {
	    Ns_GetTime(&now);
	    Ns_DiffTime(timePtr, &now, &wait);
	    if (wait.sec < 0 || (wait.sec == 0 && wait.usec <= 0)) {
		status = NS_TIMEOUT;
	    } else {
	    	ts.tv_sec = wait.sec;
	    	ts.tv_nsec = wait.usec * 1000;
	    }
        }
	if (status == NS_OK &&
		sigtimedwait(&set, NULL, timePtr ? &ts : NULL) == -1) {
            if (errno == EAGAIN) {
	    	status = NS_TIMEOUT;
	    } else if (errno != EINTR) {
		NsThreadFatal("Ns_CondTimedWait", "sigtimedwait", errno);
	    }
	}

	/*
	 * Check for parent death now as SIGHUP, the death signal is blocked.
	 */

	CheckHUP();
    	NsLockSet(cPtr->lock);
    }

    /*
     * On what appears to be a timeout, first check the thread state again
     * in case the signal arrived just before the lock could be re-aquired.
     * If so, reset the status to NS_OK.  Otherwise, remove this thread from
     * the condition queue and leave the status NS_TIMEOUT.
     */

    if (status == NS_TIMEOUT) {
	if (sPtr->state == SprocRunning) {
	    status = NS_OK;
	} else {
	    waitPtrPtr = &cPtr->waitPtr;
	    while (*waitPtrPtr != sPtr) {
		waitPtrPtr = &(*waitPtrPtr)->nextWaitPtr;
	    }
	    *waitPtrPtr = sPtr->nextWaitPtr;
	    sPtr->nextWaitPtr = NULL;
	    sPtr->state = SprocRunning;
	}
    }

    /*
     * Check for the next sproc to wakeup in the case of
     * a rolling broadcast (see Ns_CondBroadcast).
     */

    wakeupPtr = sPtr->wakeupPtr;
    if (wakeupPtr != NULL) {
	sPtr->wakeupPtr = NULL;
	wakeup = GetWakeup(wakeupPtr);
    }

    /*
     * Unlock the condition and lock the associated mutex.
     */
    
    NsLockUnset(cPtr->lock);

    /*
     * Signal the next process, if any, now that the lock is
     * released. 
     */

    if (wakeupPtr != NULL) {
	SendWakeup(wakeup);
    }

    Ns_MutexLock(mutexPtr);

    /*
     * Unblock SIGHUP which will deliver any pending death signal.
     */

    sigprocmask(SIG_UNBLOCK, &set, NULL);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetWakeup, SendWakeup --
 *
 *	Get/send a signal to wakeup a sproc from condition wait. The
 *	wakup is in two parts so the kill() can be sent after the lock
 *	is released.
 *
 * Results:
 *	GetWakeup:  Pid to pass to SendWakup.
 *
 * Side effects:
 *	GetWakeup:  Sets Sproc to running.
 *
 *----------------------------------------------------------------------
 */

static int
GetWakeup(Sproc *sPtr)
{
    sPtr->state = SprocRunning;
    return sPtr->pid;
}

static void
SendWakeup(int pid)
{
    if (kill(pid, SIGHUP) != 0) {
    	NsThreadError("SendWakeup: kill(%d, SIGHUP) failed: %s", 
	    pid, strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadCreate --
 *
 *	Sproc specific thread create function called by Ns_ThreadCreate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New process is queued for start by manager thread which itself
 *	is created when first needed.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    Sproc	*sPtr;
    int		 trigger = 0;

    /*
     * Create the manager sproc if necessary.
     */

    if (mgrPid < 0) {
    	static Sproc mgrSproc;
	
	initPid = getpid();
	if (pipe(mgrPipe) != 0) {
            NsThreadFatal("NsThreadCreate", "pipe", errno);
	}
        fcntl(mgrPipe[0], F_SETFD, 1);
        fcntl(mgrPipe[1], F_SETFD, 1);
	ns_signal(SIGCLD, CatchCLD);	/* NB: Trap exit of manager. */
	mgrSproc.thrPtr = NsNewThread();
	mgrSproc.thrPtr->proc = MgrThread;
	mgrSproc.thrPtr->stackSize = 8192;
	mgrSproc.state = SprocRunning;
	mgrPid = StartSproc(&mgrSproc);
    }
    
    /*
     * Allocate a new sproc and queue for start, triggering 
     * wakeup if necessary.
     */
     
    sPtr = NsAlloc(sizeof(Sproc));
    sPtr->thrPtr = thrPtr;
    sPtr->state = SprocRunning;

    Ns_MasterLock();
    if (firstStartPtr == NULL) {
	trigger = 1;
    }
    sPtr->nextRunPtr = firstStartPtr;
    firstStartPtr = sPtr;
    Ns_MasterUnlock();
    if (trigger) {
	MgrTrigger();
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *	Terminate a sproc processes, which will be reaped later by the
 *  	manager thread.  Note that __exit(2) is called instead of exit(3)
 *  	(which would kill all remaining threads - see below) or
 *	_exit(2) (which would bypass various profiling hooks).
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
    Sproc *sPtr = GETSPROC();

    if (sPtr->thrPtr != NULL) {
    	NsCleanupThread(sPtr->thrPtr);
    }
    Ns_MasterLock();
    sPtr->state = SprocExited;
    sPtr->thrPtr = NULL;
    Ns_MasterUnlock();
    __exit(0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsSetThread --
 *
 *	Sproc specific routine for setting a thread's data structure.
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
    Sproc *sPtr = GETSPROC();

    sPtr->thrPtr = thrPtr;
    thrPtr->tid = sPtr->pid;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread --
 *
 *	Sproc specific routine for getting a thread's structure. 
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
    Sproc *sPtr = GETSPROC();
    Thread *thrPtr;

    thrPtr = sPtr->thrPtr;
    if (thrPtr == NULL) {
	/* NB: Unlike pthread/win32, only possible for init thread. */
	thrPtr = NsNewThread();
	NsSetThread(thrPtr);
    }
    return thrPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * InitSproc --
 *
 *	Get the Sproc structure for the initial process.
 *	The sPtr is NULL for the initial thread because it never has
 *	a chance to be set in SprocMain as with subsequent threads.
 *	In this case, just return a static structure here, initializing 
 *	the pid and state elements the first time.  Note that it's unsafe
 *	to simply set the prdaPtrPtr for the initial thread because if
 *	this is done before the arena is created it appears to become
 *	a shared variable instead of a per-thread variable.
 *
 * Results:
 *	Pointer to static Sproc.
 *
 * Side effects:
 *	Sproc is initialized on first use.
 *
 *----------------------------------------------------------------------
 */

static Sproc *
InitSproc(void)
{
    static Sproc initSproc;

    if (initSproc.pid == 0) {
	initSproc.pid = getpid();
	initSproc.state = SprocRunning;
    }
    return &initSproc;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadYield --
 *
 *	Sproc specific thread yield.
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
    sginap(0);
}


/*
 *----------------------------------------------------------------------
 *
 * ns_sigmask --
 *
 *	Set the thread signal mask.
 *
 * Results:
 *	0 on success, otherwise an error code.
 *
 * Side effects:
 *	Previously blocked signals will be returned in oset if not null.
 *
 *----------------------------------------------------------------------
 */

int
ns_sigmask(int how, sigset_t * set, sigset_t * oset)
{
    if (sigprocmask(how, set, oset) != 0) {
	return errno;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * SprocMain --
 *
 *	Startup routine for sproc process threads.
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
SprocMain(void *arg, size_t stacksize)
{
    Sproc *sPtr = arg;

    *prdaPtrPtr = sPtr;
    sPtr->pid = getpid();
    CatchHUP();
    NsThreadMain(sPtr->thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetArena --
 *
 *	Return the SGI arena, creating it if necessary.  The arena
 *	is shared memory where locks are maintained.
 *
 * Results:
 *	Pointer to arena.
 *
 * Side effects:
 *	Arena is created the first time this routine is called.  Note
 *	this is safe because a mutex protects creating any new thread
 *	and the initialization of the mutex will ensure the arena is
 *	created before any additional thread could present a race
 *	condition.
 *
 *----------------------------------------------------------------------
 */

static int
GetOpt(char *env, int def, int min, int max)
{
    char *val;
    int n;

    val = getenv(env);
    if (val != NULL) {
	n = atoi(val);
	if (n >= min && n <= max) {
	    return n;
	}
	NsThreadError("ignored invalid %s: %s", env, val);
    }
    return def;
}

static usptr_t *
GetArena(void)
{
    static usptr_t *arenaPtr = NULL;

    if (arenaPtr == NULL) {
	int opt;

	opt = GetOpt("NS_THREAD_CONF_LOCKTYPE", US_NODEBUG,
		     US_NODEBUG, US_DEBUGPLUS);
	if (usconfig(CONF_LOCKTYPE, opt) < 0) {
            NsThreadFatal("GetArena", "usconfig(CONF_LOCKTYPE)", errno);
	}
	opt = GetOpt("NS_THREAD_CONF_INITUSERS", 100, 2, 10000);
	if (usconfig(CONF_INITUSERS, opt) < 0) {
            NsThreadFatal("GetArena", "usconfig(CONF_INITUSERS)", errno);
	}
	opt = GetOpt("NS_THREAD_CONF_INITSIZE", 256*1024, 64*1024, INT_MAX);
	if (usconfig(CONF_INITSIZE, opt) < 0) {
            NsThreadFatal("GetArena", "usconfig(CONF_INITSIZE)", errno);
	}
	arenaPtr = usinit("/dev/zero");
	if (arenaPtr == NULL) {
            NsThreadFatal("GetArena", "usinit", errno);
	}
    }

    return arenaPtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  CatchCLD --
 *
 *	SIGCLD signal handler for the initial process which watches
 *	for death of the manager thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Initial process will exit immediately if manager exits.
 *
 *----------------------------------------------------------------------
 */

static void
CatchCLD(int signal)
{
    if (waitpid(mgrPid, NULL, WNOHANG) == mgrPid) {
    	_exit(0);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CatchHUP --
 *
 *	Install the SIGHUP signal handler.  SIGHUP is used to notify a
 *	child sproc process when the manager thread exits or to notify
 *	the manager thread if the initial process thread exits.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Signal handler is set for this thread.
 *
 *----------------------------------------------------------------------
 */

static void
CatchHUP(void)
{
    struct sigaction sa;
    sigset_t set;

    /*
     * Install the CheckHUP signal handler for SIGHUP.
     */

    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = CheckHUP;
    sigaction(SIGHUP, &sa, NULL);

    /*
     * Have the OS send this process a SIGHUP when the parent process
     * dies.  This is a SGI-specific system call which allows all threads,
     * to catch a death of the initial or manager threads.  Note that
     * the normal order of shutdown is to kill and wait for all threads
     * directly through exit() (see below).
     */

    prctl(PR_TERMCHILD);

    /*
     * Unblock SIGHUP now that the handler is installed.
     */

    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigprocmask(SIG_UNBLOCK, &set, NULL);

    /*
     * Reset SIGCLD from the handler used by the thread manager.
     */

    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGCLD, &sa, NULL);

    /*
     * Call CheckHUP now in case we just missed a SIGHUP.
     */

    CheckHUP();
}


/*
 *----------------------------------------------------------------------
 *
 * CheckHUP --
 *
 *	SIGHUP signal handler which checks for death of the manager or
 *	initial process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Process will exit if it appears the thread manager process has
 *	died.  Note that a thread existing is this way will not dump
 *	it's profiling data.  To ensure profiling, threads must call
 *	NsThreadExit.
 *
 *----------------------------------------------------------------------
 */

static void
CheckHUP(void)
{
    if (getppid() == 1 && getpid() != initPid) {
	_exit(0);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StartSproc --
 *
 *	Start a new sproc process, called by the manager thread and
 *  	the initial thread when creating the manager.
 *
 * Results:
 *	New process id.
 *
 * Side effects:
 *	Process will be created with all attributes shared.
 *
 *----------------------------------------------------------------------
 */

static int
StartSproc(Sproc *sPtr)
{
    int pid;

    pid = sprocsp(SprocMain, PR_SALL, (void *) sPtr,
    	    	    	     NULL, sPtr->thrPtr->stackSize);
    if (pid < 0) {
	NsThreadFatal("StartSproc", "sprocsp", errno);
    }
    return pid;
}


/*
 *----------------------------------------------------------------------
 *
 * MgrThread --
 *
 *	Startup routine for the manager process.  This process is
 *	sproc'ed the first time NsThreadCreate is called and is
 *	responsible for sproc'ing and reaping all future process
 *	threads.  This manager thread is used instead of having
 *	processes sproc additional processes directly to ensure
 *	a single process is the parent of all other processes which
 *	enables the death of any thread to be noticed immediately.
 *  	The trigger pipe is used as a simple semaphore to wakeup
 *  	the manager either when new sprocs are to be created,
 *  	or dead sprocs are to be reaped. A pipe is used instead
 *  	of a condition because a write() is a safe operation
 *  	to perform in the SIGCHLD signal handler.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes will be endlessly sproc'ed and reaped.
 *
 *----------------------------------------------------------------------
 */

static void
MgrThread(void *arg)
{
    int             status, pid, new;
    char    	    c;
    Sproc	   *sPtr, *nextPtr, **sPtrPtr;
    Sproc          *runPtr, *startPtr;
    struct sigaction sa;
    sigset_t        set;

    Ns_ThreadSetName("-sproc-");

    /*
     * Watch for parent death like other threads.
     */

    CatchHUP();

    /*
     * Wake up the manager on thread exit.
     */

    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = MgrTrigger;
    sigaction(SIGCLD, &sa, NULL);

    /*
     * Endlessly wait for wakeup messages on the trigger pipe.
     */

    runPtr = NULL;
    while (read(mgrPipe[0], &c, 1)) {
    	Ns_MasterLock();
	if (shutdownPending) {
	    Ns_MasterUnlock();
	    break;
	}
	startPtr = firstStartPtr;
	firstStartPtr = NULL;
	Ns_MasterUnlock();

        /*
         * Check for exited threads to be reaped. If the thread died
         * from a signal or because user code called _exit(), simply
         * kill the thread manager which will cause all other threads
         * to receive a SIGHUP and die when the CatchHUP signal handler
	 * is invoked.  On error, call NsThreadError and _exit()
	 * instead of NsThreadAbort to avoid over-writing a core file
	 * written by a crashed thread.
         */

        while ((pid = waitpid(0, &status, WNOHANG)) > 0) {
            if (WIFSIGNALED(status)) {
		NsThreadError("sproc %d killed by signal %d", pid,
			WTERMSIG(status));
		_exit(1);
            }
	    if (WEXITSTATUS(status) != 0) {
		NsThreadError("sproc %d exited with non-zero status %d",
		    pid, WEXITSTATUS(status));
		_exit(1);
	    }
    	    sPtrPtr = &runPtr;
	    while ((*sPtrPtr)->pid != pid) {
	    	sPtrPtr = &(*sPtrPtr)->nextRunPtr;
	    }
    	    sPtr = *sPtrPtr;
	    *sPtrPtr = sPtr->nextRunPtr;
	    Ns_MasterLock();
	    if (sPtr->state != SprocExited) {
		NsThreadError("sproc %d called _exit() directly", pid);
		_exit(1);
	    }
	    Ns_MasterUnlock();
	    NsFree(sPtr);
        }

        /*
         * Start any new threads.
         */

	while ((sPtr = startPtr) != NULL) {
	    startPtr = sPtr->nextRunPtr;
	    sPtr->nextRunPtr = runPtr;
	    runPtr = sPtr;
	    StartSproc(sPtr);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * MgrTrigger --
 *
 *	Wakeup the manager thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Manager thread will wakeup if it's in a blocking read.
 *
 *----------------------------------------------------------------------
 */

static void
MgrTrigger(void)
{
    if (write(mgrPipe[1], "", 1) != 1) {
        NsThreadAbort("trigger write() faild: %s", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * exit --
 *
 *	Replacement for standard exit(3) routine to kill and wait
 *  	for all threads to die before calling the real libc
 *	__exit(3) routine.  It's important to wait for all
 *	threads to leave the share group to ensure exit(3) cleanup
 *	is performed (i.e., run atexit(3) procs, close streams, etc.).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All other threads are almost instantly killed.
 *
 *----------------------------------------------------------------------
 */

void
exit(int status)
{
    int maxticks, nshare, pid, nmax;
    Sproc *sPtr;

    if (mgrPid > 0) {

	/*
	 * Ignore the manager death signal and then signal
	 * shutdown pending.
	 */

	pid = getpid();
	if (pid == initPid) {
	    ns_signal(SIGCLD, SIG_IGN);
	} else {
	    ns_signal(SIGHUP, SIG_IGN);
	}
	Ns_MasterLock();
	shutdownPending = 1;
	Ns_MasterUnlock();
	MgrTrigger();

	/*
	 * Spin wait up to two seconds for all other threads to leave
	 * the share group.
	 */

	if (pid == initPid) {
	    nmax = 1;
	} else {
	    nmax = 2;
	}
	maxticks = CLK_TCK * 2;
	while ((nshare = prctl(PR_GETNSHARE)) > nmax && --maxticks >= 0) {
	    sginap(2);
	}
	if (nshare > nmax) {
	    NsThreadError("warning: exit timeout: %d sprocs remain in share group",
		nshare);
	}
    }
    __exit(status);
}


/*
 *----------------------------------------------------------------------
 *
 * fork --
 *
 *	Fork wrapper to forget about the thread manager and update
 *	the pid's in the child.
 *
 * Results:
 *	See fork(2).
 *
 * Side effects:
 *	Child will not have access to parent's thread manager.
 *
 *----------------------------------------------------------------------
 */

pid_t
fork(void)
{
    extern pid_t _fork(void);
    pid_t pid;
    Sproc *sPtr;

    pid = _fork();
    if (pid == 0) {

	/*
	 * Close off the thread manager pipe if opened.
	 */

	if (mgrPid != -1) {
    	    mgrPid = -1;
    	    close(mgrPipe[0]);
    	    close(mgrPipe[1]);
	}

	/*
	 * Update the new process pid.
	 */

    	sPtr = GETSPROC();
	sPtr->pid = getpid();
	if (sPtr->thrPtr != NULL) {
	    sPtr->thrPtr->tid = sPtr->pid;
	}
    }
    return pid;
}

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
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <mutex.h>

extern void	__exit(int);

/*
 * The following structure maintains the sproc-specific state of a
 * process thread including queue pointers for startup, exit, and
 * condition wait and the process id.  The pid is stored in two
 * locations because the order of update and visibility of a single
 * update cannot be guaranteed.
 */

typedef struct Sproc {
    int     	   pid;     	/* Thread initialized pid. */
    int     	   startPid;	/* Manager initialized pid. */
    struct Sproc  *nextPtr; 	/* Pointer to next starting, free,
    	    	    	    	 * or running Sproc */
    struct Thread *thrPtr;  	/* Pointer to NsThread structure. */
    struct Sproc  *prevWaitPtr; /* Previous Sproc in CondWait. */
    struct Sproc  *nextWaitPtr; /* Next Sproc in CondWait. */
    struct Sproc  *nextWakeupPtr; /* Next Sproc in CondWait. */
    enum {  	    	    	/* State of sproc structure as follows: */
	SprocRunning,	    	/* Sproc is running freely. */
	SprocStarting,	    	/* Sproc is starting, not yet initialized. */
	SprocCondWait,	    	/* Sproc is in a condition wait. */
	SprocExited 	    	/* Sproc has exited and to be reaped. */
    } state;
} Sproc;

/*
 * The following structure defines a queue of threads in a condition wait.
 */

typedef struct {
    Ns_Mutex    lock;	    	/* Lock around Cond structure. */
    Sproc      *firstWaitPtr;	/* First waiting Sproc or NULL. */
    Sproc      *lastWaitPtr;	/* Last waiting Sproc or NULL. */
} Cond;

/*
 * The prdaPtrPtr pointer is declared static but is not actually shared by
 * all threads.  Instead, it's a pointer to a virtual address which always
 * points to the "per-process data area" (see <sys/prctl.h> for details). The
 * address of the current thread's Sproc process is stored at this location
 * at thread startup in SprocMain() and accessed via NsGetThread2() called
 * by NsGetThread().  If you're interested in how per-thread context
 * management would be done on other platforms check out the LinuxThreads
 * source code.  For example, on Sparc Linux (and Solaris) a pointer to
 * thread context is stored in CPU register #6 and on Intel Linux it's
 * calculated based on the known spacing of thread context mmaped()'ed at
 * high virtual memory addresses.
 */

static Sproc  **prdaPtrPtr = (Sproc **) (&((PRDA)->usr2_prda));

static Sproc   *firstStartPtr;	/* List of sprocs to be started. */
static Ns_Mutex mgrLock;	/* Lock around mgrCond and sproc lists. */
static Ns_Cond  mgrCond;	/* Thread/manager signal condition. */
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
static void     StartSproc(Sproc *sPtr);
static int	GetWakeup(Sproc *sPtr);
static void	WakeupSproc(int pid);
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
 * NsMutexInit --
 *
 *	Allocate and initialize a mutex in the arena.  Note this function
 *	is rarley called directly as static NULL mutexes are now self
 *  	initalized when first locked.
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
    ulock_t         lock;
    usptr_t	   *arena = GetArena();

    lock = usnewlock(arena);
    if (lock == NULL) {
    	NsThreadFatal("Ns_MutexInit", "usnewlock", errno);
    }
    usinitlock(lock);
    *lockPtr = (void *) lock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexDestroy --
 *
 *	Destroy a mutex in the arena.  Note this function is almost never
 *	used as mutexes typically exist until the process exits.
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
    usptr_t	   *arena = GetArena();

    usfreelock((ulock_t) *lockPtr, arena);
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexLock --
 *
 *	Lock a mutex in the arena.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Lock is aquired after a possible spin or blocking wait and
 *  	any global memory modified until the cooresponding Ns_MutexUnlock
 *  	will be made visible to all processors.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexLock(void **lockPtr)
{
    if (ussetlock((ulock_t) *lockPtr) == -1) {
	NsThreadFatal("Ns_MutexLock", "ussetlock", errno);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexTryLock --
 *
 *	Try once to aquire a mutex in the arena.
 *
 * Results:
 *	NS_OK if locked, NS_TIMEOUT otherwise.
 *
 * Side effects:
 *  	See Ns_MutexLock.
 *
 *----------------------------------------------------------------------
 */

int
NsMutexTryLock(void **lockPtr)
{
    int locked;

    locked = uscsetlock((ulock_t) *lockPtr, 1);
    if (locked == -1) {
    	NsThreadFatal("Ns_MutexTryLock", "uscsetlock", errno);
    } else if (locked == 0) {
	return NS_TIMEOUT;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsMutexUnlock --
 *
 *	Unlock a mutex in the arena.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Lock is released, some other thread is possibly resumed,
 *  	and visibility of any modified global data is guaranteed
 *  	in all processors.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexUnlock(void **lockPtr)
{
    if (usunsetlock((ulock_t) *lockPtr) != 0) {
	NsThreadFatal("Ns_MutexUnlock", "usunsetlock", errno);
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
    char	   name[32];
    static unsigned long nextid;

    sprintf(name, "%lu", test_then_add(&nextid, 1));
    cPtr = ns_malloc(sizeof(Cond));
    Ns_MutexInit(&cPtr->lock);
    Ns_MutexSetName2(&cPtr->lock, "nsthread:cond", name);
    cPtr->firstWaitPtr = cPtr->lastWaitPtr = NULL;
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
	cPtr->firstWaitPtr = cPtr->lastWaitPtr = NULL;
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
    Sproc   	 *sPtr;
    Cond         *cPtr = GETCOND(condPtr);
    int		  wakeup;

    Ns_MutexLock(&cPtr->lock);
    sPtr = cPtr->firstWaitPtr;
    if (sPtr != NULL) {
    	cPtr->firstWaitPtr = sPtr->nextWaitPtr;
	if (cPtr->lastWaitPtr == sPtr) {
	    cPtr->lastWaitPtr = NULL;
	}
	sPtr->nextWakeupPtr = NULL;
	wakeup = GetWakeup(sPtr);
    }
    Ns_MutexUnlock(&cPtr->lock);
    if (sPtr != NULL) {
	WakeupSproc(wakeup);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Broadcast a condition by resuming the first waiting thread.
 *	The first thread will then signal the next thread in
 *	Ns_CondTimedWait.
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

    Ns_MutexLock(&cPtr->lock);
    sPtr = cPtr->firstWaitPtr;
    while (sPtr != NULL) {
	sPtr->nextWakeupPtr = sPtr->nextWaitPtr;
	sPtr = sPtr->nextWaitPtr;
    }
    sPtr = cPtr->firstWaitPtr;
    if (sPtr != NULL) {
	cPtr->firstWaitPtr = cPtr->lastWaitPtr = NULL;
	wakeup = GetWakeup(sPtr);
    }
    Ns_MutexUnlock(&cPtr->lock);
    if (sPtr != NULL) {
	WakeupSproc(wakeup);
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
    Sproc	   *sPtr, *nextPtr;
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
    Ns_MutexLock(&cPtr->lock);
    sPtr->nextWaitPtr = NULL;
    sPtr->prevWaitPtr = cPtr->lastWaitPtr;
    cPtr->lastWaitPtr = sPtr;
    if (sPtr->prevWaitPtr != NULL) {
        sPtr->prevWaitPtr->nextWaitPtr = sPtr;
    }
    if (cPtr->firstWaitPtr == NULL) {
        cPtr->firstWaitPtr = sPtr;
    }

    /*
     * Unlock the coordinating mutex and wait for a wakeup signal
     * or timeout.  Note that because the state of the sproc is check
     * and the relative timeout is recalculated on each interation of
     * the loop it's safe to receive the wakeup signal multiple times,
     * perhaps from a previously missed wakeup.
     */

    Ns_MutexUnlock(mutexPtr);

    sPtr->state = SprocCondWait;
    status = NS_OK;
    while (status == NS_OK && sPtr->state == SprocCondWait) {
        Ns_MutexUnlock(&cPtr->lock);
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

        Ns_MutexLock(&cPtr->lock);
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
            if (cPtr->firstWaitPtr == sPtr) {
                cPtr->firstWaitPtr = sPtr->nextWaitPtr;
            } else {
                sPtr->prevWaitPtr->nextWaitPtr = sPtr->nextWaitPtr;
            }
            if (cPtr->lastWaitPtr == sPtr) {
                cPtr->lastWaitPtr = sPtr->prevWaitPtr;
            } else if (sPtr->nextWaitPtr != NULL) {
                sPtr->nextWaitPtr->prevWaitPtr = sPtr->prevWaitPtr;
            }
	    sPtr->nextWaitPtr = sPtr->prevWaitPtr = NULL;
	    sPtr->state = SprocRunning;
	}
    }

    /*
     * Check for the next sproc to wakeup in the case of
     * a broadcast.
     */

    nextPtr = sPtr->nextWakeupPtr;
    if (nextPtr != NULL) {
	sPtr->nextWakeupPtr = NULL;
	wakeup = GetWakeup(nextPtr);
    }

    /*
     * Unlock the condition and lock the coordinating mutex.
     */
    
    Ns_MutexUnlock(&cPtr->lock);

    /*
     * Signal the next process, if any, now that the lock is
     * released. 
     */

    if (nextPtr != NULL) {
	WakeupSproc(wakeup);
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
 * WakeupSproc --
 *
 *	Wakeup a process sleeping on a condition variable queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	First process is removed from the queue, marked running and
 *	signaled with SIGHUP.
 *
 *----------------------------------------------------------------------
 */

static void
WakeupSproc(int pid)
{
    if (kill(pid, SIGHUP) != 0) {
    	NsThreadError("Wakeup", "kill", errno);
    }
}

static int
GetWakeup(Sproc *sPtr)
{
    sPtr->state = SprocRunning;
    return sPtr->pid;
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
 *	New process is sproc'ed by manager thread.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    Sproc	*sPtr;

    Ns_MutexLock(&mgrLock);

    /*
     * Create the manager sproc if necessary.
     */

    if (mgrPid < 0) {
    	static Sproc mgrSproc;
	
	Ns_MutexSetName2(&mgrLock, "nsthread", "sprocmgr");
	initPid = getpid();
	if (pipe(mgrPipe) != 0) {
            NsThreadFatal("NsThreadCreate", "pipe", errno);
	}
	ns_signal(SIGCLD, CatchCLD);	/* NB: Trap exit of manager. */
	mgrSproc.thrPtr = NsNewThread(MgrThread, NULL, 8192, 0);
	mgrSproc.state = SprocRunning;
	StartSproc(&mgrSproc);
	mgrPid = mgrSproc.startPid;
    }
    
    /*
     * Allocate a new sproc, queue for start, trigger the manager,
     * and wait for startup to complete.
     */
     
    sPtr = ns_calloc(1, sizeof(Sproc));
    sPtr->thrPtr = thrPtr;
    if (thrPtr->flags & NS_THREAD_DETACHED) {
    	sPtr->state = SprocRunning;
    } else {
    	sPtr->state = SprocStarting;
    }
    if (firstStartPtr == NULL) {
	MgrTrigger();
    }
    sPtr->nextPtr = firstStartPtr;
    firstStartPtr = sPtr;
    while (sPtr->state == SprocStarting) {
        Ns_CondWait(&mgrCond, &mgrLock);
    }
    Ns_MutexUnlock(&mgrLock);
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

    Ns_MutexLock(&mgrLock);
    sPtr->state = SprocExited;
    sPtr->thrPtr = NULL;
    Ns_MutexUnlock(&mgrLock);
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
NsSetThread(Thread *thisPtr)
{
    Sproc *sPtr = GETSPROC();

    thisPtr->tid = sPtr->pid;
    sPtr->thrPtr = thisPtr;
    NsSetThread2(thisPtr);
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

    return (sPtr->thrPtr ? sPtr->thrPtr : NsGetThread2());
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
	initSproc.pid = initSproc.startPid = getpid();
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
 *	Start condition may be signaled.
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

    /*
     * Signal the parent of this thread that initialization is
     * complete.
     */

    Ns_MutexLock(&mgrLock);
    if (sPtr->state == SprocStarting) {
    	sPtr->state = SprocRunning;
    	Ns_CondBroadcast(&mgrCond);
    }
    Ns_MutexUnlock(&mgrLock);

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
 *	None.
 *
 * Side effects:
 *	Process will be created with all attributes shared.
 *
 *----------------------------------------------------------------------
 */

static void
StartSproc(Sproc *sPtr)
{
    sPtr->startPid = sprocsp(SprocMain, PR_SALL, (void *) sPtr,
    	    	    	     NULL, sPtr->thrPtr->stackSize);
    if (sPtr->startPid < 0) {
	NsThreadFatal("StartSproc", "sprocsp", errno);
    }
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
    	Ns_MutexLock(&mgrLock);
	if (shutdownPending) {
	    Ns_MutexUnlock(&mgrLock);
	    break;
	}
	startPtr = firstStartPtr;
	firstStartPtr = NULL;
	Ns_MutexUnlock(&mgrLock);

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
	    while ((*sPtrPtr)->startPid != pid) {
	    	sPtrPtr = &(*sPtrPtr)->nextPtr;
	    }
    	    sPtr = *sPtrPtr;
	    *sPtrPtr = sPtr->nextPtr;
	    Ns_MutexLock(&mgrLock);
	    if (sPtr->state != SprocExited) {
		NsThreadError("sproc %d called _exit() directly", pid);
		_exit(1);
	    }
	    Ns_MutexUnlock(&mgrLock);
	    ns_free(sPtr);
        }

        /*
         * Start any new threads.
         */

	while ((sPtr = startPtr) != NULL) {
	    startPtr = sPtr->nextPtr;
	    sPtr->nextPtr = runPtr;
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
	Ns_MutexLock(&mgrLock);
	shutdownPending = 1;
	Ns_MutexUnlock(&mgrLock);
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
 *	Fork wrapper to forget about the thread manager in the child.
 *
 * Results:
 *	See fork(2).
 *
 * Side effects:
 *	Child will not have access to thread manager.
 *
 *----------------------------------------------------------------------
 */

pid_t
fork(void)
{
    extern pid_t _fork(void);
    pid_t pid;

    pid = _fork();
    if (pid == 0 && mgrPid != -1) {
    	mgrPid = -1;
    	close(mgrPipe[0]);
    	close(mgrPipe[1]);
    }
    return pid;
}

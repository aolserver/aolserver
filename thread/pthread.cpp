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
 * pthread.c --
 *
 *	Interface routines for nsthreads using Unix pthreads.
 */

#include "thread.h"
#include <pthread.h>

/*
 * This interface supports both Draft4 Pthreads on HP/10 and final
 * Posix 1003.1c Pthreads on all other platforms.  Because NsThreads
 * implements most higher level functions directly (tls, semaphore,
 * etc.), only a subset of the pthread routines are required which
 * reduces the differences between the two interfaces to little more
 * than how errors are returned (error result vs. -1 and errno), a
 * few function and argument type changes, and special treatment
 * for the pthread_mutex_trylock, pthread_cond_timedwait, and
 * pthread_getspecific routines.
 */
 
#ifndef HAVE_PTHREAD_D4
#include <sched.h>		/* sched_yield() */
#define ERRLOCKOK(e)		((e) == 0)
#define ERRLOCKBUSY(e)		((e) == EBUSY)
#define ERRTIMEDOUT(e)		((e) == ETIMEDOUT)
#define MUTEX_INIT_ATTR         0
#define COND_INIT_ATTR          0
#else
#define ERRLOCKOK(e)		((e) == 1)
#define ERRLOCKBUSY(e)		((e) == 0)
#define ERRTIMEDOUT(e)		(((e) != 0) && (errno == EAGAIN))
#define NsThreadFatal(n,p,e)	NsThreadFatal((n),(p),(errno))
#define pthread_detach(t)	pthread_detach(&(t))
#define pthread_create(t,a,p,c) pthread_create((t), *(a), (p), (c))
#define pthread_key_create  	pthread_keycreate
#define pthread_attr_init       pthread_attr_create
#define pthread_attr_destroy    pthread_attr_delete
#define pthread_sigmask		sigprocmask
#define sched_yield		pthread_yield
#define MUTEX_INIT_ATTR         pthread_mutexattr_default
#define COND_INIT_ATTR          pthread_condattr_default
#define PTHREAD_ONCE_INIT	pthread_once_init
#endif
#if defined(MACOSX) 
#define pthread_sigmask                sigprocmask
#endif

/*
 * The following structure and one-time init callback are used
 * for the pthread-specific master lock.
 */

struct {
    int		    count;  /* Recursive lock count. */
    pthread_t	    owner;  /* Current lock owner. */
    pthread_mutex_t lock;   /* Lock for master structure. */
    pthread_cond_t  cond;   /* Condition to wakeup waiter. */
} master;

static void InitMaster(void);

/*
 * The following pthread tls key and callback are used to
 * maintain the single slot for storing nsthreads context.
 */

static pthread_key_t GetKey(void);
static void CleanupThread(void *arg);


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
    return "pthread";
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterLock --
 *
 *	Enter the single master critical section lock.
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
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    static char *func = "Ns_MasterLock";
    pthread_t self = pthread_self();
    int err;

    /*
     * Initialize the master lock and condition the first time.
     */

    err = pthread_once(&once, InitMaster);
    if (err != 0) {
        NsThreadFatal(func, "pthread_once", err);
    }

    /*
     * Enter the critical section.
     */

    err = pthread_mutex_lock(&master.lock);
    if (err != 0) {
        NsThreadFatal(func, "pthread_mutex_lock", err);
    }
    while (master.owner != self && master.count > 0) {
	pthread_cond_wait(&master.cond, &master.lock);
	if (err != 0) {
	    NsThreadFatal(func, "pthread_cond_wait", err);
	}
    }
    master.owner = self;
    ++master.count;
    err = pthread_mutex_unlock(&master.lock);
    if (err != 0) {
        NsThreadFatal(func, "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterUnlock --
 *
 *	Leave the single master critical section lock.
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
    static char *func = "Ns_MasterUnlock";
    pthread_t self = pthread_self();
    int err;

    err = pthread_mutex_lock(&master.lock);
    if (err != 0) {
        NsThreadFatal(func, "pthread_mutex_lock", err);
    }
    if (master.owner == self && --master.count == 0) {
	master.owner = 0;
	err = pthread_cond_signal(&master.cond);
	if (err != 0) {
	    NsThreadFatal(func, "pthread_cond_signal", err);
	}
    }
    err = pthread_mutex_unlock(&master.lock);
    if (err != 0) {
        NsThreadFatal(func, "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockAlloc --
 *
 *	Allocate a mutex lock.
 *
 * Results:
 *	Pointer to pthread lock.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsLockAlloc(void)
{
    static char *func = "NsMutexInit";
    pthread_mutex_t *lockPtr;
#ifdef USE_PTHREAD_PSHARED
    pthread_mutexattr_t attr;
#endif
    int		     err;

    lockPtr = NsAlloc(sizeof(pthread_mutex_t));
#ifndef USE_PTHREAD_PSHARED
    err = pthread_mutex_init(lockPtr, MUTEX_INIT_ATTR);
#else
    err = pthread_mutexattr_init(&attr);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_mutexattr_init", err);
    }
    err = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_mutexattr_setpshared", err);
    }
    err = pthread_mutex_init(lockPtr, &attr);
#endif
    if (err != 0) {
    	NsThreadFatal(func, "ptread_mutex_init", err);
    }
#ifdef USE_PTHREAD_PSHARED
    err = pthread_mutexattr_destroy(&attr);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_mutexattr_destroy", err);
    }
#endif
    return lockPtr;
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
    pthread_mutex_t *lockPtr = lock;
    int		     err;

    err = pthread_mutex_destroy(lockPtr);
    if (err != 0) {
        NsThreadFatal("NsMutexDestroy", "ptread_mutex_destroy", err);
    }
    NsFree(lockPtr);
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
 *	See pthread_mutex_lock.
 *
 *----------------------------------------------------------------------
 */

void
NsLockSet(void *lock)
{
    pthread_mutex_t *lockPtr = lock;
    int		     err;

    err = pthread_mutex_lock(lockPtr);
    if (err != 0) {
    	NsThreadFatal("NsMutexLock", "ptread_mutex_lock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockTry --
 *
 *	Try to set a mutex lock once.
 *
 * Results:
 *	1 if locked, 0 if lock already held.
 *
 * Side effects:
 *	See pthread_mutex_trylock.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(void *lock)
{
    pthread_mutex_t *lockPtr = lock;
    int		     err;

    err = pthread_mutex_trylock(lockPtr);
    if (ERRLOCKBUSY(err)) {
	return 0;
    } else if (!(ERRLOCKOK(err))) {
    	NsThreadFatal("NsMutexTryLock", "ptread_mutex_trylock", err);
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockUnlock --
 *
 *	Release a mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_mutex_unlock.
 *
 *----------------------------------------------------------------------
 */

void
NsLockUnset(void *lock)
{
    pthread_mutex_t *lockPtr = lock;
    int              err;

    err = pthread_mutex_unlock(lockPtr);
    if (err != 0) {
    	NsThreadFatal("NsMutexUnlock", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Pthread condition variable initialization.  Note this routine
 *	isn't used directly very often as static condition variables 
 *	are now self initialized when first used.
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
    static char *func = "Ns_CondInit";
    pthread_cond_t *cond;
    int             err;
#ifdef USE_PTHREAD_PSHARED
    pthread_condattr_t attr;
#endif

    cond = NsAlloc(sizeof(pthread_cond_t));
#ifndef USE_PTHREAD_PSHARED
    err = pthread_cond_init(cond, COND_INIT_ATTR);
#else
    err = pthread_condattr_init(&attr);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_condattr_init", err);
    }
    err = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_condattr_setpshared", err);
    }
    err = pthread_cond_init(cond, &attr);
#endif
    if (err != 0) {
    	NsThreadFatal(func, "pthread_cond_init", err);
    }
#ifdef USE_PTHREAD_PSHARED
    err = pthread_condattr_destroy(&attr);
    if (err != 0) {
    	NsThreadFatal(func, "ptread_condattr_destroy", err);
    }
#endif
    *condPtr = (Ns_Cond) cond;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	Pthread condition destroy.  Note this routine is almost never
 *	used as condition variables normally exist in memory until
 *	the process exits.
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
    pthread_cond_t *cond = GETCOND(condPtr);
    int             err;

    err = pthread_cond_destroy(cond);
    if (err != 0) {
    	NsThreadFatal("Ns_CondDestroy", "pthread_cond_destroy", err);
    }
    NsFree(cond);
    *condPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondSignal --
 *
 *	Pthread condition signal.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_signal.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *condPtr)
{
    pthread_cond_t *cond = GETCOND(condPtr);
    int             err;

    err = pthread_cond_signal(cond);
    if (err != 0) {
        NsThreadFatal("Ns_CondSignal", "pthread_cond_signal", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondBroadcast --
 *
 *	Pthread condition broadcast.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_broadcast.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *condPtr)
{
    pthread_cond_t *cond = GETCOND(condPtr);
    int             err;

    err = pthread_cond_broadcast(cond);
    if (err != 0) {
        NsThreadFatal("Ns_CondBroadcast", "pthread_cond_broadcast", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondWait --
 *
 *	Pthread indefinite condition wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_wait.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr)
{
    pthread_cond_t  *cond;
    pthread_mutex_t *lockPtr;
    Thread	    *ownerPtr;
    Mutex	    *mPtr;
    int              err;

    cond = GETCOND(condPtr);
    mPtr = GETMUTEX(mutexPtr);

    /*
     * Save and then clear the outer mutex owner field before
     * pthread_cond_wait releases the lock in case lock metering
     * is enabled.
     */

    ownerPtr = mPtr->ownerPtr;
    mPtr->ownerPtr = NULL;
    lockPtr = mPtr->lock;
    err = pthread_cond_wait(cond, lockPtr);
#ifdef HAVE_ETIME_BUG
    /*
     * On Solaris, we have noticed that when the condition and/or
     * mutex are process-shared instead of process-private that
     * pthread_cond_wait may incorrectly return ETIME.  Because
     * we're not sure why ETIME is being returned (perhaps it's
     * from an underlying _lwp_cond_timedwait???), we allow
     * the condition to return.  This should be o.k. because
     * properly written condition code must be in a while
     * loop capable of handling spurious wakeups.
     */

    if (err == ETIME) {
	err = 0;
    }
#endif
    if (err != 0) {
	NsThreadFatal("Ns_CondWait", "pthread_cond_wait", err);
    }

    /*
     * Restore the outer mutex owner and increment the lock count.
     */

    mPtr->ownerPtr = ownerPtr;
    ++mPtr->nlock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondTimedWait --
 *
 *	Pthread absolute time wait.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_cond_timewait.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr, Ns_Time *timePtr)
{
    pthread_cond_t  *cond;
    pthread_mutex_t *lockPtr;
    Thread	    *ownerPtr;
    Mutex	    *mPtr;
    int              err, status;
    struct timespec  ts;

    if (timePtr == NULL) {
	Ns_CondWait(condPtr, mutexPtr);
	return NS_OK;
    }

    cond = GETCOND(condPtr);
    mPtr = GETMUTEX(mutexPtr);
    lockPtr = mPtr->lock;
    ownerPtr = mPtr->ownerPtr;
    mPtr->ownerPtr = NULL;

    /*
     * Convert the microsecond-based Ns_Time to a nanosecond-based
     * struct timespec.
     */

    ts.tv_sec = timePtr->sec;
    ts.tv_nsec = timePtr->usec * 1000;

    /*
     * As documented on Linux, pthread_cond_timedwait may return
     * EINTR if a signal arrives.  We have noticed that 
     * EINTR can be returned on Solaris as well although this
     * is not documented (perhaps, as above, it's possible it
     * bubbles up from _lwp_cond_timedwait???).  Anyway, unlike
     * the ETIME case above, we'll assume the wakeup is truely
     * spurious and simply restart the wait knowing that the
     * ts structure has not been modified.
     */

    do {
    	err = pthread_cond_timedwait(cond, lockPtr, &ts);
    } while (err == EINTR);

#ifdef HAVE_ETIME_BUG

    /*
     * See comments above and note that here ETIME is still considered
     * a spurious wakeup, not an indication of timeout because we're
     * not making any assumptions about the nature or this bug.
     * While we're less certain, this should still be ok as properly
     * written condition code should tolerate the wakeup.
     */

    if (err == ETIME) {
	err = 0;
    }
#endif

    if (ERRTIMEDOUT(err)) {
	status = NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_timedwait", err);
    } else {
	status = NS_OK;
    }

    mPtr->ownerPtr = ownerPtr;
    ++mPtr->nlock;

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsSetThread --
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

void
NsSetThread(Thread *thrPtr)
{
    pthread_t thread;
    int err;

    thread = pthread_self();
#ifdef HAVE_PTHREAD_D4
    thrPtr->tid = pthread_getunique_np(&thread);
#else
    thrPtr->tid = (int) thread;
#endif
    err = pthread_setspecific(GetKey(), thrPtr);
    if (err != 0) {
        NsThreadFatal("NsSetThread", "pthread_setspecific", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread --
 *
 *	Pthread routine to return this thread's nsthread data structure.
 *	If this thread doesn't have a Thread structure (e.g., the
 *	initial thread or a thread created without nsthreads from
 *	Java), a default structure is allocated and set.
 *
 * Results:
 *	Pointer to per-thread data structure.
 *
 * Side effects:
 *	Key is allocated the first time.
 *
 *----------------------------------------------------------------------
 */

Thread      *
NsGetThread(void)
{
    Thread *thisPtr;

#ifdef HAVE_PTHREAD_D4
    if (pthread_getspecific(GetKey(), (pthread_addr_t *) &thisPtr) != 0) {
	thisPtr = NULL;
    }
#else
    thisPtr = (Thread *) pthread_getspecific(GetKey());
#endif
    if (thisPtr == NULL) {
	thisPtr = NsNewThread();
	NsSetThread(thisPtr);
    }
    return thisPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadCreate --
 *
 *	Pthread thread creation.  All nsthreads are created as detached
 *	pthreads as the nsthread library handles joining itself in an
 *	interface independent manner.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See pthread_create and pthread_detach.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    pthread_t thr;
    pthread_attr_t attr, *attrPtr;
    static char *func = "NsThreadCreate";
    int     err;

    /*
     * Set the stack size.  It could be smarter to leave the default 
     * on platforms which map large stacks with guard zones
     * (e.g., Solaris and Linux).
     */

    err = pthread_attr_init(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_init", err);
    }
    err = pthread_attr_setstacksize(&attr, thrPtr->stackSize); 
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_setstacksize", err);
    }

    /*
     * System scope threads are used by default on Solaris and UnixWare.
     * Other platforms are either already system scope (Linux, HP11), 
     * support only process scope (SGI), user-level thread libraries
     * anyway (HP10, FreeBSD), or found to be unstable with this
     * setting (OSF).
     */

#ifdef USE_PTHREAD_SYSSCOPE
    err = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_setscope",err);
    }
#endif
    err = pthread_create(&thr, &attr, (void *(*)(void *)) NsThreadMain, thrPtr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_create", err);
    }
    err = pthread_detach(thr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_detach", err);
    }
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_destroy", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *	Pthread exit routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See CleanupThread which will be called by pthread's tls
 *	destructors.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadExit(void)
{
    pthread_exit(NULL);
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
 * ns_sigmask --
 *
 *	Set the thread's signal mask.
 *
 * Results:
 *	0 on success, otherwise an error code.
 *
 * Side effects:
 *	See pthread_sigmask.
 *
 *----------------------------------------------------------------------
 */

int
ns_sigmask(int how, sigset_t *set, sigset_t *oset)
{
    return pthread_sigmask(how, set, oset);
}


/*
 *----------------------------------------------------------------------
 *
 * InitMaster --
 *
 *	Once routine to initialize the pthread master lock.
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
InitMaster(void)
{
    int err;

    master.owner = 0;
    master.count = 0;
    err = pthread_mutex_init(&master.lock, NULL);
    if (err != 0) {
        NsThreadFatal("InitMaster", "pthread_mutex_init", err);
    }
    err = pthread_cond_init(&master.cond, NULL);
    if (err != 0) {
        NsThreadFatal("InitMaster", "pthread_cond_init", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetKey --
 *
 *	Return the single pthread TLS key, initalizing if needed.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static pthread_key_t
GetKey(void)
{
    static pthread_key_t key;
    static int initialized;
    int err;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    err = pthread_key_create(&key, CleanupThread);
	    if (err != 0) {
		NsThreadFatal("GetKey", "pthread_key_create", err);
	    }
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }
    return key;
}


/*
 *----------------------------------------------------------------------
 *
 * CleanupThread --
 *
 *	Pthread TLS callback for the nsthread context. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See NsCleanupThread.
 *
 *----------------------------------------------------------------------
 */

static void
CleanupThread(void *arg)
{
    Thread *thisPtr = arg;
    pthread_key_t key = GetKey();
    int err;

    /*
     * NB: Must manually reset and then clear the pthread tls
     * slot for the benefit of callbacks in NsCleanupThread.
     */

    err = pthread_setspecific(key, thisPtr);
    if (err != 0) {
        NsThreadFatal("CleanupThread", "pthread_setspecific", err);
    }
    NsCleanupThread(thisPtr);
    err = pthread_setspecific(key, NULL);
    if (err != 0) {
        NsThreadFatal("CleanupThread", "pthread_setspecific", err);
    }
}

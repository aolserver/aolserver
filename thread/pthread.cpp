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
#endif

/*
 * The following pthread thread local storage key and one time initializer
 * are used to store the per-thread nsthread data structure.
 */

#ifndef PTHREAD_ONCE_INIT
static pthread_once_t keyOnce;
#else
static pthread_once_t keyOnce = PTHREAD_ONCE_INIT;
#endif

static pthread_key_t key;

/* 
 * Static routines defined in this file.
 */

static void KeyInit(void);


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
 * NsMutexInit --
 *
 *	Pthread mutex initialization.  Note this routines isn't normally
 *	called directly as mutexes are now self-initializing when first
 *	locked.
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
    int             err;
    pthread_mutex_t *lock;
#ifdef USE_PTHREAD_PSHARED
    pthread_mutexattr_t attr;
#endif

    lock = ns_malloc(sizeof(pthread_mutex_t));
#ifndef USE_PTHREAD_PSHARED
    err = pthread_mutex_init(lock, MUTEX_INIT_ATTR);
#else
    err = pthread_mutexattr_init(&attr);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexInit", "ptread_mutexattr_init", err);
    }
    err = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexInit", "ptread_mutexattr_setpshared", err);
    }
    err = pthread_mutex_init(lock, &attr);
#endif
    if (err != 0) {
    	NsThreadFatal("Ns_MutexInit", "ptread_mutex_init", err);
    }
#ifdef USE_PTHREAD_SYSSCOPE
    err = pthread_mutexattr_destroy(&attr);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexInit", "ptread_mutexattr_destroy", err);
    }
#endif
    *lockPtr = (void *) lock;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexDestroy --
 *
 *	Pthread mutex destroy.  Note this routine is almost never used
 *	as mutexes normally exists in memory until the process exits.
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
    pthread_mutex_t *lock = (pthread_mutex_t *) (*lockPtr);
    int err;

    err = pthread_mutex_destroy(lock);
    if (err != 0) {
        NsThreadFatal("Ns_MutexDestroy", "ptread_mutex_destroy", err);
    }
    ns_free(*lockPtr);
    *lockPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexLock --
 *
 *	Pthread mutex lock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread may be suspended by the pthread library if the lock is
 *	already held.
 *
 *----------------------------------------------------------------------
 */

void
NsMutexLock(void **lockPtr)
{
    pthread_mutex_t *lock = (pthread_mutex_t *) (*lockPtr);
    int err;

    err = pthread_mutex_lock(lock);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexLock", "ptread_mutex_lock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexTryLock --
 *
 *	Pthread mutex non-blocking lock.
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
    pthread_mutex_t *lock = (pthread_mutex_t *) (*lockPtr);
    int err;

    err = pthread_mutex_trylock(lock);
    if (ERRLOCKBUSY(err)) {
	return NS_TIMEOUT;
    } else if (!(ERRLOCKOK(err))) {
    	NsThreadFatal("Ns_MutexTryLock", "ptread_mutex_trylock", err);
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MutexUnlock --
 *
 *	Pthread mutex unlock.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Other waiting thread, if any, is resumed.
 *
 *----------------------------------------------------------------------
 */
void
NsMutexUnlock(void **lockPtr)
{
    pthread_mutex_t *lock = (pthread_mutex_t *) (*lockPtr);
    int             err;

    err = pthread_mutex_unlock(lock);
    if (err != 0) {
    	NsThreadFatal("Ns_MutexUnlock", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondInit --
 *
 *	Pthread condition variable initialization.  Note this routine
 *	isn't used directly very often as condition variables are now self
 *	initializing when first used.
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
    int             err;
    pthread_cond_t *cPtr;
#ifdef USE_PTHREAD_PSHARED
    pthread_condattr_t attr;
#endif

    cPtr = ns_malloc(sizeof(pthread_cond_t));
#ifndef USE_PTHREAD_PSHARED
    err = pthread_cond_init(cPtr, COND_INIT_ATTR);
#else
    err = pthread_condattr_init(&attr);
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "ptread_condattr_init", err);
    }
    err = pthread_condattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "ptread_condattr_setpshared", err);
    }
    err = pthread_cond_init(cPtr, &attr);
#endif
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "pthread_cond_init", err);
    }
#ifdef USE_PTHREAD_PSHARED
    err = pthread_condattr_destroy(&attr);
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "ptread_condattr_destroy", err);
    }
#endif
    *condPtr = (Ns_Cond) cPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CondDestroy --
 *
 *	Pthread condition destroy.  Note this routine is almost never used
 *	as condition variables normally exist in memory until the process
 *	exits.
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
    	int             err;

    	err = pthread_cond_destroy((pthread_cond_t *) (*condPtr));
    	if (err != 0) {
    	    NsThreadFatal("Ns_CondDestroy", "pthread_cond_destroy", err);
    	}
    	ns_free(*condPtr);
        *condPtr = NULL;
    }
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
 *	A single waiting thread, if any, is resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *condPtr)
{
    int err;
    pthread_cond_t *cPtr = GETCOND(condPtr);

    err = pthread_cond_signal(cPtr);
    if (err != 0) {
        NsThreadFatal("Ns_CondSet", "pthread_cond_signal", err);
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
 *	All waiting threads, if any, are resumed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondBroadcast(Ns_Cond *condPtr)
{
    int err;
    pthread_cond_t *cPtr = GETCOND(condPtr);

    err = pthread_cond_broadcast(cPtr);
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
 *	Thread will be suspended.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr)
{
    int err;
    pthread_cond_t *cPtr;
    pthread_mutex_t *lPtr;
    Thread *oPtr;
    Mutex *mPtr;

    cPtr = GETCOND(condPtr);
    mPtr = GETMUTEX(mutexPtr);
    oPtr = mPtr->ownerPtr;
    lPtr = mPtr->lock;
    mPtr->ownerPtr = NULL;

#ifdef HAVE_ETIME_BUG
    /* NB: Solaris may inappropriately return ETIME.  */
    do {
	err = pthread_cond_wait(cPtr, lPtr);
    } while (err == ETIME);
#else
    err = pthread_cond_wait(cPtr, lPtr);
#endif

    if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_wait", err);
    }

    mPtr->ownerPtr = oPtr;
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
 *	Thread will be suspended until a signal or timeout occurs.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *mutexPtr, Ns_Time *timePtr)
{
    struct timespec ts;
    int err, status;
    pthread_cond_t *cPtr;
    pthread_mutex_t *lPtr;
    Thread *oPtr;
    Mutex *mPtr;

    if (timePtr == NULL) {
	Ns_CondWait(condPtr, mutexPtr);
	return NS_OK;
    }

    cPtr = GETCOND(condPtr);
    mPtr = GETMUTEX(mutexPtr);
    oPtr = mPtr->ownerPtr;
    mPtr->ownerPtr = NULL;
    lPtr = mPtr->lock;
    ts.tv_sec = timePtr->sec;
    ts.tv_nsec = timePtr->usec * 1000;

#ifdef HAVE_ETIME_BUG
    /* NB: Solaris may inappropriately return ETIME.  */
    do {
	err = pthread_cond_timedwait(cPtr, lPtr, &ts);
    } while (err == ETIME);
#elif defined(HAVE_COND_EINTR)
    /* NB: EINTR generated by GDB debugger on Linux. */
    do {
    	err = pthread_cond_timedwait(cPtr, lPtr, &ts);
    } while (err == EINTR);
#else
    err = pthread_cond_timedwait(cPtr, lPtr, &ts);
#endif
    if (ERRTIMEDOUT(err)) {
	status = NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_timedwait", err);
    } else {
	status = NS_OK;
    }
    mPtr->ownerPtr = oPtr;
    ++mPtr->nlock;
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsSetThread --
 *
 *	Pthread routine to set this thread's nsthread data structure.
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
NsSetThread(Thread *thisPtr)
{
    pthread_t tid;

    tid = pthread_self();
#ifdef HAVE_PTHREAD_D4
    thisPtr->tid = pthread_getunique_np(&tid);
#else
    thisPtr->tid = (int) tid;
#endif
    pthread_once(&keyOnce, KeyInit);
    pthread_setspecific(key, thisPtr);
    NsSetThread2(thisPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetThread --
 *
 *	Pthread routine to return this thread's nsthread data structure.
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

    pthread_once(&keyOnce, KeyInit);
#ifdef HAVE_PTHREAD_D4
    if (pthread_getspecific(key, (pthread_addr_t *) &thisPtr) != 0) {
	thisPtr = NULL;
    }
#else
    thisPtr = (Thread *) pthread_getspecific(key);
#endif
    return (thisPtr ? thisPtr : NsGetThread2());
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
 *	Thread is created by the pthread library.
 *
 *----------------------------------------------------------------------
 */

void
NsThreadCreate(Thread *thrPtr)
{
    pthread_t thr;
    pthread_attr_t attr, *attrPtr;
    int     err;

    /*
     * Set the stack size.  It could be smarter to leave the default 
     * on platforms which map large stacks with guard zones
     * (e.g., Solaris and Linux).
     */

    err = pthread_attr_init(&attr);
    if (err != 0) {
        NsThreadFatal("Ns_ThreadCreate", "pthread_attr_init", err);
    }
    err = pthread_attr_setstacksize(&attr, thrPtr->stackSize); 
    if (err != 0) {
        NsThreadFatal("NsThreadCreate", "pthread_attr_setstacksize", err);
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
        NsThreadFatal("NsThreadCreate", "pthread_attr_setscope",err);
    }
#endif

    err = pthread_create(&thr, &attr, (void *(*)(void *)) NsThreadMain, thrPtr);
    if (err != 0) {
        NsThreadFatal("NsThreadCreate", "pthread_create", err);
    }
    err = pthread_detach(thr);
    if (err != 0) {
        NsThreadFatal("NsThreadCreate", "pthread_detach", err);
    }
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
        NsThreadFatal("NsThreadCreate", "pthread_attr_destroy", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsThreadExit --
 *
 *	Pthread thread exit routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pthread is destroyed.
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
 *	Thead may be suspended by operating system.
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
 *	Thread's signal mask is updated and previous signal mask is
 *	returned in oset if not null.
 *
 *----------------------------------------------------------------------
 */
int
ns_sigmask(int how, sigset_t * set, sigset_t * oset)
{
    return pthread_sigmask(how, set, oset);
}


/*
 *----------------------------------------------------------------------
 *
 * KeyInit --
 *
 *	One-time initializer for the thread local storage key. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Key is allocated.
 *
 *----------------------------------------------------------------------
 */

static void
KeyInit(void)
{
    int err;

    err = pthread_key_create(&key, NULL);
    if (err != 0) {
        NsThreadFatal("KeyInit", "pthread_key_create", err);
    }
}

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
 *	Interface routines for nsthreads using pthreads.
 *
 */

#include "thread.h"
#include <pthread.h>

/* TODO: Move to configure. */
#ifdef __APPLE__
#define HAVE_PTHREAD_GET_STACKADDR_NP 1
#endif
#ifdef __linux
#define HAVE_PTHREAD_GETATTR_NP 1
#endif

#if defined(HAVE_PTHREAD_GETATTR_NP) && defined(GETATTRNP_NOT_DECLARED)
extern int pthread_getattr_np(pthread_t tid, pthread_attr_t *attr);
#endif

/*
 * The following constants define an integer magic number for marking pages
 * at thread startup to be later checked at thread exit for stack usage and
 * possible overflow.
 */

#define STACK_MAGIC	0xefefefef

typedef struct Thread {
    unsigned long uid;
    int	   marked;
    void  *stackaddr;
    size_t stacksize;
    void  *slots[NS_THREAD_MAXTLS];
} Thread;

static void *ThreadMain(void *arg);
static Thread *NewThread(void);
static void FreeThread(void *arg);
static void SetKey(char *func, void *arg);
static void StackPages(Thread *thrPtr, int mark);
static int StackDown(char **outer);
static int PageRound(int size);
static pthread_cond_t *GetCond(Ns_Cond *cond);
static Thread *GetThread(void);
static Ns_Mutex uidlock;

/*
 * The following single Tls key is used to store the Thread struct
 * including stack info, startup args, and TLS slots.
 */

static pthread_key_t	key;

/*
 * The following variables are used to manage stack sizes, guardzones, and
 * size monitoring.
 */

static int stackdown;
static int pagesize;
static int guardsize;
static int markpages;
static FILE *logfp = NULL;
static char *dumpdir = NULL;


/*
 *----------------------------------------------------------------------
 *
 * NsInitThreads --
 *
 *	Pthread library load time init routine.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates pthread key and set various stack management values.
 *
 *----------------------------------------------------------------------
 */

void
NsInitThreads(void)
{
    char *env;
    int err;

    err = pthread_key_create(&key, FreeThread);
    if (err != 0) {
	NsThreadFatal("NsPthreadsInit", "pthread_key_create", err);
    }
    stackdown = StackDown(&env);
    pagesize = getpagesize();
    env = getenv("NS_THREAD_GUARDSIZE");
    if (env == NULL
	    || Tcl_GetInt(NULL, env, &guardsize) != TCL_OK
	    || guardsize < 2) {
	guardsize = 2 * pagesize;
    }
    guardsize = PageRound(guardsize);
    markpages = getenv("NS_THREAD_MARKPAGES") ? 1 : 0;
    dumpdir = getenv("NS_THREAD_DUMPDIR");
    env = getenv("NS_THREAD_LOGFILE");
    if (env != NULL) {
	if (strcmp(env, "-") == 0) {
	    logfp = stderr;
	} else {
	    logfp = fopen(env, "a");
	}
    }
    Ns_MutexSetName(&uidlock, "ns:uidlock");
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetTls --
 *
 *	Return the TLS slots.
 *
 * Results:
 *	Pointer to slots array.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void **
NsGetTls(void)
{
    Thread *thrPtr = GetThread();

    return thrPtr->slots;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetStack --
 *
 *	Return information about the stack.
 *
 * Results:
 *	0:	Could not return stack.
 *	-1:	Stack grows down.
 *	1:	Stack grows up.
 *
 * Side effects:
 *	Given addrPtr and sizePtr are updated with stack base
 *	address and size.
 *
 *----------------------------------------------------------------------
 */

int
NsGetStack(void **addrPtr, size_t *sizePtr)
{
    Thread *thrPtr = GetThread();

    if (thrPtr->stackaddr == NULL) {
	return 0;
    }
    *addrPtr = thrPtr->stackaddr;
    *sizePtr = thrPtr->stacksize;
    return (stackdown ? -1 : 1);
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
    return "pthread";
}


/*
 *----------------------------------------------------------------------
 *
 * NsLockAlloc --
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
    pthread_mutex_t *lock;
    int err;

    lock = ns_malloc(sizeof(pthread_mutex_t));
    err = pthread_mutex_init(lock, NULL);
    if (err != 0) {
    	NsThreadFatal("NsLockAlloc", "pthread_mutex_init", err);
    }
    return lock;
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
    int err;

    err = pthread_mutex_destroy((pthread_mutex_t *) lock);
    if (err != 0) {
    	NsThreadFatal("NsLockFree", "pthread_mutex_destroy", err);
    }
    ns_free(lock);
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
 *	May wait wakeup event if lock already held.
 *
 *----------------------------------------------------------------------
 */

void
NsLockSet(void *lock)
{
    int err;

    err = pthread_mutex_lock((pthread_mutex_t *) lock);
    if (err != 0) {
    	NsThreadFatal("NsLockSet", "pthread_mutex_lock", err);
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
 *	1 if lock set, 0 otherwise.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

int
NsLockTry(void *lock)
{
    int err;

    err = pthread_mutex_trylock((pthread_mutex_t *) lock);
    if (err == EBUSY) {
	return 0;
    } else if (err != 0) {
    	NsThreadFatal("NsLockTry", "pthread_mutex_trylock", err);
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
 *	May signal wakeup event for a waiting thread.
 *
 *----------------------------------------------------------------------
 */

void
NsLockUnset(void *lock)
{
    int err;

    err = pthread_mutex_unlock((pthread_mutex_t *) lock);
    if (err != 0) {
    	NsThreadFatal("NsLockUnset", "pthread_mutex_unlock", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsCreateThread --
 *
 *	Pthread specific thread create function called by
 *	Ns_ThreadCreate.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on thread startup routine.
 *
 *----------------------------------------------------------------------
 */

void
NsCreateThread(void *arg, long stacksize, Ns_Thread *resultPtr)
{
    static char *func = "NsCreateThread";
    pthread_attr_t attr;
    pthread_t tid;
    size_t size;
    int err;

    err = pthread_attr_init(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_init", err);
    }

    /*
     * Round the stacksize to a pagesize and include the guardzone.
     */

    size = PageRound(stacksize) + guardsize;
    err = pthread_attr_setstacksize(&attr, size); 
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_setstacksize", err);
    }

    /*
     * System scope always preferred, ignore any unsupported error.
     */

    err = pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    if (err != 0 && err != ENOTSUP) {
        NsThreadFatal(func, "pthread_setscope", err);
    }
    err = pthread_create(&tid, &attr, ThreadMain, arg);
    if (err != 0) {
        NsThreadFatal(func, "pthread_create", err);
    }
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
        NsThreadFatal(func, "pthread_attr_destroy", err);
    }
    if (resultPtr != NULL) {
	*resultPtr = (Ns_Thread) tid;
    } else {
	err = pthread_detach(tid);
	if (err != 0) {
	    NsThreadFatal(func, "pthread_detach", err);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ThreadMain --
 *
 *	Pthread startup routine.
 *
 * Results:
 *	Does not return.
 *
 * Side effects:
 *	NsThreadMain will call Ns_ThreadExit.
 *
 *----------------------------------------------------------------------
 */

static void *
ThreadMain(void *arg)
{
    Thread *thrPtr = GetThread();

    if (thrPtr->stackaddr != NULL && markpages) {
	StackPages(thrPtr, 1);
	thrPtr->marked = 1;
    }
    NsThreadMain(arg);
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadExit --
 *
 *	Terminate a thread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cleanup will be handled by the pthread call to CleanupTls.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadExit(void *arg)
{
    pthread_exit(arg);
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
 *	Requested thread is destroyed after join.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadJoin(Ns_Thread *thread, void **argPtr)
{
    pthread_t tid = (pthread_t) *thread;
    int err;

    err = pthread_join(tid, argPtr);
    if (err != 0) {
	NsThreadFatal("Ns_ThreadJoin", "pthread_join", err);
    }
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
 * Ns_ThreadId --
 *
 *	Return the numeric thread id.
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
    return (int) pthread_self();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadSelf --
 *
 *	Return thread handle suitable for Ns_ThreadJoin.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Value at threadPtr is updated with thread's handle.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ThreadSelf(Ns_Thread *threadPtr)
{
    *threadPtr = (Ns_Thread) pthread_self();
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
Ns_CondInit(Ns_Cond *cond)
{
    pthread_cond_t *condPtr;
    int             err;

    condPtr = ns_malloc(sizeof(pthread_cond_t));
    err = pthread_cond_init(condPtr, NULL);
    if (err != 0) {
    	NsThreadFatal("Ns_CondInit", "pthread_cond_init", err);
    }
    *cond = (Ns_Cond) condPtr;
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
Ns_CondDestroy(Ns_Cond *cond)
{
    pthread_cond_t *condPtr = (pthread_cond_t *) *cond;
    int             err;

    if (condPtr != NULL) {
    	err = pthread_cond_destroy(condPtr);
    	if (err != 0) {
    	    NsThreadFatal("Ns_CondDestroy", "pthread_cond_destroy", err);
    	}
    	ns_free(condPtr);
    	*cond = NULL;
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
 *	See pthread_cond_signal.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CondSignal(Ns_Cond *cond)
{
    int             err;

    err = pthread_cond_signal(GetCond(cond));
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
Ns_CondBroadcast(Ns_Cond *cond)
{
    int             err;

    err = pthread_cond_broadcast(GetCond(cond));
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
Ns_CondWait(Ns_Cond *cond, Ns_Mutex *mutex)
{
    int              err;

    err = pthread_cond_wait(GetCond(cond), NsGetLock(mutex));
    if (err != 0) {
	NsThreadFatal("Ns_CondWait", "pthread_cond_wait", err);
    }
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
Ns_CondTimedWait(Ns_Cond *cond, Ns_Mutex *mutex, Ns_Time *timePtr)
{
    int              err, status = NS_ERROR;
    struct timespec  ts;

    if (timePtr == NULL) {
	Ns_CondWait(cond, mutex);
	return NS_OK;
    }

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
     * is not documented.  We assume the wakeup is truely
     * spurious and simply restart the wait knowing that the
     * ts structure has not been modified.
     */

    do {
    	err = pthread_cond_timedwait(GetCond(cond), NsGetLock(mutex), &ts);
    } while (err == EINTR);
    if (err == ETIMEDOUT) {
	status = NS_TIMEOUT;
    } else if (err != 0) {
	NsThreadFatal("Ns_CondTimedWait", "pthread_cond_timedwait", err);
    } else {
	status = NS_OK;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCond --
 *
 *	Cast an Ns_Cond to pthread_cond_t, initializing if needed.
 *
 * Results:
 *	Pointer to pthread_cond_t.
 *
 * Side effects:
 *	Ns_Cond is initialized the first time.
 *
 *----------------------------------------------------------------------
 */

static pthread_cond_t *
GetCond(Ns_Cond *cond)
{
    if (*cond == NULL) {
    	Ns_MasterLock();
    	if (*cond == NULL) {
	    Ns_CondInit(cond);
    	}
    	Ns_MasterUnlock();
    }
    return (pthread_cond_t *) *cond;
}


/*
 *----------------------------------------------------------------------
 *
 * GetThread --
 *
 *	Return the Thread struct for the current thread.
 *
 * Results:
 *	Pointer to Thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Thread *
GetThread(void)
{
    Thread *thrPtr;

    thrPtr = pthread_getspecific(key);
    if (thrPtr == NULL) {
    	thrPtr = NewThread();
	SetKey("NsGetTls", thrPtr);
    }
    return thrPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NewThread --
 *
 *	Create a new Thread struct for the current thread, determing
 *	the stack addr and size if possible.
 *
 * Results:
 *	Pointer to Thread.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Thread *
NewThread(void)
{
    Thread *thrPtr;
    static unsigned int nextuid = 0;
    pthread_t tid;
#if defined(HAVE_PTHREAD_GETATTR_NP)
    static char *func = "NewThread";
    pthread_attr_t attr;
    int err;
#endif

    thrPtr = ns_calloc(1, sizeof(Thread));
    Ns_MutexLock(&uidlock);
    thrPtr->uid = nextuid++;
    Ns_MutexUnlock(&uidlock);

    tid = pthread_self();
#if defined(HAVE_PTHREAD_GETATTR_NP)
    err = pthread_getattr_np(tid, &attr);
    if (err != 0) {
	NsThreadFatal(func, "pthread_getattr_np", err);
    }
    err = pthread_attr_getstackaddr(&attr, &thrPtr->stackaddr);
    if (err != 0) {
	NsThreadFatal(func, "pthread_attr_getstackaddr", err);
    }
    err = pthread_attr_getstacksize(&attr, &thrPtr->stacksize);
    if (err != 0) {
	NsThreadFatal(func, "pthread_attr_getstacksize", err);
    }
    thrPtr->stacksize -= guardsize;
    err = pthread_attr_destroy(&attr);
    if (err != 0) {
	NsThreadFatal(func, "pthread_attr_destroy", err);
    }
#elif defined(HAVE_PTHREAD_GET_STACKADDR_NP)
    thrPtr->stackaddr = pthread_get_stackaddr_np(tid);
    thrPtr->stacksize = pthread_get_stacksize_np(tid) - guardsize;
#endif
    return thrPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeThread --
 *
 *	Pthread TLS cleanup.  This routine is called during thread
 *	exit.  This routine could be called more than once if some
 *	other pthread cleanup requires nsthreads TLS.
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
FreeThread(void *arg)
{
    Thread *thrPtr = arg;

    /*
     * Restore the current slots during cleanup so handlers can access
     * TLS in other slots.
     */

    SetKey("FreeThread", arg);
    NsCleanupTls(thrPtr->slots);
    SetKey("FreeThread", NULL);
    if (thrPtr->marked) {
	StackPages(thrPtr, 0);
    }
    ns_free(thrPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SetKey --
 *
 *	Set the pthread key.
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
SetKey(char *func, void *arg)
{
    int err;

    err = pthread_setspecific(key, arg);
    if (err != 0) {
	NsThreadFatal(func, "pthread_setspecific", err);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StackDown --
 *
 *	Determine if the stack grows down.
 *
 * Results:
 *	1 if stack grows down, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
StackDown(char **outer)
{
   char *local;

   return (&local < outer ? 1 : 0);
}


/*
 *----------------------------------------------------------------------
 *
 * StackPages --
 *
 *	Mark or count used stack pages.
 *
 * Results:
 *	Count of used pages.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
StackPages(Thread *thrPtr, int mark)
{
    Ns_Time now;
    caddr_t start, end, guard, base;
    int fd, overflow, pagewords, pages, maxpage, bytes;
    uint32_t *ip;
    char file[100];

    /*
     * Determine the range of pages to mark or check.  Basically the first
     * page to be used is ignored assuming it's being used now and the
     * guard pages are marked completely to check for overflow.  All pages
     * in between are marked on the first integer word to be later counted
     * to get a pages used count.
     */

    if (stackdown) {
	start = thrPtr->stackaddr - thrPtr->stacksize + guardsize;
	end   = thrPtr->stackaddr - pagesize;
	guard = start - guardsize;
    } else {
	start = thrPtr->stackaddr + pagesize;
	end = thrPtr->stackaddr + thrPtr->stacksize - guardsize;
	guard = end;
    }

    /*
     * Completely mark the guard page.
     */

    overflow = 0;
    ip = (uint32_t *) guard;
    while (ip < (uint32_t *) (guard + guardsize)) {
	if (mark) {
	    *ip = STACK_MAGIC;
	} else if (*ip != STACK_MAGIC) {
	    overflow = 1;
	    break;
	}
	++ip;
    }
    
    /*
     * For each stack page, either mark with the magic number at thread
     * startup or count unmarked pages at thread cleanup.
     */

    pagewords = pagesize / sizeof(uint32_t);
    maxpage = pages = 1;
    ip = (uint32_t *) start;
    if (stackdown) {
	/* NB: Mark last word, not first, of each page. */
	ip += pagewords - 1;
    }
    while (ip < (uint32_t *) end) {
	if (mark) {
	    *ip = STACK_MAGIC;
	} else if (*ip != STACK_MAGIC) {
	    maxpage = pages;
	}
	++pages;
	ip += pagewords;
    }
    if (!mark) {
        pages = maxpage;
    }
    bytes = pages * pagesize;
    if (!mark && dumpdir != NULL) {
	sprintf(file, "%s/nsstack.%lu", dumpdir, thrPtr->uid);
	fd = open(file, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if (fd >= 0) {
	    base = thrPtr->stackaddr;
	    if (stackdown) {
		base -= thrPtr->stacksize;
	    }
	    (void) write(fd, base, thrPtr->stacksize);
	    close(fd);
	}
    }
    if (logfp) {
	Ns_GetTime(&now);
	fprintf(logfp, "%s: time: %ld:%ld, thread: %lu, %s: %d pages, %d bytes%s\n", 
		mark ? "create" : "exit", now.sec, now.usec, thrPtr->uid,
		mark ? "stackavil" : "stackuse", pages, bytes,
		overflow ? " - possible overflow!" : "");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PageRound --
 *
 *	Round bytes up to next pagesize.
 *
 * Results:
 *	Rounded size.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
PageRound(int size)
{
    if (size % pagesize) {
	size += pagesize;
    }
    size = (size / pagesize) * pagesize;
    return size;
}

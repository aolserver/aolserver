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
 * nsthread.h --
 *
 *	Core threading and system headers.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/include/nsthread.h,v 1.9 2000/11/06 17:55:42 jgdavidson Exp $
 */

#ifndef NSTHREAD_H
#define NSTHREAD_H

#ifndef __linux
#ifdef FD_SETSIZE
#undef FD_SETSIZE
#endif
#define FD_SETSIZE 1024
#endif

#ifndef WIN32
#define NS_EXPORT
#define NS_IMPORT
#else
#define NS_EXPORT		__declspec(dllexport)
#define NS_IMPORT		__declspec(dllimport)
#endif

#ifdef NSTHREAD_EXPORTS
#define NS_STORAGE_CLASS	NS_EXPORT
#else
#define NS_STORAGE_CLASS	NS_IMPORT
#endif

#ifdef __cplusplus
#define NS_EXTERN		extern "C" NS_STORAGE_CLASS
#else
#define NS_EXTERN		extern NS_STORAGE_CLASS
#endif

/*
 * Required constants and system headers.
 */

#ifdef WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <winsock.h>
#include <sys/timeb.h>
#include <sys/types.h>
#include <io.h>
#include <process.h>
#include <direct.h>

typedef struct DIR_ *DIR;
struct dirent {
    char *d_name;
};
NS_EXTERN DIR *opendir(char *pathname);
NS_EXTERN struct dirent *readdir(DIR *dp);
NS_EXTERN int closedir(DIR *dp);
#define sleep(n)	(Sleep((n)*1000))

#else

#ifndef _REENTRANT
#define _REENTRANT
#endif
#if defined(__sgi) && !defined(_SGI_MP_SOURCE)
#define _SGI_MP_SOURCE
#endif
#if defined(__sun) && !defined(_POSIX_PTHREAD_SEMANTICS)
#define _POSIX_PTHREAD_SEMANTICS
#endif
#include <sys/types.h>
#include <dirent.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#ifndef O_TEXT
#define O_TEXT 0
#endif
#ifndef O_BINARY
#define O_BINARY 0
#endif

#endif /* WIN32 */

#include <limits.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif


/*
 * Various constants.
 */

#define NS_OK              	0
#define NS_ERROR         	(-1)
#define NS_TIMEOUT       	(-2)
#define NS_THREAD_MAXTLS	200
#define NS_THREAD_DETACHED	1
#define NS_THREAD_JOINED	2
#define NS_THREAD_EXITED	4

#define NS_THREAD_NAMESIZE	32


/*
 *==========================================================================
 * Typedefs
 *==========================================================================
 */

/*
 * The following objects are defined as pointers to dummy structures
 * to ensure proper type checking.  The actual objects underlying
 * objects are platform specific. 
 */

typedef struct Ns_Thread_	*Ns_Thread;
typedef struct Ns_Tls_		*Ns_Tls;
typedef struct Ns_Pool_		*Ns_Pool;
typedef struct Ns_Mutex_	*Ns_Mutex;
typedef struct Ns_Cond_		*Ns_Cond;
typedef struct Ns_Cs_		*Ns_Cs;
typedef struct Ns_Sema_		*Ns_Sema;
typedef struct Ns_RWLock_	*Ns_RWLock;
typedef struct Ns_Event_	*Ns_Event;
typedef struct Ns_Semaphore_	*Ns_Semaphore;
typedef struct Ns_CriticalSection_ *Ns_CriticalSection;
typedef struct Ns_ThreadLocalStorage_ *Ns_ThreadLocalStorage;

typedef struct Ns_Time {
    time_t	sec;
    long	usec;
} Ns_Time;

typedef void (Ns_ThreadProc) (void *arg);
typedef void (Ns_TlsCleanup) (void *arg);

typedef struct Ns_ThreadInfo {
    Ns_Thread *thread;
    char *name;
    char *parent;
    int tid;
    time_t ctime;
    int flags;
    Ns_ThreadProc *proc;
    void *arg;
} Ns_ThreadInfo;

typedef struct Ns_MutexInfo {
    Ns_Mutex *mutexPtr;
    char *name;
    char *owner;
    int id;
    unsigned long nlock;
    unsigned long nbusy;
} Ns_MutexInfo;

typedef void (Ns_ThreadInfoProc) (Ns_ThreadInfo *infoPtr, void *arg);
typedef void (Ns_MutexInfoProc) (Ns_MutexInfo *infoPtr, void *arg);

/*
 *==========================================================================
 * C API functions
 *==========================================================================
 */

/*
 * compat.c:
 */

NS_EXTERN int Ns_InitializeMutex(Ns_Mutex *mutexPtr);
NS_EXTERN int Ns_DestroyMutex(Ns_Mutex *mutexPtr);
NS_EXTERN int Ns_LockMutex(Ns_Mutex *mutexPtr);
NS_EXTERN int Ns_UnlockMutex(Ns_Mutex *mutexPtr);
NS_EXTERN int Ns_InitializeCriticalSection(Ns_CriticalSection *cs);
NS_EXTERN int Ns_DestroyCriticalSection(Ns_CriticalSection *cs);
NS_EXTERN int Ns_EnterCriticalSection(Ns_CriticalSection *cs);
NS_EXTERN int Ns_LeaveCriticalSection(Ns_CriticalSection *cs);
NS_EXTERN int Ns_InitializeEvent(Ns_Event *event);
NS_EXTERN int Ns_DestroyEvent(Ns_Event *event);
NS_EXTERN int Ns_SetEvent(Ns_Event *event);
NS_EXTERN int Ns_BroadcastEvent(Ns_Event *event);
NS_EXTERN int Ns_WaitForEvent(Ns_Event *event, Ns_Mutex *lock);
NS_EXTERN int Ns_TimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock, int timeout);
NS_EXTERN int Ns_AbsTimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock,
				   time_t abstime);
NS_EXTERN int Ns_UTimedWaitForEvent(Ns_Event *event, Ns_Mutex *lock, int seconds,
				 int microseconds);
NS_EXTERN int Ns_InitializeRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_DestroyRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_ReadLockRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_ReadUnlockRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_WriteLockRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_WriteUnlockRWLock(Ns_RWLock *lock);
NS_EXTERN int Ns_InitializeSemaphore(Ns_Semaphore *sema, int initCount);
NS_EXTERN int Ns_DestroySemaphore(Ns_Semaphore *sema);
NS_EXTERN int Ns_WaitForSemaphore(Ns_Semaphore *sema);
NS_EXTERN int Ns_ReleaseSemaphore(Ns_Semaphore *sema, int count);
NS_EXTERN int Ns_AllocThreadLocalStorage(Ns_ThreadLocalStorage *tls,
				      Ns_TlsCleanup *cleanup);
NS_EXTERN int Ns_SetThreadLocalStorage(Ns_ThreadLocalStorage *tls, void *p);
NS_EXTERN int Ns_GetThreadLocalStorage(Ns_ThreadLocalStorage *tls, void **p);
NS_EXTERN int Ns_WaitForThread(Ns_Thread *thrPtr);
NS_EXTERN int Ns_WaitThread(Ns_Thread *thrPtr, int *exitCodePtr);
NS_EXTERN void Ns_ExitThread(int exitCode);
NS_EXTERN int Ns_BeginDetachedThread(Ns_ThreadProc *proc, void *arg);
NS_EXTERN int Ns_BeginThread(Ns_ThreadProc *proc, void *arg, Ns_Thread *thrPtr);
NS_EXTERN int Ns_GetThreadId(void);
NS_EXTERN void Ns_GetThread(Ns_Thread *threadPtr);
NS_EXTERN char *NsThreadLibName(void);

/*
 * cs.c:
 */

NS_EXTERN void Ns_CsInit(Ns_Cs *csPtr);
NS_EXTERN void Ns_CsDestroy(Ns_Cs *csPtr);
NS_EXTERN void Ns_CsEnter(Ns_Cs *csPtr);
NS_EXTERN void Ns_CsLeave(Ns_Cs *csPtr);

/*
 * fork.c:
 */

NS_EXTERN int ns_fork(void);
NS_EXTERN int Ns_Fork(void);

/*
 * master.c:
 */

NS_EXTERN void Ns_MasterLock(void);
NS_EXTERN void Ns_MasterUnlock(void);

/*
 * memory.c:
 */

NS_EXTERN int nsMemPools;
NS_EXTERN void *ns_malloc(size_t size);
NS_EXTERN void *ns_calloc(size_t num, size_t size);
NS_EXTERN void ns_free(void *buf);
NS_EXTERN void *ns_realloc(void *buf, size_t size);
NS_EXTERN char *ns_strdup(char *string);
NS_EXTERN char *ns_strcopy(char *string);

/*
 * mutex.c:
 */

NS_EXTERN void Ns_MutexInit(Ns_Mutex *mutexPtr);
NS_EXTERN void Ns_MutexInit2(Ns_Mutex *mutexPtr, char *prefix);
NS_EXTERN void Ns_MutexDestroy(Ns_Mutex *mutexPtr);
NS_EXTERN void Ns_MutexLock(Ns_Mutex *mutexPtr);
NS_EXTERN int  Ns_MutexTryLock(Ns_Mutex *mutexPtr);
NS_EXTERN void Ns_MutexUnlock(Ns_Mutex *mutexPtr);
NS_EXTERN void Ns_MutexSetName(Ns_Mutex *mutexPtr, char *name);
NS_EXTERN void Ns_MutexSetName2(Ns_Mutex *mutexPtr, char *prefix, char *name);
NS_EXTERN void Ns_MutexEnum(Ns_MutexInfoProc *proc, void *arg);


/*
 * oldpool.c:
 */

NS_EXTERN Ns_Pool *Ns_PoolCreate(char *name);
NS_EXTERN void Ns_PoolDestroy(Ns_Pool *pool);
NS_EXTERN void *Ns_PoolAlloc(Ns_Pool *pool, size_t size);
NS_EXTERN void Ns_PoolFree(Ns_Pool *pool, void *cp);
NS_EXTERN void *Ns_PoolRealloc(Ns_Pool *pool, void *ptr, size_t size);
NS_EXTERN void *Ns_PoolCalloc(Ns_Pool *pool, size_t elsize, size_t nelem);
NS_EXTERN char *Ns_PoolStrDup(Ns_Pool *pool, char *old);
NS_EXTERN char *Ns_PoolStrCopy(Ns_Pool *pool, char *old);
NS_EXTERN Ns_Pool *Ns_ThreadPool(void);
NS_EXTERN void *Ns_ThreadMalloc(size_t size);
NS_EXTERN void *Ns_ThreadAlloc(size_t size);
NS_EXTERN void *Ns_ThreadRealloc(void *ptr, size_t size);
NS_EXTERN void Ns_ThreadFree(void *ptr);
#define Ns_ThreadAlloc	Ns_ThreadMalloc
NS_EXTERN void *Ns_ThreadCalloc(size_t nelem, size_t elsize);
NS_EXTERN char *Ns_ThreadStrDup(char *old);
NS_EXTERN char *Ns_ThreadStrCopy(char *old);

/*
 * pthread.c, sproc.c, win32.c:
 */

NS_EXTERN void Ns_CondInit(Ns_Cond *condPtr);
NS_EXTERN void Ns_CondDestroy(Ns_Cond *condPtr);
NS_EXTERN void Ns_CondSignal(Ns_Cond *condPtr);
NS_EXTERN void Ns_CondBroadcast(Ns_Cond *condPtr);
NS_EXTERN void Ns_CondWait(Ns_Cond *condPtr, Ns_Mutex *lockPtr);
NS_EXTERN int Ns_CondTimedWait(Ns_Cond *condPtr, Ns_Mutex *lockPtr,
			    Ns_Time *timePtr);

/*
 * reentrant.c:
 */

NS_EXTERN struct dirent *ns_readdir(DIR * pDir);
NS_EXTERN struct tm *ns_localtime(const time_t * clock);
NS_EXTERN struct tm *ns_gmtime(const time_t * clock);
NS_EXTERN char *ns_ctime(const time_t * clock);
NS_EXTERN char *ns_asctime(const struct tm *tmPtr);
NS_EXTERN char *ns_strtok(char *s1, const char *s2);
NS_EXTERN char *ns_inet_ntoa(struct in_addr addr);

/*
 * rwlock.c:
 */

NS_EXTERN void Ns_RWLockInit(Ns_RWLock *lockPtr);
NS_EXTERN void Ns_RWLockDestroy(Ns_RWLock *lockPtr);
NS_EXTERN void Ns_RWLockRdLock(Ns_RWLock *lockPtr);
NS_EXTERN void Ns_RWLockWrLock(Ns_RWLock *lockPtr);
NS_EXTERN void Ns_RWLockUnlock(Ns_RWLock *lockPtr);

/*
 * sema.c:
 */

NS_EXTERN void Ns_SemaInit(Ns_Sema *semaPtr, int initCount);
NS_EXTERN void Ns_SemaDestroy(Ns_Sema *semaPtr);
NS_EXTERN void Ns_SemaWait(Ns_Sema *semaPtr);
NS_EXTERN void Ns_SemaPost(Ns_Sema *semaPtr, int count);

/*
 * signal.c:
 */

#ifndef WIN32
NS_EXTERN int ns_sigmask(int how, sigset_t * set, sigset_t * oset);
NS_EXTERN int ns_sigwait(sigset_t * set, int *sig);
NS_EXTERN int ns_signal(int sig, void (*proc)(int));
#endif

/*
 * thread.c:
 */

NS_EXTERN void Ns_ThreadCreate(Ns_ThreadProc *proc, void *arg, long stackSize,
			    Ns_Thread *resultPtr);
NS_EXTERN void Ns_ThreadCreate2(Ns_ThreadProc *proc, void *arg, long stackSize,
			    int flags, Ns_Thread *resultPtr);
NS_EXTERN void Ns_ThreadExit(void *arg);
NS_EXTERN void Ns_ThreadJoin(Ns_Thread *threadPtr, void **argPtr);
NS_EXTERN void Ns_ThreadYield(void);
NS_EXTERN void Ns_ThreadSetName(char *name);
NS_EXTERN void Ns_ThreadEnum(Ns_ThreadInfoProc *proc, void *arg);
NS_EXTERN int Ns_ThreadId(void);
NS_EXTERN void Ns_ThreadSelf(Ns_Thread *thrPtr);
NS_EXTERN int Ns_CheckStack(void);
NS_EXTERN char *Ns_ThreadGetName(void);
NS_EXTERN char *Ns_ThreadGetParent(void);
NS_EXTERN void  Ns_ThreadMemStats(FILE *fp);

/*
 * time.c:
 */

NS_EXTERN void Ns_GetTime(Ns_Time *timePtr);
NS_EXTERN void Ns_AdjTime(Ns_Time *timePtr);
NS_EXTERN void Ns_DiffTime(Ns_Time *t1, Ns_Time *t0, Ns_Time *resultPtr);
NS_EXTERN void Ns_IncrTime(Ns_Time *timePtr, time_t sec, long usec);

/*
 * tls.c:
 */

NS_EXTERN void Ns_TlsAlloc(Ns_Tls *tlsPtr, Ns_TlsCleanup *cleanup);
NS_EXTERN void Ns_TlsSet(Ns_Tls *tlsPtr, void *value);
NS_EXTERN void *Ns_TlsGet(Ns_Tls *tlsPtr);


/*
 * Global variables which may be modified before calling any Ns_Thread
 * routines.
 */

NS_EXTERN long nsThreadStackSize;	/* Thread stack size (default: 64k). */
NS_EXTERN int nsThreadMutexMeter;	/* Meter mutex trylock fails. */

/*
 * Efficiency macros for renamed functions.
 */

#define Ns_Readdir ns_readdir
#define Ns_Localtime ns_localtime
#define Ns_Gmtime ns_gmtime
#define Ns_Ctime ns_ctime
#define Ns_Asctime ns_asctime
#define Ns_Strtok ns_strtok
#define Ns_InetNtoa ns_inet_ntoa
#define Ns_Signal ns_signal
#define Ns_Sigmask ns_sigmask
#define Ns_Sigwait ns_sigwait
#define Ns_Malloc ns_malloc
#define Ns_Realloc ns_realloc
#define Ns_Free ns_free
#define Ns_Calloc ns_calloc
#define Ns_StrDup ns_strdup
#define Ns_StrCopy ns_strcopy
#define Ns_Fork ns_fork

#endif /* NSTHREAD_H */

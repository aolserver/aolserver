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
 * thread.h --
 *
 *	Internal nsthread definitions.
 *
 * RCS: $Id: thread.h,v 1.5 2000/10/20 21:54:09 jgdavidson Exp $
 *
 */

#ifndef THREAD_H
#define THREAD_H

#include "nsthread.h"

extern int nsMemPools;

#define NBUCKETS 13

struct Block;

typedef struct Pool {
    int     nfree[NBUCKETS];
    int     nused[NBUCKETS];
    struct Block *firstPtr[NBUCKETS];
} Pool;

/*
 * The following structure maintains all state for a thread
 * including thread local storage keys and condition queue pointers
 * on Sproc.
 */

typedef struct Thread {
    struct Thread  *nextPtr;	/* Next in list of all threads. */
    time_t	    ctime;	/* Thread structure create time. */
    int		    flags;	/* Detached, joined, etc. */
    Ns_ThreadProc  *proc;	/* Thread startup routine. */ 
    void           *arg;	/* Argument to startup proc. */
    int             tid;        /* Small id for thread (logging and such). */
    char	    name[NS_THREAD_NAMESIZE+1]; /* Thread name. */
    char	    parent[NS_THREAD_NAMESIZE+1]; /* Parent name. */
    Ns_Pool	   *pool;	/* Per-thread memory pool. */
    long	    stackSize;	/* Stack size in bytes for this thread. */
    void	   *stackBase;	/* Approximate stack base for Ns_CheckStack. */
    void	   *exitarg;	/* Return code from Ns_ExitThread. */
    Pool      	    memPool; 	/* Fast memory block cache pool. */
    void           *tlsPtr[NS_THREAD_MAXTLS]; /* TLS slots. */
}               Thread;

/*
 * The following structure defines a metered mutex.
 */

typedef struct Mutex {
    struct Mutex *nextPtr;
    struct Thread *ownerPtr;
    int id;
    char name[NS_THREAD_NAMESIZE+1];
    unsigned long nlock;
    unsigned long nbusy;
    void *lock;
} Mutex;

/*
 * Global functions used within the nsthread core.
 */

extern Thread  *NsGetThread(void);
extern void     NsSetThread(Thread *thrPtr);
extern Thread  *NsNewThread(void);
extern Thread  *NsNewThread2(Ns_ThreadProc *proc, void *arg, long stack, int flags);
extern void     NsCleanupThread(Thread *thrPtr);
extern void	NsCleanupTls(Thread *thrPtr);
extern void     NsThreadCreate(Thread *thrPtr);
extern void     NsThreadExit(void);
extern void     NsThreadMain(void *arg);
extern void     NsThreadError(char *fmt, ...);
extern void     NsThreadAbort(char *fmt, ...);
extern void     NsThreadFatal(char *nsFuncName, char *osFuncName, int errNum);
extern void	NsMutexInit(void **lockPtr);
extern void	NsMutexDestroy(void **lockPtr);
extern void	NsMutexLock(void **lockPtr);
extern int	NsMutexTryLock(void **lockPtr);
extern void	NsMutexUnlock(void **lockPtr);
extern Mutex   *NsGetMutex(Ns_Mutex *mutexPtr);
extern void    *NsGetCond(Ns_Cond *condPtr);

#define GETMUTEX(mPtr)	(*(mPtr)?((Mutex *)*(mPtr)):NsGetMutex((mPtr)))
#define GETCOND(cPtr)	(*(cPtr)?((void *)*(cPtr)):NsGetCond((cPtr)))

/*
 * Pools
 */

extern void	NsInitPools(void);
extern void    *NsPoolRealloc(void *ptr, size_t size);
extern void    *NsPoolMalloc(size_t size);
extern void     NsPoolFree(void *ptr);
extern void	NsPoolFlush(Pool *poolPtr);
extern void	NsPoolDump(Pool *poolPtr, FILE *fp);

#endif /* THREAD_H */

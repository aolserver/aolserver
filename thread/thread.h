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
 * RCS: $Id: thread.h,v 1.10 2001/03/26 22:08:59 jgdavidson Exp $
 *
 */

#ifndef THREAD_H
#define THREAD_H

#include "nsthread.h"

/*
 * The following constants define the default and minimum stack
 * sizes for new threads.
 */

#define STACK_DEFAULT	65536	/* 64k */
#define STACK_MIN	16384	/* 16k */

/*
 * The following defines the estimated stack space required to
 * return an NS_OK in Ns_CheckStack().
 */

#define STACK_CHECK	2048	/* 2k */

/*
 * The following structure maintains all state for a thread
 * including thread local storage slots.
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
    void           *tlsPtr[NS_THREAD_MAXTLS]; /* TLS slots. */
}               Thread;

/*
 * The following structure defines a metered mutex with
 * a platform specific lock.
 */

typedef struct Mutex {
    struct Mutex    *nextPtr;
    struct Thread   *ownerPtr;
    void	    *lock;
    int		     id;
    unsigned long    nlock;
    unsigned long    nbusy;
    char	     name[NS_THREAD_NAMESIZE+1];
} Mutex;

/*
 * The following platform specific routines are provided by
 * interface code.
 */

extern void    *NsLockAlloc(void);
extern void	NsLockFree(void *lock);
extern void	NsLockSet(void *lock);
extern int	NsLockTry(void *lock);
extern void	NsLockUnset(void *lock);
extern Thread  *NsGetThread(void);
extern void     NsSetThread(Thread *thrPtr);
extern void     NsThreadCreate(Thread *thrPtr);
extern void     NsThreadExit(void);

/*
 * The following routines are platform independent core API's.
 */

extern Thread  *NsNewThread(void);
extern void     NsCleanupThread(Thread *thrPtr);
extern void	NsCleanupTls(Thread *thrPtr);
extern void     NsThreadMain(void *arg);
extern void     NsThreadError(char *fmt, ...);
extern void     NsThreadAbort(char *fmt, ...);
extern void     NsThreadFatal(char *nsFuncName, char *osFuncName, int errNum);

/*
 * The following macros and API's are for self-initializing
 * statically allocated mutex and condition objects.
 */

#define GETMUTEX(mPtr)	(*(mPtr)?((Mutex *)*(mPtr)):NsGetMutex((mPtr)))
#define GETCOND(cPtr)	(*(cPtr)?((void *)*(cPtr)):NsGetCond((cPtr)))
extern Mutex   *NsGetMutex(Ns_Mutex *mutexPtr);
extern void    *NsGetCond(Ns_Cond *condPtr);

/*
 * The following routines are for allocating thread objects,
 * bypassing the Pool API's.
 */

extern void    *NsAlloc(size_t size);
extern void     NsFree(void *);

#endif /* THREAD_H */

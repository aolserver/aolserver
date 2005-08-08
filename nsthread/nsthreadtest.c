/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.com/.
 * 
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 * 
 * The Original Code is AOLserver Code and related documentation distributed by
 * AOL.
 * 
 * The Initial Developer of the Original Code is America Online, Inc. Portions
 * created by AOL are Copyright (C) 1999 America Online, Inc. All Rights
 * Reserved.
 * 
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License (the "GPL"), in which case the provisions of
 * GPL are applicable instead of those above.  If you wish to allow use of
 * your version of this file only under the terms of the GPL and not to allow
 * others to use your version of this file under the License, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL. If you do not delete the
 * provisions above, a recipient may use your version of this file under
 * either the License or the GPL.
 */

/*
 * test.c -
 *
 *	Collection of thread interface tests.  This code is somewhat sloppy
 *	but contains several examples of of using conditions, mutexes,
 *	thread local storage, and creating/joining threads.
 */

#include "nsthread.h"

/*
 * Special direct include of pthread.h for compatibility tests.
 */

#ifdef _WIN32
#define PTHREAD_TEST 0
#else
#include <pthread.h>
#define PTHREAD_TEST 1
#endif

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/nsthreadtest.c,v 1.6 2005/08/08 11:30:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

/*
 * Collection of synchronization objects for tests.
 */

static Ns_Mutex block;
static Ns_Mutex mlock;
static Ns_Mutex lock;
static Ns_Cond  cond;
static Ns_Tls   key;
static Ns_RWLock rwlock;
static Ns_Sema  sema;
static Ns_Cs    cs;
static Ns_Mutex dlock;
static Ns_Cond  dcond;
static int      dstop;

/*
 * Msg -
 *
 *	Simple message logger with thread id and name.
 */

void
Msg(char *fmt,...)
{
    va_list         ap;
    char           *s, *r;
    time_t          now;

    time(&now);
    s = ns_ctime(&now);
    r = strchr(s, '\n');
    if (r) {
	*r = '\0';
    }
    va_start(ap, fmt);
    Ns_MutexLock(&mlock);
    printf("[%s][%s]: ", Ns_ThreadGetName(), s);
    vfprintf(stdout, fmt, ap);
    printf("\n");
    Ns_MutexUnlock(&mlock);
    va_end(ap);
}

/*
 * TlsLogArg -
 *
 *	Log and then free TLS slot data at thread exit.
 */ 

void
TlsLogArg(void *arg)
{
    int            *ip = arg;

    Msg("tls cleanup %d", *ip);
    ns_free(ip);
}

/*
 * RecursiveStackCheck, CheckStackThread -
 *
 *	Thread which recursively probes stack for max depth.
 */

int
RecursiveStackCheck(int n)
{
    if (Ns_CheckStack() == NS_OK) {
	n = RecursiveStackCheck(n);
    }
    ++n;
    return n;
}

void 
CheckStackThread(void *arg)
{
    int n;

    Ns_ThreadSetName("checkstack");
    n = RecursiveStackCheck(0);
    Ns_ThreadExit((void *) n);
}

/*
 * WorkThread -
 *
 *	Thread which exercies a varity of sync objects and TLS.
 */

void
WorkThread(void *arg)
{
    int             i = (int) arg;
    int            *ip;
    time_t          now;
    Ns_Thread       self;
    char            name[32];

    sprintf(name, "-work:%d-", i);
    Ns_ThreadSetName(name);

    if (i == 2) {
	Ns_RWLockWrLock(&rwlock);
	Msg("rwlock write aquired");
	sleep(2);
    } else {
	Ns_RWLockRdLock(&rwlock);
	Msg("rwlock read aquired aquired");
	sleep(1);
    }
    Ns_CsEnter(&cs);
    Msg("enter critical section once");
    Ns_CsEnter(&cs);
    Msg("enter critical section twice");
    Ns_CsLeave(&cs);
    Ns_CsLeave(&cs);
    Ns_ThreadSelf(&self);
    arg = Ns_TlsGet(&key);
    Ns_SemaWait(&sema);
    Msg("got semaphore posted from main");
    if (arg == NULL) {
	arg = ns_malloc(sizeof(int));
	Ns_TlsSet(&key, arg);
    }
    ip = arg;
    *ip = i;

    if (i == 5) {
	Ns_Time         to;
	int             st;

	Ns_GetTime(&to);
	Msg("time: %ld %ld", to.sec, to.usec);
	Ns_IncrTime(&to, 5, 0);
	Msg("time: %ld %ld", to.sec, to.usec);
	Ns_MutexLock(&lock);
	time(&now);
	Msg("timed wait starts: %s", ns_ctime(&now));
	st = Ns_CondTimedWait(&cond, &lock, &to);
	Ns_MutexUnlock(&lock);
	time(&now);
	Msg("timed wait ends: %s - status: %d", ns_ctime(&now), st);
    }
    if (i == 9) {
	Msg("sleep 4 seconds start");
	sleep(4);
	Msg("sleep 4 seconds done");
    }
    time(&now);
    Ns_RWLockUnlock(&rwlock);
    Msg("rwlock unlocked");
    Msg("exiting");
    Ns_ThreadExit((void *) i);
}

/*
 * AtExit -
 *
 *	Test of atexit() handler.
 */

void
AtExit(void)
{
    Msg("atexit handler called!");
}

/*
 * MemThread, MemTime -
 *
 *	Time allocations of malloc and zippy ns_malloc.
 */

#define NA 10000

int             nthreads = 10;
int             memstart;
int             nrunning;

void
MemThread(void *arg)
{
    int             i;
    void           *ptr;

    Ns_ThreadSetName("memthread");
    Ns_MutexLock(&lock);
    ++nrunning;
    Ns_CondBroadcast(&cond);
    while (!memstart) {
	Ns_CondWait(&cond, &lock);
    }
    Ns_MutexUnlock(&lock);

    ptr = NULL;
    for (i = 0; i < NA; ++i) {
	if (arg) {
	    if (ptr)
		ns_free(ptr);
	    ptr = ns_malloc(10);
	} else {
	    if (ptr)
		free(ptr);
	    ptr = malloc(10);
	}
    }
}

void
MemTime(int ns)
{
    Ns_Time         start, end, diff;
    int             i;
    Ns_Thread      *tids;

    tids = ns_malloc(sizeof(Ns_Thread *) * nthreads);
    Ns_MutexLock(&lock);
    nrunning = 0;
    memstart = 0;
    Ns_MutexUnlock(&lock);
    printf("starting %d %smalloc threads...", nthreads, ns ? "ns_" : "");
    fflush(stdout);
    Ns_GetTime(&start);
    for (i = 0; i < nthreads; ++i) {
	Ns_ThreadCreate(MemThread, (void *) ns, 0, &tids[i]);
    }
    Ns_MutexLock(&lock);
    while (nrunning < nthreads) {
	Ns_CondWait(&cond, &lock);
    }
    printf("waiting....");
    fflush(stdout);
    memstart = 1;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
    for (i = 0; i < nthreads; ++i) {
	Ns_ThreadJoin(&tids[i], NULL);
    }
    Ns_GetTime(&end);
    Ns_DiffTime(&end, &start, &diff);
    printf("done:  %d seconds, %d usec\n", (int) diff.sec, (int) diff.usec);
}


void
DumpString(Tcl_DString *dsPtr)
{
    char **largv;
    int i, largc;

    if (Tcl_SplitList(NULL, dsPtr->string, &largc, (CONST char***)&largv) == TCL_OK) {
	for (i = 0; i < largc; ++i) {
	    printf("\t%s\n", largv[i]);
	}
	ckfree((char *) largv);
    }
    Tcl_DStringTrunc(dsPtr, 0);
}


void
DumperThread(void *arg)
{
    Ns_Time         to;
    Tcl_DString     ds;

    Tcl_DStringInit(&ds);
    Ns_ThreadSetName("-dumper-");
    Ns_MutexLock(&block);
    Ns_MutexLock(&dlock);
    while (!dstop) {
	Ns_GetTime(&to);
	Ns_IncrTime(&to, 1, 0);
	Ns_CondTimedWait(&dcond, &dlock, &to);
	Ns_MutexLock(&mlock);
	Ns_ThreadList(&ds, NULL);
	DumpString(&ds);
	Ns_MutexList(&ds);
	DumpString(&ds);
#if !defined(_WIN32) && defined(USE_THREAD_ALLOC) && (STATIC_BUILD == 0)
	/* NB: Not yet exported in WIN32 Tcl. */
	Tcl_GetMemoryInfo(&ds);
#endif
	DumpString(&ds);
	Ns_MutexUnlock(&mlock);
    }
    Ns_MutexUnlock(&dlock);
    Ns_MutexUnlock(&block);
}


static void
DetachedThread(void *ignored)
{
    int i;

    for (i = 0; i < 10; ++i) {
	Msg("\n\ndetached! %d", i);
	sleep(1);
    }
}

#if PTHREAD_TEST

/*
 * Routines to test compatibility with pthread-created
 * threads, i.e., that non-Ns_ThreadCreate'd threads
 * can call Ns API's which will cleanup at thread exit.
 */

static Ns_Mutex plock;
static Ns_Cond pcond;
static int pgo;

void
PthreadTlsCleanup(void *arg)
{
    int i = (int) arg;
    printf("pthread[%d]: log: %d\n", (int) pthread_self(), i);
}

void *
Pthread(void *arg)
{
    static Ns_Tls tls;

    /* 
     * Allocate TLS first time (this is recommended TLS
     * self-initialization style.
     */

    Ns_ThreadSetName("pthread");
    sleep(5);
    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	     Ns_TlsAlloc(&tls, PthreadTlsCleanup);
	}
	Ns_MasterUnlock();
    }

    Ns_TlsSet(&tls, arg);

    /*
     * Wait for exit signal from main().
     */

    Ns_MutexLock(&plock);
    while (!pgo) {
	Ns_CondWait(&pcond, &plock);
    }
    Ns_MutexUnlock(&plock);
    return arg;
}

#endif

/*
 * main -
 *
 *	Fire off a bunch of weird threads to exercise the thread
 *	interface.
 */

int main(int argc, char *argv[])
{
    int             i, code;
    Ns_Thread       threads[10];
    Ns_Thread       self, dumper;
    void *arg;
    char *p;
#if PTHREAD_TEST
    pthread_t tids[10];
#endif

    NsThreads_LibInit();
    Ns_ThreadSetName("-main-");

    /*
     * Jump directly to memory test if requested. 
     */

    for (i = 1; i < argc; ++i) {
	p = argv[i];
	switch (*p) {
	    case 'n':
		break;
	    case 'm':
	    	nthreads = atoi(p + 1);
		goto mem;
		break;
	}
    }

    Ns_ThreadCreate(DetachedThread, NULL, 0, NULL);
    Ns_ThreadCreate(DumperThread, NULL, 0, &dumper);
    Ns_MutexSetName(&lock, "startlock");
    Ns_MutexSetName(&dlock, "dumplock");
    Ns_MutexSetName(&mlock, "msglock");
    Ns_MutexSetName(&block, "busylock");
    Ns_ThreadStackSize(81920);
    Ns_SemaInit(&sema, 3);
    Msg("sema initialized to 3");
    atexit(AtExit);
    Msg("pid = %d", getpid());
    Ns_TlsAlloc(&key, TlsLogArg);
    for (i = 0; i < 10; ++i) {
	Msg("starting work thread %d", i);
	Ns_ThreadCreate(WorkThread, (void *) i, 0, &threads[i]);
    }
    sleep(1);
    /* Ns_CondSignal(&cond); */
    Ns_SemaPost(&sema, 10);
    Msg("sema post 10");
    Ns_RWLockWrLock(&rwlock);
    Msg("rwlock write locked (main thread)");
    sleep(1);
    Ns_RWLockUnlock(&rwlock);
    Msg("rwlock write unlocked (main thread)");
    for (i = 0; i < 10; ++i) {
	Msg("waiting for thread %d to exit", i);
	Ns_ThreadJoin(&threads[i], (void **) &code);
	Msg("thread %d exited - code: %d", i, code);
    }
#if PTHREAD_TEST
    for (i = 0; i < 10; ++i) {
	pthread_create(&tids[i], NULL, Pthread, (void *) i);
	printf("pthread: create %d = %d\n", i, (int) tids[i]);
	Ns_ThreadYield();
    }
    Ns_MutexLock(&plock);
    pgo = 1;
    Ns_MutexUnlock(&plock);
    Ns_CondBroadcast(&pcond);
    for (i = 0; i < 10; ++i) {
	pthread_join(tids[i], &arg);
	printf("pthread: join %d = %d\n", i, (int) arg);
    }
#endif
    Ns_ThreadSelf(&self);
    Ns_MutexLock(&dlock);
    dstop = 1;
    Ns_CondSignal(&dcond);
    Ns_MutexUnlock(&dlock);
    Ns_ThreadJoin(&dumper, NULL);
    Msg("threads joined");
    for (i = 0; i < 10; ++i) {
	Ns_ThreadCreate(CheckStackThread, NULL, 8192*(i+1), &threads[i]);
    }
    for (i = 0; i < 10; ++i) {
        Ns_ThreadJoin(&threads[i], &arg);
	printf("check stack %d = %d\n", i, (int) arg);
    }
    /*Ns_ThreadEnum(DumpThreads, NULL);*/
    /*Ns_MutexEnum(DumpLocks, NULL);*/
mem:
    MemTime(0);
    MemTime(1);
    return 0;
}

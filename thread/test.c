/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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


#include "nsthread.h"
#undef Ns_ThreadMalloc

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/test.c,v 1.2 2000/05/02 14:39:33 kriston Exp $, compiled: " __DATE__ " " __TIME__;

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


void
junk(void *arg)
{
    int            *ip = arg;

    Msg("tls cleanup %d", *ip);
    ns_free(ip);
}

int
CheckStack(int n)
{
    if (Ns_CheckStack() == NS_OK) {
	n = CheckStack(n);
    }
    ++n;
    return n;
}


void
thread(void *arg)
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


void
PauseThread(void *arg)
{
    int             n = (int) arg;

    Ns_ThreadSetName("-pausethread-");
    Msg("sleep %d seconds start", n);
    sleep(n);
    Msg("sleep %d seconds end", n);
}


void
AtExit(void)
{
    Msg("atexit handler called!");
}


#define NA 10000

int             nthreads = 10;
int             memstart;
int             nrunning;

void
MemThread(void *arg)
{
    int             i;
    void           *ptr;

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
DumpLock(Ns_MutexInfo * infoPtr, void *arg)
{
    printf("%32s: %s %10lu %10lu\n", infoPtr->name,
	   infoPtr->owner ? infoPtr->owner : "(unlocked)",
	   infoPtr->nlock, infoPtr->nbusy);
}

void
DumpThread(Ns_ThreadInfo * iPtr, void *ignored)
{
    printf("\t%d(%d): %s %s %p %p %s", iPtr->tid, iPtr->flags, iPtr->name, iPtr->parent,
	   iPtr->proc, iPtr->arg, ns_ctime(&iPtr->ctime));
}


void
Dumper(void *arg)
{
    Ns_Time         to;

    Ns_ThreadSetName("-dumper-");
    Ns_MutexLock(&block);
    Ns_MutexLock(&dlock);
    while (!dstop) {
	Ns_GetTime(&to);
	Ns_IncrTime(&to, 1, 0);
	Ns_CondTimedWait(&dcond, &dlock, &to);
	Ns_MutexLock(&mlock);
	printf("current threads:\n");
	Ns_ThreadEnum(DumpThread, NULL);
	printf("current locks:\n");
	Ns_MutexEnum(DumpLock, NULL);
	Ns_MutexUnlock(&mlock);
    }
    Ns_MutexUnlock(&dlock);
    Ns_MutexUnlock(&block);
}


int
main(int argc, char *argv[])
{
    int             i, code;
    Ns_Thread       threads[10];
    Ns_Thread       self, dumper;
    extern int      nsMemPools;

    nsThreadMutexMeter = 1;
    Ns_ThreadSetName("-main-");
    nsMemPools = 1;
    if (argv[1] != NULL && argv[1][0] == 'm') {
	i = atoi(argv[1] + 1);
	if (i > 0) {
	    nthreads = i;
	}
	goto mem;
    }
    Ns_ThreadCreate(Dumper, NULL, 0, &dumper);
    Ns_MutexSetName(&lock, "startlock");
    Ns_MutexSetName(&dlock, "dumplock");
    Ns_MutexSetName(&mlock, "msglock");
    Ns_MutexSetName(&block, "busylock");
    nsThreadStackSize = 81920;
    Ns_SemaInit(&sema, 3);
    Msg("sema initialized to 3");
    atexit(AtExit);
    Ns_ThreadCreate(PauseThread, (void *) 30, 0, NULL);
    Ns_ThreadCreate(PauseThread, (void *) 30, 0, NULL);
    Ns_ThreadCreate(PauseThread, (void *) 30, 0, NULL);
    Msg("pid = %d", getpid());
    Ns_TlsAlloc(&key, junk);
    for (i = 0; i < 10; ++i) {
	Msg("starting thread %d", i);
	Ns_ThreadCreate(thread, (void *) i, 0, &threads[i]);
    }
    sleep(2);
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
    Ns_ThreadSelf(&self);
    Ns_MutexLock(&dlock);
    dstop = 1;
    Ns_CondSignal(&dcond);
    Ns_MutexUnlock(&dlock);
    Ns_ThreadJoin(&dumper, NULL);
    Msg("threads joined");
mem:
    MemTime(0);
    MemTime(1);
    Ns_ThreadEnum(DumpThread, NULL);
    Ns_MutexEnum(DumpLock, NULL);
    return 0;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/sched.c,v 1.14 2003/03/10 16:02:05 mpagenva Exp $, compiled: " __DATE__ " " __TIME__;

/*
 * sched.c --
 *
 *	Support for the background task and scheduled procedure
 *	interfaces.  The implementation is based on the paper:
 *
 *	"A Heap-based Callout Implementation to Meet Real-Time Needs",
 *	by Barkley and Lee, in "Proceeding of the Summer 1988 USENIX
 *	Conference".
 *
 *	The heap code in particular is based on:
 *
 *	"Chapter 9. Priority Queues and Heapsort", Sedgewick "Algorithms
 *	in C, 3rd Edition", Addison-Wesley, 1998.
 */

#include "nsd.h"

/*
 * The following structure defines a scheduled event.
 */
 
typedef struct Event {
    struct Event   *nextPtr;
    Tcl_HashEntry  *hPtr;	/* Entry in event hash or NULL if deleted. */
    unsigned int    id;		/* Unique event id. */
    int             qid;	/* Current priority queue id. */
    time_t          nextqueue;	/* Next time to queue for run. */
    time_t	    lastqueue;	/* Last time queued for run. */
    time_t	    laststart;	/* Last time run started. */
    time_t	    lastend;	/* Last time run finished. */
    int             flags;	/* One or more of NS_SCHED_ONCE, NS_SCHED_THREAD,
				 * NS_SCHED_DAILY, or NS_SCHED_WEEKLY. */
    int             interval;	/* Interval specification. */
    Ns_SchedProc   *proc;	/* Procedure to execute. */
    void           *arg;	/* Client data for procedure. */
    Ns_SchedProc   *deleteProc;	/* Procedure to cleanup when done (if any). */
} Event;

/*
 * Local functions defined in this file.
 */

static Ns_ThreadProc SchedThread;	/* Detached event firing thread. */
static Ns_ThreadProc EventThread;	/* Proc for NS_SCHED_THREAD events. */
static void QueueEvent(Event *ePtr, time_t *nowPtr);	/* Queue event on heap. */
static Event *DeQueueEvent(int qid);	/* Remove event from heap. */
static void FreeEvent(Event *ePtr);	/* Free completed or cancelled event. */

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable eventsTable; /* Hash table of events. */
static Ns_Mutex lock;		/* Lock around heap and hash table. */
static Ns_Cond  schedcond;	/* Condition to wakeup SchedThread. */
static Ns_Cond  eventcond;	/* Condition to wakeup EventThread(s). */
static Event  **queue;		/* Heap priority queue (dynamically re-sized). */
static int      nqueue;		/* Number of events in queue. */
static int      maxqueue;	/* Max queue events (dynamically re-sized). */
static int      running;
static int  	shutdownPending;
static Ns_Thread schedThread;
static int nThreads;
static int nIdleThreads;
static Event *threadEventPtr;
static Ns_Thread *eventThreads;

/*
 * Macro to exchange two events in the heap, used in QueueEvent() and
 * DeQueueEvent().
 */

#define EXCH(i,j) \
    {\
    	Event *tmp = queue[(i)];\
	queue[(i)] = queue[(j)], queue[(j)] = tmp;\
	queue[(i)]->qid = (i), queue[(j)]->qid = (j);\
    }
    

/*
 *----------------------------------------------------------------------
 *
 * NsInitSched -- 
 *
 *	Initialize scheduler API.
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
NsInitSched(void)
{
    Ns_MutexInit(&lock);
    Ns_MutexSetName(&lock, "ns:sched");
    Tcl_InitHashTable(&eventsTable, TCL_ONE_WORD_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_After -- 
 *
 *	Schedule a one-shot event.
 *
 * Results:
 *	Event id or NS_ERROR if delay is out of range.
 *
 * Side effects:
 *	See Ns_ScheduleProcEx().
 *
 *----------------------------------------------------------------------
 */

int
Ns_After(int delay, Ns_Callback *proc, void *arg, Ns_Callback *deleteProc)
{
    if (delay < 0) {
	return NS_ERROR;
    }
    return Ns_ScheduleProcEx((Ns_SchedProc *) proc, arg, NS_SCHED_ONCE,
    	    	    	     delay, (Ns_SchedProc *) deleteProc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ScheduleProc --
 *
 *	Schedule a proc to run at a given interval. 
 *
 * Results:
 *	Event id or NS_ERROR if interval is invalid.
 *
 * Side effects:
 *	See Ns_ScheduleProcEx().
 *
 *----------------------------------------------------------------------
 */

int
Ns_ScheduleProc(Ns_Callback *proc, void *arg, int thread, int interval)
{
    if (interval < 0) {
	return NS_ERROR;
    }
    return Ns_ScheduleProcEx((Ns_SchedProc *) proc, arg,
    	thread ? NS_SCHED_THREAD : 0, interval, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ScheduleDaily --
 *
 *	Schedule a proc to run once a day. 
 *
 * Results:
 *	Event id or NS_ERROR if hour and/or minute is out of range.
 *
 * Side effects:
 *	See Ns_ScheduleProcEx 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ScheduleDaily(Ns_SchedProc * proc, void *clientData, int flags,
	int hour, int minute, Ns_SchedProc *cleanupProc)
{
    int seconds;

    if (hour > 23   ||
	hour < 0    ||
	minute > 59 ||
	minute < 0) {
        return NS_ERROR;
    }
    seconds = (hour * 3600) + (minute * 60);
    return Ns_ScheduleProcEx(proc, clientData, flags | NS_SCHED_DAILY,
	seconds, cleanupProc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ScheduleWeekly --
 *
 *	Schedule a proc to run once a week. 
 *
 * Results:
 *	Event id or NS_ERROR if day, hour, and/or minute is out of range.
 *
 * Side effects:
 *	See Ns_ScheduleProcEx 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ScheduleWeekly(Ns_SchedProc * proc, void *clientData, int flags,
	int day, int hour, int minute, Ns_SchedProc *cleanupProc)
{
    int seconds;

    if (day < 0     ||
	day > 6     ||
	hour > 23   ||
	hour < 0    ||
	minute > 59 ||
	minute < 0) {
        return NS_ERROR;
    }
    seconds = (((day * 24) + hour) * 3600) + (minute * 60);
    return Ns_ScheduleProcEx(proc, clientData, flags | NS_SCHED_WEEKLY,
	seconds, cleanupProc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ScheduleProcEx --
 *
 *	Schedule a proc to run at a given interval.  The interpretation
 *	of interval (whether interative, daily, or weekly) is handled
 * 	by QueueEvent.
 *
 * Results:
 *	Event id of NS_ERROR if interval is out of range.
 *
 * Side effects:
 *	Event is allocated, hashed, and queued.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ScheduleProcEx(Ns_SchedProc *proc, void *arg, int flags,
	int interval, Ns_SchedProc *deleteProc)
{
    Event          *ePtr;
    int             id, new;
    static int	    nextId;
    time_t	    now;

    if (interval < 0) {
    	return NS_ERROR;
    }

    time(&now);
    ePtr = ns_malloc(sizeof(Event));
    ePtr->flags = flags;
    ePtr->nextqueue = 0;
    ePtr->lastqueue = ePtr->laststart = ePtr->lastend = -1;
    ePtr->interval = interval;
    ePtr->proc = proc;
    ePtr->deleteProc = deleteProc;
    ePtr->arg = arg;

    Ns_MutexLock(&lock);
    if (shutdownPending) {
    	id = NS_ERROR;
    	ns_free(ePtr);
    } else {
	do {
	    id = nextId++;
	    if (nextId < 0) {
	    	nextId = 0;
	    }
	    ePtr->hPtr = Tcl_CreateHashEntry(&eventsTable, (char *) id, &new);
	} while (!new);
	Tcl_SetHashValue(ePtr->hPtr, ePtr);
	ePtr->id = (unsigned int)id;
	QueueEvent(ePtr, &now);
    }
    Ns_MutexUnlock(&lock);

    return id;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Cancel, Ns_UnscheduleProc -- 
 *
 *	Cancel a previously scheduled event.
 *
 * Results:
 *	Ns_UnscheduleProc:  None.
 *	Ns_Cancel:          1 if cancelled, 0 otherwise.
 *
 * Side effects:
 *	See FreeEvent().
 *
 *----------------------------------------------------------------------
 */

void
Ns_UnscheduleProc(int id)
{
    (void) Ns_Cancel(id);
}

int
Ns_Cancel(int id)
{
    Tcl_HashEntry  *hPtr = NULL;
    Event          *ePtr = NULL;
    int		    cancelled;

    cancelled = 0;
    Ns_MutexLock(&lock);
    if (!shutdownPending) {
    	hPtr = Tcl_FindHashEntry(&eventsTable, (char *) id);
    	if (hPtr != NULL) {
	    ePtr = Tcl_GetHashValue(hPtr);
	    Tcl_DeleteHashEntry(hPtr);
	    ePtr->hPtr = NULL;
	    if (ePtr->qid > 0) {
	    	DeQueueEvent(ePtr->qid);
	    	cancelled = 1;
	    }
    	}
    }
    Ns_MutexUnlock(&lock);
    if (cancelled) {
	FreeEvent(ePtr);
    }
    return cancelled;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Pause -- 
 *
 *	Pause a schedule procedure.
 *
 * Results:
 *	1 if proc paused, 0 otherwise.
 *
 * Side effects:
 *	Proc will not run at the next scheduled time.
 *
 *----------------------------------------------------------------------
 */

int
Ns_Pause(int id)
{
    Tcl_HashEntry  *hPtr;
    Event          *ePtr;
    int		    paused;

    paused = 0;
    Ns_MutexLock(&lock);
    if (!shutdownPending) {
    	hPtr = Tcl_FindHashEntry(&eventsTable, (char *) id);
    	if (hPtr != NULL) {
	    ePtr = Tcl_GetHashValue(hPtr);
	    if (!(ePtr->flags & NS_SCHED_PAUSED)) {
		ePtr->flags |= NS_SCHED_PAUSED;
	    	if (ePtr->qid > 0) {
	    	    DeQueueEvent(ePtr->qid);
		}
		paused = 1;
	    }
    	}
    }
    Ns_MutexUnlock(&lock);
    return paused;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Resume -- 
 *
 *	Resume a scheduled proc.
 *
 * Results:
 *	1 if proc resumed, 0 otherwise.
 *
 * Side effects:
 *	Proc will be rescheduled.
 *
 *----------------------------------------------------------------------
 */

int
Ns_Resume(int id)
{
    Tcl_HashEntry  *hPtr;
    Event          *ePtr;
    int		    resumed;
    time_t	    now;

    resumed = 0;
    Ns_MutexLock(&lock);
    if (!shutdownPending) {
    	hPtr = Tcl_FindHashEntry(&eventsTable, (char *) id);
    	if (hPtr != NULL) {
	    ePtr = Tcl_GetHashValue(hPtr);
	    if ((ePtr->flags & NS_SCHED_PAUSED)) {
		ePtr->flags &= ~NS_SCHED_PAUSED;
		time(&now);
	    	QueueEvent(ePtr, &now);
		resumed = 1;
	    }
	}
    }
    Ns_MutexUnlock(&lock);
    return resumed;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartSchedShutdown, NsWaitSchedShutdown --
 *
 *	Inititiate and then wait for sched shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May timeout waiting for sched shutdown.
 *
 *----------------------------------------------------------------------
 */

void
NsStartSchedShutdown(void)
{
    Ns_MutexLock(&lock);
    if (running) {
    	Ns_Log(Notice, "sched: shutdown pending");
	shutdownPending = 1;
	Ns_CondSignal(&schedcond);
    }
    Ns_MutexUnlock(&lock);
}

void
NsWaitSchedShutdown(Ns_Time *toPtr)
{
    int status;
    
    Ns_MutexLock(&lock);
    status = NS_OK;
    while (status == NS_OK && running) {
	status = Ns_CondTimedWait(&schedcond, &lock, toPtr);
    }
    Ns_MutexUnlock(&lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "sched: timeout waiting for sched exit");
    } else if (schedThread != NULL) {
	Ns_ThreadJoin(&schedThread, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * QueueEvent --
 *
 *	Add an event to the priority queue heap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	SchedThread() may be created and/or signalled.
 *
 *----------------------------------------------------------------------
 */

static void
QueueEvent(Event *ePtr, time_t *nowPtr)
{
    struct tm      *tp;

    if (ePtr->flags & NS_SCHED_PAUSED) {
	return;
    }

    /*
     * Calculate the time from now in seconds this event should run.
     */
     
    if (ePtr->flags & (NS_SCHED_DAILY | NS_SCHED_WEEKLY)) {
	tp = ns_localtime(nowPtr);
	tp->tm_sec = ePtr->interval;
	tp->tm_hour = 0;
	tp->tm_min = 0;
	if (ePtr->flags & NS_SCHED_WEEKLY) {
	    tp->tm_mday -= tp->tm_wday;
	}
	ePtr->nextqueue = mktime(tp);
	if (ePtr->nextqueue <= *nowPtr) {
	    tp->tm_mday += (ePtr->flags & NS_SCHED_WEEKLY) ? 7 : 1;
	    ePtr->nextqueue = mktime(tp);
	}
    } else {
	ePtr->nextqueue = *nowPtr + ePtr->interval;
    }

    /*
     * Place the new event at the end of the queue array and
     * heap it up into place.  The queue array is extended
     * if necessary.
     */
     
    ePtr->qid = ++nqueue;
    if (maxqueue <= nqueue) {
	maxqueue += 1000;
	queue = ns_realloc(queue, (sizeof(Event *)) * (maxqueue + 1));
    }
    queue[nqueue] = ePtr;
    if (nqueue > 1) {
	int             j, k;

	k = nqueue;
	j = k / 2;
	while (k > 1 && queue[j]->nextqueue > queue[k]->nextqueue) {
	    EXCH(j, k);
	    k = j;
	    j = k / 2;
	}
    }
    
    /*
     * Signal or create the SchedThread if necessary.
     */
     
    if (running) {
        Ns_CondSignal(&schedcond);
    } else {
	running = 1;
	Ns_ThreadCreate(SchedThread, NULL, 0, &schedThread);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DeQueueEvent --
 *
 *	Remove an event from the priority queue heap.
 *
 * Results:
 *	Pointer to removed event.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Event   *
DeQueueEvent(int k)
{
    Event          *ePtr;
    int             j;

    /*
     * Swap out the event to be removed and heap down to restore the
     * order of events to be fired.
     */
     
    EXCH(k, nqueue);
    ePtr = queue[nqueue--];
    ePtr->qid = 0;

    while ((j = 2 * k) <= nqueue) {
	if (j < nqueue && queue[j]->nextqueue > queue[j + 1]->nextqueue) {
	    ++j;
	}
	if (queue[j]->nextqueue > queue[k]->nextqueue) {
	    break;
	}
	EXCH(k, j);
	k = j;
    }

    return ePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * EventThread --
 *
 *	Run detached thread events.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See FinishEvent().
 *
 *----------------------------------------------------------------------
 */

static void
EventThread(void *arg)
{
    Event          *ePtr;
    char	    name[20], idle[20];
    time_t	    now;

    sprintf(idle, "-sched:idle%d-", (int) arg);
    Ns_ThreadSetName(idle);
    Ns_Log(Notice, "starting");
    Ns_MutexLock(&lock);
    while (1) {
	while (threadEventPtr == NULL && !shutdownPending) {
	    Ns_CondWait(&eventcond, &lock);
	}
	if (threadEventPtr == NULL) {
	    break;
	}
	ePtr = threadEventPtr;
	threadEventPtr = ePtr->nextPtr;
	if (threadEventPtr != NULL) {
	    Ns_CondSignal(&eventcond);
	}
	--nIdleThreads;
	Ns_MutexUnlock(&lock);
    	sprintf(name, "-sched:%u-", ePtr->id);
    	Ns_ThreadSetName(name);
    	(*ePtr->proc) (ePtr->arg, (int)ePtr->id);
    	Ns_ThreadSetName(idle);
    	time(&now);
    	Ns_MutexLock(&lock);
	++nIdleThreads;
    	if (ePtr->hPtr == NULL) {
	    Ns_MutexUnlock(&lock);
	    FreeEvent(ePtr);
	    Ns_MutexLock(&lock);
	} else {
	    ePtr->flags &= ~NS_SCHED_RUNNING;
	    ePtr->lastend = now;
    	    QueueEvent(ePtr, &now);
    	}
    }
    Ns_MutexUnlock(&lock);
    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * FreeEvent --
 *
 *	Free and event after run.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Event is freed or re-queued.
 *
 *----------------------------------------------------------------------
 */

static void
FreeEvent(Event *ePtr)
{
    if (ePtr->deleteProc != NULL) {
	(*ePtr->deleteProc) (ePtr->arg, (int)ePtr->id);
    }
    ns_free(ePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SchedThread --
 *
 *	Detached thread to fire events on time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on event procedures.
 *
 *----------------------------------------------------------------------
 */

static void
SchedThread(void *ignored)
{
    Event          *ePtr, *readyPtr;
    time_t          now;
    Ns_Time         timeout;
    int		    elapsed;
    Ns_Thread      *joinThreads;
    int             nJoinThreads;

    Ns_WaitForStartup();
    Ns_ThreadSetName("-sched-");
    Ns_Log(Notice, "sched: starting");
    readyPtr = NULL;
    Ns_MutexLock(&lock);
    while (!shutdownPending) {
    
    	/*
	 * For events ready to run, either create a thread for
	 * detached events or add to a list of synchronous events.
	 */
	 
	time(&now);
	while (nqueue > 0 && queue[1]->nextqueue <= now) {
	    ePtr = DeQueueEvent(1);
	    if (ePtr->flags & NS_SCHED_ONCE) {
		Tcl_DeleteHashEntry(ePtr->hPtr);
		ePtr->hPtr = NULL;
	    }
	    ePtr->lastqueue = now;
	    if (ePtr->flags & NS_SCHED_THREAD) {
	    	ePtr->flags |= NS_SCHED_RUNNING;
	    	ePtr->laststart = now;
		ePtr->nextPtr = threadEventPtr;
		threadEventPtr = ePtr;
	    } else {
	    	ePtr->nextPtr = readyPtr;
	    	readyPtr = ePtr;
	    }
	}

	/*
	 * Dispatch any threaded events.
	 */

	if (threadEventPtr != NULL) {
	    if (nIdleThreads == 0) {
		eventThreads = ns_realloc(eventThreads, sizeof(Ns_Thread) * (nThreads+1));
		Ns_ThreadCreate(EventThread, (void *) nThreads, 0, &eventThreads[nThreads]);
		++nIdleThreads;
		++nThreads;
	    }
	    Ns_CondSignal(&eventcond);
	}
	
	/*
	 * Run and re-queue or free synchronous events. 
	 */
	 
	while ((ePtr = readyPtr) != NULL) {
	    readyPtr = ePtr->nextPtr;
	    ePtr->laststart = now;
	    ePtr->flags |= NS_SCHED_RUNNING;
	    Ns_MutexUnlock(&lock);
	    (*ePtr->proc) (ePtr->arg, (int)ePtr->id);
	    time(&now);
	    elapsed = (int) difftime(now, ePtr->laststart);
	    if (elapsed > nsconf.sched.maxelapsed) {
		Ns_Log(Warning, "sched: "
		       "excessive time taken by proc %d (%d seconds)",
		       ePtr->id, elapsed);
	    }
	    if (ePtr->hPtr == NULL) {
		FreeEvent(ePtr);
		ePtr = NULL;
	    }
	    Ns_MutexLock(&lock);
	    if (ePtr != NULL) {
	    	ePtr->flags &= ~NS_SCHED_RUNNING;
	    	ePtr->lastend = now;
		QueueEvent(ePtr, &now);
	    }
	}

	/*
	 * Wait for the next ready event.
	 */

	if (nqueue == 0) {
	    Ns_CondWait(&schedcond, &lock);
	} else if (!shutdownPending) {
	    timeout.sec = queue[1]->nextqueue;
	    timeout.usec = 0;
	    (void) Ns_CondTimedWait(&schedcond, &lock, &timeout);
	}
    }
    
    /*
     * Wait for any detached event threads to exit
     * and then cleanup the scheduler and signal
     * shutdown complete.
     */
     
    Ns_Log(Notice, "sched: shutdown started");
    if (nThreads > 0) {
    	Ns_Log(Notice, "sched: waiting for event threads...");
	Ns_CondBroadcast(&eventcond);
	while (nThreads > 0) {
            joinThreads = eventThreads;
            nJoinThreads = nThreads;
            eventThreads = NULL;
            nThreads = 0;
            Ns_MutexUnlock(&lock);
            while (--nJoinThreads >= 0 ) {
                Ns_ThreadJoin(&joinThreads[nJoinThreads], NULL);
            }
            ns_free(joinThreads);
            Ns_MutexLock(&lock);
	}
    }
    Ns_MutexUnlock(&lock);
    while (nqueue > 0) {
	FreeEvent(queue[nqueue--]);
    }
    ns_free(queue);
    Tcl_DeleteHashTable(&eventsTable);
    Ns_Log(Notice, "sched: shutdown complete");
    Ns_MutexLock(&lock);
    running = 0;
    Ns_CondBroadcast(&schedcond);
    Ns_MutexUnlock(&lock);
}


void
NsGetScheduled(Tcl_DString *dsPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Event *ePtr;
    time_t now;
    char buf[100];

    time(&now);
    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&eventsTable, &search);
    while (hPtr != NULL) {
	ePtr = Tcl_GetHashValue(hPtr);
	Tcl_DStringStartSublist(dsPtr);
	sprintf(buf, "%u %d %d %ld %ld %ld %ld",
		ePtr->id, ePtr->flags, ePtr->interval, ePtr->nextqueue,
		ePtr->lastqueue, ePtr->laststart, ePtr->lastend);
	Tcl_DStringAppend(dsPtr, buf, -1);
	Ns_GetProcInfo(dsPtr, (void *) ePtr->proc, ePtr->arg);
	Tcl_DStringEndSublist(dsPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);
}

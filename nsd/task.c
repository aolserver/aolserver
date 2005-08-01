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
 * task.c --
 *
 *	Support for I/O tasks.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/task.c,v 1.4 2005/08/01 20:29:24 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following defines a task queue.
 */

#define NAME_SIZE 31

typedef struct TaskQueue {
    struct TaskQueue *nextPtr;	  /* Next in list of all queues. */
    struct Task  *firstSignalPtr; /* First in list of task signals. */
    Ns_Thread	  tid;		  /* Thread id. */
    Ns_Mutex	  lock;		  /* Queue list and signal lock. */
    Ns_Cond	  cond;		  /* Task and queue signal condition. */
    int		  shutdown;	  /* Shutdown flag. */
    int		  stopped;	  /* Stop flag. */
    SOCKET	  trigger[2];	  /* Trigger pipe. */
    char	  name[NAME_SIZE+1]; /* String name. */
} TaskQueue;

/*
 * The following bits are used to send signals to a task queue
 * and manage the state tasks.
 */

#define TASK_INIT			0x01
#define TASK_CANCEL		0x02
#define TASK_WAIT			0x04
#define TASK_TIMEOUT		0x08
#define TASK_DONE			0x10
#define TASK_PENDING		0x20

/*
 * The following defines a task.
 */

typedef struct Task {
    struct TaskQueue *queuePtr;	  /* Monitoring queue. */
    struct Task  *nextWaitPtr;	  /* Next on wait queue. */
    struct Task  *nextSignalPtr;  /* Next on signal queue. */
    SOCKET	  sock;		  /* Underlying socket. */
    Ns_TaskProc  *proc;		  /* Queue callback. */
    void         *arg;		  /* Callback data. */
    int		  idx;		  /* Poll index. */
    int		  events;	  /* Poll events. */
    Ns_Time	  timeout;	  /* Non-null timeout data. */
    int		  signal;	  /* Signal bits sent to/from queue thread. */
    int		  flags;	  /* Flags private to queue. */
} Task;

/*
 * Local functions defined in this file
 */

static void TriggerQueue(TaskQueue *queuePtr);
static void JoinQueue(TaskQueue *queuePtr);
static void StopQueue(TaskQueue *queuePtr);
static int SignalQueue(Task *taskPtr, int bit);
static Ns_ThreadProc TaskThread;
static void RunTask(Task *taskPtr, int revents, Ns_Time *nowPtr);
#define Call(tp,w) ((*((tp)->proc))((Ns_Task *)(tp),(tp)->sock,(tp)->arg,(w)))

/*
 * Static variables defined in this file
 */

static TaskQueue *firstQueuePtr;	/* List of all queues. */
static Ns_Mutex lock;		/* Lock for queue list. */

/*
 * The following maps AOLserver sock "when" bits to poll event bits.
 * The order is significant and determines the order of callbacks
 * when multiple events are ready.
 */

static struct {
    int when;		/* AOLserver when bit. */
    int event;		/* Poll event bit. */
} map[] = {
    {NS_SOCK_EXCEPTION,	POLLPRI},
    {NS_SOCK_WRITE,	POLLOUT},
    {NS_SOCK_READ,	POLLIN}
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_CreateTaskQueue --
 *
 *	Create a new task queue.
 *
 * Results:
 *	Handle to task queue.. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Ns_TaskQueue *
Ns_CreateTaskQueue(char *name)
{
    TaskQueue *queuePtr;

    queuePtr = ns_calloc(1, sizeof(TaskQueue));
    strncpy(queuePtr->name, name ? name : "", NAME_SIZE);
    if (ns_sockpair(queuePtr->trigger) != 0) {
	Ns_Fatal("queue: ns_sockpair() failed: %s",
		 ns_sockstrerror(ns_sockerrno));
    }
    Ns_MutexLock(&lock);
    queuePtr->nextPtr = firstQueuePtr;
    firstQueuePtr = queuePtr;
    Ns_ThreadCreate(TaskThread, queuePtr, 0, &queuePtr->tid);
    Ns_MutexUnlock(&lock);
    return (Ns_TaskQueue *) queuePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DestoryTaskQueue --
 *
 *	Stop and join a task queue.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Pending tasks callbacks, if any, are cancelled.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DestroyTaskQueue(Ns_TaskQueue *queue)
{
    TaskQueue *queuePtr = (TaskQueue *) queue;
    TaskQueue **nextPtrPtr;

    /*
     * Remove queue from list of all queues.
     */

    Ns_MutexLock(&lock);
    nextPtrPtr = &firstQueuePtr;
    while (*nextPtrPtr != queuePtr) {
	nextPtrPtr = &(*nextPtrPtr)->nextPtr;
    }
    *nextPtrPtr = queuePtr->nextPtr;
    Ns_MutexUnlock(&lock);

    /*
     * Signal stop and wait for join.
     */

    StopQueue(queuePtr);
    JoinQueue(queuePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskCreate --
 *
 *	Create a new task.
 *
 * Results:
 *	Handle to task.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

Ns_Task *
Ns_TaskCreate(SOCKET sock, Ns_TaskProc *proc, void *arg)
{
    Task *taskPtr;

    taskPtr = ns_calloc(1, sizeof(Task));
    taskPtr->sock = sock;
    taskPtr->proc = proc;
    taskPtr->arg = arg;
    return (Ns_Task *) taskPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskEnqueue --
 *
 *	Add a task to a queue.
 *
 * Results:
 *	NS_OK if task sent, NS_ERROR otherwise.
 *
 * Side effects:
 *	Queue will begin running the task.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TaskEnqueue(Ns_Task *task, Ns_TaskQueue *queue)
{
    Task *taskPtr = (Task *) task;
    TaskQueue *queuePtr = (TaskQueue *) queue;

    taskPtr->queuePtr = queuePtr;
    if (!SignalQueue(taskPtr, TASK_INIT)) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskRun --
 *
 *	Run a task directly, waiting for completion.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on task callback.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TaskRun(Ns_Task *task)
{
    Task *taskPtr = (Task *) task;
    struct pollfd pfd;
    Ns_Time now, *timeoutPtr;

    pfd.fd = taskPtr->sock;
    Call(taskPtr, NS_SOCK_INIT);
    while (!(taskPtr->flags & TASK_DONE)) {
	if (taskPtr->flags & TASK_TIMEOUT) {
	    timeoutPtr = &taskPtr->timeout;
	} else {
	    timeoutPtr = NULL;
	}
	pfd.revents = 0;
	pfd.events = taskPtr->events;
	if (NsPoll(&pfd, 1, timeoutPtr) != 1) {
	    break;
	}
	Ns_GetTime(&now);
	RunTask(taskPtr, pfd.revents, &now);
    }
    taskPtr->signal |= TASK_DONE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskCancel --
 *
 *	Signal a task queue to stop running a task.
 *
 * Results:
 *	NS_OK if cancel sent, NS_ERROR otherwise.
 *
 * Side effects:
 *	Task callback will be invoke with NS_SOCK_CANCEL and is
 *	expected to call Ns_TaskDone to indicate completion.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TaskCancel(Ns_Task *task)
{
    Task *taskPtr = (Task *) task;

    if (taskPtr->queuePtr == NULL) {
	taskPtr->signal |= TASK_CANCEL;
    } else if (!SignalQueue(taskPtr, TASK_CANCEL)) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskWait --
 *
 *	Wait for a task to complete.  Infinite wait is indicated
 *	by a NULL timeoutPtr.	
 *
 * Results:
 *	NS_TIMEOUT if task did not complete by absolute time,
 *	NS_OK otherwise.
 *
 * Side effects:
 *	May wait up to specified timeout.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TaskWait(Ns_Task *task, Ns_Time *timeoutPtr)
{
    Task *taskPtr = (Task *) task;
    TaskQueue *queuePtr = taskPtr->queuePtr;
    int status = NS_OK;

    if (queuePtr == NULL) {
	if (!(taskPtr->signal & TASK_DONE)) {
	    status = NS_TIMEOUT;
	}
    } else {
    	Ns_MutexLock(&queuePtr->lock);
    	while (status == NS_OK && !(taskPtr->signal & TASK_DONE)) {
	    status = Ns_CondTimedWait(&queuePtr->cond, &queuePtr->lock,
				      timeoutPtr);
    	}
    	Ns_MutexUnlock(&queuePtr->lock);
	if (status == NS_OK) {
	    taskPtr->queuePtr = NULL;
	}
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskCallback --
 *
 *	Update pending conditions and timeout for a task.  This
 *	routine  is expected to be called from within the task
 *	callback proc including to set the initial wait conditions
 *	from within the NS_SOCK_INIT callback.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Task callback will be invoked when ready or on timeout.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TaskCallback(Ns_Task *task, int when, Ns_Time *timeoutPtr)
{
    Task *taskPtr = (Task *) task;
    int i;

    /*
     * Map from AOLserver when bits to poll event bits.
     */

    taskPtr->events = 0;
    for (i = 0; i < 3; ++i) {
	if (when & map[i].when) {
	    taskPtr->events |= map[i].event;
	}
    }

    /*
     * Copy timeout, if any.
     */

    if (timeoutPtr == NULL) {
	taskPtr->flags &= ~TASK_TIMEOUT;
    } else {
	taskPtr->flags |= TASK_TIMEOUT;
	taskPtr->timeout = *timeoutPtr;
    }

    /*
     * Mark as waiting if there are events or a timeout.
     */

    if (taskPtr->events || timeoutPtr) {
	taskPtr->flags |= TASK_WAIT;
    } else {
	taskPtr->flags &= ~TASK_WAIT;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskDone --
 *
 *	Mark a task as done.  This routine should be called from
 *	within the task callback.  The task queue thread will signal
 *	other waiting threads, if any, on next spin.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Task queue will signal this task is done on next spin.
 *
 *----------------------------------------------------------------------
 */

void
Ns_TaskDone(Ns_Task *event)
{
    Task *taskPtr = (Task *) event;

    taskPtr->flags |= TASK_DONE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TaskFree --
 *
 *	Free task structure.  The caller is responsible for
 *	ensuring the task is no longer being run or monitored
 *	a task queue.
 *
 * Results:
 *	The task SOCKET which the caller is responsible for closing
 *	or reusing.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
Ns_TaskFree(Ns_Task *task)
{
    Task *taskPtr = (Task *) task;
    SOCKET sock = taskPtr->sock;
    
    ns_free(taskPtr);
    return sock;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartQueueShutdown --
 *
 *	Trigger all task queues to begin shutdown.
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
NsStartQueueShutdown(void)
{
    TaskQueue *queuePtr;

    /*
     * Trigger all queues to shutdown.
     */

    Ns_MutexLock(&lock);
    queuePtr = firstQueuePtr;
    while (queuePtr != NULL) {
	StopQueue(queuePtr);
	queuePtr = queuePtr->nextPtr;
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitQueueShutdown --
 *
 *	Wait for all task queues to shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May timeout waiting for shutdown.
 *
 *----------------------------------------------------------------------
 */

void
NsWaitQueueShutdown(Ns_Time *toPtr)
{
    TaskQueue *queuePtr, *nextPtr;
    int status;
    
    /*
     * Clear out list of any remaining task queues.
     */

    Ns_MutexLock(&lock);
    queuePtr = firstQueuePtr;
    firstQueuePtr = NULL;
    Ns_MutexUnlock(&lock);

    /*
     * Join all queues possible within total allowed time.
     */

    status = NS_OK;
    while (status == NS_OK && queuePtr != NULL) {
	nextPtr = queuePtr->nextPtr;
	Ns_MutexLock(&queuePtr->lock);
	while (status == NS_OK && !queuePtr->stopped) {
	    status = Ns_CondTimedWait(&queuePtr->cond, &queuePtr->lock, toPtr);
	}
	Ns_MutexUnlock(&queuePtr->lock);
	if (status == NS_OK) {
	    JoinQueue(queuePtr);
	}
	queuePtr = nextPtr;
    }
    if (status != NS_OK) {
	Ns_Log(Warning, "timeout waiting for event queue shutdown");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RunTask --
 *
 *	Run a single task from either a task queue or a directly via
 *	Ns_TaskRun.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Depends on callbacks of given task.
 *
 *----------------------------------------------------------------------
 */

static void
RunTask(Task *taskPtr, int revents, Ns_Time *nowPtr)
{
    int i;

    /*
     * NB: Treat POLLHUP as POLLIN on systems which return it.
     */

    if (revents & POLLHUP) {
	revents |= POLLIN;
    }
    if (revents) {
    	for (i = 0; i < 3; ++i) {
	    if (revents & map[i].event) {
	    	Call(taskPtr, map[i].when);
	    }
    	}
    } else if ((taskPtr->flags & TASK_TIMEOUT)
		&& Ns_DiffTime(&taskPtr->timeout, nowPtr, NULL) < 0) {
	taskPtr->flags &= ~ TASK_WAIT;
	Call(taskPtr, NS_SOCK_TIMEOUT);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SignalQueue --
 *
 *	Send a signal for a task to a task queue.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Task queue will process signal on next spin.
 *
 *----------------------------------------------------------------------
 */

static int
SignalQueue(Task *taskPtr, int bit)
{
    TaskQueue *queuePtr = taskPtr->queuePtr;
    int pending = 0, shutdown;

    Ns_MutexLock(&queuePtr->lock);
    shutdown = queuePtr->shutdown;
    if (!shutdown) {

	/*
	 * Mark the signal and add event to signal list if not
	 * already there.
	 */

	taskPtr->signal |= bit;
    	pending = (taskPtr->signal & TASK_PENDING);
    	if (!pending) {
	    taskPtr->signal |= TASK_PENDING;
	    taskPtr->nextSignalPtr = queuePtr->firstSignalPtr;
	    queuePtr->firstSignalPtr = taskPtr;
    	}
    }
    Ns_MutexUnlock(&queuePtr->lock);
    if (shutdown) {
	return 0;
    }
    if (!pending) {
	TriggerQueue(queuePtr);
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * TriggerQueue --
 *
 *	Wakeup a task queue.
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
TriggerQueue(TaskQueue *queuePtr)
{
    if (send(queuePtr->trigger[1], "", 1, 0) != 1) {
	Ns_Fatal("queue: trigger send() failed: %s",
		  ns_sockstrerror(ns_sockerrno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * StopQueue --
 *
 *	Signal a task queue to shutdown.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Queue will exit on next spin and call remaining tasks
 *	with NS_SOCK_EXIT.
 *
 *----------------------------------------------------------------------
 */

static void
StopQueue(TaskQueue *queuePtr)
{
    Ns_MutexLock(&queuePtr->lock);
    queuePtr->shutdown = 1;
    Ns_MutexUnlock(&queuePtr->lock);
    TriggerQueue(queuePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * JoinQueue --
 *
 *	Cleanup resources of a task queue.
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
JoinQueue(TaskQueue *queuePtr)
{
    Ns_ThreadJoin(&queuePtr->tid, NULL);
    ns_sockclose(queuePtr->trigger[0]);
    ns_sockclose(queuePtr->trigger[1]);
    Ns_MutexDestroy(&queuePtr->lock);
    ns_free(queuePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TaskThread --
 *
 *	Run an task queue.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Depends on callbacks of given tasks.
 *
 *----------------------------------------------------------------------
 */

static void
TaskThread(void *arg)
{
    TaskQueue	 *queuePtr = arg;
    char          c;
    int           n, broadcast, max, nfds, shutdown;
    Task	 *taskPtr, *nextPtr, *firstWaitPtr;
    struct pollfd *pfds;
    Ns_Time	  now, *timeoutPtr;
    char	  name[NAME_SIZE+10];

    sprintf(name, "task:%s", queuePtr->name);
    Ns_ThreadSetName(name);
    Ns_Log(Notice, "starting");

    max = 100;
    pfds = ns_malloc(sizeof(struct pollfd) * max);
    firstWaitPtr = NULL;

    while (1) {

	/*
	 * Get the shutdown flag and process any incoming signals.
	 */

    	Ns_MutexLock(&queuePtr->lock);
	shutdown = queuePtr->shutdown;
	while ((taskPtr = queuePtr->firstSignalPtr) != NULL) {
	    queuePtr->firstSignalPtr = taskPtr->nextSignalPtr;
	    taskPtr->nextSignalPtr = NULL;
	    if (!(taskPtr->flags & TASK_WAIT)) {
		taskPtr->flags |= TASK_WAIT;
		taskPtr->nextWaitPtr = firstWaitPtr;
		firstWaitPtr = taskPtr;
	    }
	    if (taskPtr->signal & TASK_INIT) {
		taskPtr->signal &= ~TASK_INIT;
		taskPtr->flags  |= TASK_INIT;
	    }
	    if (taskPtr->signal & TASK_CANCEL) {
		taskPtr->signal &= ~TASK_CANCEL;
		taskPtr->flags  |= TASK_CANCEL;
	    }
	    taskPtr->signal &= ~TASK_PENDING;
	}
	Ns_MutexUnlock(&queuePtr->lock);

	/*
	 * Invoke pre-poll callbacks, determine minimum timeout, and set
	 * the pollfd structs for all waiting tasks.
	 */

    	pfds[0].fd = queuePtr->trigger[0];
    	pfds[0].events = POLLIN;
	pfds[0].revents = 0;
	nfds = 1;
	timeoutPtr = NULL;
	taskPtr = firstWaitPtr;
	firstWaitPtr = NULL;
	broadcast = 0;
	while (taskPtr != NULL) {
	    nextPtr = taskPtr->nextWaitPtr;
	    
	    /*
	     * Call init and/or cancel and signal done if necessary.
	     * Note that a task can go from init to done immediately
	     * so all required callbacks are invoked before determining
	     * if a wait is required.
	     */

	    if (taskPtr->flags & TASK_INIT) {
		taskPtr->flags &= ~TASK_INIT;
		Call(taskPtr, NS_SOCK_INIT);
	    }
	    if (taskPtr->flags & TASK_CANCEL) {
		taskPtr->flags &= ~(TASK_CANCEL|TASK_WAIT);
		taskPtr->flags |= TASK_DONE;
		Call(taskPtr, NS_SOCK_CANCEL);
	    }
	    if (taskPtr->flags & TASK_DONE) {
		taskPtr->flags &= ~(TASK_DONE|TASK_WAIT);
    		Ns_MutexLock(&queuePtr->lock);
    		taskPtr->signal |= TASK_DONE;
    		Ns_MutexUnlock(&queuePtr->lock);
		broadcast = 1;
	    }
	    if (taskPtr->flags & TASK_WAIT) {
		if (max <= nfds) {
	    	    max  = nfds + 100;
	    	    pfds = ns_realloc(pfds, (size_t) max);
		}
	    	taskPtr->idx = nfds;
	    	pfds[nfds].fd = taskPtr->sock;
	    	pfds[nfds].events = taskPtr->events;
	    	pfds[nfds].revents = 0;
	    	if ((taskPtr->flags & TASK_TIMEOUT) && (timeoutPtr == NULL
			|| Ns_DiffTime(&taskPtr->timeout,
				       timeoutPtr, NULL) < 0)) {
		    timeoutPtr = &taskPtr->timeout;
	    	}
		taskPtr->nextWaitPtr = firstWaitPtr;
		firstWaitPtr = taskPtr;
		++nfds;
	    }
	    taskPtr = nextPtr;
        }

	/*
	 * Signal other threads which may be waiting on tasks to complete.
	 */

	if (broadcast) {
    	    Ns_CondBroadcast(&queuePtr->cond);
	}

	/*
	 * Break now if shutting down now that all signals have been processed.
	 */

	if (shutdown) {
	    break;
	}

    	/*
	 * Poll sockets and drain the trigger pipe if necessary.
	 */

	n = NsPoll(pfds, nfds, timeoutPtr);
	if ((pfds[0].revents & POLLIN) && recv(pfds[0].fd, &c, 1, 0) != 1) {
	    Ns_Fatal("queue: trigger read() failed: %s",
		      ns_sockstrerror(ns_sockerrno));
	}

    	/*
	 * Execute any ready events or timeouts for waiting tasks.
	 */
	 
	Ns_GetTime(&now);
	taskPtr = firstWaitPtr;
	while (taskPtr != NULL) {
	    RunTask(taskPtr, pfds[taskPtr->idx].revents, &now);
	    taskPtr = taskPtr->nextWaitPtr;
        }
    }

    Ns_Log(Notice, "shutdown pending");

    /*
     * Call exit for all remaining tasks.
     */

    taskPtr = firstWaitPtr;
    while (taskPtr != NULL) {
	Call(taskPtr, NS_SOCK_EXIT);
	taskPtr = taskPtr->nextWaitPtr;
    }

    /*
     * Signal all tasks done and shutdown complete.
     */

    Ns_MutexLock(&queuePtr->lock);
    while ((taskPtr = firstWaitPtr) != NULL) {
	firstWaitPtr = taskPtr->nextWaitPtr;
    	taskPtr->signal |= TASK_DONE;
    }
    queuePtr->stopped = 1;
    Ns_MutexUnlock(&queuePtr->lock);
    Ns_CondBroadcast(&queuePtr->cond);

    Ns_Log(Notice, "shutdown complete");
}

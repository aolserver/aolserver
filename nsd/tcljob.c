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
 * tcljob.c --
 *
 *	Tcl job queueing routines.
 *
 * Lock rules:
 *
 *   Lock the queuelock when modifing tp structure elements.
 *
 *   Lock the queue's lock when modifing queue structure elements.
 *
 *   Jobs are shared between tp and the queue but are owned by the queue,
 *   so use queue's lock is used to control access to the jobs.
 *
 *   To avoid deadlock, when locking both the queuelock and queue's
 *   lock lock the queuelock first.
 *
 *
 *   To avoid deadlock, the tp queuelock should be locked before the
 *   queue's lock.
 *
 *
 * Notes:
 *
 *   The threadpool's max number of thread is the sum of all the current
 *   queue's max threads.
 *
 *   The number of threads in the thread pool can be greater than
 *   the current max number of threads. This situtation can occur when
 *   a queue is deleted. Later on if a new queue is created it will simply
 *   use one of the previously created threads. Basically the number of
 *   threads is a "high water mark".
 *
 *   The queues are reference counted. Only when a queue is empty and
 *   its reference count is zero can it be deleted. 
 *
 *   We can no longer use a Tcl_Obj to represent the queue because queues can
 *   now be deleted. Tcl_Objs are deleted when the object goes out of
 *   scope, whereas queues are deleted when delete is called. By doing
 *   this the queue can be used across tcl interpreters.
 *
 * ToDo:
 *   
 *   Users can leak queues. A queue will stay around until a user
 *   cleans it up. It order to help the user out we would like to
 *   add an "-autoclean" option to queue create function. However,
 *   AOLServer does not currently supply a "good" connection cleanup
 *   callback. We tryed to use "Ns_RegisterConnCleanup" however it does
 *   not have a facility to remove registered callbacks.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tcljob.c,v 1.31 2005/08/23 21:41:31 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Default Max Threads
 * - If a user does not specify the a max number of threads for a queue, 
 *   then the following default is used.
 */
#define NS_JOB_DEFAULT_MAXTHREADS 4

typedef enum JobStates {
    JOB_SCHEDULED = 0,
    JOB_RUNNING,
    JOB_DONE
} JobStates;

typedef enum JobTypes {
    JOB_NON_DETACHED = 0,
    JOB_DETACHED
} JobTypes;

typedef enum JobRequests {
    JOB_NONE = 0,
    JOB_WAIT,
    JOB_CANCEL
} JobRequests;

typedef enum QueueRequests {
    QUEUE_REQ_NONE = 0,
    QUEUE_REQ_DELETE
} QueueRequests;

typedef enum ThreadPoolRequests {
    THREADPOOL_REQ_NONE = 0,
    THREADPOOL_REQ_STOP
} ThreadPoolRequests;


/*
 * Job structure. Jobs are enqueued on queues.
 */

typedef struct Job {
    struct Job      *nextPtr;
    char	    *server;
    JobStates        state;
    int              code;
    JobTypes         type;
    JobRequests      req;
    char            *errorCode;
    char            *errorInfo;
    char            *queueId;
    Tcl_DString      id;
    Tcl_DString      script;
    Tcl_DString      results;
    Ns_Time          startTime;
    Ns_Time          endTime;
} Job;

/*
 * Queue structure. A queue manages a set of jobs.
 */

typedef struct JobQueue {
    char                *name;
    char                *desc;
    Ns_Mutex            lock;
    Ns_Cond             cond;
    unsigned int        nextid;
    QueueRequests       req;
    int                 maxThreads;
    int                 nRunning;
    Tcl_HashTable       jobs;
    int                 refCount;
} JobQueue;

/*
 * Thread pool structure. ns_job mananges a global set of threads.
 */

typedef struct ThreadPool {
    Ns_Cond             cond;
    Ns_Mutex            queuelock;
    Tcl_HashTable       queues;
    ThreadPoolRequests  req;
    int                 nextThreadId;
    unsigned long       nextQueueId;
    int                 maxThreads;
    int                 nthreads;
    int                 nidle;
    Job                 *firstPtr;
} ThreadPool;

/*
 * Function prototypes/forward declarations.
 */

static void JobThread(void *arg);
static Job *NextJob(void);
static JobQueue *NewQueue(CONST char* queueName, CONST char* queueDesc, int maxThreads);
static void FreeQueue(JobQueue *queuePtr);
static Job* NewJob(CONST char* server, CONST char* queueName, int type, Tcl_Obj *script);
static void FreeJob(Job *jobPtr);
static int LookupQueue(Tcl_Interp       *interp,
                       CONST char       *queue_name,
                       JobQueue        **queuePtr,
                       int              locked);
static int ReleaseQueue(JobQueue *queuePtr, int locked);
static int AnyDone(JobQueue *queue);
static CONST char* GetJobCodeStr(int code);
static CONST char* GetJobStateStr(JobStates state);
static CONST char* GetJobTypeStr(JobTypes type);
static CONST char* GetJobReqStr(JobRequests req);
static CONST char* GetQueueReqStr(QueueRequests req);
static CONST char* GetTpReqStr(ThreadPoolRequests req);

static int AppendField(Tcl_Interp        *interp,
                       Tcl_Obj           *list,
                       CONST char        *name,
                       CONST char        *value);

static int AppendFieldInt(Tcl_Interp        *interp,
                          Tcl_Obj           *list,
                          CONST char        *name,
                          int               value);

static int AppendFieldLong(Tcl_Interp       *interp,
                           Tcl_Obj          *list,
                           CONST char       *name,
                           long             value);
                                

static int AppendFieldDouble(Tcl_Interp        *interp,
                             Tcl_Obj           *list,
                             CONST char        *name,
                             double            value);

static double ComputeDelta(Ns_Time *start, Ns_Time *end);

/*
 * Globals
 */

static ThreadPool tp;


/*
 *----------------------------------------------------------------------
 *
 * NsInitTclQueueType --
 *
 *	Initialize the Tcl job queue.
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
NsTclInitQueueType(void)
{
    Tcl_InitHashTable(&tp.queues, TCL_STRING_KEYS);
    Ns_MutexSetName(&tp.queuelock, "nsd:tcljobs");
    tp.nextThreadId = 0;
    tp.nextQueueId = 0;
    tp.maxThreads = 0;
    tp.nthreads = 0;
    tp.nidle = 0;
    tp.firstPtr = NULL;
    tp.req = THREADPOOL_REQ_NONE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartJobsShutdown --
 *
 *	Signal stop of the Tcl job threads.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All pending jobs are cancelled and waiting threads interrupted.
 *
 *----------------------------------------------------------------------
 */

void
NsStartJobsShutdown(void)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
    while (hPtr != NULL) {
	Ns_MutexLock(&tp.queuelock);
    	tp.req = THREADPOOL_REQ_STOP;
    	Ns_CondBroadcast(&tp.cond);
    	Ns_MutexUnlock(&tp.queuelock);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsWaitJobsShutdown --
 *
 *	Wait for Tcl job threads to exit.
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
NsWaitJobsShutdown(Ns_Time *toPtr)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int status = NS_OK;

    hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
    while (status == NS_OK && hPtr != NULL) {
	Ns_MutexLock(&tp.queuelock);
    	while (status == NS_OK && tp.nthreads > 0) {
	    status = Ns_CondTimedWait(&tp.cond, &tp.queuelock, toPtr);
    	}
    	Ns_MutexUnlock(&tp.queuelock);
	hPtr = Tcl_NextHashEntry(&search);
    }
    if (status != NS_OK) {
	Ns_Log(Warning, "tcljobs: timeout waiting for exit");
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclJobCmd --
 *
 *	Implement the ns_job command to manage background tasks.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Jobs may be queued to run in another thread.
 *
 *----------------------------------------------------------------------
 */

int
NsTclJobObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp            *itPtr = arg;
    JobQueue            *queuePtr = NULL;
    Job                 *jobPtr = NULL, **nextPtrPtr;
    int                 code, new, create = 0, max;
    char                *jobId = NULL, buf[100], *queueId;
    Tcl_HashEntry       *hPtr, *jPtr;
    Tcl_HashSearch      search;
    int                 argIndex;

    static CONST char *opts[] = {
        "cancel", "create", "delete", "genid", "jobs", "joblist",
        "threadlist", "queue", "queues", "queuelist", "wait",
        "waitany",  NULL
    };
    
    enum {
        JCancelIdx, JCreateIdx, JDeleteIdx, JGenIDIdx, JJobsIdx, JJobsListIdx,
        JThreadListIdx, JQueueIdx, JQueuesIdx, JQueueListIdx, JWaitIdx, JWaitAnyIdx
    } _nsmayalias opt;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    code = TCL_OK;
    switch (opt) {
        case JCreateIdx:
        {
            /*
             * ns_job create
             *
             * Create a new thread pool queue.
             */
            Tcl_Obj *queueIdObj = NULL;
            char *queueDesc = "";

            argIndex = 2;
            if ((objc < 3) || (objc > 6)) {
                Tcl_WrongNumArgs(interp, 2, objv,
                                 "?-desc description? queueId ?maxThreads?");
                return TCL_ERROR;
            }
            if (objc > 3) {
                if (strncmp(Tcl_GetString(objv[argIndex]), "-desc", strlen("-desc")) == 0) {
                    ++argIndex;
                    queueDesc = Tcl_GetString(objv[argIndex++]);
                }
            }

            queueIdObj = objv[argIndex++];
            queueId = Tcl_GetString(queueIdObj);

            max = NS_JOB_DEFAULT_MAXTHREADS;
            if ((objc > argIndex) &&
                Tcl_GetIntFromObj(interp, objv[argIndex++], &max) != TCL_OK) {
                return TCL_ERROR;
            }

            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_CreateHashEntry(&tp.queues, Tcl_GetString(queueIdObj), &new);
            if (new) {
                queuePtr = NewQueue(Tcl_GetHashKey(&tp.queues, hPtr),queueDesc, max);
                Tcl_SetHashValue(hPtr, queuePtr);
            }
            Ns_MutexUnlock(&tp.queuelock);
            if (!new) {
                Tcl_AppendResult(interp, "queue already exists: ", queueId, NULL);
                return TCL_ERROR;
            }
            Tcl_SetObjResult(interp, queueIdObj);
        }
        break;
        case JDeleteIdx:
        {
            /*
             * ns_job delete
             *
             * Request that the specified queue be deleted. The queue will
             * only be deleted when all jobs are removed.
             */
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            
            if (LookupQueue(interp, Tcl_GetString(objv[2]), &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            queuePtr->req = QUEUE_REQ_DELETE;
            ReleaseQueue(queuePtr, 0);
                
            Ns_CondBroadcast(&tp.cond);
            Tcl_SetResult(interp, "", TCL_STATIC);
        }
        break;
        case JQueueIdx:
        {
            /*
             * ns_job queue
             *
             * Add a new job the specified queue.
             */
            int job_type = JOB_NON_DETACHED;

            argIndex = 2;
            if ((objc != 4) && (objc != 5)) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-detached? queueId script");
                return TCL_ERROR;
            }
            if (objc > 4) {
                if (strcmp(Tcl_GetString(objv[argIndex++]), "-detached") == 0) {
                    job_type = JOB_DETACHED;
                } else {
                    Tcl_WrongNumArgs(interp, 2, objv, "?-detached? queueId script");
                    return TCL_ERROR;
                }
            }
                
            Ns_MutexLock(&tp.queuelock);
            if (LookupQueue(interp, Tcl_GetString(objv[argIndex++]),
                            &queuePtr, 1) != TCL_OK) {
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }
            
            /*
             * Create a new job and add to the Thread Pool's list of jobs.
             */
            jobPtr = NewJob(itPtr->servPtr->server,
                            queuePtr->name,
                            job_type,
                            objv[argIndex++]);
            Ns_GetTime(&jobPtr->startTime);


            if ((tp.req == THREADPOOL_REQ_STOP) || 
                (queuePtr->req == QUEUE_REQ_DELETE))
            {
                Tcl_AppendResult(interp,
                                 "The specified queue is being deleted or "
                                 "the system is stopping.", NULL);
                FreeJob(jobPtr);
                ReleaseQueue(queuePtr, 1);
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }

            /*
             * Add the job to the thread pool's job list.
             */
            nextPtrPtr = &tp.firstPtr;
            while (*nextPtrPtr != NULL) {
                nextPtrPtr = &((*nextPtrPtr)->nextPtr);
            }
            *nextPtrPtr = jobPtr;
            
            /*
             * Start a new thread if there are less than maxThreads currently
             * running and there currently no idle threads.
             */
            if (tp.nidle == 0 && tp.nthreads < tp.maxThreads) {
                create = 1;
                ++tp.nthreads;
            } else {
                create = 0;
            }

            /*
             * Add the job to queue.
             */
            jobId = buf;
            do {
                sprintf(jobId, "job%d", queuePtr->nextid++);
                hPtr = Tcl_CreateHashEntry(&queuePtr->jobs, jobId, &new);
            } while (!new);

            Tcl_DStringAppend(&jobPtr->id, jobId, -1);
            Tcl_SetHashValue(hPtr, jobPtr);
            Ns_CondBroadcast(&tp.cond);

            ReleaseQueue(queuePtr, 1);
            Ns_MutexUnlock(&tp.queuelock);

            if (create) {
                Ns_ThreadCreate(JobThread, 0, 0, NULL);
            }

            Tcl_SetResult(interp, jobId, TCL_VOLATILE);
        }
        break;
        case JWaitIdx:
        {
            /*
             * ns_job wait
             *
             * Wait for the specified job.
             */
            int         timeoutFlag = 0;
            Ns_Time     timeout;
            int         timedOut = 0;
            Ns_Time     delta_timeout;

            argIndex = 2;
            if ((objc != 4) && (objc != 6)) {
                Tcl_WrongNumArgs(interp, 2, objv,
                                 "?-timeout seconds:microseconds? queueId jobId");
                return TCL_ERROR;
            }
            if (objc > 4) {
                if (strcmp(Tcl_GetString(objv[argIndex++]), "-timeout") == 0) {
                    timeoutFlag = 1;
                    if (Ns_TclGetTimeFromObj(interp, objv[argIndex++], 
                                             &delta_timeout) != TCL_OK) {
                        return TCL_ERROR;
                    }
                    
                    /*
                     * Set the timeout time. This is an absolute time.
                     */
                    Ns_GetTime(&timeout);
                    Ns_IncrTime(&timeout, delta_timeout.sec, delta_timeout.usec);
                }
            }

            if (LookupQueue(interp, Tcl_GetString(objv[argIndex++]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            jobId = Tcl_GetString(objv[argIndex++]);

            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);
            if (hPtr == NULL) {

                ReleaseQueue(queuePtr, 0);
                Tcl_AppendResult(interp, "no such job: ", jobId, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(hPtr);

            if ((jobPtr->type == JOB_DETACHED) ||
                (jobPtr->req == JOB_CANCEL) ||
                (jobPtr->req == JOB_WAIT)) {
                Tcl_AppendResult(interp, "cannot wait on job: ",
                    Tcl_DStringValue(&jobPtr->id), NULL);
                ReleaseQueue(queuePtr, 0);
                return TCL_ERROR;
            }
                
            jobPtr->req = JOB_WAIT;
        
            if (timeoutFlag) {
                while (jobPtr->state != JOB_DONE) {
                    timedOut = Ns_CondTimedWait(&queuePtr->cond,
                                                &queuePtr->lock, &timeout);
                    if (timedOut == NS_TIMEOUT) {
                        Tcl_SetResult(interp, "Wait timed out.", TCL_STATIC);
                        jobPtr->req = JOB_NONE;

                        ReleaseQueue(queuePtr, 0);

                        return TCL_ERROR;
                    }
                }
            } else {
                while (jobPtr->state != JOB_DONE) {
                    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
                }
            }

            /*
             * At this point the job we were waiting on has completed, so we return
             * the job's results and errorcodes, then clean up the job.
             */

            /*
             * The following is a sanity check that ensures no other
             * process removed this job's entry.
             */
            if (((hPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId)) == NULL) ||
                (jobPtr == Tcl_GetHashValue(hPtr))) {
                Tcl_SetResult(interp, "Internal ns_job error.", TCL_STATIC);
            }

            Tcl_DeleteHashEntry(hPtr);
            ReleaseQueue(queuePtr, 0);

            Tcl_DStringResult(interp, &jobPtr->results);
            if (jobPtr->errorCode != NULL) {
                Tcl_SetVar(interp, "errorCode", jobPtr->errorCode, TCL_GLOBAL_ONLY);
            }
            if (jobPtr->errorInfo != NULL) {
                Tcl_SetVar(interp, "errorInfo", jobPtr->errorInfo, TCL_GLOBAL_ONLY);
            }
            code = jobPtr->code;
            FreeJob(jobPtr);
        }
        break;
        case JCancelIdx:
        {
            /*
             * ns_job cancel
             *
             * Cancel the specified job.
             */
            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId jobId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]), &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            jobId = Tcl_GetString(objv[3]);
            jPtr = Tcl_FindHashEntry(&queuePtr->jobs, jobId);
            if (jPtr == NULL) {

                ReleaseQueue(queuePtr, 0);
                Tcl_AppendResult(interp, "no such job: ", jobId, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(jPtr);

            if (jobPtr->req == JOB_WAIT) {
                Tcl_AppendResult(interp,
                                 "Can not cancel this job because someone"
                                 " is waiting on it. Job ID: ",
                                 Tcl_DStringValue(&jobPtr->id), NULL);
            
                ReleaseQueue(queuePtr, 0);

                return TCL_ERROR;
            }
                
            jobPtr->req = JOB_CANCEL;

            if (jobPtr->state == JOB_DONE) {
                Tcl_DeleteHashEntry(jPtr);
                FreeJob(jobPtr);                
            } 

            Ns_CondBroadcast(&queuePtr->cond);
            
            Ns_CondBroadcast(&tp.cond);

            Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (jobPtr->state == JOB_RUNNING));

            ReleaseQueue(queuePtr, 0);
        }
        break;
        case JWaitAnyIdx:
        {
            /*
             * ns_job waitany
             *
             * Wait for any job on the queue complete.
             */
            int         timeoutFlag = 0;
            Ns_Time     timeout;
            int         timedOut = 0;
            Ns_Time     delta_timeout;

            argIndex = 2;
            if ((objc != 3) && (objc != 5)) {
                Tcl_WrongNumArgs(interp, 2, objv,
                                 "?-timeout seconds:microseconds? queueId");
                return TCL_ERROR;
            }       
            if (objc > 3) {
                if (strcmp(Tcl_GetString(objv[argIndex++]), "-timeout") == 0) {
                    timeoutFlag = 1;
                    if (Ns_TclGetTimeFromObj(interp, objv[argIndex++],
                                             &delta_timeout) != TCL_OK) {
                        return TCL_ERROR;
                    }
                    /*
                     * Set the timeout time. This is an absolute time.
                     */
                    Ns_GetTime(&timeout);
                    Ns_IncrTime(&timeout, delta_timeout.sec, delta_timeout.usec);
                }
            }

            if (LookupQueue(interp, Tcl_GetString(objv[argIndex++]),
                            &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }

            /* 
             * While there are jobs in queue or no jobs are "done", wait
             * on the queue condition variable. 
             */
            if (timeoutFlag) {
                while ((Tcl_FirstHashEntry(&queuePtr->jobs, &search) != NULL) &&
                       (!AnyDone(queuePtr))) {
                    
                    timedOut = Ns_CondTimedWait(&queuePtr->cond,
                                                &queuePtr->lock, &timeout);
                    if (timedOut == NS_TIMEOUT) {
                        Tcl_SetResult(interp, "Wait timed out.", TCL_STATIC);
                        
                        ReleaseQueue(queuePtr, 0);
                        return TCL_ERROR;
                    }
                }
            } else {
                while ((Tcl_FirstHashEntry(&queuePtr->jobs, &search) != NULL) &&
                       (!AnyDone(queuePtr))) {
                    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
                }
            }

            ReleaseQueue(queuePtr, 0);
            Tcl_SetResult(interp, "", TCL_STATIC);
        }
        break;
        case JJobsIdx:
        {
            /*
             * ns_job jobs
             *
             * Returns a list of job IDs in arbitrary order.
             */
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]), &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }
            hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
            while (hPtr != NULL) {
                jobId = Tcl_GetHashKey(&queuePtr->jobs, hPtr);
                Tcl_AppendElement(interp, jobId);
                hPtr = Tcl_NextHashEntry(&search);
            }
            ReleaseQueue(queuePtr, 0);
        }
        break;
        case JQueuesIdx:
        {
            /*
             * ns_job queues
             *
             * Returns a list of the current queues.
             */
            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
            while (hPtr != NULL) {
                queuePtr = Tcl_GetHashValue(hPtr);
                Tcl_AppendElement(interp, queuePtr->name);
                hPtr = Tcl_NextHashEntry(&search);
            }
            Ns_MutexUnlock(&tp.queuelock);
        }
        break; 
        case JJobsListIdx:
        {
            /*
             * ns_job joblist
             *
             * Returns a list of all the jobs in the queue. The "job" consists of:
             *
             * Job ID
             * Job State (Scheduled, Running, or Done)
             * Job Results (or job script, if job has not yet completed).
             * Job Code (TCL_OK, TCL_ERROR, TCL_RETURN, TCL_BREAK, TCL_CONTINE)
             * Job Running Time (TBD)
             */

            Tcl_Obj     *jobList, *jobFieldList;
            char        *jobId, *jobState, *jobCode, *jobType;
            char        *jobResults, *jobScript, *jobReq;
            double      delta;

            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queueId");
                return TCL_ERROR;
            }
            if (LookupQueue(interp, Tcl_GetString(objv[2]), &queuePtr, 0) != TCL_OK) {
                return TCL_ERROR;
            }

            /* Create a Tcl List to hold the list of jobs. */
            jobList = Tcl_NewListObj(0, NULL);

            hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
            while (hPtr != NULL) {

                jobPtr = Tcl_GetHashValue(hPtr);

                jobId = Tcl_GetHashKey(&queuePtr->jobs, hPtr);
                jobCode = GetJobCodeStr(jobPtr->code);
                jobState = GetJobStateStr(jobPtr->state);
                jobType = GetJobTypeStr(jobPtr->type);
                jobReq = GetJobReqStr(jobPtr->req);
                jobResults = Tcl_DStringValue(&jobPtr->results);
                jobScript = Tcl_DStringValue(&jobPtr->script);

                if ((jobPtr->state == JOB_SCHEDULED) ||
                    (jobPtr->state == JOB_RUNNING)) {
                    Ns_GetTime(&jobPtr->endTime);
                }
                delta = ComputeDelta(&jobPtr->startTime, &jobPtr->endTime);

                /* Create a Tcl List to hold the list of job fields. */
                jobFieldList = Tcl_NewListObj(0, NULL);

                /* Add Job ID */
                if ((AppendField(interp, jobFieldList,
                                 "id", jobId) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "state", jobState) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "results", jobResults) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "script", jobScript) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "code", jobCode) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "type", jobType) != TCL_OK) ||
                    (AppendField(interp, jobFieldList,
                                 "req", jobReq) != TCL_OK) ||
                    (AppendFieldDouble(interp, jobFieldList,
                                       "time", delta) != TCL_OK) ||
                    (AppendFieldLong(interp,
                                     jobFieldList,
                                     "starttime",
                                     jobPtr->startTime.sec) != TCL_OK) ||
                    (AppendFieldLong(interp,
                                     jobFieldList,
                                     "endtime",
                                     jobPtr->endTime.sec) != TCL_OK))
                {
                    /* AppendField sets results if an error occurs. */
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, jobList, jobFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    
                    ReleaseQueue(queuePtr, 0);
                    return TCL_ERROR;
                }

                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, jobList);

            ReleaseQueue(queuePtr, 0);
        }
        break;
        case JQueueListIdx:
        {
            /*
             * ns_job queuelist
             *
             * Returns a list of all the queues and the queue information.
             */
            Tcl_Obj     *queueList, *queueFieldList;
            char        *queueReq;

            /* Create a Tcl List to hold the list of jobs. */
            queueList = Tcl_NewListObj(0, NULL);

            Ns_MutexLock(&tp.queuelock);
            hPtr = Tcl_FirstHashEntry(&tp.queues, &search);
            while (hPtr != NULL) {

                queuePtr = Tcl_GetHashValue(hPtr);

                /* Create a Tcl List to hold the list of queue fields. */
                queueFieldList = Tcl_NewListObj(0, NULL);

                queueReq = GetQueueReqStr(queuePtr->req);

                /* Add queue name */
                if ((AppendField(interp, queueFieldList,
                                 "name", queuePtr->name) != TCL_OK) ||
                    (AppendField(interp, queueFieldList,
                                 "desc", queuePtr->desc) != TCL_OK) ||
                    (AppendFieldInt(interp, queueFieldList,
                                    "maxthreads", queuePtr->maxThreads) != TCL_OK) ||
                    (AppendFieldInt(interp, queueFieldList,
                                    "numrunning", queuePtr->nRunning) != TCL_OK) ||
                    (AppendField(interp, queueFieldList,
                                 "req", queueReq) != TCL_OK))
                {
                    /* AppendField sets results if an error occurs. */
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&tp.queuelock);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, queueList,
                                             queueFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&tp.queuelock);
                    return TCL_ERROR;
                }

                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, queueList);
            Ns_MutexUnlock(&tp.queuelock);
        }
        break; 
        case JGenIDIdx:
        {
            /*
             * ns_job genID
             *
             * Generate a unique queue name.
             */
            Ns_Time currentTime;
            Ns_GetTime(&currentTime);
            snprintf(buf, 100, "queue_id_%x_%x", tp.nextQueueId++, currentTime.sec);
            Tcl_SetResult(interp, buf, TCL_VOLATILE);
        }
        break;
        case JThreadListIdx:
        {
            /*
             * ns_job threadlist
             *
             * Return a list of the thread pool's fields.
             *
             */ 

            Tcl_Obj     *tpFieldList;
            char        *tpReq;

            /* Create a Tcl List to hold the list of thread fields. */
            tpFieldList = Tcl_NewListObj(0, NULL);

            tpReq = GetTpReqStr(tp.req);

            Ns_MutexLock(&tp.queuelock);
            
            /* Add queue name */
            if ((AppendFieldInt(interp, tpFieldList,
                                "maxthreads", tp.maxThreads) != TCL_OK) ||
                (AppendFieldInt(interp, tpFieldList,
                                "numthreads", tp.nthreads) != TCL_OK) ||
                (AppendFieldInt(interp, tpFieldList,
                                "numidle", tp.nidle) != TCL_OK) ||
                (AppendField(interp, tpFieldList,
                             "req", tpReq) != TCL_OK))

            {
                /* AppendField sets results if an error occurs. */
                Tcl_DecrRefCount(tpFieldList);
                Ns_MutexUnlock(&tp.queuelock);
                return TCL_ERROR;
            }
            Ns_MutexUnlock(&tp.queuelock);
            Tcl_SetObjResult(interp, tpFieldList);
        }
        break;
    }
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * JobThread --
 *
 *	Background thread for the ns_job command.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Jobs will be run from the queue.
 *
 *----------------------------------------------------------------------
 */

static void
JobThread(void *arg)
{
    Tcl_Interp          *interp;
    Job                 *jobPtr;
    char                buf[100];
    CONST char          *err;
    JobQueue           *queuePtr;
    Tcl_HashEntry       *jPtr;

    Ns_WaitForStartup();
    Ns_MutexLock(&tp.queuelock);
    snprintf(buf, 100, "-ns_job_%x-", tp.nextThreadId++);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "Starting thread: %s", buf);
    while (1) {
	++tp.nidle;
	while (((jobPtr = NextJob()) == NULL) &&
               !(tp.req == THREADPOOL_REQ_STOP)) {
	    Ns_CondWait(&tp.cond, &tp.queuelock);
	}
	--tp.nidle;

        if (tp.req == THREADPOOL_REQ_STOP) {
            break;
        }

        /*
         * Run the job.
         */
        Ns_MutexUnlock(&tp.queuelock);

        interp = Ns_TclAllocateInterp(jobPtr->server);
        Ns_GetTime(&jobPtr->endTime);
        Ns_GetTime(&jobPtr->startTime);
        jobPtr->code = Tcl_EvalEx(interp, jobPtr->script.string, -1, 0);
        
        /*
         * Save the results.
         */
        Tcl_DStringAppend(&jobPtr->results, Tcl_GetStringResult(interp), -1);
        err = Tcl_GetVar(interp, "errorCode", TCL_GLOBAL_ONLY);
        if (err != NULL) {
            jobPtr->errorCode = ns_strdup(err);
        }
        err = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
        if (err != NULL) {
            jobPtr->errorInfo = ns_strdup(err);
        }
        Ns_GetTime(&jobPtr->endTime);
        Ns_TclDeAllocateInterp(interp);
        
        Ns_MutexLock(&tp.queuelock);
            
        LookupQueue(NULL, jobPtr->queueId, &queuePtr, 1);

        --(queuePtr->nRunning);
        jobPtr->state = JOB_DONE;

        /*
         * Clean any cancelled or detached jobs.
         */
        if ((jobPtr->req == JOB_CANCEL) || (jobPtr->type == JOB_DETACHED)) {
            jPtr = Tcl_FindHashEntry(&queuePtr->jobs, Tcl_DStringValue(&jobPtr->id));
            Tcl_DeleteHashEntry(jPtr);
            FreeJob(jobPtr);
        }

        Ns_CondBroadcast(&queuePtr->cond);
        ReleaseQueue(queuePtr, 1);
    }

    --tp.nthreads;

    Ns_CondBroadcast(&tp.cond);
    Ns_MutexUnlock(&tp.queuelock);

    Ns_Log(Notice, "exiting");
}


/*
 *----------------------------------------------------------------------
 * Get the "next" job.
 *
 * Queues have a "maxThreads" so if the queue is already at
 * "maxThreads" jobs of that queue will be skipped.
 *
 * Note: the "queuelock" should be locked when calling this function.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static Job *
NextJob(void)
{
    JobQueue            *queuePtr;
    Tcl_HashEntry       *jPtr;
    Job                 *prev = NULL;
    Job                 *tmp = NULL;
    Job                 *jobPtr = NULL;
    int                 done = 0;

    jobPtr = tp.firstPtr;
    prev = tp.firstPtr;
    while ((!done) && (jobPtr != NULL)) {
        LookupQueue(NULL, jobPtr->queueId, &queuePtr, 1);

        /*
         * Check if the job is not cancel and 
         */

        if (jobPtr->req == JOB_CANCEL) {
            /*
             * Remove job from the list.
             */
            tmp = jobPtr;
            if (jobPtr == tp.firstPtr) {
                tp.firstPtr = jobPtr->nextPtr;
            } else {
                prev->nextPtr = jobPtr->nextPtr;
            }
            jobPtr = jobPtr->nextPtr;

            /*
             * Remove cancelled job from the queue and free it.
             */
            jPtr = Tcl_FindHashEntry(&queuePtr->jobs, Tcl_DStringValue(&tmp->id));
            Tcl_DeleteHashEntry(jPtr);
            FreeJob(tmp);
        } else if (queuePtr->nRunning < queuePtr->maxThreads) {
            /*
             * Remove job from the list.
             */
            if (jobPtr == tp.firstPtr) {
                tp.firstPtr = jobPtr->nextPtr;
            } else {
                prev->nextPtr = jobPtr->nextPtr;
            }

            done = 1;
            jobPtr->state = JOB_RUNNING;
            ++(queuePtr->nRunning);
        } else {
            /*
             * Advance the list pointer.
             */
            prev = jobPtr;
            jobPtr = jobPtr->nextPtr;
        }

        ReleaseQueue(queuePtr, 1);
    }

    return jobPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NewQueue --
 *
 *	Create a thread pool queue.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static JobQueue *
NewQueue(CONST char* queueName, CONST char* queueDesc, int maxThreads)
{
    JobQueue *queuePtr = NULL;

    queuePtr = ns_calloc(1, sizeof(JobQueue));
    queuePtr->req = QUEUE_REQ_NONE;

    queuePtr->name = ns_calloc(1, strlen(queueName) + 1);
    strcpy(queuePtr->name, queueName);

    queuePtr->desc = ns_calloc(1, strlen(queueDesc) + 1);
    strcpy(queuePtr->desc, queueDesc);
    queuePtr->maxThreads = maxThreads;

    queuePtr->refCount = 0;

    Ns_MutexSetName2(&queuePtr->lock, "tcljob", queueName);
    Tcl_InitHashTable(&queuePtr->jobs, TCL_STRING_KEYS);

    tp.maxThreads += maxThreads;

    return queuePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeQueue --
 *
 *	Cleanup the
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
FreeQueue(JobQueue *queuePtr)
{
    Ns_MutexDestroy(&queuePtr->lock);
    Tcl_DeleteHashTable(&queuePtr->jobs);
    ns_free(queuePtr->desc);
    ns_free(queuePtr->name);
    ns_free(queuePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NewJob --
 *
 *	Create a new job and initialize it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Job *
NewJob(CONST char* server, CONST char* queueId, int type, Tcl_Obj *script)
{
    Job *jobPtr = NULL;

    jobPtr = ns_malloc(sizeof(Job));
    jobPtr->nextPtr = NULL;
    jobPtr->server = server;
    jobPtr->state = JOB_SCHEDULED;      
    jobPtr->code = TCL_OK;
    jobPtr->type = type;
    jobPtr->req = JOB_NONE;
    jobPtr->errorCode = jobPtr->errorInfo = NULL;

    jobPtr->queueId = ns_calloc(1, strlen(queueId) + 1);
    strcpy(jobPtr->queueId, queueId);

    Tcl_DStringInit(&jobPtr->id);
    Tcl_DStringInit(&jobPtr->script);
    Tcl_DStringAppend(&jobPtr->script, Tcl_GetString(script), -1);
    Tcl_DStringInit(&jobPtr->results);

    return jobPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeJob --
 *
 *	Destory a Job structure.
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
FreeJob(Job *jobPtr)
{
    Tcl_DStringFree(&jobPtr->results);
    Tcl_DStringFree(&jobPtr->script);
    Tcl_DStringFree(&jobPtr->id);

    ns_free(jobPtr->queueId);

    if (jobPtr->errorCode) {
	ns_free(jobPtr->errorCode);
    }
    if (jobPtr->errorInfo) {
	ns_free(jobPtr->errorInfo);
    }
    ns_free(jobPtr);
}


/*
 *----------------------------------------------------------------------
 * Find the specified queue and lock it if found.
 *
 * Specify "locked" true if the "queuelock" is already locked.
 *
 * PWM: With the new locking scheme refCount is not longer necessary.
 * However, if there is ever a case in the future where an unlocked
 * queue can be referenced then we will again need the refCount.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static int LookupQueue(Tcl_Interp       *interp,
                       CONST char       *queueId,
                       JobQueue        **queuePtr,
                       int              locked)
{
    Tcl_HashEntry *hPtr;

    if (!locked)
        Ns_MutexLock(&tp.queuelock);

    *queuePtr = NULL;

    hPtr = Tcl_FindHashEntry(&tp.queues, queueId);
    if (hPtr != NULL) {
	*queuePtr = Tcl_GetHashValue(hPtr);
        Ns_MutexLock(&(*queuePtr)->lock);
        ++((*queuePtr)->refCount);
    } 

    if (!locked)
        Ns_MutexUnlock(&tp.queuelock);

    if (*queuePtr == NULL) {
        if (interp != NULL) {
            Tcl_AppendResult(interp, "no such queue: ", queueId, NULL);
        }
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * Release queue
 *
 * Specify "locked" true if the queuelock is already locked.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static int
ReleaseQueue(JobQueue *queuePtr, int locked)
{
    Tcl_HashEntry       *qPtr;
    Tcl_HashSearch      search;
    int                 deleted = 0;

    --(queuePtr->refCount);

    /*
     * If user requested that the queue be deleted and
     * no other thread is referencing the queueu and
     * the queue is emtpy, then delete it.
     *
     */
    if ((queuePtr->req == QUEUE_REQ_DELETE) && 
        (queuePtr->refCount <= 0) &&
        (Tcl_FirstHashEntry(&queuePtr->jobs, &search) == NULL)) {
        
        if (!locked)
            Ns_MutexLock(&tp.queuelock);
        
        /*
         * Remove the queue from the list.
         */
        qPtr = Tcl_FindHashEntry(&tp.queues, queuePtr->name);
        if (qPtr != NULL) {
            Tcl_DeleteHashEntry(qPtr);
            tp.maxThreads -= queuePtr->maxThreads;
            deleted = 1;
        }
        
        Ns_MutexUnlock(&queuePtr->lock);
        FreeQueue(queuePtr);
    
        if (!locked)
            Ns_MutexUnlock(&tp.queuelock);
    } else {
        Ns_MutexUnlock(&queuePtr->lock);
    }
    
    return deleted;
}


/*
 *----------------------------------------------------------------------
 * Check if any jobs on the queue are "done".
 *
 * 1 (true) there is at least one job done.
 * 0 (false) there are no jobs done.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AnyDone(JobQueue *queue)
{
    Tcl_HashEntry       *hPtr;
    Job                 *jobPtr;
    Tcl_HashSearch      search;

    hPtr = Tcl_FirstHashEntry(&queue->jobs, &search);

    while (hPtr != NULL) {
        jobPtr = Tcl_GetHashValue(hPtr);
        if (jobPtr->state == JOB_DONE) {
            return 1;
        }
        hPtr = Tcl_NextHashEntry(&search);
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 * Convert the job code into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static CONST char*
GetJobCodeStr(int code)
{
    static CONST char *codeArr[] = {
        "TCL_OK",       /* 0 */
        "TCL_ERROR",    /* 1 */
        "TCL_RETURN",   /* 2 */
        "TCL_BREAK",    /* 3 */
        "TCL_CONTINUE", /* 4 */
        "UNKNOWN_CODE"  /* 5 */
    };
    static int max_code_index = 5;
    
    /* Check the caller's input. */
    if (code > (max_code_index)) {
        code = max_code_index;
    }

    return codeArr[code];
}


/*
 *----------------------------------------------------------------------
 * Convert the job states into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static CONST char*
GetJobStateStr(JobStates state)
{
    static CONST char *stateArr[] = {
        "scheduled",        /* 0 */
        "running",          /* 1 */
        "done",             /* 2 */
        "unknown"           /* 3 */
    };
    static int max_state_index = 3;
    
    /* Check the caller's input. */
    if (state > (max_state_index)) {
        state = max_state_index;
    }

    return stateArr[state];
}


/*
 *----------------------------------------------------------------------
 * Convert the job states into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetJobTypeStr(JobTypes type)
{
    static CONST char *typeArr[] = {
        "nondetached",     /* 0 */
        "detached",        /* 1 */
        "unknown"          /* 2 */
    };
    static int max_type_index = 2;
    
    /* Check the caller's input. */
    if (type > (max_type_index)) {
        type = max_type_index;
    }

    return typeArr[type];
}


/*
 *----------------------------------------------------------------------
 * Convert the job req into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetJobReqStr(JobRequests req)
{
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "wait",     /* 1 */
        "cancel",   /* 2 */
        "unknown"   /* 3 */
    };
    static int req_max_index = 3;
    
    /* Check the caller's input. */
    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 * Convert the queue req into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetQueueReqStr(QueueRequests req)
{
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "delete"    /* 1 */
    };
    static int req_max_index = 1;
    
    /* Check the caller's input. */
    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 * Convert the thread pool req into a string.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
static CONST char*
GetTpReqStr(ThreadPoolRequests req)
{
    static CONST char *reqArr[] = {
        "none",     /* 0 */
        "stop"      /* 1 */
    };
    static int req_max_index = 1;
    
    /* Check the caller's input. */
    if (req > (req_max_index)) {
        req = req_max_index;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 * Append job field to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendField(Tcl_Interp        *interp,
            Tcl_Obj           *list,
            CONST char        *name,
            CONST char        *value)
{
    /* Add Job ID */
    if (Tcl_ListObjAppendElement(interp, list,
                                 Tcl_NewStringObj(name,
                                                  (int)strlen(name))) == TCL_ERROR) {
        /*
         * Note: If there is an error occurs within Tcl_ListObjAppendElement
         * it will set the result.
         */
        return TCL_ERROR;
    }

    if (Tcl_ListObjAppendElement(interp, list,
                                 Tcl_NewStringObj(value,
                                                  (int)strlen(value))) == TCL_ERROR) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * Append job field to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendFieldInt(Tcl_Interp        *interp,
               Tcl_Obj           *list,
               CONST char        *name,
               int               value)
{
    /* Add Job ID */
    if (Tcl_ListObjAppendElement(interp, list,
                                 Tcl_NewStringObj(name,
                                                  (int)strlen(name))) == TCL_ERROR) {
        /*
         * Note: If there is an error occurs within Tcl_ListObjAppendElement
         * it will set the result.
         */
        return TCL_ERROR;
    }

    if (Tcl_ListObjAppendElement(interp, list, Tcl_NewIntObj(value)) == TCL_ERROR) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * Append job field to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendFieldLong(Tcl_Interp         *interp,
                Tcl_Obj            *list,
                CONST char         *name,
                long               value)
{
    /* Add Job ID */
    if (Tcl_ListObjAppendElement(interp, list,
                                 Tcl_NewStringObj(name,
                                                  (int)strlen(name))) == TCL_ERROR) {
        /*
         * Note: If there is an error occurs within Tcl_ListObjAppendElement
         * it will set the result.
         */
        return TCL_ERROR;
    }
        
    if (Tcl_ListObjAppendElement(interp, list, Tcl_NewLongObj(value)) == TCL_ERROR) {
        return TCL_ERROR;
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * Append the job field to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendFieldDouble(Tcl_Interp        *interp,
                  Tcl_Obj           *list,
                  CONST char        *name,
                  double            value)
{
    /* Add Job ID */
    if (Tcl_ListObjAppendElement(interp, list,
                                 Tcl_NewStringObj(name,
                                                  (int)strlen(name))) == TCL_ERROR) {
        /*
         * Note: If there is an error occurs within Tcl_ListObjAppendElement
         * it will set the result.
         */
        return TCL_ERROR;
    }

    if (Tcl_ListObjAppendElement(interp, list, Tcl_NewDoubleObj(value)) == TCL_ERROR) {
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * Compute the time difference and return the result in milliseconds.
 *----------------------------------------------------------------------
 */

static double ComputeDelta(Ns_Time *start, Ns_Time *end) {
    Ns_Time     diff;
    Ns_DiffTime(end, start, &diff);
    return ((double)diff.sec * 1000.0) + ((double)diff.usec / 1000.0);
}

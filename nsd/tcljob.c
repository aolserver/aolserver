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
 * Question(s):
 *
 *  *) What is "ns_job jobs" suppose to do?
 *
 *
 * Todo:
 *
 * *) Add a command to delete a queue.
 *    - This is not an easy task. We would have to change the queuelock logic or
 *      add a semephore to the queue.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tcljob.c,v 1.13 2003/09/11 21:51:36 pmoosman Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

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
    QUEUE_REQ_STOP
} QueueRequests;

/*
 * The following structure defines a job queued in a
 * server Tcl job pool.
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
    Tcl_DString      id;
    Tcl_DString      ds;
} Job;

/*
 * The following structure defines a job queue.
 */

typedef struct Queue {
    char                *name;
    Ns_Mutex            lock;
    Ns_Cond             cond;
    unsigned int        nextid;
    QueueRequests       req;
    int                 maxthreads;
    int                 nidle;          /* Number of idle threads. */
    int                 nthreads;
    Job                 *firstPtr;
    Tcl_HashTable       jobs;
} Queue;


/*
 * The following define the job queue Tcl_Obj type.
 */

static int SetQueueFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr);
static void UpdateStringOfQueue(Tcl_Obj *objPtr);
static void SetQueueInternalRep(Tcl_Obj *objPtr, Queue *queuePtr);
static int GetQueueFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Queue **queuePtrPtr);

static Tcl_ObjType queueType = {
    "ns:job",
    (Tcl_FreeInternalRepProc *) NULL,
    (Tcl_DupInternalRepProc *) NULL,
    UpdateStringOfQueue,
    SetQueueFromAny
};

/*
 * Function prototypes/forward declarations.
 */

static void JobThread(void *arg);

Job* NewJob(CONST char* server, int type, Tcl_Obj *script);
void FreeJob(Job *jobPtr);

Queue* NewQueue(CONST char* queue_name, int maxThreads);
void FreeQueue(Queue *queuePtr);

static CONST char* GetJobCodeStr(int code);
static CONST char* GetJobStateStr(JobStates state);
static CONST char* GetJobTypeStr(JobTypes type);
static CONST char* GetJobReqStr(JobRequests req);
static CONST char* GetQueueReqStr(QueueRequests req);

static int AppendFieldEntry(Tcl_Interp        *interp,
                            Tcl_Obj           *list,
                            CONST char        *name,
                            CONST char        *value);
static int AppendFieldEntryInt(Tcl_Interp        *interp,
                               Tcl_Obj           *list,
                               CONST char        *name,
                               int               value);

static int AnyDone(Queue *queue);


/*
 * Globals
 */
static Ns_Mutex queuelock;
static Tcl_HashTable queues;



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
    Tcl_RegisterObjType(&queueType);
    Tcl_InitHashTable(&queues, TCL_STRING_KEYS);
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
    Queue *queuePtr;

    hPtr = Tcl_FirstHashEntry(&queues, &search);
    while (hPtr != NULL) {
    	queuePtr = Tcl_GetHashValue(hPtr);
	Ns_MutexLock(&queuePtr->lock);
    	queuePtr->req = QUEUE_REQ_STOP;
    	Ns_CondBroadcast(&queuePtr->cond);
    	Ns_MutexUnlock(&queuePtr->lock);
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
    Queue *queuePtr;
    int status = NS_OK;

    hPtr = Tcl_FirstHashEntry(&queues, &search);
    while (status == NS_OK && hPtr != NULL) {
    	queuePtr = Tcl_GetHashValue(hPtr);
	Ns_MutexLock(&queuePtr->lock);
    	while (status == NS_OK && queuePtr->nthreads > 0) {
	    status = Ns_CondTimedWait(&queuePtr->cond, &queuePtr->lock, toPtr);
    	}
    	Ns_MutexUnlock(&queuePtr->lock);
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
    Queue               *queuePtr;
    Job                 *jobPtr = NULL, **nextPtrPtr;
    int                 code, new, create = 0, max;
    char                *job_id = NULL, buf[20], *queue_name;
    Tcl_HashEntry       *hPtr;
    Tcl_HashSearch      search;

    static CONST char *opts[] = {
        "cancel", "create", "jobs", "job_list", "queue", "queues", "queue_list",
        "wait", "wait_any",  NULL
    };

    enum {
        JCancelIdx, JCreateIdx, JJobsIdx, JJobsListIdx,
        JQueueIdx, JQueuesIdx, JQueueListIdx, JWaitIdx, JWaitAnyIdx
    } opt;

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
             * Create a new thread pool queue.
             */
            if (objc != 3 && objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name ?maxthreads?");
                return TCL_ERROR;
            }
            max = 4;
            if (objc == 4 && Tcl_GetIntFromObj(interp, objv[3], &max) != TCL_OK) {
                return TCL_ERROR;
            }
            queue_name = Tcl_GetString(objv[2]);
            Ns_MutexLock(&queuelock);
            hPtr = Tcl_CreateHashEntry(&queues, queue_name, &new);
            if (new) {
                queuePtr = NewQueue(Tcl_GetHashKey(&queues, hPtr), max);
                Tcl_SetHashValue(hPtr, queuePtr);
            }
            Ns_MutexUnlock(&queuelock);
            if (!new) {
                Tcl_AppendResult(interp, "queue already exists: ", queue_name, NULL);
                return TCL_ERROR;
            }
            SetQueueInternalRep(objv[2], queuePtr);
            Tcl_SetObjResult(interp, objv[2]);
        }
        break;
        case JQueueIdx:
        {
            /*
             * Add a new job the specified queue.
             */
            int job_type;


            if ((objc != 4) && (objc != 5)) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name script ?detached?");
                return TCL_ERROR;
            }
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }

            job_type = JOB_NON_DETACHED;
            if ((objc == 5) && Tcl_GetIntFromObj(interp, objv[4], &job_type) != TCL_OK) {
                return TCL_ERROR;
            }
        
            Ns_MutexLock(&queuePtr->lock);
            if (queuePtr->req == QUEUE_REQ_STOP) {
            
                Tcl_AppendResult(interp,
                                 "The specified queue is going away. "
                                 "Queue Name : ", queuePtr->name, NULL);
                Ns_MutexUnlock(&queuePtr->lock);
                return TCL_ERROR;
            }

            jobPtr = NewJob((itPtr->servPtr ? itPtr->servPtr->server : NULL),
                            job_type,
                            objv[3]);

            nextPtrPtr = &queuePtr->firstPtr;
            while (*nextPtrPtr != NULL) {
                nextPtrPtr = &((*nextPtrPtr)->nextPtr);
            }
            *nextPtrPtr = jobPtr;
            if (queuePtr->nidle == 0 && queuePtr->nthreads < queuePtr->maxthreads) {
                create = 1;
                ++queuePtr->nthreads;
            } else {
                create = 0;
            }
            job_id = buf;
            do {
                sprintf(job_id, "job%d", queuePtr->nextid++);
                hPtr = Tcl_CreateHashEntry(&queuePtr->jobs, job_id, &new);
            } while (!new);

            Tcl_DStringAppend(&jobPtr->id, job_id, -1);
            Tcl_SetHashValue(hPtr, jobPtr);
            Ns_CondBroadcast(&queuePtr->cond);

            Ns_MutexUnlock(&queuePtr->lock);

            if (create) {
                Ns_ThreadCreate(JobThread, queuePtr, 0, NULL);
            }
            Tcl_SetResult(interp, job_id, TCL_VOLATILE);
        }
        break;
        case JWaitIdx:
        {
            /*
             * Wait for the specified job.
             */
            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name id");
                return TCL_ERROR;
            }
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            job_id = Tcl_GetString(objv[3]);

            Ns_MutexLock(&queuePtr->lock);
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, job_id);
            if (hPtr == NULL) {
                Ns_MutexUnlock(&queuePtr->lock);
                Tcl_AppendResult(interp, "no such job: ", job_id, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(hPtr);

            if (jobPtr->type == JOB_DETACHED) {
                Tcl_AppendResult(interp,
                                 "Cannot wait on a detached job. Job ID :",
                                 Tcl_DStringValue(&jobPtr->id), NULL);
                Ns_MutexUnlock(&queuePtr->lock);
                return TCL_ERROR;
            }
        
            if (jobPtr->req == JOB_CANCEL) {
                Tcl_AppendResult(interp,
                                 "Cannot wait on a cancelled job.. Job ID :",
                                 Tcl_DStringValue(&jobPtr->id), NULL);
                Ns_MutexUnlock(&queuePtr->lock);
                return TCL_ERROR;
            }

            if (jobPtr->req == JOB_WAIT) {
                Tcl_AppendResult(interp,
                                 "Some thread is already waiting for "
                                 "the specified job. Job ID :",
                                 Tcl_DStringValue(&jobPtr->id), NULL);
                Ns_MutexUnlock(&queuePtr->lock);
                return TCL_ERROR;
            }
        
            jobPtr->req = JOB_WAIT;
        
            while (jobPtr->state != JOB_DONE) {
                Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
            }

            /*
             * The following is a sanity check to ensure that the no
             * other process removed this job's entry.
             */
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, job_id);
            assert(hPtr != NULL);
            assert(jobPtr == Tcl_GetHashValue(hPtr));

            /*
             * At this point the job we were waiting on has completed, so we return
             * the job's results and errorcodes, then clean up the job.
             */

            Ns_MutexUnlock(&queuePtr->lock);

            Tcl_DStringResult(interp, &jobPtr->ds);
            if (jobPtr->errorCode != NULL) {
                Tcl_SetVar(interp, "errorCode", jobPtr->errorCode, TCL_GLOBAL_ONLY);
            }
            if (jobPtr->errorInfo != NULL) {
                Tcl_SetVar(interp, "errorInfo", jobPtr->errorInfo, TCL_GLOBAL_ONLY);
            }
            code = jobPtr->code;
            if (hPtr != NULL) {
                Tcl_DeleteHashEntry(hPtr);                
            }
            FreeJob(jobPtr);
        }
        break;
        case JCancelIdx:
        {
            /*
             * Cancel the specified job.
             */
            if (objc != 4) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name id");
                return TCL_ERROR;
            }
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            job_id = Tcl_GetString(objv[3]);
            Ns_MutexLock(&queuePtr->lock);
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, job_id);
            if (hPtr == NULL) {
                Ns_MutexUnlock(&queuePtr->lock);
                Tcl_AppendResult(interp, "no such job: ", job_id, NULL);
                return TCL_ERROR;
            }

            jobPtr = Tcl_GetHashValue(hPtr);

            if (jobPtr->req == JOB_WAIT) {
                Tcl_AppendResult(interp,
                                 "Can not cancel this job because someone"
                                 " is waiting on it. Job ID: ",
                                 Tcl_DStringValue(&jobPtr->id), NULL);
            
                Ns_MutexUnlock(&queuePtr->lock);
                return TCL_ERROR;
            }
                
            if (jobPtr->state != JOB_RUNNING) {
                Tcl_DeleteHashEntry(hPtr);
            }
            jobPtr->req = JOB_CANCEL;

            Ns_MutexUnlock(&queuePtr->lock);
            Tcl_SetBooleanObj(Tcl_GetObjResult(interp), (jobPtr->state == JOB_RUNNING));
        }
        break;
        case JWaitAnyIdx:
        {
            /*
             * Wait for any job on the queue complete.
             */
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name");
                return TCL_ERROR;
            }       
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            Ns_MutexLock(&queuePtr->lock);

            /* 
             * While there are jobs in queue or no jobs are "done", wait
             * on the queue condition variable. 
             */
            while ((Tcl_FirstHashEntry(&queuePtr->jobs, &search) != NULL) &&
                   (!AnyDone(queuePtr))) {
                Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
            }
            Ns_MutexUnlock(&queuePtr->lock);
        }
        break;
        case JJobsIdx:
        {
            /*
             * Returns a list of job "results" in arbitrary order. (I don't
             * see how this function is useful.
             */
            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name");
                return TCL_ERROR;
            }
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            Ns_MutexLock(&queuePtr->lock);
            hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
            while (hPtr != NULL) {
                jobPtr = Tcl_GetHashValue(hPtr);
                Tcl_AppendElement(interp, jobPtr->ds.string);
                hPtr = Tcl_NextHashEntry(&search);
            }
            Ns_MutexUnlock(&queuePtr->lock);
        }
        break;
        case JQueuesIdx:
        {
            /*
             * Returns a list of the current queues.
             */
            hPtr = Tcl_FirstHashEntry(&queues, &search);
            while (hPtr != NULL) {
                queuePtr = Tcl_GetHashValue(hPtr);
                Tcl_AppendElement(interp, queuePtr->name);
                hPtr = Tcl_NextHashEntry(&search);
            }
        }
        break; 
        case JJobsListIdx:
        {
            /*
             * Returns a list of all the jobs in the queue. The "job" consists of:
             *
             * Job ID
             * Job State (Scheduled, Running, or Done)
             * Job Results (or job script, if job has not yet completed).
             * Job Code (TCL_OK, TCL_ERROR, TCL_RETURN, TCL_BREAK, TCL_CONTINE)
             * Job Running Time (TBD)
             */

            Tcl_Obj     *jobList, *jobFieldList;
            char        *jobId, *jobState, *jobCode, *jobType, *jobResults, *jobReq;


            if (objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "queue_name");
                return TCL_ERROR;
            }
            if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
                return TCL_ERROR;
            }
            Ns_MutexLock(&queuePtr->lock);

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
                jobResults = Tcl_DStringValue(&jobPtr->ds);

                /* Create a Tcl List to hold the list of job fields. */
                jobFieldList = Tcl_NewListObj(0, NULL);

                /* Add Job ID */
                if (AppendFieldEntry(interp, jobFieldList, "ID", jobId) != TCL_OK)
                {
                    /* AppendFieldEntry sets results if an error occurs. */
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add Job State */
                if (AppendFieldEntry(interp, jobFieldList, "STATE", jobState) != TCL_OK) {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add Job Results */
                if (AppendFieldEntry(interp, jobFieldList, "RESULTS", jobResults) != TCL_OK)
                {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add Job Code */
                if (AppendFieldEntry(interp, jobFieldList, "CODE", jobCode) != TCL_OK)
                {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add Job Type */
                if (AppendFieldEntry(interp, jobFieldList, "TYPE", jobType) != TCL_OK)
                {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add Job Request */
                if (AppendFieldEntry(interp, jobFieldList, "REQ", jobReq) != TCL_OK)
                {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, jobList, jobFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(jobList);
                    Tcl_DecrRefCount(jobFieldList);
                    Ns_MutexUnlock(&queuePtr->lock);
                    return TCL_ERROR;
                }

                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, jobList);

            Ns_MutexUnlock(&queuePtr->lock);
        }
        break;
        case JQueueListIdx:
        {
            /*
             * Returns a list of all the queues and the queue information.
             */

            Tcl_Obj     *queueList, *queueFieldList;
            char        *queueReq;

            /* Create a Tcl List to hold the list of jobs. */
            queueList = Tcl_NewListObj(0, NULL);

            Ns_MutexLock(&queuelock);
            hPtr = Tcl_FirstHashEntry(&queues, &search);
            while (hPtr != NULL) {

                queuePtr = Tcl_GetHashValue(hPtr);

                /* Create a Tcl List to hold the list of queue fields. */
                queueFieldList = Tcl_NewListObj(0, NULL);

                queueReq = GetQueueReqStr(queuePtr->req);

                /* Add queue name */
                if (AppendFieldEntry(interp, queueFieldList,
                                     "NAME", queuePtr->name) != TCL_OK)
                {
                    /* AppendFieldEntry sets results if an error occurs. */
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                /* Add max threads */
                if (AppendFieldEntryInt(interp, queueFieldList,
                                        "MAX_THREADS", queuePtr->maxthreads) != TCL_OK)
                {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                /* Add number of current threads */
                if (AppendFieldEntryInt(interp, queueFieldList,
                                        "NUM_THREADS", queuePtr->nthreads) != TCL_OK)
                {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                /* Add number idle */
                if (AppendFieldEntryInt(interp, queueFieldList,
                                        "NUM_IDLE", queuePtr->nidle) != TCL_OK)
                {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                /* Add queue req */
                if (AppendFieldEntry(interp, queueFieldList,
                                     "REQ", queueReq) != TCL_OK)
                {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                /* Add the job to the job list */
                if (Tcl_ListObjAppendElement(interp, queueList,
                                             queueFieldList) != TCL_OK) {
                    Tcl_DecrRefCount(queueList);
                    Tcl_DecrRefCount(queueFieldList);
                    Ns_MutexUnlock(&queuelock);
                    return TCL_ERROR;
                }

                hPtr = Tcl_NextHashEntry(&search);
            }
            Tcl_SetObjResult(interp, queueList);
            Ns_MutexUnlock(&queuelock);
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
    Tcl_Interp *interp;
    Job *jobPtr;
    Queue *queuePtr = arg;
    char buf[100];
    CONST char *err;
    Tcl_HashEntry *hPtr;

    Ns_WaitForStartup();
    Ns_MutexLock(&queuePtr->lock);
    sprintf(buf, "-tcljob%d-", queuePtr->nthreads);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "starting");
    while (1) {
	++queuePtr->nidle;
	while ((queuePtr->firstPtr == NULL) &&
               !(queuePtr->req == QUEUE_REQ_STOP)) {
	    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
	}
	--queuePtr->nidle;
	if (queuePtr->firstPtr == NULL) {
	    break;
	}
	jobPtr = queuePtr->firstPtr;
	queuePtr->firstPtr = jobPtr->nextPtr;

	if (jobPtr->req == JOB_CANCEL) {
	    FreeJob(jobPtr);
	    continue;
	}

	jobPtr->state = JOB_RUNNING;

	Ns_MutexUnlock(&queuePtr->lock);
	interp = Ns_TclAllocateInterp(jobPtr->server);
	jobPtr->code = Tcl_EvalEx(interp, jobPtr->ds.string, -1, 0);
	Tcl_DStringTrunc(&jobPtr->ds, 0);
	Tcl_DStringAppend(&jobPtr->ds, Tcl_GetStringResult(interp), -1);
	err = Tcl_GetVar(interp, "errorCode", TCL_GLOBAL_ONLY);
	if (err != NULL) {
	    jobPtr->errorCode = ns_strdup(err);
	}
	err = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
	if (err != NULL) {
	    jobPtr->errorInfo = ns_strdup(err);
	}
	Ns_TclDeAllocateInterp(interp);
	Ns_MutexLock(&queuePtr->lock);

        jobPtr->state = JOB_DONE;

	if ((jobPtr->req == JOB_CANCEL) ||
            (jobPtr->type == JOB_DETACHED)) {
            hPtr = Tcl_FindHashEntry(&queuePtr->jobs, Tcl_DStringValue(&jobPtr->id));
            Tcl_DeleteHashEntry(hPtr);
	    FreeJob(jobPtr);
	}
        Ns_CondBroadcast(&queuePtr->cond);
    }
    --queuePtr->nthreads;

    Ns_CondBroadcast(&queuePtr->cond);
    Ns_MutexUnlock(&queuePtr->lock);

    Ns_Log(Notice, "exiting");
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

Queue*
NewQueue(CONST char* queue_name, int maxThreads)
{
    Queue *queuePtr = NULL;

    queuePtr = ns_calloc(1, sizeof(Queue));
    queuePtr->req = QUEUE_REQ_NONE;
    queuePtr->name = queue_name;
    queuePtr->maxthreads = maxThreads;
    queuePtr->nthreads = 0;

    Ns_MutexSetName2(&queuePtr->lock, "tcljob", queue_name);
    Tcl_InitHashTable(&queuePtr->jobs, TCL_STRING_KEYS);

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

void
FreeQueue(Queue *queuePtr)
{
    Ns_MutexDestroy(&queuePtr->lock);
    Tcl_DeleteHashTable(&queuePtr->jobs);
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

Job*
NewJob(CONST char* server, int type, Tcl_Obj *script)
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
    Tcl_DStringInit(&jobPtr->id);
    Tcl_DStringInit(&jobPtr->ds);
    Tcl_DStringAppend(&jobPtr->ds, Tcl_GetString(script), -1);

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
    Tcl_DStringFree(&jobPtr->ds);
    Tcl_DStringFree(&jobPtr->id);
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
 *
 * GetQueueFromObj --
 *
 *	Return the internal value of a Queue Tcl_Obj.
 *
 * Results:
 *	TCL_OK or TCL_ERROR if not a valid Ns_Time.
 *
 * Side effects:
 *	Object is set to Queue type if necessary.
 *
 *----------------------------------------------------------------------
 */

static int
GetQueueFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Queue **queuePtrPtr)
{
    if (Tcl_ConvertToType(interp, objPtr, &queueType) != TCL_OK) {
	return TCL_ERROR;
    }
    *queuePtrPtr = objPtr->internalRep.otherValuePtr;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfQueue --
 *
 *	Update the string representation for a Queue object.
 *	Note: This procedure does not free an existing old string rep
 *	so storage will be lost if this has not already been done. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The object's string is set to a valid string that results from
 *	the Ns_Time-to-string conversion.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfQueue(objPtr)
    register Tcl_Obj *objPtr;	/* Int object whose string rep to update. */
{
    Queue *queuePtr = objPtr->internalRep.otherValuePtr;
    size_t len;

    len = strlen(queuePtr->name);
    objPtr->bytes = ckalloc(len + 1);
    strcpy(objPtr->bytes, queuePtr->name);
    objPtr->length = len;
}


/*
 *----------------------------------------------------------------------
 *
 * SetQueueFromAny --
 *
 *	Attempt to generate a Queue internal form for the Tcl object.
 *
 * Results:
 *	The return value is a standard object Tcl result. If an error occurs
 *	during conversion, an error message is left in the interpreter's
 *	result unless "interp" is NULL.
 *
 * Side effects:
 *	If no error occurs, an int is stored as "objPtr"s internal
 *	representation. 
 *
 *----------------------------------------------------------------------
 */

static int
SetQueueFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    Tcl_HashEntry *hPtr;
    Queue *queuePtr;
    char *queue;

    queuePtr = NULL;
    queue = Tcl_GetString(objPtr);
    Ns_MutexLock(&queuelock);
    hPtr = Tcl_FindHashEntry(&queues, queue);   
    if (hPtr != NULL) {
	queuePtr = Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&queuelock);
    if (queuePtr == NULL) {
	Tcl_AppendResult(interp, "no such queue: ", queue, NULL);
	return TCL_ERROR;
    }
    SetQueueInternalRep(objPtr, queuePtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SetQueueInternalRep --
 *
 *	Set the internal Queue, freeing a previous internal rep if
 *	necessary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Object will be a Queue type.
 *
 *----------------------------------------------------------------------
 */

static void
SetQueueInternalRep(Tcl_Obj *objPtr, Queue *queuePtr)
{
    Tcl_ObjType *typePtr = objPtr->typePtr;

    if (typePtr != NULL && typePtr->freeIntRepProc != NULL) {
	(*typePtr->freeIntRepProc)(objPtr);
    }
    objPtr->typePtr = &queueType;
    objPtr->internalRep.otherValuePtr = queuePtr;
    Tcl_InvalidateStringRep(objPtr);
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
    static int num_of_codes = 5;
    
    /* Check the caller's input. */
    if (code > (num_of_codes)) {
        code = num_of_codes;
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
        "JOB_SCHEDULED",        /* 0 */
        "JOB_RUNNING",          /* 1 */
        "JOB_DONE",             /* 2 */
        "UNKNOWN_STATE"         /* 3 */
    };
    static int num_of_states = 3;
    
    /* Check the caller's input. */
    if (state > (num_of_states)) {
        state = num_of_states;
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
        "JOB_NON_DETACHED",     /* 0 */
        "JOB_DETACHED",         /* 1 */
        "UNKNOWN_TYPE"          /* 2 */
    };
    static int num_of_types = 2;
    
    /* Check the caller's input. */
    if (type > (num_of_types)) {
        type = num_of_types;
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
        "JOB_NONE",     /* 0 */
        "JOB_WAIT",     /* 1 */
        "JOB_CANCEL",   /* 2 */
        "UNKNOWN_REQ"   /* 3 */
    };
    static int num_of_reqs = 3;
    
    /* Check the caller's input. */
    if (req > (num_of_reqs)) {
        req = num_of_reqs;
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
        "QUEUE_REQ_NONE",     /* 0 */
        "QUEUE_REQ_STOP"      /* 1 */
    };
    static int num_of_reqs = 1;
    
    /* Check the caller's input. */
    if (req > (num_of_reqs)) {
        req = num_of_reqs;
    }

    return reqArr[req];
}


/*
 *----------------------------------------------------------------------
 * Append job info to the job field list.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendFieldEntry(Tcl_Interp        *interp,
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
 * Append job info to the job field list. int version.
 *
 * Results:
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

static int
AppendFieldEntryInt(Tcl_Interp        *interp,
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
AnyDone(Queue *queue) {

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

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
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tcljob.c,v 1.9 2003/01/20 23:15:13 shmooved Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define JOB_DONE    1
#define JOB_CANCEL  2
#define JOB_RUNNING 4
#define JOB_ERRCODE 8
#define JOB_ERRINFO 16

/*
 * The following structure defines a job queued in a
 * server Tcl job pool.
 */

typedef struct Job {
    struct Job      *nextPtr;
    char	    *server;
    int              flags;
    int              code;
    char            *errorCode;
    char            *errorInfo;
    Tcl_DString      ds;
} Job;

/*
 * The following structure defines a job queue.
 */

typedef struct Queue {
    char *name;
    Ns_Mutex lock;
    Ns_Cond cond;
    unsigned int nextid;
    int stopping;
    int maxthreads;
    int nidle;
    int nthreads;
    Job *firstPtr;
    Tcl_HashTable jobs;
} Queue;

/*
 * The following define the job queue Tcl_Obj type.
 */

static int  SetQueueFromAny (Tcl_Interp *interp, Tcl_Obj *objPtr);
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

static Ns_Mutex queuelock;
static Tcl_HashTable queues;
static void FreeJob(Job *jobPtr);
static Ns_ThreadProc JobThread;


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
    	queuePtr->stopping = 1;
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

static void
AppendJob(Tcl_Interp *interp, Job *jobPtr)
{
    Tcl_AppendElement(interp, jobPtr->ds.string);
}

int
NsTclJobObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    Queue *queuePtr;
    Job *jobPtr, **nextPtrPtr;
    int code, new, create, running, max;
    char *id, buf[20], *queue;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    static CONST char *opts[] = {
        "cancel", "create", "jobs", "queue", "queues", "wait", NULL
    };
    enum {
        JCancelIdx, JCreateIdx, JJobsIdx, JQueueIdx, JQueuesIdx, JWaitIdx 
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
	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "queue ?maxthreads?");
	    return TCL_ERROR;
	}
	max = 4;
	if (objc == 4 && Tcl_GetIntFromObj(interp, objv[3], &max) != TCL_OK) {
	    return TCL_ERROR;
	}
	queue = Tcl_GetString(objv[2]);
	Ns_MutexLock(&queuelock);
	hPtr = Tcl_CreateHashEntry(&queues, queue, &new);
	if (new) {
	    queuePtr = ns_calloc(1, sizeof(Queue));
	    queuePtr->name = Tcl_GetHashKey(&queues, hPtr);
	    queuePtr->maxthreads = max;
	    Ns_MutexSetName2(&queuePtr->lock, "tcljob", queue);
	    Tcl_InitHashTable(&queuePtr->jobs, TCL_STRING_KEYS);
	    Tcl_SetHashValue(hPtr, queuePtr);
	}
	Ns_MutexUnlock(&queuelock);
	if (!new) {
	    Tcl_AppendResult(interp, "queue already exists: ", queue, NULL);
	    return TCL_ERROR;
	}
	SetQueueInternalRep(objv[2], queuePtr);
	Tcl_SetObjResult(interp, objv[2]);
	break;

    case JQueueIdx:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "queue script");
	    return TCL_ERROR;
	}
	if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	jobPtr = ns_malloc(sizeof(Job));
	jobPtr->flags = jobPtr->code = 0;
	jobPtr->server = (itPtr->servPtr ? itPtr->servPtr->server : NULL);
	jobPtr->nextPtr = NULL;
	jobPtr->errorCode = jobPtr->errorInfo = NULL;
	Tcl_DStringInit(&jobPtr->ds);
	Tcl_DStringAppend(&jobPtr->ds, Tcl_GetString(objv[3]), -1);
	Ns_MutexLock(&queuePtr->lock);
	if (!queuePtr->stopping) {
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
	    id = buf;
	    do {
		sprintf(id, "job%d", queuePtr->nextid++);
		hPtr = Tcl_CreateHashEntry(&queuePtr->jobs, id, &new);
	    } while (!new);
	    Tcl_SetHashValue(hPtr, jobPtr);
	    Ns_CondBroadcast(&queuePtr->cond);
	    jobPtr = NULL;
	}
	Ns_MutexUnlock(&queuePtr->lock);
	if (jobPtr != NULL) {
	    FreeJob(jobPtr);
	    Tcl_SetResult(interp, "server stopping", TCL_STATIC);
	    return TCL_ERROR;
	}
	if (create) {
	    Ns_ThreadCreate(JobThread, queuePtr, 0, NULL);
	}
	Tcl_SetResult(interp, id, TCL_VOLATILE);
	break;

    case JWaitIdx:
    case JCancelIdx:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "queue id");
	    return TCL_ERROR;
	}
	if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	id = Tcl_GetString(objv[3]);
	Ns_MutexLock(&queuePtr->lock);
	hPtr = Tcl_FindHashEntry(&queuePtr->jobs, id);
	if (hPtr != NULL) {
	    jobPtr = Tcl_GetHashValue(hPtr);
	    Tcl_DeleteHashEntry(hPtr);
	    if (opt == JCancelIdx) {
		jobPtr->flags |= JOB_CANCEL;
		running = jobPtr->flags & JOB_RUNNING;
	    } else {
		while (!(jobPtr->flags & JOB_DONE)) {
		    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
		}
	    }
	}
	Ns_MutexUnlock(&queuePtr->lock);
	if (hPtr == NULL) {
	    Tcl_AppendResult(interp, "no such job: ", id, NULL);
	    return TCL_ERROR;
	}
	if (opt == JCancelIdx) {
	    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), running);
	} else {
	    Tcl_DStringResult(interp, &jobPtr->ds);
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

    case JJobsIdx:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "queue id");
	    return TCL_ERROR;
	}
	if (GetQueueFromObj(interp, objv[2], &queuePtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	Ns_MutexLock(&queuePtr->lock);
	hPtr = Tcl_FirstHashEntry(&queuePtr->jobs, &search);
	while (hPtr != NULL) {
	    jobPtr = Tcl_GetHashValue(hPtr);
	    AppendJob(interp, jobPtr);
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Ns_MutexUnlock(&queuePtr->lock);
	break;

    case JQueuesIdx:
        hPtr = Tcl_FirstHashEntry(&queues, &search);
        while (hPtr != NULL) {
            queuePtr = Tcl_GetHashValue(hPtr);
            Tcl_AppendElement(interp, queuePtr->name);
            hPtr = Tcl_NextHashEntry(&search);
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

    Ns_WaitForStartup();
    Ns_MutexLock(&queuePtr->lock);
    sprintf(buf, "-tcljob%d-", queuePtr->nthreads);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "starting");
    while (1) {
	++queuePtr->nidle;
	while (queuePtr->firstPtr == NULL && !queuePtr->stopping) {
	    Ns_CondWait(&queuePtr->cond, &queuePtr->lock);
	}
	--queuePtr->nidle;
	if (queuePtr->firstPtr == NULL) {
	    break;
	}
	jobPtr = queuePtr->firstPtr;
	queuePtr->firstPtr = jobPtr->nextPtr;
	jobPtr->flags |= JOB_RUNNING;
	if (jobPtr->flags & JOB_CANCEL) {
	    FreeJob(jobPtr);
	    continue;
	}
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
	jobPtr->flags |= JOB_DONE;
	if (jobPtr->flags & JOB_CANCEL) {
	    FreeJob(jobPtr);
	} else {
	    Ns_CondBroadcast(&queuePtr->cond);
	}
    }
    --queuePtr->nthreads;
    Ns_CondBroadcast(&queuePtr->cond);
    Ns_MutexUnlock(&queuePtr->lock);
    Ns_Log(Notice, "exiting");
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



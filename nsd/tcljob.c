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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tcljob.c,v 1.7 2002/08/25 20:10:39 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
    int              flags;
    int              code;
    char            *errorCode;
    char            *errorInfo;
    Tcl_DString      ds;
} Job;

static void FreeJob(Job *jobPtr);
static Ns_ThreadProc JobThread;


/*
 *----------------------------------------------------------------------
 *
 * NsTclStopJobs --
 *
 *	Signal stop of the Tcl job queue.
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
NsTclStopJobs(NsServer *servPtr)
{
    Job *jobPtr;

    Ns_MutexLock(&servPtr->job.lock);
    servPtr->job.stop = 1;
    Tcl_DeleteHashTable(&servPtr->job.table);
    while ((jobPtr = servPtr->job.firstPtr) != NULL) {
	servPtr->job.firstPtr = jobPtr->nextPtr;
	FreeJob(jobPtr);
    }
    Ns_CondBroadcast(&servPtr->job.cond);
    Ns_MutexUnlock(&servPtr->job.lock);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWaitJobs --
 *
 *	Wait for Tcl jobs threads to exit.
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
NsTclWaitJobs(NsServer *servPtr, Ns_Time *toPtr)
{
    int status = NS_OK;

    Ns_MutexLock(&servPtr->job.lock);
    while (status == NS_OK && servPtr->job.threads.current > 0) {
	status = Ns_CondTimedWait(&servPtr->job.cond, &servPtr->job.lock, toPtr);
    }
    Ns_MutexUnlock(&servPtr->job.lock);
    if (status != NS_OK) {
	Ns_Log(Warning, "job[%s]: timeout waiting for exit", servPtr->server);
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
NsTclJobCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Job *jobPtr, **nextPtrPtr;
    int code, new, create, stop, running;
    char *cmd, id[20];
    Tcl_HashEntry *hPtr;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " command arg\"", NULL);
	return TCL_ERROR;
    }
    code = TCL_ERROR;
    cmd = argv[1];

    if (STREQ(cmd, "queue")) {
	jobPtr = ns_malloc(sizeof(Job));
	jobPtr->flags = jobPtr->code = 0;
	jobPtr->nextPtr = NULL;
	jobPtr->errorCode = jobPtr->errorInfo = NULL;
	Tcl_DStringInit(&jobPtr->ds);
	Tcl_DStringAppend(&jobPtr->ds, argv[2], -1);
	Ns_MutexLock(&servPtr->job.lock);
	stop = servPtr->job.stop;
	if (!stop) {
	    nextPtrPtr = &servPtr->job.firstPtr;
	    while (*nextPtrPtr != NULL) {
		nextPtrPtr = &((*nextPtrPtr)->nextPtr);
	    }
	    *nextPtrPtr = jobPtr;
	    if (servPtr->job.threads.idle == 0 &&
		servPtr->job.threads.current < servPtr->job.threads.max) {
		create = 1;
		++servPtr->job.threads.current;
	    } else {
		create = 0;
	    }
	    do {
		sprintf(id, "job%d", servPtr->job.nextid++);
		hPtr = Tcl_CreateHashEntry(&servPtr->job.table, id, &new);
	    } while (!new);
	    Tcl_SetHashValue(hPtr, jobPtr);
	    Ns_CondBroadcast(&servPtr->job.cond);
	}
	Ns_MutexUnlock(&servPtr->job.lock);
	if (stop) {
	    FreeJob(jobPtr);
	} else {
	    if (create) {
		Ns_ThreadCreate(JobThread, servPtr, 0, NULL);
	    }
	    Tcl_SetResult(interp, id, TCL_VOLATILE);
	    code = TCL_OK;
	}

    } else if (STREQ(cmd, "wait") || STREQ(cmd, "cancel")) {
	Ns_MutexLock(&servPtr->job.lock);
	if (!(stop = servPtr->job.stop) && (hPtr = Tcl_FindHashEntry(&servPtr->job.table, argv[2]))) {
	    jobPtr = Tcl_GetHashValue(hPtr);
	    Tcl_DeleteHashEntry(hPtr);
	    if (*cmd == 'c') {
		jobPtr->flags |= JOB_CANCEL;
		running = jobPtr->flags & JOB_RUNNING;
	    } else {
		while (!(stop = servPtr->job.stop) && !(jobPtr->flags & JOB_DONE)) {
		    Ns_CondWait(&servPtr->job.cond, &servPtr->job.lock);
		}
	    }
	}
	Ns_MutexUnlock(&servPtr->job.lock);
	if (!stop) {
	    if (hPtr == NULL) {
		Tcl_AppendResult(interp, "no such job: ", argv[2], NULL);
	    } else if (*cmd == 'c') {
		Tcl_SetResult(interp, running ? "1" : "0", TCL_STATIC);
		code = TCL_OK;
	    } else {
		Tcl_SetResult(interp, jobPtr->ds.string, TCL_VOLATILE);
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
	}

    } else {
	Tcl_AppendResult(interp, "unknown command \"",
	    cmd, "\": should be queue, wait, or cancel", NULL);
	return TCL_ERROR;
    }
    if (stop) {
	Tcl_SetResult(interp, "server shutting down", TCL_STATIC);
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
    NsServer *servPtr = arg;
    char *server = servPtr->server;
    Tcl_Interp *interp;
    Job *jobPtr;
    char buf[100];
    CONST char *err;

    Ns_MutexLock(&servPtr->job.lock);
    sprintf(buf, "-job%d:%s-", servPtr->job.threads.next++, server);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "starting");
    while (!servPtr->job.stop) {
	++servPtr->job.threads.idle;
	while (!servPtr->job.stop && servPtr->job.firstPtr == NULL) {
	    Ns_CondWait(&servPtr->job.cond, &servPtr->job.lock);
	}
	--servPtr->job.threads.idle;
	if (servPtr->job.stop) {
	    break;
	}
	jobPtr = servPtr->job.firstPtr;
	servPtr->job.firstPtr = jobPtr->nextPtr;
	jobPtr->flags |= JOB_RUNNING;
	if (jobPtr->flags & JOB_CANCEL) {
	    FreeJob(jobPtr);
	    continue;
	}
	Ns_MutexUnlock(&servPtr->job.lock);
	interp = Ns_TclAllocateInterp(server);
	jobPtr->code = Tcl_Eval(interp, jobPtr->ds.string);
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
	Ns_MutexLock(&servPtr->job.lock);
	jobPtr->flags |= JOB_DONE;
	if (jobPtr->flags & JOB_CANCEL || servPtr->job.stop) {
	    FreeJob(jobPtr);
	} else {
	    Ns_CondBroadcast(&servPtr->job.cond);
	}
    }
    --servPtr->job.threads.current;
    Ns_CondBroadcast(&servPtr->job.cond);
    Ns_MutexUnlock(&servPtr->job.lock);
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

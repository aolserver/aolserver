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
 * tclthread.c --
 *
 *	Tcl wrappers around all thread objects 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclthread.c,v 1.13 2002/07/08 02:50:55 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#ifdef NS_NOCOMPAT
#undef NS_NOCOMPAT
#endif
#include "nsd.h"

typedef struct ThreadArg {
    int detached;
    char *server;
    char script[1];
} ThreadArg;

/*
 * Local functions defined in this file
 */

static int GetAddr(Tcl_Interp *interp, char type, char *id, void **addrPtr);
static void SetAddr(Tcl_Interp *interp, int type, void *addr);
static int GetArgs(Tcl_Interp *interp, int objc, Tcl_Obj **objv,
	CONST char *opts[], int type, int create, int *optPtr, void **addrPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsTclMutexCmd --
 *
 *	Implements ns_mutex. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclMutexCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Mutex *lockPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ...\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "create")) {
        lockPtr = ns_malloc(sizeof(Ns_Mutex));
	Ns_MutexInit(lockPtr);
	if (argc > 2) {
	    Ns_MutexSetName(lockPtr, argv[2]);
	}
        SetAddr(interp, 'm', lockPtr);
    } else {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " lock\"", NULL);
            return TCL_ERROR;
        } else if (GetAddr(interp, 'm', argv[2],
			  (void **) &lockPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(argv[1], "lock")) {
	    Ns_MutexLock(lockPtr);
        } else if (STREQ(argv[1], "unlock")) {
	    Ns_MutexUnlock(lockPtr);
        } else if (STREQ(argv[1], "destroy")) {
	    Ns_MutexDestroy(lockPtr);
            ns_free(lockPtr);
        } else {
            Tcl_AppendResult(interp, "unknown command \"",
                argv[1], "\": should be create, destroy, lock or unlock",
			     NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMutexObjCmd --
 *
 *	Implements ns_mutex as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclMutexObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Mutex *lockPtr;
    static CONST char *opts[] = {
	"create", "destroy", "lock", "unlock", NULL
    };
    enum {
	MCreateIdx, MDestroyIdx, MLockIdx, MUnlockIdx
    } opt;

    if (!GetArgs(interp, objc, objv, opts, 'm', MCreateIdx,
		  (int *) &opt, (void **) &lockPtr)) {
	return TCL_ERROR;
    }
    switch (opt) {
    case MCreateIdx:
	Ns_MutexInit(lockPtr);
	if (objc > 2) {
	    Ns_MutexSetName(lockPtr, Tcl_GetString(objv[2]));
	}
	break;
    case MLockIdx:
	Ns_MutexLock(lockPtr);
	break;
    case MUnlockIdx:
	Ns_MutexUnlock(lockPtr);
	break;
    case MDestroyIdx:
	Ns_MutexDestroy(lockPtr);
        ns_free(lockPtr);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCritSecCmd --
 *
 *	Implements ns_critsec. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See doc. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCritSecCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cs *csPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ...\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "create")) {
        csPtr = ns_malloc(sizeof(Ns_Cs));
	Ns_CsInit(csPtr);
        SetAddr(interp, 'c', csPtr);
    } else {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " cs\"", NULL);
            return TCL_ERROR;
        } else if (GetAddr(interp, 'c', argv[2], (void **) &csPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(argv[1], "enter")) {
	    Ns_CsEnter(csPtr);
        } else if (STREQ(argv[1], "leave")) {
	    Ns_CsLeave(csPtr);
        } else if (STREQ(argv[1], "destroy")) {
	    Ns_CsDestroy(csPtr);
            ns_free(csPtr);
        } else {
            Tcl_AppendResult(interp, "unknown command \"",
                argv[1], "\": should be create, destroy, enter or leave",
			     NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCritSecObjCmd --
 *
 *	Implements ns_critsec. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See doc. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCritSecObjCmd(ClientData data, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    Ns_Cs *csPtr;
    static CONST char *opts[] = {
	"create", "destroy", "enter", "leave", NULL
    };
    enum {
	CCreateIdx, CDestroyIdx, CEnterIdx, CLeaveIdx
    } opt;

    if (!GetArgs(interp, objc, objv, opts, 'c', CCreateIdx,
		  (int *) &opt, (void **) &csPtr)) {
	return TCL_ERROR;
    }
    switch (opt) {
    case CCreateIdx:
	Ns_CsInit(csPtr);
	break;
    case CEnterIdx:
	Ns_CsEnter(csPtr);
	break;
    case CLeaveIdx:
	Ns_CsLeave(csPtr);
	break;
    case CDestroyIdx:
	Ns_CsDestroy(csPtr);
        ns_free(csPtr);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSemaCmd --
 *
 *	Implements ns_sema. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSemaCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Sema *semaPtr;
    int      cnt;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ...\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "create")) {
        if (argc < 3) {
            cnt = 0;
        } else if (Tcl_GetInt(interp, argv[2], &cnt) != TCL_OK) {
            return TCL_ERROR;
        }
        semaPtr = ns_malloc(sizeof(Ns_Sema));
	Ns_SemaInit(semaPtr, cnt);
        SetAddr(interp, 's', semaPtr);
    } else {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " sema ?cnt?\"", NULL);
            return TCL_ERROR;
        } else if (GetAddr(interp, 's', argv[2], 
				      (void **) &semaPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(argv[1], "release")) {
            if (argc < 4) {
                cnt = 1;
            } else if (Tcl_GetInt(interp, argv[3], &cnt) != TCL_OK) {
                return TCL_ERROR;
            }
	    Ns_SemaPost(semaPtr, cnt);
        } else if (STREQ(argv[1], "wait")) {
	    Ns_SemaWait(semaPtr);
        } else if (STREQ(argv[1], "destroy")) {
	    Ns_SemaDestroy(semaPtr);
            ns_free(semaPtr);
        } else {
            Tcl_AppendResult(interp, "unknown command \"", argv[1], 
			     "\": should be create, destroy, release or wait", 
			     NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSemaObjCmd --
 *
 *	Implements ns_sema. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSemaObjCmd(ClientData data, Tcl_Interp *interp, int objc,
		Tcl_Obj **objv)
{
    Ns_Sema *semaPtr;
    int      cnt;
    static CONST char *opts[] = {
	"create", "destroy", "release", "wait", NULL
    };
    enum {
	SCreateIdx, SDestroyIdx, SReleaseIdx, SWaitIdx
    } opt;

    if (!GetArgs(interp, objc, objv, opts, 's', SCreateIdx,
		  (int *) &opt, (void **) &semaPtr)) {
	return TCL_ERROR;
    }
    switch (opt) {
    case SCreateIdx:
        if (objc < 3) {
            cnt = 0;
        } else if (Tcl_GetIntFromObj(interp, objv[2], &cnt) != TCL_OK) {
            return TCL_ERROR;
        }
	Ns_SemaInit(semaPtr, cnt);
	break;
    case SReleaseIdx:
        if (objc < 4) {
            cnt = 1;
        } else if (Tcl_GetIntFromObj(interp, objv[3], &cnt) != TCL_OK) {
            return TCL_ERROR;
        }
	Ns_SemaPost(semaPtr, cnt);
	break;
    case SWaitIdx:
	Ns_SemaWait(semaPtr);
	break;
    case SDestroyIdx:
	Ns_SemaDestroy(semaPtr);
        ns_free(semaPtr);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclEventCmd --
 *
 *	Implements ns_event.
 *
 * Results:
 *	See docs. 
 *
 * Side effects:
 *	See docs.  NOTE: it's actually implemented with a cond.
 *
 *----------------------------------------------------------------------
 */

int
NsTclEventCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Obj *objPtr;
    Ns_Cond *condPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ...\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "create")) {
        condPtr = ns_malloc(sizeof(Ns_Cond));
	Ns_CondInit(condPtr);
        SetAddr(interp, 'e', condPtr);
    } else {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " event ?...?\"", NULL);
            return TCL_ERROR;
        } else if (GetAddr(interp, 'e', argv[2], 
				      (void **) &condPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(argv[1], "timedwait") || STREQ(argv[1], "wait")
		|| STREQ(argv[1], "abswait")) {
            Ns_Mutex       *lock;
            int             timeout, result;

            if (argc < 4) {
                Tcl_AppendResult(interp, "wrong # args: should be \"",
                    argv[0], " ", argv[1], " event lock ?timeout?\"", NULL);
                return TCL_ERROR;
            } else if (GetAddr(interp, 'm', argv[3], 
					  (void **) &lock) != TCL_OK) {
                return TCL_ERROR;
            }
            if (argc < 5) {
                timeout = 0;
            } else if (Tcl_GetInt(interp, argv[4], &timeout) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (argv[1][0] == 't') {
		if (timeout == 0) {
		    Ns_CondWait(condPtr, lock);
		    result = NS_OK;
		} else {
		    Ns_Time to;
		    to.sec = timeout;
		    to.usec = 0;
		    result = Ns_CondTimedWait(condPtr, lock, &to);
		}
	    } else if (argv[1][0] == 'a') {
            	result = Ns_AbsTimedWaitForEvent((Ns_Event *) condPtr, lock, timeout);
	    } else {
            	result = Ns_TimedWaitForEvent((Ns_Event *) condPtr, lock, timeout);
	    }
	    switch (result) {
            case NS_OK:
		objPtr = Tcl_NewBooleanObj(1);
                break;
            case NS_TIMEOUT:
		objPtr = Tcl_NewBooleanObj(0);
                break;
            default:
                return TCL_ERROR;
                break;
            }
	    Tcl_SetObjResult(interp, objPtr);
        } else if (STREQ(argv[1], "broadcast")) {
	    Ns_CondBroadcast(condPtr);
        } else if (STREQ(argv[1], "set")) {
	    Ns_CondSignal(condPtr);
        } else if (STREQ(argv[1], "destroy")) {
	    Ns_CondDestroy(condPtr);
            ns_free(condPtr);
        } else {
            Tcl_AppendResult(interp, "unknown command \"", argv[1],
			     "\": should be create, destroy, wait, set, or "
			     "broadcast", NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCondObjCmd --
 *
 *	Implements ns_cond and ns_event.
 *
 * Results:
 *	See docs. 
 *
 * Side effects:
 *	See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCondObjCmd(ClientData data, Tcl_Interp *interp, int objc,
		Tcl_Obj **objv)
{
    Tcl_Obj *objPtr;
    Ns_Cond *condPtr;
    Ns_Mutex *lock;
    Ns_Time   timeout;
    int       result;
    static CONST char *opts[] = {
	"abswait", "broadcast", "create", "destroy", "set",
	"signal", "timedwait", "wait", NULL
    };
    enum {
	EAbsWaitIdx, EBroadcastIdx, ECreateIdx, EDestroyIdx, ESetIdx,
	ESignalIdx, ETimedWaitIdx, EWaitIdx
    } opt;

    if (!GetArgs(interp, objc, objv, opts, 'e', ECreateIdx,
		  (int *) &opt, (void **) &condPtr)) {
	return TCL_ERROR;
    }
    switch (opt) {
    case ECreateIdx:
	Ns_CondInit(condPtr);
	break;
    case EAbsWaitIdx:
    case ETimedWaitIdx:
    case EWaitIdx:
        if (objc < 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "condId mutexId ?timeout?");
            return TCL_ERROR;
        }
	if (GetAddr(interp, 'm', Tcl_GetString(objv[3]), (void **) &lock) != TCL_OK) {
            return TCL_ERROR;
        }
        if (objc < 5) {
            timeout.sec = timeout.usec = 0;
        } else if (Ns_TclGetTimeFromObj(interp, objv[4], &timeout) != TCL_OK) {
            return TCL_ERROR;
        }
	if (opt == EAbsWaitIdx) {
            result = Ns_CondTimedWait(condPtr, lock, &timeout);
	} else if (opt == ETimedWaitIdx) {
	    Ns_Event *eventPtr = (Ns_Event *) condPtr;
            result = Ns_TimedWaitForEvent(eventPtr, lock, timeout.sec);
	} else {
	    if (objc < 5) {
		Ns_CondWait(condPtr, lock);
		result = NS_OK;
	    } else {
		Ns_Time abstime;
		Ns_GetTime(&abstime);
		Ns_IncrTime(&abstime, timeout.sec, timeout.usec);
		result = Ns_CondTimedWait(condPtr, lock, &abstime);
	    }
	}
	if (result == NS_OK) {
	    objPtr = Tcl_NewBooleanObj(1);
	} else if (result == NS_TIMEOUT) {
	    objPtr = Tcl_NewBooleanObj(0);
	} else {
            return TCL_ERROR;
        }
	Tcl_SetObjResult(interp, objPtr);
	break;

    case EBroadcastIdx:
	Ns_CondBroadcast(condPtr);
	break;

    case ESetIdx:
    case ESignalIdx:
	Ns_CondSignal(condPtr);
	break;

    case EDestroyIdx:
	Ns_CondDestroy(condPtr);
        ns_free(condPtr);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRWLockCmd --
 *
 *	Implements ns_rwlock. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRWLockCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_RWLock *rwlockPtr;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " command ...\"", NULL);
	return TCL_ERROR;
    }
    
    if (STREQ(argv[1], "create")) {
	rwlockPtr = ns_malloc(sizeof(Ns_RWLock));
	Ns_RWLockInit(rwlockPtr);
	SetAddr(interp, 'r', rwlockPtr);
    } else {
	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " ", argv[1], " rwlock ?...?\"", NULL);
	    return TCL_ERROR;
	} else if (GetAddr(interp, 'r', argv[2], 
				      (void**) &rwlockPtr) != TCL_OK) {
	    return TCL_ERROR;
	}

	if (STREQ(argv[1], "destroy")) {
	    Ns_RWLockDestroy(rwlockPtr);
	    ns_free(rwlockPtr);
	} else if (STREQ(argv[1], "readlock")) {
	    Ns_RWLockRdLock(rwlockPtr);
	} else if (STREQ(argv[1], "readunlock")) {
	    Ns_RWLockUnlock(rwlockPtr);
	} else if (STREQ(argv[1], "writelock")) {
	    Ns_RWLockWrLock(rwlockPtr);
	} else if (STREQ(argv[1], "writeunlock")) {
	    Ns_RWLockUnlock(rwlockPtr);
	} else if (STREQ(argv[1], "unlock")) {
	    Ns_RWLockUnlock(rwlockPtr);
	} else {
	    Tcl_AppendResult(interp, "unknown command \"",
			     argv[1], "\":should be create, destroy, "
			     "readlock, readunlock, writelock, "
			     "writeunlock", NULL);
	    return TCL_ERROR;
	}
    }
    return TCL_OK;

}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRWLockObjCmd --
 *
 *	Implements ns_rwlock. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRWLockObjCmd(ClientData data, Tcl_Interp *interp, int objc,
		  Tcl_Obj **objv)
{
    Ns_RWLock *rwlockPtr;
    static CONST char *opts[] = {
	"create", "destroy", "readlock", "readunlock",
	"writelock", "writeunlock", "unlock", NULL
    };
    enum {
	RCreateIdx, RDestroyIdx, RReadLockIdx, RReadUnlockIdx,
	RWriteLockIdx, RWriteUnlockIdx, RUnlockIdx
    } opt;

    if (!GetArgs(interp, objc, objv, opts, 'r', RCreateIdx,
		  (int *) &opt, (void **) &rwlockPtr)) {
	return TCL_ERROR;
    }
    switch (opt) {
    case RCreateIdx:
	Ns_RWLockInit(rwlockPtr);
	break;
    case RReadLockIdx:
	Ns_RWLockRdLock(rwlockPtr);
	break;
    case RWriteLockIdx:
	Ns_RWLockWrLock(rwlockPtr);
	break;
    case RReadUnlockIdx:
    case RWriteUnlockIdx:
    case RUnlockIdx:
	Ns_RWLockUnlock(rwlockPtr);
	break;
    case RDestroyIdx:
	Ns_RWLockDestroy(rwlockPtr);
	ns_free(rwlockPtr);
	break;
    }
    return TCL_OK;

}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThreadCmd --
 *
 *	Implements ns_thread. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclThreadCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    void *status;
    Ns_Thread tid;
    ThreadArg *argPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command arg\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "begin") || STREQ(argv[1], "create") ||
	STREQ(argv[1], "begindetached")) {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " script\"", NULL);
            return TCL_ERROR;
        }
	argPtr = ns_malloc(sizeof(ThreadArg) + strlen(argv[2]));
	argPtr->server = (itPtr ? itPtr->servPtr->server : NULL);
	argPtr->detached = STREQ(argv[1], "begindetached");
	strcpy(argPtr->script, argv[2]);
	Ns_ThreadCreate(NsTclThread, argPtr, 0, &tid);
        SetAddr(interp, 't', tid);
    } else if (STREQ(argv[1], "wait") || STREQ(argv[1], "join")) {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " tid\"", NULL);
            return TCL_ERROR;
        }
        if (GetAddr(interp, 't', argv[2], (void **) &tid) 
	    != TCL_OK) {
            return TCL_ERROR;
        }
	Ns_ThreadJoin(&tid, &status);
	Tcl_SetResult(interp, (char *) status, (Tcl_FreeProc *) ns_free);
    } else if (STREQ(argv[1], "get")) {
        Ns_ThreadSelf(&tid);
        SetAddr(interp, 't', tid);
    } else if (STREQ(argv[1], "getid") || STREQ(argv[1], "id")) {
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_ThreadId()));
    } else if (STREQ(argv[1], "name")) {
	if (argc > 2) {
	    Ns_ThreadSetName(argv[2]);
	}
	Tcl_SetResult(interp, Ns_ThreadGetName(), TCL_VOLATILE);
    } else if (STREQ(argv[1], "yield")) {
        Ns_ThreadYield();
    } else {
        Tcl_AppendResult(interp, "unknown command \"",
            argv[1], "\":  should be begin, begindetached, create "
            "get, getid, id, join, wait, or yield", NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * SetAddr --
 *
 *	Set the interp result with an opaque thread-object string id.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Interp result set.
 *
 *----------------------------------------------------------------------
 */

static void
SetAddr(Tcl_Interp *interp, int type, void *addr)
{
    char buf[40];

    sprintf(buf, "%cid%p", type, addr);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
}


/*
 *----------------------------------------------------------------------
 *
 * GetObj --
 *
 *	Take an opaque thread-object Tcl handle and convert it into a 
 *	pointer. 
 *
 * Results:
 *	TCL_OK or TCL_ERROR 
 *
 * Side effects:
 *	An error will be put appended to the interp on failure 
 *
 *----------------------------------------------------------------------
 */

static int
GetAddr(Tcl_Interp *interp, char type, char *id, void **addrPtr)
{
    void *addr;

    if (*id++ != type || *id++ != 'i' || *id++ != 'd'
	|| sscanf(id, "%p", &addr) != 1 || addr == NULL) {
	Tcl_AppendResult(interp, "invalid thread object id \"",
	    id, "\"", NULL);
	return TCL_ERROR;
    }
    *addrPtr = addr;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThread --
 *
 *	Tcl thread main.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copy of string result is return as exit arg to be reaped
 *	by ns_thread wait.
 *
 *----------------------------------------------------------------------
 */

void
NsTclThread(void *arg)
{
    ThreadArg *argPtr = arg;
    Ns_DString ds, *dsPtr;

    if (argPtr->detached) {
	dsPtr = NULL;
    } else {
	Ns_DStringInit(&ds);
	dsPtr = &ds;
    }
    (void) Ns_TclEval(dsPtr, argPtr->server, argPtr->script);
    ns_free(argPtr);
    if (!argPtr->detached) {
	Ns_ThreadExit(Ns_DStringExport(&ds));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclThreadArgProc --
 *
 *	Proc info routine to copy Tcl thread script.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will copy script to given dstring.
 *
 *----------------------------------------------------------------------
 */

void
NsTclThreadArgProc(Tcl_DString *dsPtr, void *arg)
{
    ThreadArg *argPtr = arg;

     Tcl_DStringAppendElement(dsPtr, argPtr->script);
}


static int
GetArgs(Tcl_Interp *interp, int objc, Tcl_Obj **objv, CONST char *opts[],
	 int type, int create, int *optPtr, void **addrPtr)
{
    int   opt;
    void *addr;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return 0;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    &opt) != TCL_OK) {
	return 0;
    }
    if (opt == create) {
	addr = ns_malloc(sizeof(void *));
        SetAddr(interp, type, addr);
    } else {
        if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "object");
            return 0;
	}
    	if (GetAddr(interp, type, Tcl_GetString(objv[2]), &addr) != TCL_OK) {
	    return 0;
	}
    }
    *addrPtr = addr;
    *optPtr = opt;
    return 1;
}

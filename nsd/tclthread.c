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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclthread.c,v 1.6 2001/11/05 20:26:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#ifdef NS_NOCOMPAT
#undef NS_NOCOMPAT
#endif
#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int GetObj(Tcl_Interp *interp, char type, char *id, void **addrPtr);
static void SetObj(Tcl_Interp *interp, int type, void *addr);


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
        SetObj(interp, 'm', lockPtr);
    } else {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " lock\"", NULL);
            return TCL_ERROR;
        } else if (GetObj(interp, 'm', argv[2],
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
        SetObj(interp, 'c', csPtr);
    } else {
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " cs\"", NULL);
            return TCL_ERROR;
        } else if (GetObj(interp, 'c', argv[2], (void **) &csPtr) != TCL_OK) {
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
        SetObj(interp, 's', semaPtr);
    } else {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " sema ?cnt?\"", NULL);
            return TCL_ERROR;
        } else if (GetObj(interp, 's', argv[2], 
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
    Ns_Cond *condPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command ...\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "create")) {
        condPtr = ns_malloc(sizeof(Ns_Cond));
	Ns_CondInit(condPtr);
        SetObj(interp, 'e', condPtr);
    } else {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " event ?...?\"", NULL);
            return TCL_ERROR;
        } else if (GetObj(interp, 'e', argv[2], 
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
            } else if (GetObj(interp, 'm', argv[3], 
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
                interp->result = "1";
                break;
            case NS_TIMEOUT:
                interp->result = "0";
                break;
            default:
                return TCL_ERROR;
                break;
            }
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
	SetObj(interp, 'r', rwlockPtr);
    } else {
	if (argc < 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " ", argv[1], " rwlock ?...?\"", NULL);
	    return TCL_ERROR;
	} else if (GetObj(interp, 'r', argv[2], 
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
			     "readlock, readunlock, writelock, writeunlock");
	    return TCL_ERROR;
	}
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
NsTclThreadCmd(ClientData data, Tcl_Interp *interp, int argc, char **argv)
{
    void *status;
    Ns_Thread tid;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " command arg\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "begin") || STREQ(argv[1], "create")) {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " script\"", NULL);
            return TCL_ERROR;
        }
        if (Ns_TclThread(interp, argv[2], &tid) != NS_OK) {
            return TCL_ERROR;
        }
        SetObj(interp, 't', tid);
    } else if (STREQ(argv[1], "begindetached")) {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " script\"", NULL);
            return TCL_ERROR;
        }
        if (Ns_TclDetachedThread(interp, argv[2]) != NS_OK) {
            return TCL_ERROR;
        }
    } else if (STREQ(argv[1], "wait") || STREQ(argv[1], "join")) {
        if (argc < 3) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                argv[0], " ", argv[1], " tid\"", NULL);
            return TCL_ERROR;
        }
        if (GetObj(interp, 't', argv[2], (void **) &tid) 
	    != TCL_OK) {
            return TCL_ERROR;
        }
	Ns_ThreadJoin(&tid, &status);
	sprintf(interp->result, "%d", (int) status);
    } else if (STREQ(argv[1], "get")) {
        Ns_ThreadSelf(&tid);
        SetObj(interp, 't', tid);
    } else if (STREQ(argv[1], "getid") || STREQ(argv[1], "id")) {
        sprintf(interp->result, "%d", Ns_ThreadId());
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
 * SetObj --
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
SetObj(Tcl_Interp *interp, int type, void *addr)
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
GetObj(Tcl_Interp *interp, char type, char *id, void **addrPtr)
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

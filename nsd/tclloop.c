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
 * tclloop.c --
 *
 *	Replacements for the "for", "while", and "foreach" commands to be
 *	monitored and managed by "ns_loop_ctl" command.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclloop.c,v 1.4 2009/12/24 19:50:08 dvrsn Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure supports sending a script to a loop to eval.
 */

typedef struct EvalData {
    enum {
	EVAL_WAIT,
	EVAL_DONE,
	EVAL_DROP
    } state;			/* Eval request state. */
    int code;			/* Script result code. */
    Tcl_DString script;		/* Script buffer. */
    Tcl_DString result;		/* Result buffer. */
} EvalData;

/*
 * The following structure is allocated for the "while"
 * and "for" commands to maintain a copy of the current
 * args and provide a cancel flag.
 */

typedef struct LoopData {
    enum {
	LOOP_RUN,
	LOOP_PAUSE,
	LOOP_CANCEL
    } control;			/* Loop control commands. */
    unsigned int lid;		/* Unique loop id. */
    int tid;			/* Thread id of script. */
    unsigned int spins;		/* Loop iterations. */
    Ns_Time etime;		/* Loop entry time. */
    Tcl_HashEntry *hPtr;	/* Entry in active loop table. */
    Tcl_DString args;		/* Copy of command args. */
    EvalData *evalPtr;		/* Eval request pending. */
} LoopData;

/*
 * Static procedures defined in this file.
 */

static int CheckControl(NsServer *servPtr, Tcl_Interp *interp, LoopData *dataPtr);
static void EnterLoop(NsServer *servPtr, LoopData *dataPtr, int objc,
		      Tcl_Obj **objv);
static void LeaveLoop(NsServer *servPtr, LoopData *dataPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsTclForObjCmd --
 *
 *      This procedure is invoked to process the "for" Tcl command.
 *      See the user documentation for details on what it does.
 *
 *	With the bytecode compiler, this procedure is only called when
 *	a command name is computed at runtime, and is "for" or the name
 *	to which "for" was renamed: e.g.,
 *	"set z for; $z {set i 0} {$i<100} {incr i} {puts $i}"
 *
 *	Copied from the Tcl source with additional calls to the 
 *	loop control facility.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */

int
NsTclForObjCmd(arg, interp, objc, objv)
    ClientData arg;                     /* Pointer to NsInterp. */
    Tcl_Interp *interp;                 /* Current interpreter. */
    int objc;                           /* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    LoopData data;
    int result, value;

    if (objc != 5) {
        Tcl_WrongNumArgs(interp, 1, objv, "start test next command");
        return TCL_ERROR;
    }

    result = Tcl_EvalObjEx(interp, objv[1], 0);
    if (result != TCL_OK) {
        if (result == TCL_ERROR) {
            Tcl_AddErrorInfo(interp, "\n    (\"for\" initial command)");
        }
        return result;
    }
    EnterLoop(servPtr, &data, objc, objv);
    while (1) {
	/*
	 * We need to reset the result before passing it off to
	 * Tcl_ExprBooleanObj.  Otherwise, any error message will be appended
	 * to the result of the last evaluation.
	 */

	Tcl_ResetResult(interp);
        result = Tcl_ExprBooleanObj(interp, objv[2], &value);
        if (result != TCL_OK) {
	    goto done;
        }
        if (!value) {
            break;
        }
	result = CheckControl(servPtr, interp, &data);
	if (result == TCL_OK) {
            result = Tcl_EvalObjEx(interp, objv[4], 0);
	}
        if ((result != TCL_OK) && (result != TCL_CONTINUE)) {
            if (result == TCL_ERROR) {
                char msg[32 + TCL_INTEGER_SPACE];

                sprintf(msg, "\n    (\"for\" body line %d)",Tcl_GetErrorLine(interp));
                Tcl_AddErrorInfo(interp, msg);
            }
            break;
        }
        result = Tcl_EvalObjEx(interp, objv[3], 0);
	if (result == TCL_BREAK) {
            break;
        } else if (result != TCL_OK) {
            if (result == TCL_ERROR) {
                Tcl_AddErrorInfo(interp, "\n    (\"for\" loop-end command)");
            }
	    goto done;
        }
    }
    if (result == TCL_BREAK) {
        result = TCL_OK;
    }
    if (result == TCL_OK) {
        Tcl_ResetResult(interp);
    }
done:
    LeaveLoop(servPtr, &data);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWhileObjCmd --
 *
 *      This procedure is invoked to process the "while" Tcl command.
 *      See the user documentation for details on what it does.
 *
 *	With the bytecode compiler, this procedure is only called when
 *	a command name is computed at runtime, and is "while" or the name
 *	to which "while" was renamed: e.g., "set z while; $z {$i<100} {}"
 *
 *	Copied from the Tcl source with additional calls to the 
 *	loop control facility.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *----------------------------------------------------------------------
 */

int
NsTclWhileObjCmd(arg, interp, objc, objv)
    ClientData arg;                     /* Pointer to NsInterp. */
    Tcl_Interp *interp;                 /* Current interpreter. */
    int objc;                           /* Number of arguments. */
    Tcl_Obj *CONST objv[];       	/* Argument objects. */
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    LoopData data;
    int result, value;

    if (objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "test command");
        return TCL_ERROR;
    }
    EnterLoop(servPtr, &data, objc, objv);
    while (1) {
        result = Tcl_ExprBooleanObj(interp, objv[1], &value);
        if (result != TCL_OK) {
	    goto done;
        }
        if (!value) {
            break;
        }
	result = CheckControl(servPtr, interp, &data);
	if (result == TCL_OK) {
            result = Tcl_EvalObjEx(interp, objv[2], 0);
	}
        if ((result != TCL_OK) && (result != TCL_CONTINUE)) {
            if (result == TCL_ERROR) {
                char msg[32 + TCL_INTEGER_SPACE];

                sprintf(msg, "\n    (\"while\" body line %d)",
                        Tcl_GetErrorLine(interp));
                Tcl_AddErrorInfo(interp, msg);
            }
            break;
        }
    }
    if (result == TCL_BREAK) {
        result = TCL_OK;
    }
    if (result == TCL_OK) {
        Tcl_ResetResult(interp);
    }
done:
    LeaveLoop(servPtr, &data);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclForeachObjCmd --
 *
 *	This object-based procedure is invoked to process the "foreach" Tcl
 *	command.  See the user documentation for details on what it does.
 *
 * Results:
 *	A standard Tcl object result.
 *
 * Side effects:
 *	See the user documentation.
 *
 *----------------------------------------------------------------------
 */

int
NsTclForeachObjCmd(arg, interp, objc, objv)
    ClientData arg;             /* Pointer to NsInterp. */
    Tcl_Interp *interp;		/* Current interpreter. */
    int objc;			/* Number of arguments. */
    Tcl_Obj *CONST objv[];	/* Argument objects. */
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    LoopData data;
    int result = TCL_OK;
    int i;			/* i selects a value list */
    int j, maxj;		/* Number of loop iterations */
    int v;			/* v selects a loop variable */
    int numLists;		/* Count of value lists */
    Tcl_Obj *bodyPtr;

    /*
     * We copy the argument object pointers into a local array to avoid
     * the problem that "objv" might become invalid. It is a pointer into
     * the evaluation stack and that stack might be grown and reallocated
     * if the loop body requires a large amount of stack space.
     */
    
#define NUM_ARGS 9
    Tcl_Obj *(argObjStorage[NUM_ARGS]);
    Tcl_Obj **argObjv = argObjStorage;
    
#define STATIC_LIST_SIZE 4
    int indexArray[STATIC_LIST_SIZE];
    int varcListArray[STATIC_LIST_SIZE];
    Tcl_Obj **varvListArray[STATIC_LIST_SIZE];
    int argcListArray[STATIC_LIST_SIZE];
    Tcl_Obj **argvListArray[STATIC_LIST_SIZE];

    int *index = indexArray;		   /* Array of value list indices */
    int *varcList = varcListArray;	   /* # loop variables per list */
    Tcl_Obj ***varvList = varvListArray;   /* Array of var name lists */
    int *argcList = argcListArray;	   /* Array of value list sizes */
    Tcl_Obj ***argvList = argvListArray;   /* Array of value lists */

    if (objc < 4 || (objc%2 != 0)) {
	Tcl_WrongNumArgs(interp, 1, objv,
		"varList list ?varList list ...? command");
	return TCL_ERROR;
    }
    EnterLoop(servPtr, &data, objc, objv);

    /*
     * Create the object argument array "argObjv". Make sure argObjv is
     * large enough to hold the objc arguments.
     */

    if (objc > NUM_ARGS) {
	argObjv = (Tcl_Obj **) ckalloc(objc * sizeof(Tcl_Obj *));
    }
    for (i = 0;  i < objc;  i++) {
	argObjv[i] = objv[i];
    }

    /*
     * Manage numList parallel value lists.
     * argvList[i] is a value list counted by argcList[i]
     * varvList[i] is the list of variables associated with the value list
     * varcList[i] is the number of variables associated with the value list
     * index[i] is the current pointer into the value list argvList[i]
     */

    numLists = (objc-2)/2;
    if (numLists > STATIC_LIST_SIZE) {
	index = (int *) ckalloc(numLists * sizeof(int));
	varcList = (int *) ckalloc(numLists * sizeof(int));
	varvList = (Tcl_Obj ***) ckalloc(numLists * sizeof(Tcl_Obj **));
	argcList = (int *) ckalloc(numLists * sizeof(int));
	argvList = (Tcl_Obj ***) ckalloc(numLists * sizeof(Tcl_Obj **));
    }
    for (i = 0;  i < numLists;  i++) {
	index[i] = 0;
	varcList[i] = 0;
	varvList[i] = (Tcl_Obj **) NULL;
	argcList[i] = 0;
	argvList[i] = (Tcl_Obj **) NULL;
    }

    /*
     * Break up the value lists and variable lists into elements
     */

    maxj = 0;
    for (i = 0;  i < numLists;  i++) {
	result = Tcl_ListObjGetElements(interp, argObjv[1+i*2],
	        &varcList[i], &varvList[i]);
	if (result != TCL_OK) {
	    goto done;
	}
	if (varcList[i] < 1) {
	    Tcl_AppendToObj(Tcl_GetObjResult(interp),
	            "foreach varlist is empty", -1);
	    result = TCL_ERROR;
	    goto done;
	}
	
	result = Tcl_ListObjGetElements(interp, argObjv[2+i*2],
	        &argcList[i], &argvList[i]);
	if (result != TCL_OK) {
	    goto done;
	}
	
	j = argcList[i] / varcList[i];
	if ((argcList[i] % varcList[i]) != 0) {
	    j++;
	}
	if (j > maxj) {
	    maxj = j;
	}
    }

    /*
     * Iterate maxj times through the lists in parallel
     * If some value lists run out of values, set loop vars to ""
     */
    
    bodyPtr = argObjv[objc-1];
    for (j = 0;  j < maxj;  j++) {
	for (i = 0;  i < numLists;  i++) {
	    /*
	     * Refetch the list members; we assume that the sizes are
	     * the same, but the array of elements might be different
	     * if the internal rep of the objects has been lost and
	     * recreated (it is too difficult to accurately tell when
	     * this happens, which can lead to some wierd crashes,
	     * like Bug #494348...)
	     */

	    result = Tcl_ListObjGetElements(interp, argObjv[1+i*2],
		    &varcList[i], &varvList[i]);
	    if (result != TCL_OK) {
		panic("Tcl_ForeachObjCmd: could not reconvert variable list %d to a list object\n", i);
	    }
	    result = Tcl_ListObjGetElements(interp, argObjv[2+i*2],
		    &argcList[i], &argvList[i]);
	    if (result != TCL_OK) {
		panic("Tcl_ForeachObjCmd: could not reconvert value list %d to a list object\n", i);
	    }
	    
	    for (v = 0;  v < varcList[i];  v++) {
		int k = index[i]++;
		Tcl_Obj *valuePtr, *varValuePtr;
		
		if (k < argcList[i]) {
		    valuePtr = argvList[i][k];
		} else {
		    valuePtr = Tcl_NewObj(); /* empty string */
		}
		Tcl_IncrRefCount(valuePtr);
		varValuePtr = Tcl_ObjSetVar2(interp, varvList[i][v],
			NULL, valuePtr, 0);
		Tcl_DecrRefCount(valuePtr);
		if (varValuePtr == NULL) {
		    Tcl_ResetResult(interp);
		    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
			"couldn't set loop variable: \"",
			Tcl_GetString(varvList[i][v]), "\"", (char *) NULL);
		    result = TCL_ERROR;
		    goto done;
		}

	    }
	}
	result = CheckControl(servPtr, interp, &data);
	if (result == TCL_OK) {
	    result = Tcl_EvalObjEx(interp, bodyPtr, 0);
	}
	if (result != TCL_OK) {
	    if (result == TCL_CONTINUE) {
		result = TCL_OK;
	    } else if (result == TCL_BREAK) {
		result = TCL_OK;
		break;
	    } else if (result == TCL_ERROR) {
                char msg[32 + TCL_INTEGER_SPACE];

		sprintf(msg, "\n    (\"foreach\" body line %d)",
			Tcl_GetErrorLine(interp));
		Tcl_AddObjErrorInfo(interp, msg, -1);
		break;
	    } else {
		break;
	    }
	}
    }
    if (result == TCL_OK) {
	Tcl_ResetResult(interp);
    }

    done:
    if (numLists > STATIC_LIST_SIZE) {
	ckfree((char *) index);
	ckfree((char *) varcList);
	ckfree((char *) argcList);
	ckfree((char *) varvList);
	ckfree((char *) argvList);
    }
    if (argObjv != argObjStorage) {
	ckfree((char *) argObjv);
    }
    LeaveLoop(servPtr, &data);
    return result;
#undef STATIC_LIST_SIZE
#undef NUM_ARGS
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLoopCtlObjCmd --
 *
 *      Control command to list all active for or while loops in
 *	any thread, get info (thread id and args) for an active
 *	loop, or signal cancel of a loop.
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      May cancel an active loop.  Not cancel results in a
 *	TCL_ERROR result for the "for" or "while" command,
 *	an exception which can possibly be caught.
 *
 *----------------------------------------------------------------------
 */

int
NsTclLoopCtlObjCmd(arg, interp, objc, objv)
    ClientData arg;                     /* Pointer to NsInterp. */
    Tcl_Interp *interp;                 /* Current interpreter. */
    int objc;                           /* Number of arguments. */
    Tcl_Obj *CONST objv[];       	/* Argument objects. */
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    LoopData *dataPtr;
    EvalData eval;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Ns_Time timeout;
    int lid, result, len, status;
    char *str = "";
    Tcl_Obj *objPtr, *listPtr;
    static CONST char *opts[] = {
        "list", "info", "pause", "resume", "cancel", "eval",
	"install", NULL
    };
    enum {
        LListIdx, LInfoIdx, LPauseIdx, LResumeIdx, LCancelIdx, LEvalIdx,
	LInstallIdx
    } opt;
    static CONST char *copts[] = {
        "for", "foreach", "while", NULL
    };
    enum {
        CForIdx, CForeachIdx, CWhileIdx
    } copt;
    static Tcl_ObjCmdProc *procs[] = {
	NsTclForObjCmd, NsTclForeachObjCmd, NsTclWhileObjCmd
    };
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?id?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                            (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Handle the list and install commands and verify arguments first.
     */

    switch (opt) {
    case LListIdx:
    	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
	}
        listPtr = Tcl_NewObj();
        Ns_MutexLock(&servPtr->tcl.llock);
    	hPtr = Tcl_FirstHashEntry(&servPtr->tcl.loops, &search);
	while (hPtr != NULL) {
            lid = (int) Tcl_GetHashKey(&servPtr->tcl.loops, hPtr);
            objPtr = Tcl_NewIntObj(lid);
            Tcl_ListObjAppendElement(interp, listPtr, objPtr);
            hPtr = Tcl_NextHashEntry(&search);
        }
        Ns_MutexUnlock(&servPtr->tcl.llock);
        Tcl_SetObjResult(interp, listPtr);
	return TCL_OK;
	break;

    case LInstallIdx:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "command");
	    return TCL_ERROR;
	}
    	if (Tcl_GetIndexFromObj(interp, objv[2], copts, "command", 0,
                                (int *) &copt) != TCL_OK) {
            return TCL_ERROR;
	}
	Tcl_CreateObjCommand(interp, copts[copt], procs[copt], arg, NULL);
	return TCL_OK;
	break;

    case LEvalIdx:
    	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "id script");
            return TCL_ERROR;
	}
	break;

    default:
    	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "id");
            return TCL_ERROR;
	 }
	 break;
    }

    /*
     * All other commands require a loop id arg.
     */

    if (Tcl_GetIntFromObj(interp, objv[2], (int *) &lid) != TCL_OK) {
	return TCL_ERROR;
    }
    result = TCL_OK;
    Ns_MutexLock(&servPtr->tcl.llock);
    hPtr = Tcl_FindHashEntry(&servPtr->tcl.loops, (char *) lid);
    if (hPtr == NULL) {
	switch (opt) {
    	case LInfoIdx:
    	case LEvalIdx:
	    Tcl_AppendResult(interp, "no such loop id: ",
			     Tcl_GetString(objv[2]), NULL);
	    result = TCL_ERROR;
	    break;
    	default:
	    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 0);
	    break;
	}
	goto done;
    }

    dataPtr = Tcl_GetHashValue(hPtr);
    switch (opt) {
    case LInstallIdx:
    case LListIdx:
	/* NB: Silence warning. */
	break;

    case LInfoIdx:
	/*
	 * Info format is:
	 * {loop id} {thread id} {start time} {status} {command args}
	 */

	listPtr = Tcl_NewObj();
        objPtr = Tcl_NewIntObj(lid);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
        objPtr = Tcl_NewIntObj(dataPtr->tid);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
        objPtr = Tcl_NewObj();
	Ns_TclSetTimeObj(objPtr, &dataPtr->etime);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
        objPtr = Tcl_NewIntObj(dataPtr->spins);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
	switch (dataPtr->control) {
	case LOOP_RUN:
	    str = "running";
	    break;
	case LOOP_PAUSE:
	    str = "paused";
	    break;
	case LOOP_CANCEL:
	    str = "canceled";
	    break;
	}
	objPtr = Tcl_NewStringObj(str, -1);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
        objPtr = Tcl_NewStringObj(dataPtr->args.string, dataPtr->args.length);
        Tcl_ListObjAppendElement(interp, listPtr, objPtr);
        Tcl_SetObjResult(interp, listPtr);
	break;

    case LEvalIdx:
	if (dataPtr->evalPtr != NULL) {
	    Tcl_SetResult(interp, "eval pending", TCL_STATIC);
	    result = TCL_ERROR;
	    goto done;
	}

	/*
	 * Queue new script to eval.
	 */

	eval.state = EVAL_WAIT;
	eval.code = TCL_OK;
	Tcl_DStringInit(&eval.result);
	Tcl_DStringInit(&eval.script);
	str = Tcl_GetStringFromObj(objv[3], &len);
	Tcl_DStringAppend(&eval.script, str, len);
	dataPtr->evalPtr = &eval;

	/*
	 * Wait for result.
	 */

	Ns_GetTime(&timeout);
	Ns_IncrTime(&timeout, 2, 0);
	Ns_CondBroadcast(&servPtr->tcl.lcond);
	status = NS_OK;
	while (status == NS_OK && eval.state == EVAL_WAIT) {
	    status = Ns_CondTimedWait(&servPtr->tcl.lcond,
				      &servPtr->tcl.llock, &timeout);
	}
	switch (eval.state) {
	case EVAL_WAIT:
	    Tcl_SetResult(interp, "timeout: result dropped", TCL_STATIC);
	    dataPtr->evalPtr = NULL;
	    result = TCL_ERROR;
	    break;
	case EVAL_DROP:
	    Tcl_SetResult(interp, "dropped: loop exited", TCL_STATIC);
	    result = TCL_ERROR;
	    break;
	case EVAL_DONE:
	    Tcl_DStringResult(interp, &eval.result);
	    result = eval.code;
	}
	Tcl_DStringFree(&eval.script);
	Tcl_DStringFree(&eval.result);
	break;

    case LResumeIdx:
    case LPauseIdx:
    case LCancelIdx:
	if (opt == LCancelIdx) {
	    dataPtr->control = LOOP_CANCEL;
	} else if (opt == LPauseIdx) {
	    dataPtr->control = LOOP_PAUSE;
	} else {
	    dataPtr->control = LOOP_RUN;
	}
    	Tcl_SetBooleanObj(Tcl_GetObjResult(interp), 1);
    	Ns_CondBroadcast(&servPtr->tcl.lcond);
	break;
    }
done:
    Ns_MutexUnlock(&servPtr->tcl.llock);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * EnterLoop --
 *
 *      Add entry for the LoopData structure when a "for" or
 *	"while" command starts.
 *
 * Results:
 *      None.
*
 * Side effects:
 *      Loop can be monitored and possibly canceled by "loop.ctl".
 *
 *----------------------------------------------------------------------
 */

static void
EnterLoop(NsServer *servPtr, LoopData *dataPtr, int objc, Tcl_Obj **objv)
{
    static unsigned int next = 0;
    int i, new;

    dataPtr->control = LOOP_RUN;
    dataPtr->spins = 0;
    dataPtr->tid = Ns_ThreadId();
    dataPtr->evalPtr = NULL;
    Ns_GetTime(&dataPtr->etime);
    /* NB: Must copy strings in case loop body updates or invalidates them. */
    Tcl_DStringInit(&dataPtr->args);
    for (i = 0; i < objc; ++i) {
	Tcl_DStringAppendElement(&dataPtr->args, Tcl_GetString(objv[i]));
    }
    Ns_MutexLock(&servPtr->tcl.llock);
    do {
	dataPtr->lid = next++;
    	dataPtr->hPtr = Tcl_CreateHashEntry(&servPtr->tcl.loops,
					    (char *) dataPtr->lid, &new);
    } while (!new);
    Tcl_SetHashValue(dataPtr->hPtr, dataPtr); 
    Ns_MutexUnlock(&servPtr->tcl.llock);
}


/*
 *----------------------------------------------------------------------
 *
 * LeaveLoop --
 *
 *      Remove entry for the LoopData structure when a "for" or
 *	"while" command exits.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
LeaveLoop(NsServer *servPtr, LoopData *dataPtr)
{
    Ns_MutexLock(&servPtr->tcl.llock);
    if (dataPtr->evalPtr != NULL) {
	dataPtr->evalPtr->state = EVAL_DROP;
	Ns_CondBroadcast(&servPtr->tcl.lcond);
    }
    Tcl_DeleteHashEntry(dataPtr->hPtr);
    Ns_MutexUnlock(&servPtr->tcl.llock);
    Tcl_DStringFree(&dataPtr->args);
}


/*
 *----------------------------------------------------------------------
 *
 * CheckControl --
 *
 *      Check for control flag within a loop of a cancel or pause.
 *
 * Results:
 *      TCL_OK if not canceled, TCL_ERROR otherwise.
 *
 * Side effects:
 *      Leave cancel message as interp result.
 *
 *----------------------------------------------------------------------
 */

static int
CheckControl(NsServer *servPtr, Tcl_Interp *interp, LoopData *dataPtr)
{
    Tcl_DString script;
    char *str;
    int result, len;

    Ns_MutexLock(&servPtr->tcl.llock);
    ++dataPtr->spins;
    while (dataPtr->evalPtr != NULL || dataPtr->control == LOOP_PAUSE) {
    	if (dataPtr->evalPtr != NULL) {
	    Tcl_DStringInit(&script);
	    Tcl_DStringAppend(&script, dataPtr->evalPtr->script.string,
			      dataPtr->evalPtr->script.length);
	    Ns_MutexUnlock(&servPtr->tcl.llock);
	    result = Tcl_EvalEx(interp, script.string, script.length, 0);
	    Tcl_DStringFree(&script);
	    if (result != TCL_OK) {
		Ns_TclLogError(interp);
	    }
	    Ns_MutexLock(&servPtr->tcl.llock);
	    if (dataPtr->evalPtr == NULL) {
		Ns_Log(Error, "loopctl: dropped result: %s", Tcl_GetStringResult(interp));
	    } else {
		str = Tcl_GetStringFromObj(Tcl_GetObjResult(interp), &len);
	    	Tcl_DStringAppend(&dataPtr->evalPtr->result, str, len);
		dataPtr->evalPtr->state = EVAL_DONE;
		dataPtr->evalPtr = NULL;
	    	Ns_CondBroadcast(&servPtr->tcl.lcond);
	    }
	}
	if (dataPtr->control == LOOP_PAUSE) {
	    Ns_CondWait(&servPtr->tcl.lcond, &servPtr->tcl.llock);
	}
    }
    if (dataPtr->control == LOOP_CANCEL) {
	Tcl_SetResult(interp, "loop canceled", TCL_STATIC);
	result = TCL_ERROR;
    } else {
	result = TCL_OK;
    }
    Ns_MutexUnlock(&servPtr->tcl.llock);
    return result;
}

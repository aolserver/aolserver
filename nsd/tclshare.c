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
 * tclshare.c --
 *
 *	This file implements shared Tcl variables between interpreters.
 */

#include	"nsd.h"

/*
 * Shared variables are implemented with a per-server hash table
 * that is keyed by the variable name.  The table entries store
 * the shared value and a lock.  As some point we may want to
 * reduce the number of locks by sharing them among variables.
 */

typedef struct NsShareVar {
    Ns_Cs lock;                	/* Lock to serialize access to the value */
    int shareCount;		/* Number of threads sharing the value */
    int flags;			/* Undefined, scalar, or array */
    Tcl_Obj *objPtr;		/* Value for Scalar values */
    Tcl_HashTable array;	/* Values for Array values */
} NsShareVar;

#define SHARE_UNDEFINED	0x0
#define SHARE_SCALAR	0x1
#define SHARE_ARRAY	0x2
#define SHARE_TRACE	0x8

/*
 * Static functions defined in this file.
 */

static void ShareUnsetVar(Tcl_Interp *interp, char *varName,
	NsShareVar *valuePtr);
static char *ShareTraceProc(ClientData clientData, Tcl_Interp *interp,
	char *name1, char *name2, int flags);
static int ShareVar(NsInterp *itPtr, Tcl_Interp *interp, char *varName);
static int InitShare(NsServer *servPtr, Tcl_Interp *interp,
	char *varName, char *script);
static void RegisterShare(NsInterp *itPtr, Tcl_Interp *interp,
	char *varName, NsShareVar *valuePtr);
static char *GetGlobalizedName(Tcl_DString *dsPtr, char *varName);


/*
 *----------------------------------------------------------------------
 *
 * NsTclShareCmd --
 *
 *	This procedure is invoked to process the "ns_share" Tcl command.
 *      It links the variables passed in to values that are shared.
 *
 * Results:
 *	A standard Tcl result value.
 *
 * Side effects:
 *	Very similar to "global"
 *
 *----------------------------------------------------------------------
 */

int
NsTclShareCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
 
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " ?-init script? varName ?varName ...?\"", NULL);
	return TCL_ERROR;
    }
    if (itPtr == NULL) {
	Tcl_SetResult(interp, "no server", TCL_STATIC);
	return TCL_ERROR;
    }
    if (STREQ(argv[1], "-init")) {
        if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " -init script varName\"", NULL);
	    return TCL_ERROR;
        }
        if (ShareVar(itPtr, interp, argv[3]) != TCL_OK ||
    	    InitShare(itPtr->servPtr, interp, argv[3], argv[2]) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else {
        for (argc--, argv++; argc > 0; argc--, argv++) {
	    if (ShareVar(itPtr, interp, *argv) != TCL_OK) {
		return TCL_ERROR;
	    }
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * InitShare --
 *
 *	Helper routine to initialize a shared variable once, invoke
 *	by a call to ns_share -init. 
 *
 * Results:
 *	A standard Tcl result value.
 *
 * Side effects:
 *	Init script is evaluated once.
 *
 *----------------------------------------------------------------------
 */

static int
InitShare(NsServer *servPtr, Tcl_Interp *interp, char *varName, char *script)
{
    Tcl_HashEntry *hPtr;
    int new, result;

    Ns_MutexLock(&servPtr->share.lock);
    hPtr = Tcl_CreateHashEntry(&servPtr->share.inits, varName, &new);
    if (!new) {
	while (Tcl_GetHashValue(hPtr) == NULL) {
    	    Ns_CondWait(&servPtr->share.cond, &servPtr->share.lock);
	}
        result = TCL_OK;
    } else {
	Ns_MutexUnlock(&servPtr->share.lock);
	result = Tcl_EvalEx(interp, script, -1, 0);
	Ns_MutexLock(&servPtr->share.lock);
	Tcl_SetHashValue(hPtr, (ClientData) 1);
	Ns_CondBroadcast(&servPtr->share.cond);
    }
    Ns_MutexUnlock(&servPtr->share.lock);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * ShareVar --
 *
 *	Declare that a variable is shared among interpreters.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	This registers the shared variable in a global hash table
 *	and sets of variable traces to keep the variable in sync.
 *
 *----------------------------------------------------------------------
 */

static int
ShareVar(NsInterp *itPtr, Tcl_Interp *interp, char *varName)
{
    NsServer *servPtr = itPtr->servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_DString ds;
    NsShareVar *valuePtr;
    char *s;
    char* globalizedVarName;
    int new;

    /*
     * Ensure the variable to share is a scalar or whole array.
     */

    if ((s = strchr(varName, '(')) != NULL && (strchr(s, ')') != NULL)) {
	Tcl_AppendResult(interp, "can't share ", varName,
		": must share whole arrays", (char *) NULL);
    	return TCL_ERROR;
    }

    /*
     * Create the shared variable entry if it doesn't already exist.
     */

    globalizedVarName = GetGlobalizedName(&ds, varName);
    Ns_CsEnter(&servPtr->share.cs);
    hPtr = Tcl_CreateHashEntry(&servPtr->share.vars, globalizedVarName, &new);
    if (!new) {
    	valuePtr = Tcl_GetHashValue(hPtr);
    } else {
    	valuePtr = ns_calloc(1, sizeof(NsShareVar));
	Ns_CsInit(&valuePtr->lock);
        valuePtr->flags = SHARE_UNDEFINED;

        /*
         * See if the variable exists already as a global variable
         * If it does get its current value.
         */

        if (Tcl_VarEval(interp, "info exists ", globalizedVarName, NULL) != TCL_OK) {
            Tcl_AppendResult(interp, "error sharing ", globalizedVarName, " can't determine existence of variable", (char *) NULL);
	    Tcl_DStringFree(&ds);
            return TCL_ERROR;
        }
        
        if (strcmp(interp->result, "1") == 0) {
            /*
             * Get existing value in variable being shared.
             */

            valuePtr->objPtr = Tcl_GetVar2Ex(interp, globalizedVarName, NULL, TCL_LEAVE_ERR_MSG);
            if (valuePtr->objPtr != NULL) {
		char *string;
		int length;

		string = Tcl_GetStringFromObj(valuePtr->objPtr, &length);
		valuePtr->objPtr = Tcl_NewStringObj(string, length);
		Tcl_IncrRefCount(valuePtr->objPtr);
                valuePtr->flags = SHARE_SCALAR;
            } else {
                if (Tcl_VarEval(interp, "array get ", globalizedVarName, NULL) == TCL_OK) {
                    /* 
                     * Probably an array. 
                     */
                    int argc = 0;
                    char** argv = NULL;
                    int x;
                    Tcl_InitHashTable(&valuePtr->array, TCL_STRING_KEYS);
                    if (Tcl_SplitList(interp, interp->result, &argc, &argv) == TCL_OK) {
                        for (x = 0; x < argc; x += 2) {
                            Tcl_HashEntry* newEntry;
                            Tcl_Obj* newObj;
                            int new;
                            newEntry = Tcl_CreateHashEntry(&valuePtr->array, argv[x], &new);
                            newObj = Tcl_NewStringObj(argv[x + 1], -1);
			    Tcl_IncrRefCount(newObj);
                            Tcl_SetHashValue(newEntry, (ClientData) newObj);
                        }
                        Tcl_Free((char*) argv);
                    }
                    valuePtr->flags = SHARE_ARRAY;
                }
            }
            Tcl_VarEval(interp, "unset ", globalizedVarName, NULL);
        }
        Tcl_SetHashValue(hPtr, valuePtr);
    }
    valuePtr->shareCount++;

    /*
     * Register the variable in a per-thread table.
     * Declare it as a global variable.
     */

    RegisterShare(itPtr, interp, globalizedVarName, valuePtr);
    Tcl_VarEval(interp, "global ", varName, NULL);

    Ns_CsLeave(&servPtr->share.cs);

    /*
     * The value in the shared table is independent of the values
     * in each thread's shared variable.  If a thread deletes its
     * global variable, the UNSET trace will hook up to the
     * shared value again.  There is no need to put extra
     * reference counts on the variable to preserver the shared value.
     */

    Tcl_DStringFree(&ds);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * RegisterShare --
 *
 *	Set up a trace the first time we see a share variable.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Enter the share name in the per-thread hash table.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterShare(itPtr, interp, varName, valuePtr)
    NsInterp *itPtr;		/* Virtual server. */
    Tcl_Interp *interp;		/* The interpreter */
    char *varName;		/* Share name */
    NsShareVar *valuePtr;	/* Handle on shared value */
{
    int traceFlags = TCL_TRACE_WRITES | TCL_TRACE_UNSETS | TCL_TRACE_READS | TCL_TRACE_ARRAY;
    ClientData data, shareData;
    
    /*
     * Check if there's an existing ns_share trace on the variable. 
     * Tcl_VarTraceInfo will return the clientData for each
     * trace in reverse order in which they were created.  For ns_share
     * the address of the RegisterShare function is used as
     * a reasonably unique value.  We look at the data for each
     * trace until this value is found or NULL which normally
     * indicates no more traces.
     */

    shareData = (ClientData) RegisterShare;     
    data = NULL;
    do {
    	data = Tcl_VarTraceInfo(interp, varName, traceFlags, ShareTraceProc, data);
    } while (data != shareData && data != NULL);

    if (data == NULL) {

        /*
         * There appears to be no existing ns_share trace on the variable.
    	 * Note this code could be fooled by some other trace being registered
	 * with NULL clientData.  Oh well.
         */

        if (valuePtr->flags & SHARE_SCALAR) {
            Tcl_SetVar2Ex(interp, varName, NULL, Tcl_DuplicateObj(valuePtr->objPtr),
			  TCL_GLOBAL_ONLY);
        } else if (valuePtr->flags & SHARE_ARRAY) {
            Tcl_HashSearch search;
            Tcl_HashEntry* hPtr;

            hPtr = Tcl_FirstHashEntry(&valuePtr->array, &search);
            while (hPtr != NULL) {
                char* key;
                Tcl_Obj* objPtr;

                key = Tcl_GetHashKey(&valuePtr->array, hPtr);
                objPtr = Tcl_GetHashValue(hPtr);
                Tcl_SetVar2Ex(interp, varName, key,
			      Tcl_DuplicateObj(objPtr), TCL_GLOBAL_ONLY);
                hPtr = Tcl_NextHashEntry(&search);
            }
        }
        if (Tcl_TraceVar2(interp, varName, (char *) NULL, traceFlags,
			  ShareTraceProc, shareData) != TCL_OK) {
            Ns_Fatal("ns_share: could not trace: %s", varName);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ShareUnsetVar --
 *
 *	Carefully unset the variable associated with a shared value.
 *	We must flag the unset as being "our own" so we don't
 *	deadlock in the ShareTraceProc, and we have to restore
 *	the variable tracing.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Tcl_UnsetVar
 *
 *----------------------------------------------------------------------
 */

static void
ShareUnsetVar(interp, varName, valuePtr)
    Tcl_Interp *interp;		/* The interpreter */
    char *varName;		/* Scaler, array, or array element name */
    NsShareVar *valuePtr;	/* Shared variable state, must be locked */
{
    valuePtr->flags |= SHARE_TRACE;
    Tcl_UnsetVar(interp, varName, 0);
    if (Tcl_TraceVar2(interp, varName, (char *) NULL,
	    TCL_TRACE_WRITES | TCL_TRACE_UNSETS |
	    TCL_TRACE_READS | TCL_TRACE_ARRAY,  ShareTraceProc,
	    (ClientData) NULL) != TCL_OK) {
        Ns_Fatal("ns_share: could not trace: %s", varName);
    }
    valuePtr->flags &= ~SHARE_TRACE;
}


/*
 *----------------------------------------------------------------------
 *
 * ShareTraceProc --
 *
 *	This procedure is invoked whenever a shared variable
 *	is read, modified or deleted.  It propagates the change to the
 *	values in the share table.
 *
 * Results:
 *	Always returns NULL to indicate success.
 *
 * Side effects:
 *	The interpreter variable is kept in sync with the shared value.
 *
 *----------------------------------------------------------------------
 */

	/* ARGSUSED */
static char *
ShareTraceProc(clientData, interp, name1, name2, flags)
    ClientData clientData;	/* Not used. */
    Tcl_Interp *interp;		/* Interpreter whose share variable is
				 * being modified. */
    char *name1;		/* Name of the shared variable. */
    char *name2;		/* Name of variable being modified, or NULL
				 * if whole array is being deleted (UTF-8). */
    int flags;			/* Indicates what's happening. */
{
    NsShareVar *valuePtr;	/* The shared value */
    Tcl_HashEntry *hPtr;	/* Current hash table item */
    Tcl_HashEntry *nextPtr;	/* Next hash table item */
    Tcl_HashSearch search;	/* For iterating through shared arrays */
    Tcl_Obj *objPtr;		/* The value in the variable */
    Tcl_Obj *oldObjPtr;		/* The previous shared value */
    Tcl_Obj *newObjPtr;		/* The new shared value */
    int new;			/* For CreateHashEntry */
    int destroyed = 0;		/* True if share value is destroyed */
    int bail = 0;		/* True if this is a recursive trace */
    char* string;               /* String form of shared value */
    int length;                 /* Length of string */
    Tcl_DString ds;		/* Buffer for globalized name */
    NsInterp *itPtr = NsGetInterp(interp);
    NsServer *servPtr = itPtr->servPtr;

    name1 = GetGlobalizedName(&ds, name1);

    Ns_CsEnter(&servPtr->share.cs);
    hPtr = Tcl_FindHashEntry(&servPtr->share.vars, name1);
    if (hPtr == NULL) {
	/*
	 * This trace is firing on an upvar alias to the shared variable.
	 * Punt because there is no exported Tcl API to get the real
	 * variable name.  Also lets us cheat and unset the shared
	 * variable in the interpreter without reflecting the unset
	 * down into the shared value.  HACK ALERT.
	 */
	Ns_CsLeave(&servPtr->share.cs);
	goto done;
    }
    valuePtr = Tcl_GetHashValue(hPtr);

    /*
     * Shared variables are persistent until the interpreter is destroyed.
     * When the last interpreter sharing the value goes away, so
     * does the shared value.
     *
     * Don't unset shared values (i.e., bail out) when the interpreter is
     * being destroyed as that is a nasty side effect on other interpreters
     * still using the shared value.
     */

    if (flags & TCL_INTERP_DESTROYED) {
	valuePtr->shareCount--;
	if (valuePtr->shareCount == 0) {
	    destroyed = 1;
	    Tcl_DeleteHashEntry(hPtr);
	} else {
	    bail = 1;
	}
    }

    /*
     * The Tcl_UnsetVar calls in this procedure will trigger
     * recursive unset traces, so if we detect this we just bail
     */
    
    if (valuePtr->flags & SHARE_TRACE) {
	bail = 1;
    }
    Ns_CsLeave(&servPtr->share.cs);

    if (bail) {
	goto done;
    }
	    
    Ns_CsEnter(&valuePtr->lock);

    if ((flags & TCL_TRACE_ARRAY) && (valuePtr->flags & SHARE_ARRAY)) {
	/*
	 * The easiest way to ensure our copy is up-to-date is just
	 * to delete it and recreate it from scratch.  This makes
	 * the array names and array get operations weighty.
	 */

	ShareUnsetVar(interp, name1, valuePtr);
	hPtr = Tcl_FirstHashEntry(&valuePtr->array, &search);
	while (hPtr != NULL) {
	    name2 = Tcl_GetHashKey(&valuePtr->array, hPtr);
	    objPtr = Tcl_GetHashValue(hPtr);
	    Tcl_SetVar2Ex(interp, name1, name2, Tcl_DuplicateObj(objPtr), 0);
	    hPtr = Tcl_NextHashEntry(&search);
	}
    }

    if (flags & TCL_TRACE_WRITES) {

	/*
	 * Get a copy of the variable value for the shared value.
	 */

	objPtr = Tcl_GetVar2Ex(interp, name1, name2, 0);
	string = Tcl_GetStringFromObj(objPtr, &length);
	newObjPtr = Tcl_NewStringObj(string, length);
	Tcl_IncrRefCount(newObjPtr);
	if (name2 != NULL) {
	    /*
	     * Update the shared value.
	     */

	    if (valuePtr->flags == SHARE_UNDEFINED) {
		Tcl_InitHashTable(&valuePtr->array, TCL_STRING_KEYS);
		valuePtr->flags = SHARE_ARRAY;
	    }
	    hPtr = Tcl_CreateHashEntry(&valuePtr->array, name2, &new);
	    oldObjPtr = (Tcl_Obj *) Tcl_GetHashValue(hPtr);
	    Tcl_SetHashValue(hPtr, (char *) newObjPtr);
	} else {
	    oldObjPtr = valuePtr->objPtr;
	    valuePtr->objPtr = newObjPtr;
	}

	/*
	 * Discard the old shared value.
	 */

	if (oldObjPtr != NULL) {
	    Tcl_DecrRefCount(oldObjPtr);
	}
    }

    if (flags & TCL_TRACE_READS) {
	objPtr = NULL;
	if (name2 != NULL) {
	    hPtr = Tcl_FindHashEntry(&valuePtr->array, name2);
	    if (hPtr != NULL) {
		objPtr = (Tcl_Obj *) Tcl_GetHashValue(hPtr);
	    }
	} else if (valuePtr->objPtr != NULL) {
	    objPtr = valuePtr->objPtr;
	}
	if (objPtr != NULL) {
	    newObjPtr = Tcl_DuplicateObj(objPtr);
	    Tcl_SetVar2Ex(interp, name1, name2, newObjPtr, 0);
	}
    }

    if (flags & TCL_TRACE_UNSETS) {
	/*
	 * Unset the corresponding shared value.
	 */

	if (name2 != NULL) {
	    hPtr = Tcl_FindHashEntry(&valuePtr->array, name2);
	    if (hPtr != NULL) {
		objPtr = Tcl_GetHashValue(hPtr);
		Tcl_DecrRefCount(objPtr);
		Tcl_DeleteHashEntry(hPtr);
	    }
	} else if (valuePtr->flags & SHARE_ARRAY) {
	    hPtr = Tcl_FirstHashEntry(&valuePtr->array, &search);
	    while (hPtr != NULL) {
		nextPtr = Tcl_NextHashEntry(&search);
		objPtr = Tcl_GetHashValue(hPtr);
		Tcl_DecrRefCount(objPtr);
		Tcl_DeleteHashEntry(hPtr);
		hPtr = nextPtr;
	    }
	    Tcl_DeleteHashTable(&valuePtr->array);
	    valuePtr->flags &= ~SHARE_ARRAY;
	} else if (valuePtr->objPtr != NULL) {
	    Tcl_DecrRefCount(valuePtr->objPtr);
	    valuePtr->objPtr = NULL;
	    valuePtr->flags &= ~SHARE_SCALAR;
	}
	if (!destroyed) {
	    /*
	     * This makes the shared property of the variable "sticky"
	     * across unsets.
	     */
	    
	    if (Tcl_TraceVar2(interp, name1, (char *) NULL,
		    TCL_TRACE_WRITES | TCL_TRACE_UNSETS |
		    TCL_TRACE_READS | TCL_TRACE_ARRAY,  ShareTraceProc,
		    (ClientData) NULL) != TCL_OK) {
		Ns_Fatal("Cannot set trace on share");
	    }
	}
    }

    Ns_CsLeave(&valuePtr->lock);

    /*
     * Assert we are the only thread with a reference to this
     * valuePtr, so we can delete it without holding its lock.
     */

    if (destroyed) {
	Ns_CsDestroy(&valuePtr->lock);
	Tcl_Free((char *) valuePtr );
    }

done:
    Tcl_DStringFree(&ds);
    return NULL;
}


static char *
GetGlobalizedName(Tcl_DString *dsPtr, char *varName)
{
    Tcl_DStringInit(dsPtr);

    if (strncmp("::", varName, 2) != 0) {
	Tcl_DStringAppend(dsPtr, "::", 2);
    }
    Tcl_DStringAppend(dsPtr, varName, -1);
    return dsPtr->string;
}

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
 * tclnsshare.cpp --
 *
 *	This file implements shared Tcl variables between interpreters.
 *	Instead of hacking at the Tcl variable implementation, we
 *	leverage the variable tracing facilities to keep variable
 *	values in sync between interpreters.
 *
 *	NOTE (7.6): The Tcl 7.6 version of this code is over in
 *	the Tcl library and modifies tclVar.c
 *
 *	NOTE (8.*): There is better variable tracing now to support
 *	the shared env array, so we take a similar approach here.
 */

#include	"ns.h"


static Tcl_HashTable    shareTable;
static Ns_Mutex		shareLock;
static ClientData	shareClientData	= (ClientData) 666;
static int              init = 0;   /* Initialization flag */

static int traceFlags = TCL_TRACE_WRITES | TCL_TRACE_UNSETS | TCL_TRACE_READS | TCL_TRACE_ARRAY;;

typedef struct NsShareVar {
    Tcl_Obj *objPtr;		/* Value for Scalar values */
    Tcl_HashTable array;	/* Values for Array values */
} NsShareVar;


#if TCL_MAJOR_VERSION >= 8

static char *ShareTraceProc(ClientData clientData, Tcl_Interp *interp,
			    char *name1, char *name2, int flags);

/*
 *----------------------------------------------------------------------
 *
 * NsTclShareVar --
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

int
NsTclShareVar(Tcl_Interp *interp, char *varName)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    NsShareVar *sPtr;
    Tcl_Obj *objPtr, *newObjPtr;
    int new, ret, exists = 0;
    char *p, key;
    

    /*
     * Initialize the global table of shared variable names.
     */

    if (init == 0) {
	Ns_MasterLock();
	if (init == 0) {
	    Ns_Log(Warning, "***The use of ns_share is strongly discouraged due"
		   " to excessive lock contention. Please use nsv instead.");
	    Tcl_InitHashTable(&shareTable, TCL_STRING_KEYS);
	    init = 1;
	}
	Ns_MasterUnlock();
    }

    if (Tcl_VarEval(interp, "info exists ", varName, NULL) != TCL_OK) {
        return TCL_ERROR;
    } else if (strcmp(interp->result, "1") == 0) {
        exists = 1;
    } else {
        exists = 0;
    }
    Tcl_ResetResult(interp);

    /*
     * NB: Tcl passes "namespace" variable names when in global scope,
     * i.e. foo becomes ::foo. This is a hack to remove leading colons
     * from the shared table key.
     */
     
    p = varName;
    while (*p == ':') {
        p++;
    }
    
    Ns_LockMutex(&shareLock);
    hPtr = Tcl_CreateHashEntry(&shareTable, p, &new);
    if (!new) {
    	sPtr = Tcl_GetHashValue(hPtr);
    } else {
    	sPtr = ns_malloc(sizeof(NsShareVar));
        sPtr->objPtr = NULL;
	Tcl_InitHashTable(&sPtr->array, TCL_STRING_KEYS);
        Tcl_SetHashValue(hPtr, sPtr);
        
        objPtr = Tcl_GetVar2Ex(interp, varName, NULL, 0);
        if (objPtr != NULL) {
            int length;
            char *string;
            
            string = Tcl_GetStringFromObj(objPtr, &length);
            newObjPtr = Tcl_NewStringObj(string, length);
            Tcl_IncrRefCount(newObjPtr);
            sPtr->objPtr = newObjPtr;
        } else {
            if (Tcl_VarEval(interp, "array get ", varName, NULL) == TCL_OK) {
                /* 
                 * Probably an array. 
                 */
                int argc = 0;
                char** argv = NULL;
                int x;
                if (Tcl_SplitList(interp, interp->result, &argc, &argv) == TCL_OK) {
                    for (x = 0; x < argc; x += 2) {
                        hPtr = Tcl_CreateHashEntry(&sPtr->array, argv[x], &new);
                        newObjPtr = Tcl_NewStringObj(argv[x + 1], -1);
			Tcl_IncrRefCount(newObjPtr);
                        Tcl_SetHashValue(hPtr, (ClientData) newObjPtr);
                    }
                    if (argv != NULL) {
                        Tcl_Free((char*) argv);
                    }
                }
            }
        }
    }

    Tcl_UntraceVar(interp, varName, traceFlags, ShareTraceProc, shareClientData);
    if (Tcl_TraceVar2(interp, varName, (char *) NULL, traceFlags, 
		      ShareTraceProc, shareClientData) != TCL_OK) {
        Ns_Fatal("Cannot set trace on share");
    }

    Ns_UnlockMutex(&shareLock);
    
    /*
     * Call Tcl_GetVar to force a read trace, setting the local
     * version of the variable for the first time if necessary
     * so that "info exists" will work.
     */
     
    Tcl_GetVar(interp, varName, 0);
	
    return TCL_OK;
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

static void
EmptyShareVar(NsShareVar *sPtr)
{
    Tcl_HashEntry *hPtr;	/* Current hash table item */
    Tcl_HashEntry *nextPtr;	/* Next hash table item */
    Tcl_HashSearch search;	/* For iterating through shared arrays */
    Tcl_Obj *objPtr;		/* The value in the variable */
    
    hPtr = Tcl_FirstHashEntry(&sPtr->array, &search);
    while (hPtr != NULL) {
	nextPtr = Tcl_NextHashEntry(&search);
	objPtr = Tcl_GetHashValue(hPtr);
	Tcl_DecrRefCount(objPtr);
	Tcl_DeleteHashEntry(hPtr);
	hPtr = nextPtr;
    }
    if (sPtr->objPtr != NULL) {
	Tcl_DecrRefCount(sPtr->objPtr);
	sPtr->objPtr = NULL;
    }
}

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
    NsShareVar *sPtr;           /* The shared value */
    Tcl_HashEntry *hPtr;	/* Current hash table item */
    Tcl_HashEntry *nextPtr;	/* Next hash table item */
    Tcl_HashSearch search;	/* For iterating through shared arrays */
    Tcl_Obj *objPtr;		/* The value in the variable */
    Tcl_Obj *oldObjPtr;		/* The previous shared value */
    Tcl_Obj *newObjPtr;		/* The new shared value */
    int new;			/* For CreateHashEntry */
    char *string;               /* String form of shared value */
    char *p, *key;
    int length;                 /* Length of string */

    /*
     * NB: Tcl passes "namespace" variable names when in global scope,
     * i.e. foo becomes ::foo. This is a hack to remove leading colons
     * from the shared table key.
     */
     
    p = name1;
    while (*p == ':') {
	p++;
    }
    
#if DEBUG
    Ns_Log(Notice, "ShareTraceProc %s %s : %s %s %s %s %s %s", name1, 
	   (name2 ? name2 : ""),
	   ((flags & TCL_TRACE_WRITES) ? "TCL_TRACE_WRITES" : ""),
	   ((flags & TCL_TRACE_READS) ? "TCL_TRACE_READS" : ""),
	   ((flags & TCL_TRACE_UNSETS) ? "TCL_TRACE_UNSETS" : ""),
	   ((flags & TCL_TRACE_ARRAY) ? "TCL_TRACE_ARRAY" : ""),
	   ((flags & TCL_INTERP_DESTROYED) ? "TCL_INTERP_DESTROYED" : ""),
	   ((flags & TCL_TRACE_DESTROYED) ? "TCL_TRACE_DESTROYED" : ""));
#endif

    Ns_LockMutex(&shareLock);
    hPtr = Tcl_FindHashEntry(&shareTable, p);
    if (hPtr == NULL) {
	/*
	 * This trace is firing on an upvar alias to the shared variable.
	 * Punt because there is no exported Tcl API to get the real
	 * variable name. HACK ALERT.
	 */
	Ns_UnlockMutex(&shareLock);
	return "no such variable";
    }
    sPtr = Tcl_GetHashValue(hPtr);

    if (flags & TCL_TRACE_WRITES) {

        /*
         * Get a copy of the variable value for the shared value.
         */

        objPtr = Tcl_GetVar2Ex(interp, name1, name2, 0);
        string = Tcl_GetStringFromObj(objPtr, &length);
        newObjPtr = Tcl_NewStringObj(string, length);
        Tcl_IncrRefCount(newObjPtr);
        if (name2 != NULL) {
            hPtr = Tcl_CreateHashEntry(&sPtr->array, name2, &new);
            oldObjPtr = (Tcl_Obj *) Tcl_GetHashValue(hPtr);
            Tcl_SetHashValue(hPtr, (char *) newObjPtr);
        } else {
            oldObjPtr = sPtr->objPtr;
            sPtr->objPtr = newObjPtr;
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
	if (name2 == NULL) {
            if (sPtr->objPtr) {
	        objPtr = Tcl_DuplicateObj(sPtr->objPtr);
            } else {
                goto setall;
            }
	} else {
	    hPtr = Tcl_FindHashEntry(&sPtr->array, name2);
	    if (hPtr != NULL) {
	        objPtr = Tcl_DuplicateObj(Tcl_GetHashValue(hPtr));
            } else {

                /*
                 * Temporarily disable trace so unsetting array variables
                 * don't cause a deadlock.
                 */ 

                Tcl_UntraceVar(interp, name1, traceFlags,
			       ShareTraceProc, shareClientData);
                Tcl_UnsetVar2(interp, name1, name2, 0);
                if (Tcl_TraceVar2(interp, name1, (char *) NULL, traceFlags,
				  ShareTraceProc, shareClientData) != TCL_OK) {  
                    Ns_Fatal("Cannot set trace on share");                 
                }
            }
        }
        if (objPtr != NULL) {
	    Tcl_SetVar2Ex(interp, name1, name2, objPtr, 0);
        }
    }

    if (flags & TCL_TRACE_ARRAY) {
    setall:
        hPtr = Tcl_FirstHashEntry(&sPtr->array, &search); 
        if (hPtr != NULL) {                              
                                                         
            /*             
             * Temporarily disable trace so unsetting array variables
             * don't cause a deadlock.                               
             */                                                      
                                      
            Tcl_UntraceVar(interp, name1, traceFlags,
			   ShareTraceProc, shareClientData);
            Tcl_UnsetVar(interp, name1, 0);              
            while (hPtr != NULL) {                       
                key = Tcl_GetHashKey(&sPtr->array, hPtr);
                objPtr = Tcl_GetHashValue(hPtr);         
                newObjPtr = Tcl_DuplicateObj(objPtr);    
                Tcl_SetVar2Ex(interp, name1, key, newObjPtr, 0);
                hPtr = Tcl_NextHashEntry(&search);              
            }                                                   
            if (Tcl_TraceVar2(interp, name1, (char *) NULL, traceFlags,
			      ShareTraceProc, shareClientData) != TCL_OK) {  
                Ns_Fatal("Cannot set trace on share");                 
            }                                                        
        }                                             
    }

    if (flags & TCL_TRACE_DESTROYED) {
        if (!(flags & TCL_INTERP_DESTROYED)) {

            /*
             * Artificially keep reference to the variable
             * until the interp is destroyed. 
             */

            if (Tcl_TraceVar2(interp, name1, (char *) NULL, traceFlags,
			      ShareTraceProc, shareClientData) != TCL_OK) {  
                Ns_Fatal("Cannot set trace on share");                 
            }                                                        
        }
    } else if (flags & TCL_TRACE_UNSETS) {
	/*
	 * Unset the corresponding shared value.
	 */

	if (name2 != NULL) {
	    hPtr = Tcl_FindHashEntry(&sPtr->array, name2);
	    if (hPtr != NULL) {
	        objPtr = Tcl_GetHashValue(hPtr);
	        Tcl_DecrRefCount(objPtr);
	        Tcl_DeleteHashEntry(hPtr);
	    }
	} else {
            EmptyShareVar(sPtr);
        }
    }
    Ns_UnlockMutex(&shareLock);

    return NULL;
}
#endif



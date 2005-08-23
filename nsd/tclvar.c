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
 * tclvar.c --
 *
 * 	Support for the old ns_var and new nsv_* commands.
 */

#include "nsd.h"

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclvar.c,v 1.17 2005/08/23 21:41:31 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

/*
 * The following structure defines a collection of arrays.
 * Only the arrays within a given bucket share a lock,
 * allowing for more concurency in nsv.
 */

typedef struct Bucket {
    Ns_Mutex lock;
    Tcl_HashTable arrays;   
} Bucket;

/*
 * The following structure maintains the context for each variable
 * array.
 */

typedef struct Array {
    Bucket *bucketPtr;		/* Array bucket. */
    Tcl_HashEntry *entryPtr;	/* Entry in bucket array table. */
    Tcl_HashTable vars;		/* Table of variables. */
} Array;

/*
 * Forward declarations for coommands and routines defined in this file.
 */

static void SetVar(Array *, Tcl_Obj *key, Tcl_Obj *value);
static void UpdateVar(Tcl_HashEntry *hPtr, Tcl_Obj *obj);
static void FlushArray(Array *arrayPtr);
static Array *LockArray(void *arg, Tcl_Interp *interp, Tcl_Obj *array,
			int create);
#define UnlockArray(arrayPtr) \
	Ns_MutexUnlock(&((arrayPtr)->bucketPtr->lock));


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvCreateBuckets --
 *
 *	Create a new array of buckets for a server.
 *
 * Results:
 *	Pointer to bucket array.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

struct Bucket *
NsTclCreateBuckets(char *server, int n)
{
    char buf[NS_THREAD_NAMESIZE];
    Bucket *buckets;

    buckets = ns_malloc(sizeof(Bucket) * n);
    while (--n >= 0) {
        sprintf(buf, "nsv:%d", n);
        Tcl_InitHashTable(&buckets[n].arrays, TCL_STRING_KEYS);
        Ns_MutexInit(&buckets[n].lock);
        Ns_MutexSetName2(&buckets[n].lock, buf, server);
    } 
    return buckets;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvGetObjCmd --
 *
 *	Implements nsv_get.
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
NsTclNsvGetObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;

    if (objc != 3) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key");
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 0);
    if (arrayPtr == NULL) {
	return TCL_ERROR;
    }
    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2]));
    if (hPtr != NULL) {
	Tcl_SetStringObj(Tcl_GetObjResult(interp), Tcl_GetHashValue(hPtr), -1);
    }
    UnlockArray(arrayPtr);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such key: ", Tcl_GetString(objv[2]), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvExistsObjCmd --
 *
 *	Implements nsv_exists.
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
NsTclNsvExistsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;
    int exists;

    if (objc != 3) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key");
	return TCL_ERROR;
    }
    exists = 0;
    arrayPtr = LockArray(arg, NULL, objv[1], 0);
    if (arrayPtr != NULL) {
    	if (Tcl_FindHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2])) != NULL) {
	    exists = 1;
	}
    	UnlockArray(arrayPtr);
    }
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), exists);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvSetObjCmd --
 *
 *	Implelments nsv_set.
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
NsTclNsvSetObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;

    if (objc == 3) {
    	return NsTclNsvGetObjCmd(arg, interp, objc, objv);
    } else if (objc != 4) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key ?value?");
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 1);
    SetVar(arrayPtr, objv[2], objv[3]);
    UnlockArray(arrayPtr);
    Tcl_SetObjResult(interp, objv[3]);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvIncrObjCmd --
 *
 *	Implements nsv_incr as an obj command. 
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
NsTclNsvIncrObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;
    int count, current, result, new;
    char *value;
    Tcl_HashEntry *hPtr;

    if (objc != 3 && objc != 4) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key ?count?");
	return TCL_ERROR;
    }
    if (objc == 3)  {
	count = 1;
    } else if (Tcl_GetIntFromObj(interp, objv[3], &count) != TCL_OK) {
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 1);
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2]), &new);
    if (new) {
	current = 0;
	result = TCL_OK;
    } else {
    	value = Tcl_GetHashValue(hPtr);
	result = Tcl_GetInt(interp, value, &current);
    }
    if (result == TCL_OK) {
	Tcl_Obj *obj = Tcl_GetObjResult(interp);
    	current += count;
	Tcl_SetIntObj(obj, current);
    	UpdateVar(hPtr, obj);
    }
    UnlockArray(arrayPtr);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvLappendObjCmd --
 *
 *	Implements nsv_lappend command.
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
NsTclNsvLappendObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;
    int i, new;
    Tcl_HashEntry *hPtr;

    if (objc < 4) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key string ?string ...?");
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 1);
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2]), &new);
    if (new) {
	Tcl_SetListObj(Tcl_GetObjResult(interp), objc-3, objv+3);
    } else {
	Tcl_SetResult(interp, Tcl_GetHashValue(hPtr), TCL_VOLATILE);
    	for (i = 3; i < objc; ++i) {
	    Tcl_AppendElement(interp, Tcl_GetString(objv[i]));
	}
    }
    UpdateVar(hPtr, Tcl_GetObjResult(interp));
    UnlockArray(arrayPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvAppendObjCmd --
 *
 *	Implements nsv_append command.
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
NsTclNsvAppendObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;
    int i, new;
    Tcl_HashEntry *hPtr;

    if (objc < 4) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array key string ?string ...?");
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 1);
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2]), &new);
    if (!new) {
	Tcl_SetResult(interp, Tcl_GetHashValue(hPtr), TCL_VOLATILE);
    }
    for (i = 3; i < objc; ++i) {
	Tcl_AppendResult(interp, Tcl_GetString(objv[i]), NULL);
    }
    UpdateVar(hPtr, Tcl_GetObjResult(interp));
    UnlockArray(arrayPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvArrayObjCmd --
 *
 *	Implements nsv_array as an obj command.
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclNsvArrayObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Array *arrayPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *pattern, *key;
    int i, lobjc, size;
    Tcl_Obj *result, **lobjv;

    static CONST char *opts[] = {
	"set", "reset", "get", "names", "size", "exists", NULL
    };
    enum {
	CSetIdx, CResetIdx, CGetIdx, CNamesIdx, CSizeIdx, CExistsIdx
    } _nsmayalias opt;

    if (objc < 2) {
    	Tcl_WrongNumArgs(interp, 1, objv, "option ...");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    result = Tcl_GetObjResult(interp);
    switch (opt) {
    case CSetIdx:
    case CResetIdx:
	if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "array valueList");
	    return TCL_ERROR;
	}
	if (Tcl_ListObjGetElements(interp, objv[3], &lobjc,
	    	    	    	   &lobjv) != TCL_OK) {
	    return TCL_ERROR;
    	}
    	if (lobjc & 1) {
	    Tcl_AppendResult(interp, "invalid list: ",
		    	     Tcl_GetString(objv[3]), NULL);
	    return TCL_ERROR;
	}
    	arrayPtr = LockArray(arg, interp, objv[2], 1);
	if (opt == CResetIdx) {
	    FlushArray(arrayPtr);
	}
    	for (i = 0; i < lobjc; i += 2) {
	    SetVar(arrayPtr, lobjv[i], lobjv[i+1]);
	}
	UnlockArray(arrayPtr);
	break;

    case CSizeIdx:
    case CExistsIdx:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "array");
	    return TCL_ERROR;
	}
	arrayPtr = LockArray(arg, NULL, objv[2], 0);
	if (arrayPtr == NULL) {
	    size = 0;
	} else {
	    size = (opt == CSizeIdx) ? arrayPtr->vars.numEntries : 1;
	    UnlockArray(arrayPtr);
	}
	if (opt == CExistsIdx) {
	    Tcl_SetBooleanObj(result, size);
	} else {
	    Tcl_SetIntObj(result, size);
	}
	break;

    case CGetIdx:
    case CNamesIdx:
	if (objc != 3 && objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "array ?pattern?");
	    return TCL_ERROR;
	}
	arrayPtr = LockArray(arg, NULL, objv[2], 0);
	if (arrayPtr != NULL) {
	    pattern = (objc > 3) ? Tcl_GetString(objv[3]) : NULL;
	    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
	    while (hPtr != NULL) {
	        key = Tcl_GetHashKey(&arrayPtr->vars, hPtr);
	        if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
		    Tcl_AppendElement(interp, key);
	    	    if (opt == CGetIdx) {
		        Tcl_AppendElement(interp, Tcl_GetHashValue(hPtr));
		    }
		}
	    	hPtr = Tcl_NextHashEntry(&search);
	    }
	    UnlockArray(arrayPtr);
	}
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvUnsetObjCmd --
 *
 *	Implements nsv_unset as an obj command. 
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
NsTclNsvUnsetObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Tcl_HashEntry *hPtr = NULL;
    Array *arrayPtr = NULL;

    if (objc != 2 && objc != 3) {
    	Tcl_WrongNumArgs(interp, 1, objv, "array ?key?");
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, objv[1], 0);
    if (arrayPtr == NULL) {
	return TCL_ERROR;
    }
    if (objc == 2) {
    	Tcl_DeleteHashEntry(arrayPtr->entryPtr);
    } else {
    	hPtr = Tcl_FindHashEntry(&arrayPtr->vars, Tcl_GetString(objv[2]));
	if (hPtr != NULL) {
	    ns_free(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
    }
    UnlockArray(arrayPtr);
    if (objc == 2) {
	FlushArray(arrayPtr);
	Tcl_DeleteHashTable(&arrayPtr->vars);
	ns_free(arrayPtr);
    } else if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such key: ", Tcl_GetString(objv[2]), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvNamesObjCmd --
 *
 *      Implements nsv_names as an obj command.
 *
 * Results:
 *      Tcl result.
 *
 * Side effects:
 *      See docs.
 *
 *----------------------------------------------------------------------
 */

int
NsTclNsvNamesObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_Obj *result;
    Bucket *bucketPtr;
    char *pattern, *key;
    int i;
    
    if (objc != 1 && objc !=2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?pattern?");
        return TCL_ERROR;
    }
    pattern = objc < 2 ? NULL : Tcl_GetString(objv[1]);

    /* 
     * Walk the bucket list for each array.
     */

    result = Tcl_GetObjResult(interp);
    for (i = 0; i < servPtr->nsv.nbuckets; i++) {
    	bucketPtr = &servPtr->nsv.buckets[i];
        Ns_MutexLock(&bucketPtr->lock);
        hPtr = Tcl_FirstHashEntry(&bucketPtr->arrays, &search);
        while (hPtr != NULL) {
            key = Tcl_GetHashKey(&bucketPtr->arrays, hPtr);
            if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
		Tcl_ListObjAppendElement(NULL, result,
					 Tcl_NewStringObj(key, -1));
            }
            hPtr = Tcl_NextHashEntry(&search);
        }
        Ns_MutexUnlock(&bucketPtr->lock);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------
 *
 * LockArray --
 *
 *  	Find (or create) the Array structure for an array and
 *	lock it.  Array structure must be later unlocked with
 *	UnlockArray.
 *
 * Results:
 *	TCL_OK or TCL_ERROR if no such array.
 *
 * Side effects;
 *	Sets *arrayPtrPtr with Array pointer or leave error in
 *  	given Tcl_Interp.
 *
 *----------------------------------------------------------------
 */

static Array *
LockArray(void *arg, Tcl_Interp *interp, Tcl_Obj *arrayObj, int create)
{
    NsInterp *itPtr = arg;
    Bucket *bucketPtr;
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;
    char *array;
    register char *p;
    register unsigned int result;
    register int i;
    int new;
   
    array = Tcl_GetString(arrayObj);
    p = array;
    result = 0;
    while (1) {
        i = *p;
        p++;
        if (i == 0) {
            break;
        }
        result += (result<<3) + i;
    }
    i = result % itPtr->servPtr->nsv.nbuckets;
    bucketPtr = &itPtr->servPtr->nsv.buckets[i];

    Ns_MutexLock(&bucketPtr->lock);
    if (create) {
    	hPtr = Tcl_CreateHashEntry(&bucketPtr->arrays, array, &new);
	if (!new) {
	    arrayPtr = Tcl_GetHashValue(hPtr);
	} else {
	    arrayPtr = ns_malloc(sizeof(Array));
	    arrayPtr->bucketPtr = bucketPtr;
	    arrayPtr->entryPtr = hPtr;
	    Tcl_InitHashTable(&arrayPtr->vars, TCL_STRING_KEYS);
	    Tcl_SetHashValue(hPtr, arrayPtr);
	}
    } else {
    	hPtr = Tcl_FindHashEntry(&bucketPtr->arrays, array);
	if (hPtr == NULL) {
	    Ns_MutexUnlock(&bucketPtr->lock);
	    if (interp != NULL) {
	    	Tcl_AppendResult(interp, "no such array: ", array, NULL);
	    }
	    return NULL;
	}
	arrayPtr = Tcl_GetHashValue(hPtr);
    }
    return arrayPtr;
}


/*
 *----------------------------------------------------------------
 *
 * UpdateVar --
 *
 *	Update a variable entry.
 *
 * Results:
 *  	None.
 *
 * Side effects;
 *  	New value is set.
 *
 *----------------------------------------------------------------
 */

static void
UpdateVar(Tcl_HashEntry *hPtr, Tcl_Obj *obj)
{
    char *str, *old, *new;
    int len;

    str = Tcl_GetStringFromObj(obj, &len);
    old = Tcl_GetHashValue(hPtr);
    new = ns_realloc(old, (size_t)(len+1));
    memcpy(new, str, (size_t)(len+1));
    Tcl_SetHashValue(hPtr, new);
}


/*
 *----------------------------------------------------------------
 *
 * SetVar --
 *
 *	Set (or reset) an array entry.
 *
 * Results:
 *  	None.
 *
 * Side effects;
 *	New entry is created and updated.
 *
 *----------------------------------------------------------------
 */

static void
SetVar(Array *arrayPtr, Tcl_Obj *key, Tcl_Obj *value)
{
    Tcl_HashEntry *hPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, Tcl_GetString(key), &new);
    UpdateVar(hPtr, value);
}


/*
 *----------------------------------------------------------------
 *
 * FlushArray --
 *
 *	Unset all keys in an array.
 *
 * Results:
 *  	None.
 *
 * Side effects;
 *	New entry is created and updated.
 *
 *----------------------------------------------------------------
 */

static void
FlushArray(Array *arrayPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
    while (hPtr != NULL) {
	ns_free(Tcl_GetHashValue(hPtr));
	Tcl_DeleteHashEntry(hPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclVarObjCmd --
 *
 *	Implements ns_var (deprecated)
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclVarObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
	       Tcl_Obj **objv)
{
    NsInterp		 *itPtr = arg;
    NsServer		 *servPtr;
    Tcl_HashTable	 *tablePtr;
    Tcl_HashEntry        *hPtr;
    Tcl_HashSearch        search;
    int                   new, code;
    char *var = NULL, *val = NULL;
    static CONST char *opts[] = {
	"exists", "get", "list", "set", "unset", NULL
    };
    enum {
	VExistsIdx, VGetIdx, VListIdx, VSetIdx, VUnsetIdx
    } _nsmayalias opt; 

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    servPtr = itPtr->servPtr;
    tablePtr = &servPtr->var.table;
    code = TCL_OK;
    if (objc > 2) {
	var = Tcl_GetString(objv[2]);
    }
    Ns_MutexLock(&servPtr->var.lock);
    switch (opt) {
    case VExistsIdx:
    case VGetIdx:
    case VUnsetIdx:
        if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "var");
            code = TCL_ERROR;
	} else {
	    hPtr = Tcl_FindHashEntry(tablePtr, var);
	    if (opt == VExistsIdx) {
		Tcl_SetBooleanObj(Tcl_GetObjResult(interp), hPtr ? 1 : 0);
	    } else if (hPtr == NULL) {
	    	Tcl_AppendResult(interp, "no such variable \"", var, 
				     "\"", NULL);
            	code = TCL_ERROR;
	    } else if (opt == VGetIdx) {
	        Tcl_SetResult(interp, Tcl_GetHashValue(hPtr), TCL_VOLATILE);
	    } else {
            	ns_free(Tcl_GetHashValue(hPtr));
            	Tcl_DeleteHashEntry(hPtr);
	    }
	}
	break;

    case VSetIdx:
        if (objc != 4) {
	    Tcl_WrongNumArgs(interp, 2, objv, "var value");
            code = TCL_ERROR;
        } else {
            hPtr = Tcl_CreateHashEntry(tablePtr, var, &new);
            if (!new) {
                ns_free(Tcl_GetHashValue(hPtr));
            }
	    val = Tcl_GetString(objv[3]);
            Tcl_SetHashValue(hPtr, ns_strdup(val));
            Tcl_SetResult(interp, val, TCL_VOLATILE);
        }
	break;

    case VListIdx:
        hPtr = Tcl_FirstHashEntry(tablePtr, &search);
        while (hPtr != NULL) {
            Tcl_AppendElement(interp, Tcl_GetHashKey(tablePtr, hPtr));
            hPtr = Tcl_NextHashEntry(&search);
        }
	break;
    }
    Ns_MutexUnlock(&servPtr->var.lock);
    return code;
}

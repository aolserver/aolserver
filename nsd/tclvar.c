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

static char rcsid[] = "$Id: tclvar.c,v 1.6 2001/03/12 22:06:14 jgdavidson Exp $";

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

#define FLAGS_CREATE 1
#define FLAGS_NOERRMSG 2

static int InitShare(void *arg, Tcl_Interp *interp, char *varName, char *script);
static void SetVar(Array *, char *key, char *value);
static void UpdateVar(Tcl_HashEntry *hPtr, char *value);
static void FlushArray(Array *arrayPtr);
static Array *LockArray(void *arg, Tcl_Interp *, char *array, int flags);
#define UnlockArray(arrayPtr) \
	Ns_MutexUnlock(&((arrayPtr)->bucketPtr->lock));


/*
 *----------------------------------------------------------------------
 *
 * Get2Cmd --
 *
 *	Implelments nsv_get, nsv_exists.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
Get2Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int get)
{
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;

    if (argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " array key\"", NULL);
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, argv[1], 0);
    if (arrayPtr == NULL) {
    	if (get) {
	    return TCL_ERROR;
	}
	Tcl_SetResult(interp, "0", TCL_STATIC);
	return TCL_OK;
    }
    hPtr = Tcl_FindHashEntry(&arrayPtr->vars, argv[2]);
    if (hPtr != NULL && get) {
    	Tcl_SetResult(interp, Tcl_GetHashValue(hPtr), TCL_VOLATILE);
    }
    UnlockArray(arrayPtr);
    if (!get) {
	Tcl_SetResult(interp, hPtr ? "1" : "0", TCL_STATIC);
    } else if (hPtr == NULL) {
    	Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
NsTclNsvGetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return Get2Cmd(arg, interp, argc, argv, 1);
}

int
NsTclNsvExistsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return Get2Cmd(arg, interp, argc, argv, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvSetCmd --
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
NsTclNsvSetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;

    if (argc != 3 && argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " array key ?value?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3) {
    	return NsTclNsvGetCmd(arg, interp, argc, argv);
    }
    arrayPtr = LockArray(arg, interp, argv[1], FLAGS_CREATE);
    SetVar(arrayPtr, argv[2], argv[3]);
    UnlockArray(arrayPtr);
    Tcl_SetResult(interp, argv[3], TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvIncrCmd --
 *
 *	Implelments nsv_incr. 
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
NsTclNsvIncrCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    int count, current, result, new;
    char buf[20], *value;
    Tcl_HashEntry *hPtr;

    if (argc != 3 && argc != 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " array key ?count?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3)  {
	count = 1;
    } else if (Tcl_GetInt(interp, argv[3], &count) != TCL_OK) {
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, argv[1], FLAGS_CREATE);
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, argv[2], &new);
    if (new) {
	current = 0;
	result = TCL_OK;
    } else {
    	value = Tcl_GetHashValue(hPtr);
	result = Tcl_GetInt(interp, value, &current);
    }
    if (result == TCL_OK) {
    	current += count;
    	sprintf(buf, "%d", current);
    	UpdateVar(hPtr, buf);
    }
    UnlockArray(arrayPtr);
    if (result == TCL_OK) {
    	Tcl_SetResult(interp, buf, TCL_VOLATILE);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Append2Cmd --
 *
 *	Implelments nsv_append, nsv_lappend.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
Append2Cmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int lappend)
{
    Array *arrayPtr;
    int cmd = (int) arg;
    int i;
    Tcl_HashEntry *hPtr;

    if (argc < 4) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " array key string ?string ...?\"", NULL);
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, argv[1], 0);
    if (arrayPtr == NULL) {
	return TCL_ERROR;
    }
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, argv[2], &i);
    if (!i) {
    	Tcl_SetResult(interp, Tcl_GetHashValue(hPtr), TCL_VOLATILE);
    }
    for (i = 3; i < argc; ++i) {
   	if (lappend) {
    	    Tcl_AppendElement(interp, argv[i]);
	} else {
    	    Tcl_AppendResult(interp, argv[i], NULL);
	}
    }
    UpdateVar(hPtr, interp->result);
    UnlockArray(arrayPtr);
    return TCL_OK;
}

int
NsTclNsvAppendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return Append2Cmd(arg, interp, argc, argv, 0);
}

int
NsTclNsvLappendCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return Append2Cmd(arg, interp, argc, argv, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvArrayCmd --
 *
 *	Implelments nsv_array command.
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
NsTclNsvArrayCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Array *arrayPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char **largv, *pattern, *key;
    int i, cmd, largc;

    if (argc < 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " option array\"", NULL);
	return TCL_ERROR;
    }

    cmd = argv[1][0];
    if (STREQ(argv[1], "set") || STREQ(argv[1], "reset")) {
	if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be: \"",
		argv[0], " ", argv[1], " array valueList\"", NULL);
	    return TCL_ERROR;
	}
    	if (Tcl_SplitList(NULL, argv[3], &largc, &largv) != TCL_OK) {
	    return TCL_ERROR;
    	}
    	if (largc & 1) {
	    Tcl_AppendResult(interp, "invalid list: ", argv[3], NULL);
	    ckfree((char *) largv);
	    return TCL_ERROR;
	}
    	arrayPtr = LockArray(arg, interp, argv[2], FLAGS_CREATE);
    } else {
    	if (STREQ(argv[1], "get") || STREQ(argv[1], "names")) {
	    if (argc != 3 && argc != 4) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
		    argv[0], " ", argv[1], " array ?pattern?\"", NULL);
		return TCL_ERROR;
	    }
	    pattern = argv[3];
	} else if (STREQ(argv[1], "size") || STREQ(argv[1], "exists")) {
	    if (argc != 3) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
		    argv[0], " ", argv[1], " array\"", NULL);
		return TCL_ERROR;
	    }
	    if (cmd == 's') {
		cmd = 'z';
	    }
	} else {
	    Tcl_AppendResult(interp, "unkown command \"", argv[1],
		"\": should be exists, get, names, set, or size", NULL);
	    return TCL_ERROR;
	}
	arrayPtr = LockArray(arg, interp, argv[2], FLAGS_NOERRMSG);
	if (arrayPtr == NULL) {
	    if (cmd == 'z' || cmd == 'e') {
	    	Tcl_SetResult(interp, "0", TCL_STATIC);
	    }
	    return TCL_OK;
	}
    }

    switch (cmd) {
    case 'e':
	Tcl_SetResult(interp, "1", TCL_STATIC);
	break;
    case 'z':
	sprintf(interp->result, "%d", arrayPtr->vars.numEntries);
	break;
    case 'r':
	FlushArray(arrayPtr);
	/* FALLTHROUGH */
    case 's':
    	for (i = 0; i < largc; i += 2) {
	    SetVar(arrayPtr, largv[i], largv[i+1]);
	}
    	ckfree((char *) largv);
	break;
    case 'g':
    case 'n':
	hPtr = Tcl_FirstHashEntry(&arrayPtr->vars, &search);
	while (hPtr != NULL) {
	    key = Tcl_GetHashKey(&arrayPtr->vars, hPtr);
	    if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
		Tcl_AppendElement(interp, key);
	    	if (cmd == 'g') {
	    	    Tcl_AppendElement(interp, Tcl_GetHashValue(hPtr));
		}
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
	break;
    }
    UnlockArray(arrayPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvUnsetCmd --
 *
 *	Implelments nsv_unset. 
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
NsTclNsvUnsetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;

    if (argc != 2 && argc != 3) {
    	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " array ?key?\"", NULL);
	return TCL_ERROR;
    }
    arrayPtr = LockArray(arg, interp, argv[1], 0);
    if (arrayPtr == NULL) {
	return TCL_ERROR;
    }
    if (argc == 2) {
    	Tcl_DeleteHashEntry(arrayPtr->entryPtr);
    } else {
    	hPtr = Tcl_FindHashEntry(&arrayPtr->vars, argv[2]);
	if (hPtr != NULL) {
	    ns_free(Tcl_GetHashValue(hPtr));
	    Tcl_DeleteHashEntry(hPtr);
	}
    }
    UnlockArray(arrayPtr);
    if (argc == 2) {
	FlushArray(arrayPtr);
	Tcl_DeleteHashTable(&arrayPtr->vars);
	ns_free(arrayPtr);
    } else if (hPtr == NULL) {
    	Tcl_AppendResult(interp, "no such key: ", argv[2], NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNsvNamesCmd --
 *
 *      Implements nsv_names.
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
NsTclNsvNamesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Bucket *bucketPtr;
    char *pattern, *key;
    int i;
    
    if (argc != 1 && argc !=2) {
        Tcl_AppendResult(interp, "wrong # args: should be: \"",
			 argv[0], "?pattern?\"", NULL);
        return TCL_ERROR;
    }
    pattern = argv[1];

    /* 
     * Walk the bucket list for each array.
     */

    for (i = 0; i < servPtr->nsv.nbuckets; i++) {
    	bucketPtr = &servPtr->nsv.buckets[i];
        Ns_MutexLock(&bucketPtr->lock);
        hPtr = Tcl_FirstHashEntry(&bucketPtr->arrays, &search);
        while (hPtr != NULL) {
            key = Tcl_GetHashKey(&bucketPtr->arrays, hPtr);
            if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
                Tcl_AppendElement(interp, key);
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
LockArray(void *arg, Tcl_Interp *interp, char *array, int flags)
{
    NsInterp *itPtr = arg;
    Bucket *bucketPtr;
    Tcl_HashEntry *hPtr;
    Array *arrayPtr;
    register char *p;
    register unsigned int result;
    register int i;
    int new;
   
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
    if (flags & FLAGS_CREATE) {
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
	    if (!(flags & FLAGS_NOERRMSG)) {
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
UpdateVar(Tcl_HashEntry *hPtr, char *value)
{
    char *old, *new;
    size_t size;
    
    size = strlen(value) + 1;
    old = Tcl_GetHashValue(hPtr);
    if (old == NULL) {
    	new = ns_malloc(size);
    } else {
    	new = ns_realloc(old, size);
    }
    memcpy(new, value, size);
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
SetVar(Array *arrayPtr, char *key, char *value)
{
    Tcl_HashEntry *hPtr;
    int new;
    
    hPtr = Tcl_CreateHashEntry(&arrayPtr->vars, key, &new);
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
 * NsTclVarCmd --
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
NsTclVarCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp		 *itPtr = arg;
    NsServer		 *servPtr;
    Tcl_HashTable	 *tablePtr;
    Tcl_HashEntry        *hPtr;
    Tcl_HashSearch        search;
    int                   new, code;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
                         argv[0], " command ?args?", NULL);
        return TCL_ERROR;
    }
    servPtr = itPtr->servPtr;
    tablePtr = &servPtr->var.table;
    code = TCL_OK;

    Ns_MutexLock(&servPtr->var.lock);
    if (STREQ(argv[1], "get") ||
	STREQ(argv[1], "exists") ||
	STREQ(argv[1], "unset")) {
	
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args:  should be \"",
                             argv[0], " ", argv[1], " var\"", NULL);
            code = TCL_ERROR;
        } else {
            hPtr = Tcl_FindHashEntry(tablePtr, argv[2]);
            if (hPtr == NULL) {
                if (argv[1][0] == 'e') {
                    Tcl_SetResult(interp, "0", TCL_STATIC);
                } else {
                    Tcl_AppendResult(interp, "no such variable \"", argv[2],
				     "\"", NULL);
                    code = TCL_ERROR;
                }
            } else {
                if (argv[1][0] == 'e') {
                    Tcl_SetResult(interp, "1", TCL_STATIC);
                } else if (argv[1][0] == 'g') {
                    Tcl_SetResult(interp, (char *) Tcl_GetHashValue(hPtr),
				  TCL_VOLATILE);
                } else {
                    ns_free(Tcl_GetHashValue(hPtr));
                    Tcl_DeleteHashEntry(hPtr);
                }
            }
        }
    } else if (STREQ(argv[1], "set")) {
        if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # args:  should be \"",
                             argv[0], " ", argv[1], " var value\"", NULL);
            code = TCL_ERROR;
        } else {
            hPtr = Tcl_CreateHashEntry(tablePtr, argv[2], &new);
            if (!new) {
                ns_free(Tcl_GetHashValue(hPtr));
            }
            Tcl_SetHashValue(hPtr, ns_strdup(argv[3]));
            Tcl_SetResult(interp, argv[3], TCL_VOLATILE);
        }
    } else if (STREQ(argv[1], "list")) {
        hPtr = Tcl_FirstHashEntry(tablePtr, &search);
        while (hPtr != NULL) {
            Tcl_AppendElement(interp, Tcl_GetHashKey(tablePtr, hPtr));
            hPtr = Tcl_NextHashEntry(&search);
        }
    } else {
        Tcl_AppendResult(interp, "unknown command \"",
                         argv[1],
			 "\": should be get, set, unset, exists, or list",
			 NULL);
        code = TCL_ERROR;
    }
    Ns_MutexUnlock(&servPtr->var.lock);
    return code;
}

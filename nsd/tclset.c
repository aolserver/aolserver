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
 * tclset.c --
 *
 *	Implements the tcl ns_set commands 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclset.c,v 1.12 2002/07/08 02:51:34 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following represent the valid combinations of
 * NS_TCL_SET flags
 */
 
#define SET_DYNAMIC 		'd'
#define SET_STATIC    		't'
#define SET_SHARED_DYNAMIC	's'
#define SET_SHARED_STATIC  	'p'

#define IS_DYNAMIC(id)    \
	(*(id) == SET_DYNAMIC || *(id) == SET_SHARED_DYNAMIC)
#define IS_SHARED(id)     \
	(*(id) == SET_SHARED_DYNAMIC || *(id) == SET_SHARED_STATIC)

/*
 * Local functions defined in this file
 */

static int BadArgs(Tcl_Interp *interp, char **argv, char *args);
static int LookupSet(NsInterp *itPtr, char *id, int delete, Ns_Set **setPtr);
static int LookupObjSet(NsInterp *itPtr, Tcl_Obj *idPtr, int delete,
			Ns_Set **setPtr);
static int LookupInterpSet(Tcl_Interp *interp, char *id, int delete,
			   Ns_Set **setPtr);
static int EnterSet(NsInterp *itPtr, Ns_Set *set, int flags);


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclEnterSet --
 *
 *	Give this Tcl interpreter access to an existing Ns_Set.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	A pointer to the Ns_Set is added to the interpreter's list of 
 *	sets; a new handle is generated and appended to interp result.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclEnterSet(Tcl_Interp *interp, Ns_Set *set, int flags)
{
    NsInterp *itPtr = NsGetInterp(interp);

    if (itPtr == NULL) {
	Tcl_SetResult(interp, "ns_set not supported", TCL_STATIC);
	return TCL_ERROR;
    }
    return EnterSet(itPtr, set, flags);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetSet --
 *
 *	Given a Tcl ns_set handle, return a pointer to the Ns_Set. 
 *
 * Results:
 *	An Ns_Set pointer, or NULL if error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_TclGetSet(Tcl_Interp *interp, char *id)
{
    Ns_Set *set;

    if (LookupInterpSet(interp, id, 0, &set) != TCL_OK) {
	return NULL;
    }
    return set;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetSet2 --
 *
 *	Like Ns_TclGetSet, but sends errors to the tcl interp. 
 *
 * Results:
 *	TCL result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetSet2(Tcl_Interp *interp, char *id, Ns_Set **setPtr)
{
    return LookupInterpSet(interp, id, 0, setPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclFreeSet --
 *
 *	Free a set id, and if own by Tcl, the underlying Ns_Set.
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Will free the set matching the passed-in set id, and all of 
 *	its associated data. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclFreeSet(Tcl_Interp *interp, char *id)
{
    Ns_Set  *set;

    if (LookupInterpSet(interp, id, 1, &set) != TCL_OK) {
	return TCL_ERROR;
    }
    if (IS_DYNAMIC(id)) {
    	Ns_SetFree(set);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSetCmd --
 *
 *	Implelments ns_set. 
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
NsTclSetCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Set       *set, *set2Ptr;
    int           locked, i;
    char         *cmd, *key, *val;
    int           flags;
    Ns_Set      **sets;
    char         *split;
    Tcl_DString	  ds;
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    NsInterp	  *itPtr = arg;
    Tcl_Obj	  *objPtr;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " command ?args?\"", NULL);
        return TCL_ERROR;
    }
    cmd = argv[1];
    if (STREQ(cmd, "create")) {
	cmd = "new";
    }

    if (STREQ(cmd, "cleanup")) {
    	tablePtr = &itPtr->sets;
    	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    	while (hPtr != NULL) {
    	    key = Tcl_GetHashKey(tablePtr, hPtr);
	    if (IS_DYNAMIC(key)) {
    	       	set = Tcl_GetHashValue(hPtr);
	       	Ns_SetFree(set);
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
    	Tcl_DeleteHashTable(tablePtr);
    	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);

    } else if (STREQ(cmd, "list")) {
	if (argc == 2) {
	    tablePtr = &itPtr->sets;
    	    locked = 0;
	} else if (STREQ(argv[2], "-shared")) {
	    tablePtr = &itPtr->servPtr->sets.table;
	    locked = 1;
	    Ns_MutexLock(&itPtr->servPtr->sets.lock);
	} else {
    	    return BadArgs(interp, argv, "?-shared?");
	}
	if (tablePtr != NULL) {
	    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	    while (hPtr != NULL) {
		Tcl_AppendElement(interp, Tcl_GetHashKey(tablePtr, hPtr));
		hPtr = Tcl_NextHashEntry(&search);
	    }
	}
	if (locked) {
	    Ns_MutexUnlock(&itPtr->servPtr->sets.lock);
	}
    } else if (STREQ(cmd, "new")  ||
	STREQ(cmd, "copy") ||
	STREQ(cmd, "split")) {
	/*
	 * The set will be dynamic and possibly shared.
	 */
	
        flags = NS_TCL_SET_DYNAMIC;
        i = 2;
        if (argc > 2 &&
	    (STREQ(argv[2], "-shared") || STREQ(argv[2], "-persist"))) {
            flags |= NS_TCL_SET_SHARED;
            ++i;
        }
	
        switch (*cmd) {
        case 'n':
	    /*
	     * ns_set new
	     */
	    
	    set = Ns_SetCreate(argv[i++]);
	    while (i < argc) {
		key = argv[i++];
		val = argv[i++];
		Ns_SetPut(set, key, val);
	    }
            EnterSet(itPtr, set, flags);
            break;
        case 'c':
	    /*
	     * ns_set copy
	     */
	    
            if (argv[i] == NULL || argv[i + 1] != NULL) {
                return BadArgs(interp, argv, "?-shared? setId");
            }
	    if (LookupSet(itPtr, argv[i], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
            EnterSet(itPtr, Ns_SetCopy(set), flags);
            break;
        case 's':
	    /*
	     * ns_set split
	     */
	    
            if (argv[i] == NULL ||
		(argv[i + 1] != NULL && argv[i + 2] != NULL)) {
                return BadArgs(interp, argv, "?-shared? setId ?splitChar?");
            }
	    if (LookupSet(itPtr, argv[i++], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
            split = argv[i];
            if (split == NULL) {
                split = ".";
            }
            sets = Ns_SetSplit(set, *split);
            for (i = 0; sets[i] != NULL; i++) {
                EnterSet(itPtr, sets[i], flags);
            }
            ns_free(sets);
            break;
        }
    } else {
        /*
         * All futher commands require a valid set.
         */
	
        if (argc < 3) {
            return BadArgs(interp, argv, "setId ?args?");
        }
	if (LookupSet(itPtr, argv[2], 0, &set) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(cmd, "size")  ||
	    STREQ(cmd, "name")  ||
	    STREQ(cmd, "array") ||
	    STREQ(cmd, "print") ||
	    STREQ(cmd, "free")) {
	    
            if (argc != 3) {
                return BadArgs(interp, argv, "setId");
            }
            switch (*cmd) {
	    case 'a':
		Tcl_DStringInit(&ds);
		for (i = 0; i < Ns_SetSize(set); ++i) {
		    Tcl_DStringAppendElement(&ds, Ns_SetKey(set, i));
		    Tcl_DStringAppendElement(&ds, Ns_SetValue(set, i));
		}
		Tcl_DStringResult(interp, &ds);
		break;

            case 's':
		/*
		 * ns_set size
		 */
		
		objPtr = Tcl_NewIntObj(Ns_SetSize(set));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
            case 'n':
		/*
		 * ns_set name
		 */
		
                Tcl_SetResult(interp, set->name, TCL_STATIC);
                break;
		
            case 'p':
		/*
		 * ns_set print
		 */
		
                Ns_SetPrint(set);
                break;
		
            case 'f':
		/*
		 * ns_set free
		 */
		
                Ns_TclFreeSet(interp, argv[2]);
                break;
            }
        } else if (STREQ(cmd, "find")    ||
		   STREQ(cmd, "ifind")   ||
		   STREQ(cmd, "get")     ||
		   STREQ(cmd, "iget")    ||
		   STREQ(cmd, "delkey")  ||
		   STREQ(cmd, "idelkey") ||
		   STREQ(cmd, "unique")  ||
		   STREQ(cmd, "iunique")) {
	    
            if (argc != 4) {
                return BadArgs(interp, argv, "setId key");
            }
            switch (*cmd) {
            case 'f':
		/*
		 * ns_set find
		 */
		
		objPtr = Tcl_NewIntObj(Ns_SetFind(set, argv[3]));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
            case 'g':
		/*
		 * ns_set get
		 */
		
                Tcl_SetResult(interp, Ns_SetGet(set, argv[3]), TCL_STATIC);
                break;
		
            case 'd':
		/*
		 * ns_set delete
		 */
		
                Ns_SetDeleteKey(set, argv[3]);
                break;
		
            case 'u':
		/*
		 * ns_set unique
		 */
		
		objPtr = Tcl_NewIntObj(Ns_SetUnique(set, argv[3]));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
            case 'i':
                switch (cmd[1]) {
                case 'f':
		    /*
		     * ns_set ifind
		     */
		    
		    objPtr = Tcl_NewIntObj(Ns_SetIFind(set, argv[3]));
		    Tcl_SetObjResult(interp, objPtr);
                    break;
		    
                case 'g':
		    /*
		     * ns_set iget
		     */
		    
                    Tcl_SetResult(interp, Ns_SetIGet(set, argv[3]),
				  TCL_STATIC);
                    break;
		    
                case 'd':
		    /*
		     * ns_set idelete
		     */
		    
                    Ns_SetIDeleteKey(set, argv[3]);
                    break;
		    
                case 'u':
		    /*
		     * ns_set iunique
		     */
		    
		    objPtr = Tcl_NewIntObj(Ns_SetIUnique(set, argv[3]));
		    Tcl_SetObjResult(interp, objPtr);
                    break;
                }
            }
        } else if (STREQ(cmd, "value")  ||
		   STREQ(cmd, "isnull") ||
		   STREQ(cmd, "key")    ||
		   STREQ(cmd, "delete") ||
		   STREQ(cmd, "truncate")) {

	    /*
	     * These are all commands that work on an index; that is
	     * the do something to the Nth item of a set.
	     */
	    
            if (argc != 4) {
                return BadArgs(interp, argv, "setId index");
            }
            if (Tcl_GetInt(interp, argv[3], &i) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (i < 0) {
		Tcl_AppendResult(interp, "invalid index \"", argv[3],
		    "\": must be >= 0", NULL);
                return TCL_ERROR;
            }
            if (i >= Ns_SetSize(set)) {
		Tcl_AppendResult(interp, "invalid index \"", argv[3],
		    "\": beyond range of set fields", NULL);
                return TCL_ERROR;
            }
            switch (*cmd) {
            case 'v':
		/*
		 * ns_set value
		 */
		
                Tcl_SetResult(interp, Ns_SetValue(set, i), TCL_STATIC);
                break;
		
            case 'i':
		/*
		 * ns_set isnull
		 */
		
                Tcl_SetResult(interp, Ns_SetValue(set, i) ? "0" : "1",
			      TCL_STATIC);
                break;
		
            case 'k':
		/*
		 * ns_set key
		 */
		
                Tcl_SetResult(interp, Ns_SetKey(set, i), TCL_STATIC);
                break;
		
            case 'd':
		/*
		 * ns_set delete
		 */
		
                Ns_SetDelete(set, i);
                break;
		
            case 't':
		/*
		 * ns_set truncate
		 */
		
                Ns_SetTrunc(set, i);
                break;
            }
        } else if (STREQ(cmd, "put")    ||
		   STREQ(cmd, "update") ||
		   STREQ(cmd, "cput")   ||
		   STREQ(cmd, "icput")) {
	    
            if (argc != 5) {
                return BadArgs(interp, argv, "setId key value");
            }
	    switch (*cmd) {
	    case 'u':
		/*
		 * ns_set update
		 */
		
                Ns_SetDeleteKey(set, argv[3]);
                i = Ns_SetPut(set, argv[3], argv[4]);
		break;
		
	    case 'i':
		/*
		 * ns_set icput
		 */
		
                i = Ns_SetIFind(set, argv[3]);
		if (i < 0) {
		    i = Ns_SetPut(set, argv[3], argv[4]);
		}
		break;
		
	    case 'c':
		/*
		 * ns_set cput
		 */
		
                i = Ns_SetFind(set, argv[3]);
		if (i < 0) {
		    i = Ns_SetPut(set, argv[3], argv[4]);
		}
		break;
		
	    case 'p':
		/*
		 * ns_set put
		 */

		i = Ns_SetPut(set, argv[3], argv[4]);
		break;
            }
	    objPtr = Tcl_NewIntObj(i);
	    Tcl_SetObjResult(interp, objPtr);
        } else if (STREQ(cmd, "merge") ||
		   STREQ(cmd, "move")) {
	    
            if (argc != 4) {
                return BadArgs(interp, argv, "setTo, setFrom");
            }
            if (LookupSet(itPtr, argv[3], 0, &set2Ptr) != TCL_OK) {
                return TCL_ERROR;
            }
	    switch (cmd[1]) {
	    case 'e':
		/*
		 * ns_set merge
		 */
		
                Ns_SetMerge(set, set2Ptr);
		break;
		
	    case 'o':
		/*
		 * ns_set move
		 */
		
                Ns_SetMove(set, set2Ptr);
		break;
            }
            Tcl_SetResult(interp, argv[2], TCL_VOLATILE);
        } else {
            Tcl_AppendResult(interp, "unknown command \"", argv[1],
                "\":  should be one of "
		"array, "
                "copy, "
                "create, "
                "delete, "
                "delkey, "
                "free, "
                "get, "
                "iget, "
                "idelkey, "
		"isnull, "
                "iunique, "
                "key, "
                "list, "
                "merge, "
                "move, "
                "name, "
                "new, "
                "print, "
                "purge, "
                "put, "
                "size, "
                "split, "
                "truncate, "
                "unique or update", NULL);
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSetObjCmd --
 *
 *	Implelments ns_set. 
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
NsTclSetObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Set       *set, *set2Ptr, **sets;
    int           locked, i, flags;
    char         *key, *val, *name, *split;
    Tcl_DString	  ds;
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    NsInterp	  *itPtr = arg;
    Tcl_Obj	  *objPtr;
    static CONST char *opts[] = {
	 "array", "cleanup", "copy", "cput", "create", "delete",
	 "delkey", "find", "free", "get", "icput", "idelete",
	 "idelkey", "ifind", "iget", "isnull", "iunique", "key",
	 "list", "merge", "move", "name", "new", "print", "purge",
	 "put", "size", "split", "truncate", "unique", "update",
	 "value", NULL,
    };
    enum {
	 SArrayIdx, SCleanupIdx, SCopyIdx, SCPutIdx, SCreateidx,
	 SDeleteIdx, SDelkeyIdx, SFindIdx, SFreeIdx, SGetIdx,
	 SICPutIdx, SIDeleteIdx, SIDelkeyIdx, SIFindIdx, SIGetIdx,
	 SIsNullIdx, SIUniqueIdx, SKeyIdx, SListIdx, SMergeIdx,
	 SMoveIdx, sINameIdx, SNewIdx, SPrintIdx, spurgeidx, SPutIdx,
	 SSizeIdx, SSplitIdx, STruncateIdx, SUniqueIdx, SUpdateIdx,
	 SValueIdx
    };
    int opt;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    if (opt == SCreateidx) {
	opt = SNewIdx;
    }

    switch (opt) {
    case SCleanupIdx:
    	tablePtr = &itPtr->sets;
    	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    	while (hPtr != NULL) {
    	    key = Tcl_GetHashKey(tablePtr, hPtr);
	    if (IS_DYNAMIC(key)) {
    	       	set = Tcl_GetHashValue(hPtr);
	       	Ns_SetFree(set);
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
    	Tcl_DeleteHashTable(tablePtr);
    	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
	break;

    case SListIdx:
	if (objc == 2) {
	    tablePtr = &itPtr->sets;
    	    locked = 0;
	} else if (STREQ(Tcl_GetString(objv[2]), "-shared")) {
	    tablePtr = &itPtr->servPtr->sets.table;
	    locked = 1;
	    Ns_MutexLock(&itPtr->servPtr->sets.lock);
	} else {
	    Tcl_WrongNumArgs(interp, 2, objv, "?-shared?");
	    return TCL_ERROR;
	}
	if (tablePtr != NULL) {
	    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	    while (hPtr != NULL) {
		Tcl_AppendElement(interp, Tcl_GetHashKey(tablePtr, hPtr));
		hPtr = Tcl_NextHashEntry(&search);
	    }
	}
	if (locked) {
	    Ns_MutexUnlock(&itPtr->servPtr->sets.lock);
	}
	break;

    case SNewIdx:
    case SCopyIdx:
    case SSplitIdx:
	/*
	 * The following commands create new sets.
	 */
	
        flags = NS_TCL_SET_DYNAMIC;
        i = 2;
        if (objc > 2) {
	    char *oflag = Tcl_GetString(objv[2]);
	    if (STREQ(oflag, "-shared") || STREQ(oflag, "-persist")) {
            	flags |= NS_TCL_SET_SHARED;
            	++i;
	    }
        }
	
        switch (opt) {
	case SNewIdx:
	    name = (i < objc) ? Tcl_GetString(objv[i++]) : NULL;
	    set = Ns_SetCreate(name);
	    while (i < objc) {
		key = Tcl_GetString(objv[i++]);
		val = (i < objc) ? Tcl_GetString(objv[i++]) : NULL;
		Ns_SetPut(set, key, val);
	    }
            EnterSet(itPtr, set, flags);
            break;

        case SCopyIdx:
            if (i >= objc) {
		Tcl_WrongNumArgs(interp, 2, objv, "?-shared? setId");
		return TCL_ERROR;
            }
	    if (LookupObjSet(itPtr, objv[i], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
            EnterSet(itPtr, Ns_SetCopy(set), flags);
            break;
	
        case SSplitIdx:
	    if ((i - objc) < 1) {
		Tcl_WrongNumArgs(interp, 2, objv, "?-shared? setId ?splitChar");
		return TCL_ERROR;
            }
	    if (LookupObjSet(itPtr, objv[i++], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
	    split = (i < objc) ? Tcl_GetString(objv[i]) : ".";
            sets = Ns_SetSplit(set, *split);
            for (i = 0; sets[i] != NULL; i++) {
                EnterSet(itPtr, sets[i], flags);
            }
            ns_free(sets);
            break;
        }
	break;

    default:
        /*
         * All futher commands require a valid set.
         */
	
        if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "setId ?args?");
	    return TCL_ERROR;
        }
	if (LookupObjSet(itPtr, objv[2], 0, &set) != TCL_OK) {
            return TCL_ERROR;
        }

	switch (opt) {
	case SArrayIdx:
	case SSizeIdx:
	case sINameIdx:
	case SPrintIdx:
	case SFreeIdx:
	    /*
	     * These commands require only the set.
	     */

	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "setId");
		return TCL_ERROR;
            }
	    switch (opt) {
	    case SArrayIdx:
		Tcl_DStringInit(&ds);
		for (i = 0; i < Ns_SetSize(set); ++i) {
		    Tcl_DStringAppendElement(&ds, Ns_SetKey(set, i));
		    Tcl_DStringAppendElement(&ds, Ns_SetValue(set, i));
		}
		Tcl_DStringResult(interp, &ds);
		break;

            case SSizeIdx:
		objPtr = Tcl_NewIntObj(Ns_SetSize(set));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
            case sINameIdx:
                Tcl_SetResult(interp, set->name, TCL_VOLATILE);
                break;
		
	    case SPrintIdx:
                Ns_SetPrint(set);
                break;
		
	    case SFreeIdx:
                Ns_TclFreeSet(interp, Tcl_GetString(objv[2]));
                break;
            }
	    break;

	case SFindIdx:
	case SIFindIdx:
	case SGetIdx:
	case SIGetIdx:
	case SDelkeyIdx:
	case SIDelkeyIdx:
	case SUniqueIdx:
	case SIUniqueIdx:
	    /*
	     * These commands require a set and string key.
	     */

            if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "setId key");
		return TCL_ERROR;
            }
	    key = Tcl_GetString(objv[3]);
	    switch (opt) {
	    case SFindIdx:
		objPtr = Tcl_NewIntObj(Ns_SetFind(set, key));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
            case SGetIdx:
                Tcl_SetResult(interp, Ns_SetGet(set, key), TCL_VOLATILE);
                break;
		
            case SDeleteIdx:
                Ns_SetDeleteKey(set, key);
                break;
		
            case SUniqueIdx:
		objPtr = Tcl_NewIntObj(Ns_SetUnique(set, key));
		Tcl_SetObjResult(interp, objPtr);
                break;
		
	    case SIFindIdx:
		objPtr = Tcl_NewIntObj(Ns_SetIFind(set, key));
		Tcl_SetObjResult(interp, objPtr);
                break;

	    case SIGetIdx:
                Tcl_SetResult(interp, Ns_SetIGet(set, key), TCL_VOLATILE);
                break;
		    
	    case SIDeleteIdx:
                Ns_SetIDeleteKey(set, key);
                break;
		    
            case SIUniqueIdx:
		objPtr = Tcl_NewIntObj(Ns_SetIUnique(set, key));
		Tcl_SetObjResult(interp, objPtr);
                break;
            }
	    break;

	case SValueIdx:
	case SIsNullIdx:
	case SKeyIdx:
	case SDeleteIdx:
	case STruncateIdx:
	    /*
	     * These commands require a set and key/value index.
	     */

            if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "setId index");
		return TCL_ERROR;
            }
            if (Tcl_GetIntFromObj(interp, objv[3], &i) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (i < 0) {
		Tcl_AppendResult(interp, "invalid index \"",
		    Tcl_GetString(objv[3]), "\": must be >= 0", NULL);
                return TCL_ERROR;
            }
            if (i >= Ns_SetSize(set)) {
		Tcl_AppendResult(interp, "invalid index \"",
		    Tcl_GetString(objv[3]),
		    "\": beyond range of set fields", NULL);
                return TCL_ERROR;
            }
            switch (opt) {
	    case SValueIdx:
		val = Ns_SetValue(set, i);
                Tcl_SetResult(interp, val, TCL_VOLATILE);
                break;
		
            case SIsNullIdx:
		val = Ns_SetValue(set, i);
		objPtr = Tcl_NewBooleanObj(val ? 0 : 1);
                Tcl_SetObjResult(interp, objPtr);
                break;
		
	    case SKeyIdx:
		key = Ns_SetKey(set, i);
                Tcl_SetResult(interp, key, TCL_VOLATILE);
                break;
		
	    case SDeleteIdx:
                Ns_SetDelete(set, i);
                break;
		
            case STruncateIdx:
                Ns_SetTrunc(set, i);
                break;
            }
	    break;

	case SPutIdx:
	case SUpdateIdx:
	case SCPutIdx:
	case SICPutIdx:
	    /*
	     * These commands require a set, key, and value.
	     */

            if (objc != 5) {
		Tcl_WrongNumArgs(interp, 2, objv, "setId key value");
		return TCL_ERROR;
            }
	    key = Tcl_GetString(objv[3]);
	    val = Tcl_GetString(objv[4]);
	    switch (opt) {
	    case SUpdateIdx:
                Ns_SetDeleteKey(set, key);
                i = Ns_SetPut(set, key, val);
		break;
		
	    case SICPutIdx:
                i = Ns_SetIFind(set, key);
		if (i < 0) {
		    i = Ns_SetPut(set, key, val);
		}
		break;
		
	    case SCPutIdx:
                i = Ns_SetFind(set, key);
		if (i < 0) {
		    i = Ns_SetPut(set, key, val);
		}
		break;
		
	    case SPutIdx:
		i = Ns_SetPut(set, key, val);
		break;
            }
	    objPtr = Tcl_NewIntObj(i);
	    Tcl_SetObjResult(interp, objPtr);
	    break;

	case SMergeIdx:
	case SMoveIdx:
	    /*
	     * These commands require two sets.
	     */

            if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "setTo setFrom");
		return TCL_ERROR;
            }
            if (LookupObjSet(itPtr, objv[3], 0, &set2Ptr) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (opt == SMergeIdx) {
                Ns_SetMerge(set, set2Ptr);
	    } else {
                Ns_SetMove(set, set2Ptr);
	    }
	    Tcl_SetObjResult(interp, objv[2]);
	    break;
	}
    }

    return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * NsTclParseHeaderCmd --
 *
 *	This wraps Ns_ParseHeader. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Parse an HTTP header and add it to an existing set; see 
 *	Ns_ParseHeader. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclParseHeaderCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    Ns_Set *set;
    Ns_HeaderCaseDisposition disp;

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " set header ?tolower|toupper|preserve?\"", NULL);
        return TCL_ERROR;
    }
    if (LookupSet(itPtr, argv[1], 0, &set) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argc < 4) {
	disp = ToLower;
    } else if (STREQ(argv[3], "toupper")) {
        disp = ToUpper;
    } else if (STREQ(argv[3], "tolower")) {
        disp = ToLower;
    } else if (STREQ(argv[3], "preserve")) {
        disp = Preserve;
    } else {
        Tcl_AppendResult(interp, "unknown case disposition \"", argv[3],
            "\":  should be toupper, tolower, or preserve", NULL);
        return TCL_ERROR;
    }
    if (Ns_ParseHeader(set, argv[2], disp) != NS_OK) {
        Tcl_AppendResult(interp, "invalid header:  ", argv[2], NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * EnterSet --
 *
 *	Add an Ns_Set to an interp, creating a new unique id.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *      Set will be entered in the shared or private table.
 *
 *----------------------------------------------------------------------
 */

static int
EnterSet(NsInterp *itPtr, Ns_Set *set, int flags)
{
    Tcl_HashTable  *tablePtr;
    Tcl_HashEntry  *hPtr;
    int             new, next;
    unsigned char   type;
    char	    buf[20];

    if (flags & NS_TCL_SET_SHARED) {
	/*
	 * Lock the global mutex and use the shared sets.
	 */
	
	if (flags & NS_TCL_SET_DYNAMIC) {
	    type = SET_SHARED_DYNAMIC;
	} else {
	    type = SET_SHARED_STATIC;
	}
	tablePtr = &itPtr->servPtr->sets.table;
        Ns_MutexLock(&itPtr->servPtr->sets.lock);
    } else {
	tablePtr = &itPtr->sets;
	if (flags & NS_TCL_SET_DYNAMIC) {
	    type = SET_DYNAMIC;
	} else {
            type = SET_STATIC;
	}
    }

    /*
     * Allocate a new set IDs until we find an unused one.
     */
    
    next = tablePtr->numEntries;
    do {
        sprintf(buf, "%c%u", type, next);
	++next;
        hPtr = Tcl_CreateHashEntry(tablePtr, buf, &new);
    } while (!new);
    Tcl_SetHashValue(hPtr, set);
    Tcl_AppendElement(itPtr->interp, buf);

    /*
     * Unlock the global mutex (locked above) if it's a persistent set.
     */
    if (flags & NS_TCL_SET_SHARED) {
        Ns_MutexUnlock(&itPtr->servPtr->sets.lock);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LookupSet --
 *
 *	Take a tcl set handle and return a matching Set. This 
 *	takes both persistent and dynamic set handles. 
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *      If delete is set, then the hash entry will be removed.
 *  	Set will be returned in given setPtr.
 *
 *----------------------------------------------------------------------
 */

static int
LookupObjSet(NsInterp *itPtr, Tcl_Obj *idPtr, int delete, Ns_Set **setPtr)
{
    return LookupSet(itPtr, Tcl_GetString(idPtr), delete, setPtr);
}

static int
LookupInterpSet(Tcl_Interp *interp, char *id, int delete, Ns_Set **setPtr)
{
    NsInterp *itPtr;

    itPtr = NsGetInterp(interp);
    if (itPtr == NULL) {
	Tcl_SetResult(interp, "ns_set not supported", TCL_STATIC);
	return TCL_ERROR;
    }
    return LookupSet(itPtr, id, delete, setPtr);
}

static int
LookupSet(NsInterp *itPtr, char *id, int delete, Ns_Set **setPtr)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Ns_Set        *set;

    /*
     * If it's a persistent set, use the shared table, otherwise
     * use the private table.
     */
    
    set = NULL;
    if (IS_SHARED(id)) {
    	tablePtr = &itPtr->servPtr->sets.table;
        Ns_MutexLock(&itPtr->servPtr->sets.lock);
    } else {
	tablePtr = &itPtr->sets;
    }
    hPtr = Tcl_FindHashEntry(tablePtr, id);
    if (hPtr != NULL) {
        set = (Ns_Set *) Tcl_GetHashValue(hPtr);
        if (delete) {
            Tcl_DeleteHashEntry(hPtr);
        }
    }
    if (IS_SHARED(id)) {
        Ns_MutexUnlock(&itPtr->servPtr->sets.lock);
    }
    if (set == NULL) {
	Tcl_AppendResult(itPtr->interp, "no such set: ", id, NULL);
	return TCL_ERROR;
    }
    *setPtr = set;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * BadArgs --
 *
 *	Complain that the wrong # args were recieved. 
 *
 * Results:
 *	TCL result. 
 *
 * Side effects:
 *	Error message appended to interp result.
 *
 *----------------------------------------------------------------------
 */

static int
BadArgs(Tcl_Interp *interp, char **argv, char *args)
{
    Tcl_AppendResult(interp, "wrong # of args: should be \"",
        argv[0], " ", argv[1], " ", args, "\"", NULL);

    return TCL_ERROR;
}

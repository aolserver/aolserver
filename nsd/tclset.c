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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclset.c,v 1.7 2001/03/14 01:11:28 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following represent the valid combinations of
 * NS_TCL_SET flags
 */
 
#define SET_DYNAMIC 		'd'
#define SET_STATIC    		't'
#define SET_SHARED_DYNAMIC	's'
#define SET_SHARED_STATIC  	'p'

#define IS_DYNAMIC(type)    \
	((type) == SET_DYNAMIC || (type) == SET_SHARED_DYNAMIC)
#define IS_SHARED(type)     \
	((type) == SET_SHARED_DYNAMIC || (type) == SET_SHARED_STATIC)

/*
 * Local functions defined in this file
 */

static int BadArgs(Tcl_Interp *interp, char **argv, char *args);
static int LookupSet(NsInterp *itPtr, Tcl_Interp *interp, char *id,
	int delete, Ns_Set **setPtr);
static int EnterSet(NsInterp *itPtr, Tcl_Interp *interp, Ns_Set *set, int flags);


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
 *	A pointer to the NsSet is put into the interpreter's list of 
 *	sets; a new handle is generated and appended to 
 *	interp->result. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclEnterSet(Tcl_Interp *interp, Ns_Set *set, int flags)
{
    NsInterp *itPtr = NsGetInterp(interp);

    return EnterSet(itPtr, interp, set, flags);
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

    if (LookupSet(NULL, interp, id, 0, &set) != TCL_OK) {
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
    return LookupSet(NULL, interp, id, 0, setPtr);
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

    if (LookupSet(NULL, interp, id, 1, &set) != TCL_OK) {
	return TCL_ERROR;
    }
    if (IS_DYNAMIC(*id)) {
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
    char         *cmd;
    int           flags;
    Ns_Set      **setvectorPtrPtr;
    char         *split;
    Tcl_DString   ds;
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    NsInterp	  *itPtr = arg;

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
	NsFreeSets(itPtr);
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
        if (argv[2] != NULL &&
	    (STREQ(argv[2], "-shared") || STREQ(argv[2], "-persist"))) {
            flags |= NS_TCL_SET_SHARED;
            ++i;
        }
	
        switch (*cmd) {
        case 'n':
	    /*
	     * ns_set new
	     */
	    
            if (argv[i] != NULL && argv[i + 1] != NULL) {
                return BadArgs(interp, argv, "?-shared? ?name?");
            }
            EnterSet(itPtr, interp, Ns_SetCreate(argv[i]), flags);
            break;
        case 'c':
	    /*
	     * ns_set copy
	     */
	    
            if (argv[i] == NULL || argv[i + 1] != NULL) {
                return BadArgs(interp, argv, "?-shared? setId");
            }
	    if (LookupSet(itPtr, interp, argv[i], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
            EnterSet(itPtr, interp, Ns_SetCopy(set), flags);
            break;
        case 's':
	    /*
	     * ns_set split
	     */
	    
            if (argv[i] == NULL ||
		(argv[i + 1] != NULL && argv[i + 2] != NULL)) {
                return BadArgs(interp, argv, "?-shared? setId ?splitChar?");
            }
	    if (LookupSet(itPtr, interp, argv[i++], 0, &set) != TCL_OK) {
                return TCL_ERROR;
            }
            split = argv[i];
            if (split == NULL) {
                split = ".";
            }
            Tcl_DStringInit(&ds);
            setvectorPtrPtr = Ns_SetSplit(set, *split);
            for (i = 0; setvectorPtrPtr[i] != NULL; i++) {
                EnterSet(itPtr, interp, setvectorPtrPtr[i], flags);
                Tcl_DStringAppendElement(&ds, interp->result);
            }
            ns_free(setvectorPtrPtr);
            Tcl_DStringResult(interp, &ds);
            break;
        }
    } else {
        /*
         * All futher commands require a valid set.
         */
	
        if (argc < 3) {
            return BadArgs(interp, argv, "setId ?args?");
        }
	if (LookupSet(itPtr, interp, argv[2], 0, &set) != TCL_OK) {
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
		
                sprintf(interp->result, "%d", Ns_SetSize(set));
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
		
                sprintf(interp->result, "%d", Ns_SetFind(set, argv[3]));
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
		
                sprintf(interp->result, "%d", Ns_SetUnique(set, argv[3]));
                break;
		
            case 'i':
                switch (cmd[1]) {
                case 'f':
		    /*
		     * ns_set ifind
		     */
		    
                    sprintf(interp->result, "%d",
			    Ns_SetIFind(set, argv[3]));
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
		    
                    sprintf(interp->result, "%d",
			    Ns_SetIUnique(set, argv[3]));
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
                sprintf(interp->result, "Specified negative index (%d)", i);
                return TCL_ERROR;
            }
            if (i >= Ns_SetSize(set)) {
                sprintf(interp->result,
			"Can't access index %d; set only has %d field%s",
			i, Ns_SetSize(set),
			Ns_SetSize(set) != 1 ? "s" : "");
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
            sprintf(interp->result, "%d", i);
        } else if (STREQ(cmd, "merge") ||
		   STREQ(cmd, "move")) {
	    
            if (argc != 4) {
                return BadArgs(interp, argv, "setTo, setFrom");
            }
            if (LookupSet(itPtr, interp, argv[3], 0, &set2Ptr) != TCL_OK) {
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
                "copy, "
                "create, "
                "delete, "
                "delkey, "
                "free, "
                "get, "
                "iget, "
                "idelkey, "
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
    if (LookupSet(itPtr, interp, argv[1], 0, &set) != TCL_OK) {
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

static int
EnterSet(NsInterp *itPtr, Tcl_Interp *interp, Ns_Set *set, int flags)
{
    Tcl_HashTable  *tablePtr;
    Tcl_HashEntry  *hPtr;
    int             new, next;
    unsigned char   type;

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
        sprintf(interp->result, "%c%u", type, next);
	++next;
        hPtr = Tcl_CreateHashEntry(tablePtr, interp->result, &new);
    } while (!new);
    Tcl_SetHashValue(hPtr, set);

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
LookupSet(NsInterp *itPtr, Tcl_Interp *interp, char *id, int delete, Ns_Set **setPtr)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Ns_Set        *set;

    /*
     * Get the NsInterp structure if not yet known.
     */

    if (itPtr == NULL) {
	itPtr = NsGetInterp(interp);
    }
    
    /*
     * If it's a persistent set, use the shared table, otherwise
     * use the private table.
     */
    
    set = NULL;
    if (IS_SHARED(*id)) {
    	tablePtr = &itPtr->servPtr->sets.table;
        Ns_MutexLock(&itPtr->servPtr->sets.lock);
    } else {
	tablePtr = &itPtr->sets;
    }
    if (tablePtr != NULL) {
    	hPtr = Tcl_FindHashEntry(tablePtr, id);
    	if (hPtr != NULL) {
            set = (Ns_Set *) Tcl_GetHashValue(hPtr);
            if (delete) {
        	Tcl_DeleteHashEntry(hPtr);
            }
	}
    }
    if (IS_SHARED(*id)) {
        Ns_MutexUnlock(&itPtr->servPtr->sets.lock);
    }
    if (set == NULL) {
	Tcl_AppendResult(interp, "no such set: ", id, NULL);
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
 *	Will append to interp->result. 
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


/*
 *----------------------------------------------------------------------
 *
 * NsFreeSets --
 *
 *	Removes all set ids from given table, freeing dynamic sets
 *  	as needed.
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
NsFreeSets(NsInterp *itPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Tcl_HashTable *tablePtr;
    Ns_Set *set;
    char *id;

    tablePtr = &itPtr->sets;
    if (tablePtr->numEntries > 0) {
    	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    	while (hPtr != NULL) {
    	    id = Tcl_GetHashKey(tablePtr, hPtr);
	    if (IS_DYNAMIC(*id)) {
    	        set = Tcl_GetHashValue(hPtr);
	        Ns_SetFree(set);
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
    	Tcl_DeleteHashTable(tablePtr);
    	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
    }
}

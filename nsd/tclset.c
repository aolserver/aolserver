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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclset.c,v 1.4 2001/01/16 18:13:24 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following strucute maintains an Ns_Set and
 * flags (persistent, temp, etc.) for Tcl.
 */
 
 typedef struct Set {
    int     flags;
    Ns_Set *set;
} Set;

/*
 * Local functions defined in this file
 */

static int BadArgs(Tcl_Interp *interp, char **argv, char *args);
static Set *GetSet(Tcl_Interp *interp, char *setId, int delete);
static void FreeSet(Set *);
static Ns_Callback FreeSets;
static Tcl_HashTable *NewTable(void);
static Tcl_HashTable *GetSharedTable(void);
static Tcl_HashTable *GetInterpTable(Tcl_Interp *);

/*
 * The following lock is used to lock around access to the shared
 * table.
 */
 
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclEnterSet --
 *
 *	Give this Tcl interpreter access to an existing Ns_Set. A new 
 *	set handle is allocated and appended to interp->result.
 *	flags are an OR of:
 *	NS_TCL_SET_DYNAMIC:	Free the set and all its data on
 *				connection close.
 *	NS_TCL_SET_TEMPORARY:	Default behavior; opposite of persistent.
 *				(this is currently ignored).
 *	NS_TCL_SET_PERSISTENT:	Set lives until explicitly freed.
 *
 * Results:
 *	TCL_OK. 
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
    Set         *sPtr;
    Tcl_HashTable  *tablePtr;
    Tcl_HashEntry  *hPtr;
    int             new, next;
    char            prefix;

    /*
     * Allocate a new Set, which houses a pointer to the
     * real Ns_Set and the flags (is it persistent, etc.)
     */
    
    sPtr = (Set *) ns_malloc(sizeof(Set));
    sPtr->set = set;
    sPtr->flags = flags;
    
    if (flags & NS_TCL_SET_PERSISTENT) {
	/*
	 * Lock the global mutex and use the shared sets.
	 */
	
	tablePtr = GetSharedTable();
        Ns_MutexLock(&lock);
        prefix = 'p';
    } else {
	tablePtr = GetInterpTable(interp);
        prefix = 't';
    }

    /*
     * Allocate a new set IDs until we find an unused one.
     */
    
    next = tablePtr->numEntries;
    do {
        sprintf(interp->result, "%c%u", prefix, next);
	++next;
        hPtr = Tcl_CreateHashEntry(tablePtr, interp->result, &new);
    } while (!new);
    Tcl_SetHashValue(hPtr, sPtr);

    /*
     * Unlock the global mutex (locked above) if it's a persistent set.
     */
    if (flags & NS_TCL_SET_PERSISTENT) {
        Ns_MutexUnlock(&lock);
    }
    return TCL_OK;
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
Ns_TclGetSet(Tcl_Interp *interp, char *setId)
{
    Set  *sPtr;

    sPtr = GetSet(interp, setId, NS_FALSE);
    if (sPtr != NULL) {
        return sPtr->set;
    }
    return NULL;
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
Ns_TclGetSet2(Tcl_Interp *interp, char *setId, Ns_Set **setPtr)
{
    Ns_Set *set;

    set = Ns_TclGetSet(interp, setId);
    if (set == NULL) {
        Tcl_AppendResult(interp, "invalid set id: \"", setId, "\"", NULL);
        return TCL_ERROR;
    }
    *setPtr = set;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclFreeSet --
 *
 *	Free a set (based on a set handle) and each key and
 *	value with ns_free. 
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
Ns_TclFreeSet(Tcl_Interp *interp, char *setId)
{
    Set *sPtr;

    sPtr = GetSet(interp, setId, NS_TRUE);
    if (sPtr == NULL) {
        return NS_ERROR;
    }
    FreeSet(sPtr);
    return NS_OK;
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
NsTclSetCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Set       *setPtr, *set2Ptr;
    int           i;
    char         *cmd;
    int           flags;
    Ns_Set      **setvectorPtrPtr;
    char         *split;
    Tcl_DString   ds;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " command ?args?\"", NULL);
        return TCL_ERROR;
    }
    cmd = argv[1];

    /*
     * Handle new, copy, and split first.
     */

    if (STREQ(cmd, "create")) {
	cmd = "new";
    }
    if (STREQ(cmd, "new")  ||
	STREQ(cmd, "copy") ||
	STREQ(cmd, "split")) {

	/*
	 * The set is going to be dynamic; only string data can
	 * be stored in it, and will be freed with ns_free.
	 */
	
        flags = NS_TCL_SET_DYNAMIC;
        i = 2;
        if (argv[2] != NULL && STREQ(argv[2], "-persist")) {
            flags |= NS_TCL_SET_PERSISTENT;
            ++i;
        } else {
            flags |= NS_TCL_SET_TEMPORARY;
        }
	
        switch (*cmd) {
        case 'n':
	    /*
	     * ns_set new
	     */
	    
            if (argv[i] != NULL && argv[i + 1] != NULL) {
                return BadArgs(interp, argv, "?-persist? ?name?");
            }
            Ns_TclEnterSet(interp, Ns_SetCreate(argv[i]), flags);
            break;
        case 'c':
	    /*
	     * ns_set copy
	     */
	    
            if (argv[i] == NULL || argv[i + 1] != NULL) {
                return BadArgs(interp, argv, "?-persist? setId");
            }
            if (Ns_TclGetSet2(interp, argv[i], &setPtr) != TCL_OK) {
                return TCL_ERROR;
            }
            Ns_TclEnterSet(interp, Ns_SetCopy(setPtr), flags);
            break;
        case 's':
	    /*
	     * ns_set split
	     */
	    
            if (argv[i] == NULL ||
		(argv[i + 1] != NULL && argv[i + 2] != NULL)) {
		
                return BadArgs(interp, argv, "?-persist? setId ?splitChar?");
            }
            if (Ns_TclGetSet2(interp, argv[i++], &setPtr) != TCL_OK) {
                return TCL_ERROR;
            }
            split = argv[i];
            if (split == NULL) {
                split = ".";
            }
            Tcl_DStringInit(&ds);
            setvectorPtrPtr = Ns_SetSplit(setPtr, *split);
            for (i = 0; setvectorPtrPtr[i] != NULL; i++) {
                Ns_TclEnterSet(interp, setvectorPtrPtr[i],
			       NS_TCL_SET_TEMPORARY | NS_TCL_SET_DYNAMIC);
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
        if (Ns_TclGetSet2(interp, argv[2], &setPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (STREQ(cmd, "size")  ||
	    STREQ(cmd, "name")  ||
	    STREQ(cmd, "print") ||
	    STREQ(cmd, "free")) {
	    
            if (argc != 3) {
                return BadArgs(interp, argv, "setId");
            }
            switch (*cmd) {
            case 's':
		/*
		 * ns_set size
		 */
		
                sprintf(interp->result, "%d", Ns_SetSize(setPtr));
                break;
		
            case 'n':
		/*
		 * ns_set name
		 */
		
                Tcl_SetResult(interp, setPtr->name, TCL_STATIC);
                break;
		
            case 'p':
		/*
		 * ns_set print
		 */
		
                Ns_SetPrint(setPtr);
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
		
                sprintf(interp->result, "%d", Ns_SetFind(setPtr, argv[3]));
                break;
		
            case 'g':
		/*
		 * ns_set get
		 */
		
                Tcl_SetResult(interp, Ns_SetGet(setPtr, argv[3]), TCL_STATIC);
                break;
		
            case 'd':
		/*
		 * ns_set delete
		 */
		
                Ns_SetDeleteKey(setPtr, argv[3]);
                break;
		
            case 'u':
		/*
		 * ns_set unique
		 */
		
                sprintf(interp->result, "%d", Ns_SetUnique(setPtr, argv[3]));
                break;
		
            case 'i':
                switch (cmd[1]) {
                case 'f':
		    /*
		     * ns_set ifind
		     */
		    
                    sprintf(interp->result, "%d",
			    Ns_SetIFind(setPtr, argv[3]));
                    break;
		    
                case 'g':
		    /*
		     * ns_set iget
		     */
		    
                    Tcl_SetResult(interp, Ns_SetIGet(setPtr, argv[3]),
				  TCL_STATIC);
                    break;
		    
                case 'd':
		    /*
		     * ns_set idelete
		     */
		    
                    Ns_SetIDeleteKey(setPtr, argv[3]);
                    break;
		    
                case 'u':
		    /*
		     * ns_set iunique
		     */
		    
                    sprintf(interp->result, "%d",
			    Ns_SetIUnique(setPtr, argv[3]));
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
            if (i >= Ns_SetSize(setPtr)) {
                sprintf(interp->result,
			"Can't access index %d; set only has %d field%s",
			i, Ns_SetSize(setPtr),
			Ns_SetSize(setPtr) != 1 ? "s" : "");
                return TCL_ERROR;
            }
            switch (*cmd) {
            case 'v':
		/*
		 * ns_set value
		 */
		
                Tcl_SetResult(interp, Ns_SetValue(setPtr, i), TCL_STATIC);
                break;
		
            case 'i':
		/*
		 * ns_set isnull
		 */
		
                Tcl_SetResult(interp, Ns_SetValue(setPtr, i) ? "0" : "1",
			      TCL_STATIC);
                break;
		
            case 'k':
		/*
		 * ns_set key
		 */
		
                Tcl_SetResult(interp, Ns_SetKey(setPtr, i), TCL_STATIC);
                break;
		
            case 'd':
		/*
		 * ns_set delete
		 */
		
                Ns_SetDelete(setPtr, i);
                break;
		
            case 't':
		/*
		 * ns_set truncate
		 */
		
                Ns_SetTrunc(setPtr, i);
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
		
                Ns_SetDeleteKey(setPtr, argv[3]);
                i = Ns_SetPut(setPtr, argv[3], argv[4]);
		break;
		
	    case 'i':
		/*
		 * ns_set icput
		 */
		
                i = Ns_SetIFind(setPtr, argv[3]);
		if (i < 0) {
		    i = Ns_SetPut(setPtr, argv[3], argv[4]);
		}
		break;
		
	    case 'c':
		/*
		 * ns_set cput
		 */
		
                i = Ns_SetFind(setPtr, argv[3]);
		if (i < 0) {
		    i = Ns_SetPut(setPtr, argv[3], argv[4]);
		}
		break;
		
	    case 'p':
		/*
		 * ns_set put
		 */

		i = Ns_SetPut(setPtr, argv[3], argv[4]);
		break;
            }
            sprintf(interp->result, "%d", i);
        } else if (STREQ(cmd, "merge") ||
		   STREQ(cmd, "move")) {
	    
            if (argc != 4) {
                return BadArgs(interp, argv, "setTo, setFrom");
            }
            if (Ns_TclGetSet2(interp, argv[3], &set2Ptr) != TCL_OK) {
                return TCL_ERROR;
            }
	    switch (cmd[1]) {
	    case 'e':
		/*
		 * ns_set merge
		 */
		
                Ns_SetMerge(setPtr, set2Ptr);
		break;
		
	    case 'o':
		/*
		 * ns_set move
		 */
		
                Ns_SetMove(setPtr, set2Ptr);
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
                "merge, "
                "move, "
                "name, "
                "new, "
                "print, "
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
NsTclParseHeaderCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		     char **argv)
{
    Ns_Set *setPtr;
    Ns_HeaderCaseDisposition disp;

    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " set header ?tolower|toupper|preserve?\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclGetSet2(interp, argv[1], &setPtr) != TCL_OK) {
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
    if (Ns_ParseHeader(setPtr, argv[2], disp) != NS_OK) {
        Tcl_AppendResult(interp, "invalid header:  ", argv[2], NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeSet --
 *
 *	Free a Set and, if it is dynamic, its keys and values as 
 *	well. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	If dynamic, keys and values will be freed. 
 *
 *----------------------------------------------------------------------
 */

void
FreeSet(Set *sPtr)
{
    if (sPtr->flags & NS_TCL_SET_DYNAMIC) {
        Ns_SetFree(sPtr->set);
    }
    ns_free(sPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetSet --
 *
 *	Take a tcl set handle and return a matching Set. This 
 *	takes both persistent and dynamic set handles. 
 *
 * Results:
 *	A Set or NULL if error. 
 *
 * Side effects:
 *      If delete is NS_TRUE, then the hash entry will be removed.
 *
 *----------------------------------------------------------------------
 */

static Set *
GetSet(Tcl_Interp *interp, char *setId, int delete)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *hPtr;
    Set        *sPtr;

    /*
     * If it's a persistent set, use the shared table, otherwise
     * use the interp table.
     */
    
    if (*setId == 'p') {
	tablePtr = GetSharedTable();
        Ns_MutexLock(&lock);
    } else {
	tablePtr = GetInterpTable(interp);
    }
    hPtr = Tcl_FindHashEntry(tablePtr, setId);
    if (hPtr == NULL) {
        sPtr = NULL;
    } else {
        sPtr = (Set *) Tcl_GetHashValue(hPtr);
        if (delete == NS_TRUE) {
            Tcl_DeleteHashEntry(hPtr);
        }
    }
    if (*setId == 'p') {
        Ns_MutexUnlock(&lock);
    }
    return sPtr;
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
 * GetSharedTable --
 *
 *	Return the table for persistent sets.
 *
 * Results:
 *	Pointer to static, initialized Tcl_HashTable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashTable *
GetSharedTable(void)
{
    static int initialized;
    static Tcl_HashTable table;    

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    Ns_MutexSetName2(&lock, "ns", "sets");
	    Tcl_InitHashTable(&table, TCL_STRING_KEYS);
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }
    return &table;
}


/*
 *----------------------------------------------------------------------
 *
 * GetInterpTable --
 *
 *	Return the table for given interp's temporary sets.
 *
 * Results:
 *	Pointer to dynamic, initialized Tcl_HashTable.
 *
 * Side effects:
 *	See FreeSets.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashTable *
GetInterpTable(Tcl_Interp *interp)
{
    Tcl_HashTable *tablePtr;

    tablePtr = NsTclGetData(interp, NS_TCL_SETS_KEY);
    if (tablePtr == NULL) {
	tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
	NsTclSetData(interp, NS_TCL_SETS_KEY, tablePtr, FreeSets);
    }
    return tablePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeSets --
 *
 *	Tcl interp data callback to free temporary sets data.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Table is purged and freed.
 *
 *----------------------------------------------------------------------
 */

static void
FreeSets(void *arg)
{
    Tcl_HashTable *tablePtr = arg;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    Set *sPtr;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
	sPtr = Tcl_GetHashValue(hPtr);
	FreeSet(sPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}

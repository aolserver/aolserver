/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * The Original Code is ArsDigita code and related documentation
 * distributed by ArsDigita.
 * 
 * The Initial Developer of the Original Code is ArsDigita.,
 * Portions created by ArsDigita are Copyright (C) 1999 ArsDigita.
 * All Rights Reserved.
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
 * tclcache.c --
 *
 *	Tcl API for cache.c.  Based on work from the nscache module.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclcache.c,v 1.3 2009/01/31 21:35:08 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains a Tcl cache including an underlying size-based
 * Ns_Cache and options for a max time to live (default: infiniate) and timeout
 * for threads waiting on other threads to update a value (default: 2 seconds).
 */

typedef struct TclCache {
    Ns_Cache *cache;
    char *name;
    int namelen;
    Ns_Time atime;
    Ns_Time wait;
    Ns_Time ttl;
    int expires;
} TclCache;

/*
 * The following structure defines a value stored in the cache which is string
 * with optional expiration time.  Values will store any string of bytes from
 * the corresponding Tcl_Obj but no type information will be preserved.
 */

typedef struct Val {
    Ns_Time expires;
    int length;
    char string[1];
} Val;

static int CreateCacheObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			     Tcl_Obj **objv);
static int SetResult(Tcl_Interp *interp, Val *valPtr, char *varName);
static Val *NewVal(TclCache *cachePtr, Tcl_Obj *objPtr, Ns_Time *nowPtr);
static int Expired(TclCache *cachePtr, Val *valPtr, Ns_Time *nowPtr);
static int SetCacheFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr);
static void UpdateStringOfCache(Tcl_Obj *objPtr);

/*
 * The following structure defines a Tcl type for caches which maintains a pointer
 * the the cooresponding TclCache structure.
 */

static Tcl_ObjType cacheType = {
    "ns:cache",
    (Tcl_FreeInternalRepProc *) NULL,
    (Tcl_DupInternalRepProc *) NULL,
    UpdateStringOfCache,
    SetCacheFromAny
};

/*
 * The following static variables are defined in this file.
 */

static Tcl_HashTable caches;	/* Table of all caches, process wide. */
static Ns_Mutex lock;		/* Lock around list table of caches. */


/*
 *----------------------------------------------------------------------
 *
 * NsTclInitCacheType --
 *
 *	Initialize the type for Tcl caches.
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
NsTclInitCacheType(void)
{
    Ns_MutexSetName(&lock, "nstcl:caches");
    Tcl_InitHashTable(&caches, TCL_STRING_KEYS);
    Tcl_RegisterObjType(&cacheType);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheObjCmd --
 *
 *	Handle the ns_cache command.  See the documentation for details.
 *	Note that mixing the "eval" option with other options (e.g.,
 *	"get", "set", "incr", etc.) doesn't make sense. In particular,
 *	there's no generally correct way to handle a "get" or "set"
 *	encountering an in-progress "eval", i.e., a NULL value. In these
 *	case the code below returns an immediate error.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	May cause the current thread to wait for another thread to
 *	update a given entry with the eval option.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    static CONST char *opts[] = {
	"create", "names", "eval", "set", "get", "incr", 
	"append", "lappend", "flush", NULL
    };
    enum {
	CCreateIdx, CNamesIdx, CEvalIdx, CSetIdx, CGetIdx, CIncrIdx,
	CAppendIdx, CLappendIdx, CFlushIdx
    } opt;
    TclCache *cachePtr;
    Val *valPtr;
    int i, cur, err, new, status;
    char *key, *pattern, *var;
    Ns_Entry *entry;
    Ns_CacheSearch search;
    Tcl_Obj *objPtr = NULL;
    Ns_Time now, timeout;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }

    /*
     * Handle create directly as all other commands require a valid
     * cache argument.
     */

    if (opt == CCreateIdx) {
	return CreateCacheObjCmd(arg, interp, objc, objv);
    }

    if (objc < 3) {
	Tcl_WrongNumArgs(interp, 2, objv, "cache ?args ...?");
	return TCL_ERROR;
    }
    if (Tcl_ConvertToType(interp, objv[2], &cacheType) != TCL_OK) {
        return TCL_ERROR;
    }
    cachePtr = objv[2]->internalRep.otherValuePtr;

    Ns_GetTime(&now);
    err = 0;
    switch (opt) {
    case CCreateIdx:
	/* NB: Silence compiler warning. */
	break;

    case CNamesIdx:
	/*
	 * Return keys for all cache entries, flushing any expired items first.
	 */

	if (objc < 4) {
	    pattern = NULL;
	} else {
	    pattern = Tcl_GetString(objv[3]);
	}
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheFirstEntry(cachePtr->cache, &search);
	while (entry != NULL) {
	    key = Ns_CacheKey(entry);
	    valPtr = Ns_CacheGetValue(entry);
	    if (valPtr != NULL) {
		if (Expired(cachePtr, valPtr, &now)) {
		    Ns_CacheFlushEntry(entry);
		} else if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
		    Tcl_AppendElement(interp, key);
		}
	    }
	    entry = Ns_CacheNextEntry(&search);
	}
	Ns_CacheUnlock(cachePtr->cache);
	break;
		
    case CFlushIdx:
	/*
	 * Flush one or more entries from the cache.
	 */

	Ns_CacheLock(cachePtr->cache);
	for (i = 3; i < objc; ++i) {
	    key = Tcl_GetString(objv[i]);
	    entry = Ns_CacheFindEntry(cachePtr->cache, key);
	    if (entry != NULL && (valPtr = Ns_CacheGetValue(entry)) != NULL) {
		Ns_CacheFlushEntry(entry);
	    }
	}
	Ns_CacheUnlock(cachePtr->cache);
	break;

    case CGetIdx:
	/*
	 * Get the current value if not expired and not currently being refreshed.
	 */

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "key ?valueVar?");
	    return TCL_ERROR;
	}
	valPtr = NULL;
	key = Tcl_GetString(objv[3]);
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheFindEntry(cachePtr->cache, key);
	if (entry != NULL
		&& (valPtr = Ns_CacheGetValue(entry)) != NULL
		&& Expired(cachePtr, valPtr, &now)) {
	    Ns_CacheFlushEntry(entry);
	    valPtr = NULL;
	}
	if (valPtr != NULL) {
	    var = (objc < 5 ? NULL : Tcl_GetString(objv[4]));
	    err = SetResult(interp, valPtr, var);
	}
	Ns_CacheUnlock(cachePtr->cache);
	if (err) {
	    return TCL_ERROR;
	} else if (objc == 5) {
	    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), valPtr ? 1 : 0);
	} else if (valPtr == NULL) {
	    Tcl_AppendResult(interp, "no such entry: ", key, NULL);
	    return TCL_ERROR;
	}
	break;

    case CSetIdx:
	/*
	 * Set a value, ignoring current state (if any) of the entry.
	 */

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "key value");
	    return TCL_ERROR;
	}
	valPtr = NewVal(cachePtr, objv[4], &now);
	key = Tcl_GetString(objv[3]);
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheCreateEntry(cachePtr->cache, key, &new);
	Ns_CacheSetValueSz(entry, valPtr, valPtr->length);
	Ns_CacheUnlock(cachePtr->cache);
	Tcl_SetObjResult(interp, objv[4]);
	break;

    case CIncrIdx:
	/*
	 * Increment a value, assuming the previous value is a valid integer.
	 * No value or expired value is treated as starting at zero.
	 */

	if (objc != 4 && objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "key ?incr?");
	    return TCL_ERROR;
	}
	if (objc < 5) {
	    i = 1;
	} else if (Tcl_GetIntFromObj(interp, objv[4], &i) != TCL_OK) {
	    return TCL_ERROR;
	}
	err = 0;
	key = Tcl_GetString(objv[3]);
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheCreateEntry(cachePtr->cache, key, &new);
	if (!new
		&& (valPtr = Ns_CacheGetValue(entry)) != NULL
		&& Expired(cachePtr, valPtr, &now)) {
	    Ns_CacheUnsetValue(entry);
	    new = 1;
	}
	if (new) {
	    cur = 0;
	} else if (valPtr == NULL) {
	    Tcl_AppendResult(interp, "entry busy: ", key, NULL);
	    err = 1;
	} else if (Tcl_GetInt(interp, valPtr->string, &cur) != TCL_OK) {
	    err = 1;
	}
	if (!err) {
	    objPtr = Tcl_NewIntObj(cur + i);
	    valPtr = NewVal(cachePtr, objPtr, &now);
	    Ns_CacheSetValueSz(entry, valPtr, valPtr->length);
	}
	Ns_CacheUnlock(cachePtr->cache);
	if (err) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;

    case CAppendIdx:
    case CLappendIdx:
	/*
	 * Append or list append one or more elements to current value.
	 */

	if (objc < 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "key str ?str ...?");
	    return TCL_ERROR;
	}
	err = 0;
	key = Tcl_GetString(objv[3]);
	objPtr = Tcl_NewObj();
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheCreateEntry(cachePtr->cache, key, &new);
	if (!new) {
	    valPtr = Ns_CacheGetValue(entry);
	    if (valPtr == NULL) {
		Tcl_AppendResult(interp, "entry busy: ", key, NULL);
		err = 1;
	     } else if (!Expired(cachePtr, valPtr, &now)) {
		Tcl_AppendToObj(objPtr, valPtr->string, valPtr->length);
	    }
	}
	for (i = 4; !err && i <  objc; ++i) {
	    if (opt == CAppendIdx) {
		Tcl_AppendObjToObj(objPtr, objv[i]);
	    } else if (Tcl_ListObjAppendElement(interp, objPtr, objv[i])
			!= TCL_OK) {
		err = 1;
	    }
	}
	if (!err) {
	    Ns_CacheUnsetValue(entry);
	    valPtr = NewVal(cachePtr, objPtr, &now);
	    Ns_CacheSetValueSz(entry, valPtr, valPtr->length);
	}
	Ns_CacheUnlock(cachePtr->cache);
	if (err) {
	    return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, objPtr);
	break;

    case CEvalIdx:
	/*
	 * Get a value from cache, setting or refreshing the value with
	 * given script when necessary.  A NULL value is maintained in the
	 * cache during the update script to avoid multiple threads updating
	 * the same value at once.
	 */

	if (objc != 5) {
	    Tcl_WrongNumArgs(interp, 3, objv, "key script");
	    return TCL_ERROR;
	}
        status = TCL_OK;
	key = Tcl_GetString(objv[3]);
	Ns_CacheLock(cachePtr->cache);
	entry = Ns_CacheCreateEntry(cachePtr->cache, key, &new);
	if (!new && (valPtr = Ns_CacheGetValue(entry)) == NULL) {
	    /*
	     * Wait for another thread to complete an update.
	     */

	    status = NS_OK;
	    timeout = now;
	    Ns_IncrTime(&timeout, cachePtr->wait.sec, cachePtr->wait.usec);
	    do {
	    	status = Ns_CacheTimedWait(cachePtr->cache, &timeout);
	    } while (status == NS_OK
		&& (entry = Ns_CacheFindEntry(cachePtr->cache, key)) != NULL
		&& (valPtr = Ns_CacheGetValue(entry)) == NULL);
	    if (entry == NULL) {
		Tcl_AppendResult(interp, "update failed: ", key, NULL);
		err = 1;
	    } else if (valPtr == NULL) {
		Tcl_AppendResult(interp, "timeout waiting for update: ", key, NULL);
		err = 1;
	    } else {
		Ns_GetTime(&now);
	    }
	}
	if (!err) {
	    if (!new && Expired(cachePtr, valPtr, &now)) {
	    	Ns_CacheUnsetValue(entry);
	    	new = 1;
	    }
	    if (!new) {
		/*
		 * Return current value.
		 */

		valPtr = Ns_CacheGetValue(entry);
		err = SetResult(interp, valPtr, NULL);
	    } else {
		/*
		 * Refresh the entry.
		 */

	    	Ns_CacheUnlock(cachePtr->cache);
	    	status = Tcl_EvalObjEx(interp, objv[4], 0);
	    	Ns_CacheLock(cachePtr->cache);
		entry = Ns_CacheCreateEntry(cachePtr->cache, key, &new);

	    	if (status == TCL_OK || status == TCL_RETURN) {
		    objPtr = Tcl_GetObjResult(interp);
	    	    valPtr = NewVal(cachePtr, objPtr, &now);
	    	    Ns_CacheSetValueSz(entry, valPtr, valPtr->length);

                    if (status == TCL_RETURN) {
                        status = TCL_OK;
                    }
		} else {
		    Ns_CacheFlushEntry(entry);
	    	}
	    	Ns_CacheBroadcast(cachePtr->cache);
	    }
	}
	Ns_CacheUnlock(cachePtr->cache);
	if (err) {
	    return TCL_ERROR;
	}

        return status;
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateCacheObjCmd --
 *
 *	Sub-command to create a new cache.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CreateCacheObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    int i, new, size, expires;
    Ns_Time ttl, wait;
    Tcl_HashEntry *hPtr;
    TclCache *cachePtr;
    char *cache;
    static CONST char *flags[] = {
	"-timeout", "-size", "-thread", "-server", "-maxwait", NULL
    };
    enum {
	FTimeoutIdx, FSizeIdx, FThreadIdx, FServerIdx, FWaitIdx
    } flag;

    if (objc < 3 || !(objc & 1)) {
	Tcl_WrongNumArgs(interp, 2, objv, "?-flag val -flag val...?");
	return TCL_ERROR;
    }
    cache = Tcl_GetString(objv[2]);
    wait.sec = 2;
    ttl.sec = 60;
    wait.usec = ttl.usec = 0;
    expires = 0;
    size = 1024 * 1000;
    for (i = 3; i < objc; i += 2) {
	if (Tcl_GetIndexFromObj(interp, objv[i], flags, "flag", 0,
	 			(int *) &flag) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (flag) {
	case FSizeIdx:
	    if (Tcl_GetIntFromObj(interp, objv[i+1], &size) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (size < 0) {
		Tcl_AppendResult(interp, "invalid size: ", 
				 Tcl_GetString(objv[i+1]), NULL);
		return TCL_ERROR;
	    }
	    break;

	case FTimeoutIdx:
	    if (Ns_TclGetTimeFromObj(interp, objv[i+1], &ttl) != TCL_OK) {
		return TCL_ERROR;
	    }
	    expires = 1;
	    break;

	case FWaitIdx:
	    if (Ns_TclGetTimeFromObj(interp, objv[i+1], &wait) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;

	case FThreadIdx:
	case FServerIdx:
	    /* NB: Previous nscache options currently ignored. */
	    break;
	}
    }
    Ns_MutexLock(&lock);
    hPtr = Tcl_CreateHashEntry(&caches, cache, &new);
    if (new) {
	cachePtr = ns_malloc(sizeof(TclCache));
	cachePtr->name = Tcl_GetHashKey(&caches, hPtr);
	cachePtr->namelen = strlen(cachePtr->name);
	cachePtr->ttl = ttl;
	cachePtr->wait = wait;
	cachePtr->expires = expires;
	cachePtr->cache = Ns_CacheCreateSz(cache, TCL_STRING_KEYS,
					   (size_t) size, ns_free);
	Tcl_SetHashValue(hPtr, cachePtr);
    }
    Ns_MutexUnlock(&lock);
    if (!new) {
	Tcl_AppendResult(interp, "cache already exists: ", cache, NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Expired --
 *
 *	Check if a given value has expired.
 *
 * Results:
 *	0 if still valid, 1 if expired.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Expired(TclCache *cachePtr, Val *valPtr, Ns_Time *nowPtr)
{
    if (cachePtr->expires && Ns_DiffTime(&valPtr->expires, nowPtr, NULL) < 0) {
	return 1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * NewVal --
 *
 *	Allocate a new Val object from the given Tcl_Obj.
 *
 * Results:
 *	Pointer to new Val.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Val *
NewVal(TclCache *cachePtr, Tcl_Obj *objPtr, Ns_Time *nowPtr)
{
    Val *valPtr;
    char *str;
    int len;

    str = Tcl_GetStringFromObj(objPtr, &len);
    valPtr = ns_malloc(sizeof(Val) + len);
    valPtr->length = len;
    memcpy(valPtr->string, str, len);
    valPtr->string[len] = '\0';
    if (cachePtr->expires) {
	valPtr->expires = *nowPtr;
	Ns_IncrTime(&valPtr->expires, cachePtr->ttl.sec, cachePtr->ttl.usec);
    }
    return valPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * SetResult --
 *
 *	Set a Val as the current Tcl result.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
SetResult(Tcl_Interp *interp, Val *valPtr, char *varName)
{
    Tcl_Obj *objPtr;
    int err = 0;

    objPtr = Tcl_NewStringObj(valPtr->string, valPtr->length);
    Tcl_IncrRefCount(objPtr);
    if (varName == NULL) {
	Tcl_SetObjResult(interp, objPtr);
    } else if (Tcl_SetVar2Ex(interp, varName, NULL, objPtr, TCL_LEAVE_ERR_MSG) == NULL) {
	err = 1;
    }
    Tcl_DecrRefCount(objPtr);
    return err;
}


/*
 *----------------------------------------------------------------------
 *
 * UpdateStringOfCache --
 *
 *	Callback to set the string of a TclCache Tcl_Obj.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will update Tcl_Obj's bytes and length.
 *
 *----------------------------------------------------------------------
 */

static void
UpdateStringOfCache(Tcl_Obj *objPtr)
{
    TclCache *cachePtr = (TclCache *) objPtr->internalRep.otherValuePtr;

    objPtr->length = cachePtr->namelen;
    objPtr->bytes = ckalloc(objPtr->length + 1);
    strcpy(objPtr->bytes, cachePtr->name);
}


/*
 *----------------------------------------------------------------------
 *
 * SetCacheFromAny --
 *
 *	Set a Tcl_Obj internal rep to be a pointer to the TclCache
 *	looked up by string name.
 *
 * Results:
 *	TCL_OK if valid cache, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Will leave an error message in given Tcl_Interp.
 *
 *----------------------------------------------------------------------
 */

static int
SetCacheFromAny(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    Tcl_ObjType *typePtr = objPtr->typePtr;
    TclCache *cachePtr;
    Tcl_HashEntry *hPtr;
    char *cache;

    cache = Tcl_GetString(objPtr);
    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&caches, cache);
    if (hPtr != NULL) {
	cachePtr = Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&lock);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such cache: ", cache, NULL);
	return TCL_ERROR;
    }
    if (typePtr != NULL && typePtr->freeIntRepProc != NULL) {
        (*typePtr->freeIntRepProc)(objPtr);
    }
    objPtr->typePtr = &cacheType;
    objPtr->internalRep.otherValuePtr = cachePtr;
    Tcl_InvalidateStringRep(objPtr);
    objPtr->length = 0;  /* ensure there's no stumbling */
    return TCL_OK;
}

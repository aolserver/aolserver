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
 * cache.c --
 *
 *	Routines for a simple cache used by fastpath and Adp.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/cache.c,v 1.14 2003/06/01 10:47:57 vasiljevic Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

struct Cache;

/*
 * An Entry is a node in a linked list as well as being a
 * hash table entry. The linked list is there to keep track of
 * usage for the purposes of cache pruning.
 */

typedef struct Entry {
    struct Entry *nextPtr;
    struct Entry *prevPtr;
    struct Cache *cachePtr;
    Tcl_HashEntry *hPtr;
    Ns_Time mtime;
    size_t size;
    void *value;
} Entry;

/*
 * The following structure defines a cache
 */

typedef struct Cache {
    Entry *firstEntryPtr;
    Entry *lastEntryPtr;
    Tcl_HashEntry *hPtr;
    int keys;
    time_t timeout;
    int schedId;
    int schedStop;
    size_t maxSize;
    size_t currentSize;
    Ns_Callback *freeProc;
    Ns_Mutex lock;
    Ns_Cond cond;
    unsigned int nhit;
    unsigned int nmiss;
    unsigned int nflush;
    Tcl_HashTable entriesTable;
    char    name[1];
} Cache;


/*
 * Local functions defined in this file
 */

static Ns_Cache * CacheCreate(char *name, int keys, time_t timeout,
			      size_t maxSize, Ns_Callback *freeProc);
static int GetCache(Tcl_Interp *interp, char *name, Cache **cachePtrPtr);
static void Delink(Entry *ePtr);
static void Push(Entry *ePtr);

/*
 * Static variables defined in this file
 */

static Tcl_HashTable caches;
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * NsInitCache --
 *
 *	Initialize the cache API.
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
NsInitCache(void)
{
    Ns_MutexInit(&lock);
    Ns_MutexSetName(&lock, "ns:caches");
    Tcl_InitHashTable(&caches, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheCreate --
 *
 *	Calls CacheCreate with an unlimited max size.
 *
 * Results:
 *	See CacheCreate()
 *
 * Side effects:
 *	See CacheCreate()
 *
 *----------------------------------------------------------------------
 */

Ns_Cache *
Ns_CacheCreate(char *name, int keys, time_t timeout, Ns_Callback *freeProc)
{
    return CacheCreate(name, keys, timeout, 0, freeProc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheCreateSz --
 *
 *	See CacheCreate()
 *
 * Results:
 *	See CacheCreate()
 *
 * Side effects:
 *	See CacheCreate()
 *
 *----------------------------------------------------------------------
 */

Ns_Cache *
Ns_CacheCreateSz(char *name, int keys, size_t maxSize, Ns_Callback *freeProc)
{
    return CacheCreate(name, keys, -1, maxSize, freeProc);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheDestroy
 *
 *	Flush all entries and delete a cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Cache no longer usable.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheDestroy(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    /*
     * Unschedule the flusher if time-based cache.
     */

    if (cachePtr->schedId >= 0) {
    	Ns_MutexLock(&cachePtr->lock);
	cachePtr->schedStop = 1;
    	if (Ns_Cancel(cachePtr->schedId)) {
	    cachePtr->schedId = -1;
	}

	/*
	 * Wait for currently running flusher to exit.
	 */

	while (cachePtr->schedId >= 0) {
	    Ns_CondWait(&cachePtr->cond, &cachePtr->lock);
	}
    	Ns_MutexUnlock(&cachePtr->lock);
    }

    /*
     * Flush all entries.
     */

    Ns_CacheFlush(cache);

    /*
     * Remove from cache table and free cache structure.
     */

    Ns_MutexLock(&lock);
    if (cachePtr->hPtr != NULL) {
    	Tcl_DeleteHashEntry(cachePtr->hPtr);
    }
    Ns_MutexUnlock(&lock);

    Ns_MutexDestroy(&cachePtr->lock);
    Ns_CondDestroy(&cachePtr->cond);
    Tcl_DeleteHashTable(&cachePtr->entriesTable);
    ns_free(cachePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFind --
 *
 *	Find a cache by name.
 *
 * Results:
 *	A pointer to an Ns_Cache or NULL 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Cache *
Ns_CacheFind(char *name)
{
    Tcl_HashEntry *hPtr;
    Ns_Cache *cache;
    
    cache = NULL;
    Ns_MutexLock(&lock);
    hPtr = Tcl_FindHashEntry(&caches, name);
    if (hPtr != NULL) {
    	cache = (Ns_Cache *) Tcl_GetHashValue(hPtr);
    }
    Ns_MutexUnlock(&lock);
    return cache;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheMalloc --
 *
 *	Allocate memory from a cache-local pool. 
 *
 * Results:
 *	A pointer to new memory.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
Ns_CacheMalloc(Ns_Cache *cache, size_t len)
{
    return ns_malloc(len);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFree --
 *
 *	Frees memory allocated from Ns_CacheMalloc 
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
Ns_CacheFree(Ns_Cache *cache, void *ptr)
{
    ns_free(ptr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFindEntry --
 *
 *	Find a cache entry 
 *
 * Results:
 *	A pointer to an Ns_Entry cache entry 
 *
 * Side effects:
 *	The cache entry will move to the top of the LRU list. 
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheFindEntry(Ns_Cache *cache, char *key)
{
    Cache *cachePtr = (Cache *) cache;
    Tcl_HashEntry *hPtr;
    Entry *ePtr;

    hPtr = Tcl_FindHashEntry(&cachePtr->entriesTable, key);
    if (hPtr == NULL) {
	++cachePtr->nmiss;
	return NULL;
    }
    ++cachePtr->nhit;
    ePtr = Tcl_GetHashValue(hPtr);
    Delink(ePtr);
    Push(ePtr);
    
    return (Ns_Entry *) ePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheCreateEntry --
 *
 *	Create a new cache entry; this function emulates 
 *	Tcl_CreateHashEntry's interface 
 *
 * Results:
 *	A pointer to a new cache entry 
 *
 * Side effects:
 *	Memory will be allocated for the new cache entry and it will 
 *	be inserted into the cache. 
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheCreateEntry(Ns_Cache *cache, char *key, int *newPtr)
{
    Cache *cachePtr = (Cache *) cache;
    Tcl_HashEntry *hPtr;
    Entry *ePtr;

    hPtr = Tcl_CreateHashEntry(&cachePtr->entriesTable, key, newPtr);
    if (*newPtr == 0) {
	ePtr = Tcl_GetHashValue(hPtr);
    	Delink(ePtr);
	++cachePtr->nhit;
    } else {
	ePtr = ns_calloc(1, sizeof(Entry));
	ePtr->hPtr = hPtr;
	ePtr->cachePtr = cachePtr;
	Tcl_SetHashValue(hPtr, ePtr);
	++cachePtr->nmiss;
    }
    Push(ePtr);
    
    return (Ns_Entry *) ePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheName --
 *
 *	Gets the name of the cache 
 *
 * Results:
 *	A pointer to a null-terminated string 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_CacheName(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    return ePtr->cachePtr->name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheKey --
 *
 *	Gets the key of a cache entry 
 *
 * Results:
 *	A pointer to the key for the given entry 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_CacheKey(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    return Tcl_GetHashKey(&ePtr->cachePtr->entriesTable, ePtr->hPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheGetValue --
 *
 *	Get the value (contents) of a cache entry 
 *
 * Results:
 *	A pointer to the cache entry's contents 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_CacheGetValue(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    return ePtr->value;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheSetValue --
 *
 *	Set the value of a cache entry 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	See Ns_CacheSetValueSz 
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheSetValue(Ns_Entry *entry, void *value)
{
    Ns_CacheSetValueSz(entry, value, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheSetValueSz --
 *
 *	Free the cache entry's previous contents, set it to the new 
 *	contents, increase the size of the cache, and prune until 
 *	it's back under the maximum size. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Cache pruning and freeing of old contents may occur. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheSetValueSz(Ns_Entry *entry, void *value, size_t size)
{
    Entry *ePtr = (Entry *) entry;
    Cache *cachePtr = ePtr->cachePtr;

    Ns_CacheUnsetValue(entry);
    ePtr->value = value;
    ePtr->size = size;
    cachePtr->currentSize += size;
    if (ePtr->cachePtr->maxSize > 0) {
	while (cachePtr->currentSize > cachePtr->maxSize &&
	    cachePtr->lastEntryPtr != ePtr) {
	    Ns_CacheFlushEntry((Ns_Entry *) cachePtr->lastEntryPtr);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheUnsetValue --
 *
 *	Reset the value of an entry to NULL, calling the free proc for
 *  	any previous entry an updating the cache size.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheUnsetValue(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;
    Cache *cachePtr;
 
    if (ePtr->value != NULL) {
	cachePtr = ePtr->cachePtr;
	cachePtr->currentSize -= ePtr->size;
	if (cachePtr->freeProc == NS_CACHE_FREE) {
	    Ns_CacheFree((Ns_Cache *) cachePtr, ePtr->value);
	} else if (cachePtr->freeProc != NULL) {
    	    (*cachePtr->freeProc)(ePtr->value);
	}
	ePtr->size = 0;
	ePtr->value = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheDeleteEntry --
 *
 *	Delete an entry from the cache table.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheDeleteEntry(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    Delink(ePtr);
    Tcl_DeleteHashEntry(ePtr->hPtr);
    ns_free(ePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFlushEntry --
 *
 *	Delete an entry from the cache table after first unsetting
 *  	the current entry value (if any).
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheFlushEntry(Ns_Entry *entry)
{
    Entry *ePtr = (Entry *) entry;

    ++ePtr->cachePtr->nflush;
    Ns_CacheUnsetValue(entry);
    Ns_CacheDeleteEntry(entry);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFirstEntry --
 *
 *	Return a pointer to the first entry in the cache (in no 
 *	particular order) 
 *
 * Results:
 *	A pointer to said entry. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheFirstEntry(Ns_Cache *cache, Ns_CacheSearch *search)
{
    Tcl_HashSearch *sPtr = (Tcl_HashSearch *) search;
    Cache *cachePtr = (Cache *) cache;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FirstHashEntry(&cachePtr->entriesTable, sPtr);
    if (hPtr == NULL) {
	return NULL;
    }
    return (Ns_Entry *) Tcl_GetHashValue(hPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheNextEntry --
 *
 *	When used in conjunction with Ns_CacheFirstEntry, one may 
 *	walk through the whole cache. 
 *
 * Results:
 *	NULL or a pointer to an entry 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_Entry *
Ns_CacheNextEntry(Ns_CacheSearch *search)
{
    Tcl_HashSearch *sPtr = (Tcl_HashSearch *) search;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_NextHashEntry(sPtr);
    if (hPtr == NULL) {
	return NULL;
    }
    return (Ns_Entry *) Tcl_GetHashValue(hPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheFlush --
 *
 *	Flush every entry from a cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheFlush(Ns_Cache *cache)
{
    Ns_CacheSearch search;
    Ns_Entry *entry;

    entry = Ns_CacheFirstEntry(cache, &search);
    while (entry != NULL) {
	Ns_CacheFlushEntry(entry);
	entry = Ns_CacheNextEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheLock --
 *
 *	Lock the cache
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Mutex locked.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheLock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_MutexLock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheTryLock --
 *
 *	Try to lock the cache.
 *
 * Results:
 *      NS_OK if cache is locked, NS_TIMEOUT if not.
 *
 * Side effects:
 *	Mutex may eventually be locked.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CacheTryLock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    return Ns_MutexTryLock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheUnlock --
 *
 *	Unlock the cache entry
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Mutex unlocked.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheUnlock(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;

    Ns_MutexUnlock(&cachePtr->lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheTimedWait --
 *
 *	Wait for the cache's condition variable to be
 *  	signaled or the given absolute timeout if timePtr is not NULL.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread is suspended until condition is signaled or timeout.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CacheTimedWait(Ns_Cache *cache, Ns_Time *timePtr)
{
    Cache *cachePtr = (Cache *) cache;
    
    return Ns_CondTimedWait(&cachePtr->cond, &cachePtr->lock, timePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheWait --
 *
 *	Wait indefinitely for the cache's condition variable to be
 *  	signaled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Thread is suspended until condition is signaled.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheWait(Ns_Cache *cache)
{
    Ns_CacheTimedWait(cache, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheSignal --
 *
 *	Signal the cache's condition variable, waking the first waiting
 *  	thread (if any).
 *
 *  	NOTE:  Be sure you don't really want to wake all threads with
 *  	Ns_CacheBroadcast.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	A single thread may resume.
 *
 *----------------------------------------------------------------------
 */

void
Ns_CacheSignal(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;
    
    Ns_CondSignal(&cachePtr->cond);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_CacheBroadcast --
 *
 *	Broadcast the cache's condition variable, waking all waiting
 *  	threads (if any).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Threads may resume.
 *
 *----------------------------------------------------------------------
 */
    
void
Ns_CacheBroadcast(Ns_Cache *cache)
{
    Cache *cachePtr = (Cache *) cache;
    
    Ns_CondBroadcast(&cachePtr->cond);
}


/*
 *----------------------------------------------------------------------
 *
 * NsCacheArgProc --
 *
 *	Info proc for timed cache schedule procedure callback.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds name of cache to given dstring.
 *
 *----------------------------------------------------------------------
 */

void
NsCacheArgProc(Tcl_DString *dsPtr, void *arg)
{
    Cache *cachePtr = arg;

    Tcl_DStringAppendElement(dsPtr, cachePtr->name);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNamesCmd --
 *
 *	Spit back a list of cache names
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	A list of cache names will be appended to the interp result.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheNamesCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }

    Ns_MutexLock(&lock);
    hPtr = Tcl_FirstHashEntry(&caches, &search);
    while (hPtr != NULL) {
	Tcl_AppendElement(interp, Tcl_GetHashKey(&caches, hPtr));
	hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheStatsCmds --
 *
 *	Returns stats on a cache
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	Results will be appended to interp.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheStatsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Cache *cachePtr;
    char buf[200];
    int entries, flushed, hits, misses, total, hitrate;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache ?arrayVar?\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    Ns_MutexLock(&cachePtr->lock);
    entries = cachePtr->entriesTable.numEntries;
    flushed = cachePtr->nflush;
    hits = cachePtr->nhit;
    misses = cachePtr->nmiss;
    total = cachePtr->nhit + cachePtr->nmiss;
    hitrate = (total ? (cachePtr->nhit * 100) / total : 0);
    Ns_MutexUnlock(&cachePtr->lock);

    if (argc == 2) {
	sprintf(buf,
	    "entries: %d  flushed: %d  hits: %d  misses: %d  hitrate: %d",
	    entries, flushed, hits, misses, hitrate);
	Tcl_SetResult(interp, buf, TCL_VOLATILE);
    } else {
    	sprintf(buf, "%d", entries);
    	if (Tcl_SetVar2(interp, argv[2], "entries", buf,
			TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
        }
    	sprintf(buf, "%d", flushed);
    	if (Tcl_SetVar2(interp, argv[2], "flushed", buf,
			TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
        }
    	sprintf(buf, "%d", hits);
    	if (Tcl_SetVar2(interp, argv[2], "hits", buf,
			TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
        }
    	sprintf(buf, "%d", misses);
    	if (Tcl_SetVar2(interp, argv[2], "misses", buf,
			TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
        }
    	sprintf(buf, "%d", hitrate);
    	if (Tcl_SetVar2(interp, argv[2], "hitrate", buf,
			TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
        }
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheFlushCmd --
 *
 *	Wipe out a cache entry
 *
 * Results:
 *	TCL result.
 *
 * Side effects:
 *	A cache will be cleared.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheFlushCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Cache *cachePtr;
    Ns_Cache *cache;
    Ns_Entry *entry;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache ?key?\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (argc > 2 && cachePtr->keys != TCL_STRING_KEYS) {
	Tcl_AppendResult(interp, "cache keys not strings: ",
	    argv[1], NULL);
	return TCL_ERROR;
    }
    cache = (Ns_Cache *) cachePtr;
    Ns_CacheLock(cache);
    if (argc == 2) {
	Ns_CacheFlush(cache);
    } else {
    	entry = Ns_CacheFindEntry(cache, argv[2]);
    	if (entry == NULL) {
    	    Tcl_SetResult(interp, "0", TCL_STATIC);
	} else {
    	    Tcl_SetResult(interp, "1", TCL_STATIC);
	    Ns_CacheFlushEntry(entry);
    	}
    }
    Ns_CacheUnlock(cache);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheSizeCmd --
 *
 *	Returns current size of a cache.
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	Results will be appended to interp.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCacheSizeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Cache *cachePtr;
    size_t maxSize, currentSize;
    char buf[200];
    
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache\"", NULL);
	return TCL_ERROR;
    }
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
    	return TCL_ERROR;
    }
    Ns_MutexLock(&cachePtr->lock);
    maxSize = cachePtr->maxSize;
    currentSize = cachePtr->currentSize;
    Ns_MutexUnlock(&cachePtr->lock);
    sprintf(buf, "%ld %ld", (long) maxSize, (long) currentSize);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCacheKeysCmd --
 *
 *	Get cache keys.
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
NsTclCacheKeysCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_Cache *cache;
    Cache *cachePtr;
    Ns_Entry *entry;
    Ns_CacheSearch search;
    char *pattern, *key, *fmt, onebuf[20];
    int i, *iPtr;
    Ns_DString ds;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " cache ?pattern?\"", NULL);
	return TCL_ERROR;
    }
    pattern = argv[2];
    if (GetCache(interp, argv[1], &cachePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    cache = (Ns_Cache *) cachePtr;
    Ns_CacheLock(cache);
    entry = Ns_CacheFirstEntry(cache, &search);
    while (entry != NULL) {
	key = Ns_CacheKey(entry);
	if (cachePtr->keys == TCL_ONE_WORD_KEYS) {
	    sprintf(onebuf, "%p", key);
	    key = onebuf;
	} else if (cachePtr->keys != TCL_STRING_KEYS) {
	    iPtr = (int *) key;
	    fmt = "%u";
	    Ns_DStringTrunc(&ds, 0);
	    for (i = 0; i < cachePtr->keys; ++i) {
		Ns_DStringPrintf(&ds, fmt, *iPtr);
		++iPtr;
		fmt = ".%u";
	    }
	    key = ds.string;
	}
	if (pattern == NULL || Tcl_StringMatch(key, pattern)) {
	    Tcl_AppendElement(interp, key);
	}
	entry = Ns_CacheNextEntry(&search);
    }
    Ns_CacheUnlock(cache);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CacheCreate --
 *
 *	Create a new time or size based cache.
 *
 * Results:
 *	A pointer to the new cache.
 *
 * Side effects:
 *	Hash table is allocated, new pool created. A scheduled proc is
 *      created that will run every timeout seconds to flush the cache
 *  	if timeout is greater than zero.
 *
 *----------------------------------------------------------------------
 */

static Ns_Cache *
CacheCreate(char *name, int keys, time_t timeout, size_t maxSize,
	    Ns_Callback *freeProc)
{
    Cache *cachePtr;
    int new;

    cachePtr = ns_calloc(1, sizeof(Cache) + strlen(name));
    cachePtr->freeProc = freeProc;
    cachePtr->timeout = timeout;
    cachePtr->maxSize = maxSize;
    cachePtr->currentSize = 0;
    cachePtr->keys = keys;
    strcpy(cachePtr->name, name);
    cachePtr->nflush = cachePtr->nhit = cachePtr->nmiss = 0;
    Ns_MutexSetName2(&cachePtr->lock, "ns:cache", name);
    Tcl_InitHashTable(&cachePtr->entriesTable, keys);
    if (timeout > 0) {
    	cachePtr->schedId = Ns_ScheduleProc(NsCachePurge, cachePtr, 0, timeout);
    } else {
    	cachePtr->schedId = -1;
    }
    cachePtr->schedStop = 0;
    Ns_MutexLock(&lock);
    cachePtr->hPtr = Tcl_CreateHashEntry(&caches, name, &new);
    if (!new) {
	Cache *prevPtr;

	Ns_Log(Warning, "cache: duplicate cache name: %s", name);
	prevPtr = Tcl_GetHashValue(cachePtr->hPtr);
	prevPtr->hPtr = NULL;
    }
    Tcl_SetHashValue(cachePtr->hPtr, cachePtr);
    Ns_MutexUnlock(&lock);
    return (Ns_Cache *) cachePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Delink --
 *
 *	Remove a cache entry from the linked list of entries; this
 *      is used for maintaining the LRU list as well as removing entries
 *      that are still in use
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The linked list will be changed.
 *
 *----------------------------------------------------------------------
 */
 
static void
Delink(Entry *ePtr)
{
    if (ePtr->prevPtr != NULL) {
	ePtr->prevPtr->nextPtr = ePtr->nextPtr;
    } else {
	ePtr->cachePtr->firstEntryPtr = ePtr->nextPtr;
    }
    if (ePtr->nextPtr != NULL) {
	ePtr->nextPtr->prevPtr = ePtr->prevPtr;
    } else {
	ePtr->cachePtr->lastEntryPtr = ePtr->prevPtr;
    }
    ePtr->prevPtr = ePtr->nextPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Push --
 *
 *	Stick an entry at the top of the linked list of entries, making
 *      it the Most Recently Used
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The linked list will be changed and the mtime time will be
 *	updated for time-based caches.
 *
 *----------------------------------------------------------------------
 */

static void
Push(Entry *ePtr)
{
    if (ePtr->cachePtr->timeout > 0) {
	Ns_GetTime(&ePtr->mtime);
    }
    if (ePtr->cachePtr->firstEntryPtr != NULL) {
	ePtr->cachePtr->firstEntryPtr->prevPtr = ePtr;
    }
    ePtr->prevPtr = NULL;
    ePtr->nextPtr = ePtr->cachePtr->firstEntryPtr;
    ePtr->cachePtr->firstEntryPtr = ePtr;
    if (ePtr->cachePtr->lastEntryPtr == NULL) {
	ePtr->cachePtr->lastEntryPtr = ePtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetCache --
 *
 *	Get a cache by name
 *
 * Results:
 *	Tcl result.
 *
 * Side effects:
 *	*cachePtrPtr will contain a pointer to the cache if TCL_OK is
 *      returned.
 *
 *----------------------------------------------------------------------
 */

static int
GetCache(Tcl_Interp *interp, char *name, Cache **cachePtrPtr)
{
    *cachePtrPtr = (Cache *) Ns_CacheFind(name);
    if (*cachePtrPtr == NULL) {
	Tcl_AppendResult(interp, "no such cache: ", name, NULL);
	return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsCachePurge --
 *
 *	Call free procs for all entries that have expired and
 *  	delete them.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Expired entries will be removed.
 *
 *----------------------------------------------------------------------
 */

void
NsCachePurge(void *arg)
{
    Entry *ePtr;
    Cache *cachePtr = (Cache *) arg;
    Ns_Time expired;

    Ns_MutexLock(&cachePtr->lock);
    if (cachePtr->schedStop) {
	cachePtr->schedId = -1;
	Ns_CondBroadcast(&cachePtr->cond);
    } else {
	Ns_GetTime(&expired);
	Ns_IncrTime(&expired, -cachePtr->timeout, 0);
	while ((ePtr = cachePtr->lastEntryPtr) != NULL) {
	    if (ePtr->mtime.sec > expired.sec) {
		break;
	    }
	    if (ePtr->mtime.sec == expired.sec
		    && ePtr->mtime.usec > expired.usec) {
		break;
	    }
	    Ns_CacheFlushEntry((Ns_Entry *) ePtr);
    	}
    }
    Ns_MutexUnlock(&cachePtr->lock);
}

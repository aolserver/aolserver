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
 * dbinit.c --
 *
 *	This file contains routines for creating and accessing
 *	pools of database handles.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/dbinit.c,v 1.4 2000/08/17 06:09:49 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define CONFIG_USER     "user"  	/* Database login user */
#define CONFIG_PASS     "password"      /* Database login passowrd */
#define CONFIG_SOURCE   "datasource"    /* Database location string */
#define CONFIG_VERBOSE  "verbose"       /* Log SQL statements and errors */
#define CONFIG_VERBOSE_ERROR "logsqlerrors" /* Log SQL errors only */
#define CONFIG_CONNS    "connections"   /* Number of database connections. */

/*
 * The following structure defines a database pool.
 */

struct Handle;

typedef struct Pool {
    char           *name;
    char           *desc;
    char           *source;
    char           *user;
    char           *pass;
    int             type;
    Ns_Tls	    ngotTls;
    Ns_Mutex	    lock;
    Ns_Cond	    waitCond;
    Ns_Cond	    getCond;
    char	   *driver;
    struct DbDriver  *driverPtr;
    int		    waiting;
    int             nhandles;
    struct Handle  *firstPtr;
    struct Handle  *lastPtr;
    int             fVerbose;
    int             fVerboseError;
    time_t          tMaxIdle;
    time_t          tMaxOpen;
    int             stale_on_close;
}               Pool;

/*
 * The following structure defines the internal
 * state of a database handle.
 */

typedef struct Handle {
    char           *driver;
    char           *datasource;
    char           *user;
    char           *password;
    void           *connection;
    char           *poolname;
    int             connected;
    int             verbose;
    Ns_Set         *row;
    char            cExceptionCode[6];
    Ns_DString      dsExceptionMsg;
    void           *context;
    void           *statement;
    int             fetchingRows;
    /* Members above must match Ns_DbHandle */
    struct Handle  *nextPtr;
    struct Pool	   *poolPtr;
    time_t          tOpen;
    time_t          tAccess;
    int             stale;
    int             stale_on_close;
}               Handle;

/*
 * Local functions defined in this file
 */

static Pool    *GetPool(char *pool);
static void     ReturnHandle(Handle * handle);
static void	CheckPool(Pool *poolPtr);
static Ns_Callback CheckPools;
static int      IsStale(Handle *);
static int	Connect(Handle *);
static Pool    *CreatePool(char *pool, char *path, char *driver);

/*
 * Static variables defined in this file
 */

static Tcl_HashTable poolsTable;
static char    *defaultPool;
static char    *allowedPools;



/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolDescription --
 *
 *	Return the pool's description string.
 *
 * Results:
 *	Configured description string or NULL.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DbPoolDescription(char *pool)
{
    Pool         *poolPtr;

    poolPtr = GetPool(pool);
    if (poolPtr == NULL) {
        return NULL;
    }

    return poolPtr->desc;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolDefault --
 *
 *	Return the default pool.
 *
 * Results:
 *	String name of default pool or NULL if no default is defined.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DbPoolDefault(char *hServer)
{
    return defaultPool;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolList --
 *
 *	Return the list of all pools.
 *
 * Results:
 *	Double-null terminated list of pool names.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DbPoolList(char *hServer)
{
    return allowedPools;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolAllowable --
 *
 *	Check that access is allowed to a pool.
 *
 * Results:
 *	NS_TRUE if allowed, NS_FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbPoolAllowable(char *hServer, char *pool)
{
    register char *p;

    p = allowedPools;
    if (p != NULL) {
        while (*p != '\0') {
            if (STREQ(pool, p)) {
                return NS_TRUE;
            }
            p = p + strlen(p) + 1;
        }
    }

    return NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolPutHandle --
 *
 *	Cleanup and then return a handle to its pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Handle is flushed, reset, and possibly closed as required.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DbPoolPutHandle(Ns_DbHandle *handle)
{
    Handle	*handlePtr;
    Pool	*poolPtr;
    int		 ngot;

    handlePtr = (Handle *) handle;
    poolPtr = handlePtr->poolPtr;

    /*
     * Cleanup the handle.
     */

    Ns_DbFlush(handle);
    Ns_DbResetHandle(handle);

    Ns_DStringFree(&handle->dsExceptionMsg);
    handle->cExceptionCode[0] = '\0';

    /*
     * Close the handle if it's stale, otherwise update
     * the last access time.
     */

    if (IsStale(handlePtr)) {
        NsDbDisconnect(handle);
    } else {
        time(&handlePtr->tAccess);
    }

    Ns_MutexLock(&poolPtr->lock);
    ReturnHandle(handlePtr);
    if (poolPtr->waiting) {
	Ns_CondSignal(&poolPtr->getCond);
    }
    Ns_MutexUnlock(&poolPtr->lock);

    ngot = (int) Ns_TlsGet(&poolPtr->ngotTls);
    --ngot;
    Ns_TlsSet(&poolPtr->ngotTls, (void *) ngot);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolTimedGetHandle --
 *
 *	Return a single handle from a pool within the given number of
 *	seconds.
 *
 * Results:
 *	Pointer to Ns_DbHandle or NULL on error or timeout.
 *
 * Side effects:
 *	Database may be opened if needed.
 *
 *----------------------------------------------------------------------
 */

Ns_DbHandle *
Ns_DbPoolTimedGetHandle(char *pool, int wait)
{
    Ns_DbHandle       *handle;

    if (Ns_DbPoolTimedGetMultipleHandles(&handle, pool, 1, wait) != NS_OK) {
        return NULL;
    }

    return handle;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolGetHandle --
 *
 *	Return a single handle from a pool.
 *
 * Results:
 *	Pointer to Ns_DbHandle or NULL on error.
 *
 * Side effects:
 *	Database may be opened if needed.
 *
 *----------------------------------------------------------------------
 */

Ns_DbHandle *
Ns_DbPoolGetHandle(char *pool)
{
    return Ns_DbPoolTimedGetHandle(pool, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolGetMultipleHandles --
 *
 *	Return 1 or more handles from a pool.
 *
 * Results:
 *	NS_OK if handles were allocated, NS_ERROR otherwise.
 *
 * Side effects:
 *	Given array of handles is updated with pointers to allocated
 *	handles.  Also, database may be opened if needed.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbPoolGetMultipleHandles(Ns_DbHandle **handles, char *pool, int nwant)
{
    return Ns_DbPoolTimedGetMultipleHandles(handles, pool, nwant, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbPoolTimedGetMultipleHandles --
 *
 *	Return 1 or more handles from a pool within the given number
 *	of seconds.
 *
 * Results:
 *	NS_OK if the handlers where allocated, NS_TIMEOUT if the
 *	thread could not wait long enough for the handles, NS_ERROR
 *	otherwise.
 *
 * Side effects:
 *	Given array of handles is updated with pointers to allocated
 *	handles.  Also, database may be opened if needed.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbPoolTimedGetMultipleHandles(Ns_DbHandle **handles, char *pool, 
    				 int nwant, int wait)
{
    Handle    *handlePtr;
    Handle   **handlesPtrPtr = (Handle **) handles;
    Pool      *poolPtr;
    Ns_Time    timeout, *timePtr;
    int        i, ngot, status;

    /*
     * Verify the pool, the number of available handles in the pool,
     * and that the calling thread does not already own handles from
     * this pool.
     */
     
    poolPtr = GetPool(pool);
    if (poolPtr == NULL) {
	Ns_Log(Error, "dbinit: no such pool '%s'", pool);
	return NS_ERROR;
    }
    if (poolPtr->nhandles < nwant) {
	Ns_Log(Error, "dbinit: "
	       "failed to get %d handles from a db pool of only %d handles: '%s'",
	       nwant, poolPtr->nhandles, pool);
	return NS_ERROR;
    }
    ngot = (int) Ns_TlsGet(&poolPtr->ngotTls);
    if (ngot > 0) {
	Ns_Log(Error, "dbinit: db handle limit exceeded: "
	       "thread already owns %d handles from pool '%s'", pool);
	return NS_ERROR;
    }
    
    /*
     * Wait until this thread can be the exclusive thread aquireing
     * handles and then wait until all requested handles are available,
     * watching for timeout in either of these waits.
     */
     
    if (wait <= 0) {
	timePtr = NULL;
    } else {
    	Ns_GetTime(&timeout);
    	Ns_IncrTime(&timeout, wait, 0);
	timePtr = &timeout;
    }
    status = NS_OK;
    Ns_MutexLock(&poolPtr->lock);
    while (status == NS_OK && poolPtr->waiting) {
	status = Ns_CondTimedWait(&poolPtr->waitCond, &poolPtr->lock, timePtr);
    }
    if (status == NS_OK) {
    	poolPtr->waiting = 1;
    	while (status == NS_OK && ngot < nwant) {
	    while (status == NS_OK && poolPtr->firstPtr == NULL) {
	    	status = Ns_CondTimedWait(&poolPtr->getCond, &poolPtr->lock,
					  timePtr);
	    }
	    if (poolPtr->firstPtr != NULL) {
		handlePtr = poolPtr->firstPtr;
		poolPtr->firstPtr = handlePtr->nextPtr;
		handlePtr->nextPtr = NULL;
		if (poolPtr->lastPtr == handlePtr) {
		    poolPtr->lastPtr = NULL;
		}
		handlesPtrPtr[ngot++] = handlePtr;
	    }
	}
	poolPtr->waiting = 0;
    	Ns_CondSignal(&poolPtr->waitCond);
    }
    Ns_MutexUnlock(&poolPtr->lock);

    /*
     * Handle special race condition where the final requested handle
     * arrived just as the condition wait was timing out.
     */

    if (status == NS_TIMEOUT && ngot == nwant) {
	status = NS_OK;
    }

    /*
     * If status is still ok, connect any handles not already connected,
     * otherwise return any allocated handles back to the pool, then
     * update the final number of handles owned by this thread.
     */

    for (i = 0; status == NS_OK && i < ngot; ++i) {
	handlePtr = handlesPtrPtr[i];
	if (handlePtr->connected == NS_FALSE) {
	    status = Connect(handlePtr);
	}
    }
    if (status != NS_OK) {
	Ns_MutexLock(&poolPtr->lock);
	while (ngot > 0) {
	    ReturnHandle(handlesPtrPtr[--ngot]);
	}
	if (poolPtr->waiting) {
	    Ns_CondSignal(&poolPtr->getCond);
	}
	Ns_MutexUnlock(&poolPtr->lock);
    }
    Ns_TlsSet(&poolPtr->ngotTls, (void *) ngot);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbBouncePool --
 *
 *	Close all handles in the pool.
 *
 * Results:
 *	NS_OK if pool was bounce, NS_ERROR otherwise.
 *
 * Side effects:
 *	Handles are all marked stale and then closed by CheckPool.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbBouncePool(char *pool)
{
    Pool	*poolPtr;
    Handle	*handlePtr;
    
    poolPtr = GetPool(pool);
    if (poolPtr == NULL) {
	return NS_ERROR;
    }
    Ns_MutexLock(&poolPtr->lock);
    poolPtr->stale_on_close++;
    handlePtr = poolPtr->firstPtr;
    while (handlePtr != NULL) {
	if (handlePtr->connected) {
	    handlePtr->stale = 1;
	}
	handlePtr->stale_on_close = poolPtr->stale_on_close;
	handlePtr = handlePtr->nextPtr;
    }
    Ns_MutexUnlock(&poolPtr->lock);
    CheckPool(poolPtr);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbInit --
 *
 *	Initialize the database pools at startup.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Pools may be created as configured.
 *
 *----------------------------------------------------------------------
 */

void
NsDbInit(void)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;
    Pool           *poolPtr;
    Ns_Set         *pools;
    Ns_DString	    ds;
    char           *path, *allowed, *pool, *driver;
    register char  *p;
    int		    new, i, tcheck;

    Ns_DStringInit(&ds);
    Tcl_InitHashTable(&poolsTable, TCL_STRING_KEYS);

    /*
     * Add the allowed pools to the poolsTable.
     */

    path = Ns_ConfigGetPath(nsServer, NULL, "db", NULL);
    allowed = Ns_ConfigGet(path, "pools");
    defaultPool = Ns_ConfigGet(path, "defaultpool");

    pools = Ns_ConfigSection("ns/db/pools");
    if (pools != NULL && allowed != NULL) {
	if (STREQ(allowed, "*")) {
	    for (i = 0; i < Ns_SetSize(pools); ++i) {
		pool = Ns_SetKey(pools, i);
		Tcl_CreateHashEntry(&poolsTable, pool, &new);
	    }
	} else {
	    p = allowed;
	    while (p != NULL && *p != '\0') {
		p = strchr(allowed, ',');
		if (p != NULL) {
		    *p = '\0';
		}
		Tcl_CreateHashEntry(&poolsTable, allowed, &new);
		if (p != NULL) {
		    *p++ = ',';
		}
		allowed = p;
	    }
	}
    }

    /*
     * Attempt to create a database pool for each entry in the poolsTable.
     */

    hPtr = Tcl_FirstHashEntry(&poolsTable, &search);
    while (hPtr != NULL) {
	pool = Tcl_GetHashKey(&poolsTable, hPtr);
	path = Ns_ConfigGetPath(NULL, NULL, "db", "pool", pool, NULL);
	driver = Ns_ConfigGet(path, "driver");
	poolPtr = NULL;
	if (driver == NULL) {
	    Ns_Log(Error, "dbinit: no driver defined for pool '%s'", pool);
	} else {
	    poolPtr = CreatePool(pool, path, driver);
	}
	if (poolPtr != NULL) {
	    Tcl_SetHashValue(hPtr, poolPtr);
	} else {
	    Tcl_DeleteHashEntry(hPtr);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }

    /*
     * Verify the default pool exists, if any.
     */

    if (defaultPool != NULL) {
    	hPtr = Tcl_FindHashEntry(&poolsTable, defaultPool);
    	if (hPtr == NULL) {
	    Ns_Log(Error, "dbinit: no such default pool '%s'", defaultPool);
	    defaultPool = NULL;
    	}
    }

    /*
     * Construct the allowedPools list and initialize the nsdb Tcl
     * commands if any pools were actually created.
     */

    if (poolsTable.numEntries == 0) {
	Ns_Log(Debug, "dbinit: no configured pools");
	allowedPools = "";
    } else {
	tcheck = INT_MAX;
    	Ns_DStringInit(&ds);
    	hPtr = Tcl_FirstHashEntry(&poolsTable, &search);
    	while (hPtr != NULL) {
	    poolPtr = Tcl_GetHashValue(hPtr);
	    if (tcheck > poolPtr->tMaxIdle) {
		tcheck = poolPtr->tMaxIdle;
	    }
	    NsDbServerInit(poolPtr->driverPtr);
	    Ns_DStringAppendArg(&ds, poolPtr->name);
	    hPtr = Tcl_NextHashEntry(&search);
    	}
    	allowedPools = ns_malloc(ds.length + 1);
    	memcpy(allowedPools, ds.string, ds.length + 1);
    	Ns_DStringFree(&ds);
	NsDbTclInit();
	if (tcheck > 0) {
	    Ns_ScheduleProc(CheckPools, NULL, 1, tcheck);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbDisconnect --
 *
 *	Disconnect a handle by closing the database if needed.
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
NsDbDisconnect(Ns_DbHandle *handle)
{
    Handle *handlePtr = (Handle *) handle;

    NsDbClose(handle);
    handlePtr->connected = NS_FALSE;
    handlePtr->tAccess = handlePtr->tOpen = 0;
    handlePtr->stale = NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbLogSql --
 *
 *	Log a SQL statement depending on the verbose state of the
 *	handle.
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
NsDbLogSql(Ns_DbHandle *handle, char *sql)
{
    Handle *handlePtr = (Handle *) handle;

    if (handle->dsExceptionMsg.length > 0) {
        if (handlePtr->poolPtr->fVerboseError || handle->verbose) {
	    
            Ns_Log(Error, "dbinit: error(%s,%s): '%s'",
		   handle->datasource, handle->dsExceptionMsg.string, sql);
        }
    } else if (handle->verbose) {
        Ns_Log(Notice, "dbinit: sql(%s): '%s'", handle->datasource, sql);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbGetDriver --
 *
 *	Return a pointer to the driver structure for a handle.
 *
 * Results:
 *	Pointer to driver or NULL on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

struct DbDriver *
NsDbGetDriver(Ns_DbHandle *handle)
{
    Handle *handlePtr = (Handle *) handle;

    if (handlePtr != NULL && handlePtr->poolPtr != NULL) {
	return handlePtr->poolPtr->driverPtr;
    }

    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * GetPool --
 *
 *	Return the Pool structure for the given pool name.
 *
 * Results:
 *	Pointer to Pool structure or NULL if pool does not exist.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Pool *
GetPool(char *pool)
{
    Tcl_HashEntry   *hPtr;

    hPtr = Tcl_FindHashEntry(&poolsTable, pool);
    if (hPtr == NULL) {
	return NULL;
    }

    return (Pool *) Tcl_GetHashValue(hPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnHandle --
 *
 *	Return a handle to its pool.  Connected handles are pushed on
 *	the front of the list, disconnected handles are appened to
 *	the end.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Handle is returned to the pool.  Note:  The pool lock must be
 *	held by the caller and this function does not signal a thread
 *	waiting for handles.
 *
 *----------------------------------------------------------------------
 */

static void
ReturnHandle(Handle *handlePtr)
{
    Pool         *poolPtr;

    poolPtr = handlePtr->poolPtr;
    if (poolPtr->firstPtr == NULL) {
	poolPtr->firstPtr = poolPtr->lastPtr = handlePtr;
    	handlePtr->nextPtr = NULL;
    } else if (handlePtr->connected) {
	handlePtr->nextPtr = poolPtr->firstPtr;
	poolPtr->firstPtr = handlePtr;
    } else {
	poolPtr->lastPtr->nextPtr = handlePtr;
	poolPtr->lastPtr = handlePtr;
    	handlePtr->nextPtr = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * IsStale --
 *
 *	Check to see if a handle is stale.
 *
 * Results:
 *	NS_TRUE if handle stale, NS_FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
IsStale(Handle *handlePtr)
{
    time_t    now, minAccess, minOpen;
    
    if (handlePtr->connected) {
	time(&now);
	minAccess = now - handlePtr->poolPtr->tMaxIdle;
	minOpen = now - handlePtr->poolPtr->tMaxOpen;
	if ((handlePtr->poolPtr->tMaxIdle && handlePtr->tAccess < minAccess) || 
	    (handlePtr->poolPtr->tMaxOpen && (handlePtr->tOpen < minOpen)) ||
	    (handlePtr->stale == NS_TRUE) ||
	    (handlePtr->poolPtr->stale_on_close > handlePtr->stale_on_close)) {

	    if (handlePtr->poolPtr->fVerbose) {
		Ns_Log(Notice, "dbinit: closing %s handle in pool '%s'",
		       handlePtr->tAccess < minAccess ? "idle" : "old",
		       handlePtr->poolname);
	    }
	    return NS_TRUE;
	}
    }

    return NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * CheckPools --
 *
 *	Schedule procedure to check all pools.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CheckPools(void *ignored)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Pool *poolPtr;

    hPtr = Tcl_FirstHashEntry(&poolsTable, &search);
    while (hPtr != NULL) {
	poolPtr = Tcl_GetHashValue(hPtr);
	CheckPool(poolPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CheckPool --
 *
 *	Verify all handles in a pool are not stale.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stale handles, if any, are closed.
 *
 *----------------------------------------------------------------------
 */

static void
CheckPool(Pool *poolPtr)
{
    Handle       *handlePtr, *nextPtr;
    Handle       *checkedPtr;

    checkedPtr = NULL;

    /*
     * Grab the entire list of handles from the pool.
     */

    Ns_MutexLock(&poolPtr->lock);
    handlePtr = poolPtr->firstPtr;
    poolPtr->firstPtr = poolPtr->lastPtr = NULL;
    Ns_MutexUnlock(&poolPtr->lock);

    /*
     * Run through the list of handles, closing any
     * which have gone stale, and then return them
     * all to the pool.
     */

    if (handlePtr != NULL) {
    	while (handlePtr != NULL) {
	    nextPtr = handlePtr->nextPtr;
	    if (IsStale(handlePtr)) {
                NsDbDisconnect((Ns_DbHandle *) handlePtr);
	    }
	    handlePtr->nextPtr = checkedPtr;
	    checkedPtr = handlePtr;
	    handlePtr = nextPtr;
    	}

	Ns_MutexLock(&poolPtr->lock);
	handlePtr = checkedPtr;
	while (handlePtr != NULL) {
	    nextPtr = handlePtr->nextPtr;
	    ReturnHandle(handlePtr);
	    handlePtr = nextPtr;
	}
	if (poolPtr->waiting) {
	    Ns_CondSignal(&poolPtr->getCond);
	}
	Ns_MutexUnlock(&poolPtr->lock);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CreatePool --
 *
 *	Create a new pool using the given driver.
 *
 * Results:
 *	Pointer to newly allocated Pool structure.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Pool  *
CreatePool(char *pool, char *path, char *driver)
{
    Pool            *poolPtr;
    Handle          *handlePtr;
    struct DbDriver *driverPtr;
    int              i;
    char	    *source;

    driverPtr = NsDbLoadDriver(driver);
    if (driverPtr == NULL) {
	return NULL;
    }
    source = Ns_ConfigGet(path, CONFIG_SOURCE);
    if (source == NULL) {
	Ns_Log(Error, "dbinit: required datasource missing for pool '%s'",
	       pool);
	return NULL;
    }
    poolPtr = ns_malloc(sizeof(Pool));
    poolPtr->driver = driver;
    poolPtr->driverPtr = driverPtr;
    Ns_MutexInit(&poolPtr->lock);
    Ns_MutexSetName2(&poolPtr->lock, "nsdb", pool);
    Ns_CondInit(&poolPtr->waitCond);
    Ns_CondInit(&poolPtr->getCond);
    Ns_TlsAlloc(&poolPtr->ngotTls, NULL);
    poolPtr->source = source;
    poolPtr->name = pool;
    poolPtr->waiting = 0;
    poolPtr->user = Ns_ConfigGet(path, CONFIG_USER);
    poolPtr->pass = Ns_ConfigGet(path, CONFIG_PASS);
    poolPtr->desc = Ns_ConfigGet("ns/db/pools", pool);
    poolPtr->stale_on_close = 0;
    if (Ns_ConfigGetBool(path, CONFIG_VERBOSE,
			 &poolPtr->fVerbose) == NS_FALSE) {
        poolPtr->fVerbose = 0;
    } 
    if (Ns_ConfigGetBool(path, CONFIG_VERBOSE_ERROR,
			 &poolPtr->fVerboseError) == NS_FALSE) {
	poolPtr->fVerboseError = 0;
    }
    if (Ns_ConfigGetInt(path, CONFIG_CONNS, &poolPtr->nhandles) == NS_FALSE ||
	poolPtr->nhandles <= 0) {

        poolPtr->nhandles = 2;
    }
    if (Ns_ConfigGetInt(path, "MaxIdle", &i) == NS_FALSE || i < 0) {
        i = 600;                    /* 10 minutes */
    }
    poolPtr->tMaxIdle = i;
    if (Ns_ConfigGetInt(path, "MaxOpen", &i) == NS_FALSE || i < 0) {
        i = 3600;                   /* 1 hour */
    }
    poolPtr->tMaxOpen = i;
    poolPtr->firstPtr = poolPtr->lastPtr = NULL;
    for (i = 0; i < poolPtr->nhandles; ++i) {
    	handlePtr = ns_malloc(sizeof(Handle));
    	Ns_DStringInit(&handlePtr->dsExceptionMsg);
    	handlePtr->poolPtr = poolPtr;
    	handlePtr->connection = NULL;
    	handlePtr->connected = NS_FALSE;
    	handlePtr->fetchingRows = 0;
    	handlePtr->row = Ns_SetCreate(NULL);
    	handlePtr->cExceptionCode[0] = '\0';
    	handlePtr->tOpen = handlePtr->tAccess = 0;
    	handlePtr->stale = NS_FALSE;
    	handlePtr->stale_on_close = 0;

	/*
	 * The following elements of the Handle structure could
	 * be obtained by dereferencing the poolPtr.  They're
	 * only needed to maintain the original Ns_DbHandle
	 * structure definition which was designed to allow
	 * handles outside of pools, a feature no longer supported.
	 */

	handlePtr->driver = driver;
	handlePtr->datasource = poolPtr->source;
	handlePtr->user = poolPtr->user;
	handlePtr->password = poolPtr->pass;
	handlePtr->verbose = poolPtr->fVerbose;
	handlePtr->poolname = pool;
	ReturnHandle(handlePtr);
    }

    return poolPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Connect --
 *
 *	Connect a handle by opening the database.
 *
 * Results:
 *	NS_OK if connect ok, NS_ERROR otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
Connect(Handle *handlePtr)
{
    int status;

    status = NsDbOpen((Ns_DbHandle *) handlePtr);
    if (status != NS_OK) {
    	handlePtr->connected = NS_FALSE;
    	handlePtr->tAccess = handlePtr->tOpen = 0;
	handlePtr->stale = NS_FALSE;
    } else {
    	handlePtr->connected = NS_TRUE;
    	handlePtr->tAccess = handlePtr->tOpen = time(NULL);
    }

    return status;
}


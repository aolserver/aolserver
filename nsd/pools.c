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
 * pools.c --
 *
 *  Routines for the managing the connection thread pools.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/pools.c,v 1.6 2004/08/20 23:32:10 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

typedef void (PoolFunc)(Pool *poolPtr, void *arg);

static Pool *CreatePool(char *name);
static PoolFunc StartPool;
static PoolFunc StopPool;
static PoolFunc WaitPool;
static void IteratePools(PoolFunc *func, void *arg);
static int AppendPool(Tcl_Interp *interp, char *key, int val);
static int PoolResult(Tcl_Interp *interp, Pool *poolPtr);
static int GetPool(Tcl_Interp *interp, Tcl_Obj *objPtr, Pool **poolPtrPtr);

/*
 * Static variables defined in this file.
 */

static int            poolid;
static Pool          *defPoolPtr;
static Pool          *errPoolPtr;
static Tcl_HashTable  pools;


/*
 *----------------------------------------------------------------------
 *
 * NsInitPools --
 *
 *  Init thread pools.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */

void
NsInitPools(void)
{
    poolid = Ns_UrlSpecificAlloc();
    Tcl_InitHashTable(&pools, TCL_STRING_KEYS);
    defPoolPtr = CreatePool("default");
    errPoolPtr = CreatePool("error");
}

Pool *
NsGetPool(Conn *connPtr)
{
    Pool *poolPtr;

    if (connPtr->flags & NS_CONN_OVERFLOW) {
	return errPoolPtr;
    }
    poolPtr = Ns_UrlSpecificGet(connPtr->server, connPtr->request->method,
				    connPtr->request->url, poolid);
    if (poolPtr == NULL) {
	return defPoolPtr;
    }
    return poolPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * CreatePool --
 *
 *  Create a new connection pool with default, unlimited values.
 *
 * Results:
 *  Pointer to new pool.
 *
 * Side effects:
 *  Pool is added to pools hash table.
 *
 *----------------------------------------------------------------------
 */

static Pool *
CreatePool(char *name)
{
    Pool *poolPtr;
    Tcl_HashEntry *hPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&pools, name, &new);
    if (!new) {
	poolPtr = Tcl_GetHashValue(hPtr);
    } else {
    	poolPtr = ns_calloc(sizeof(Pool), 1);
    	Ns_MutexInit(&poolPtr->lock);
    	Ns_CondInit(&poolPtr->cond);
    	Tcl_SetHashValue(hPtr, poolPtr);
    	poolPtr->name = Tcl_GetHashKey(&pools, hPtr);
    	poolPtr->threads.min = 0;       
    	poolPtr->threads.max = 10;      
    	poolPtr->threads.timeout = 120; /* NB: Exit after 2 minutes idle. */
    	poolPtr->threads.maxconns = 0;  /* NB: Never exit thread. */
   }
    return poolPtr;
}

void
NsStartPools(void)
{
    IteratePools(StartPool, NULL);
}


void
NsStopPools(Ns_Time *timePtr)
{
    IteratePools(StopPool, NULL);
    IteratePools(WaitPool, timePtr);
}


static void
StartPool(Pool *poolPtr, void *ignored)
{
    int i;

    poolPtr->threads.current = poolPtr->threads.idle = poolPtr->threads.min;
    for (i = 0; i < poolPtr->threads.min; ++i) {
        NsCreateConnThread(poolPtr);
    }
}

static void
StopPool(Pool *poolPtr, void *ignored)
{
    Ns_MutexLock(&poolPtr->lock);
    poolPtr->shutdown = 1;
    Ns_CondBroadcast(&poolPtr->cond);
    Ns_MutexUnlock(&poolPtr->lock);
}

static void
WaitPool(Pool *poolPtr, void *arg)
{
    Ns_Time *timePtr = arg;
    int status;
    
    status = NS_OK;
    Ns_MutexLock(&poolPtr->lock);
    while (status == NS_OK &&
            (poolPtr->queue.wait.firstPtr != NULL ||
             poolPtr->threads.current > 0)) {
        status = Ns_CondTimedWait(&poolPtr->cond, &poolPtr->lock, timePtr);
    }
    if (status != NS_OK) {
        Ns_Log(Warning, "timeout waiting for connection thread exit");
    }
}

static void
IteratePools(PoolFunc *func, void *arg)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FirstHashEntry(&pools, &search);
    while (hPtr != NULL) {
        (*func)(Tcl_GetHashValue(hPtr), arg);
        hPtr = Tcl_NextHashEntry(&search);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclPoolsObjCmd --
 *
 *  Implements ns_pools command. 
 *
 * Results:
 *  Tcl result. 
 *
 * Side effects:
 *  See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclPoolsObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Pool *poolPtr, savedPool;
    char *pool, *pattern;
    int i, val;
    static CONST char *opts[] = {
        "get", "set", "list", "register", NULL
    };
    enum {
        PGetIdx, PSetIdx, PListIdx, PRegisterIdx
    } opt;
    static CONST char *cfgs[] = {
        "-maxthreads", "-minthreads", "-maxconns", "-timeout", NULL
    };
    enum {
        PCMaxThreadsIdx, PCMinThreadsIdx, PCMaxConnsIdx, PCTimeoutIdx
    } cfg;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 1,
                (int *) &opt) != TCL_OK) {
        return 0;
    }
    switch (opt) {
    case PListIdx:
        if (objc != 2 && objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "?pattern?");
            return TCL_ERROR;
        }
        if (objc == 2) {
            pattern = NULL;
        } else {
            pattern = Tcl_GetString(objv[2]);
        }
        hPtr = Tcl_FirstHashEntry(&pools, &search);
        while (hPtr != NULL) {
            pool = Tcl_GetHashKey(&pools, hPtr);
            if (pattern == NULL || Tcl_StringMatch(pool, pattern)) {
                Tcl_AppendElement(interp, pool);
            }
            hPtr = Tcl_NextHashEntry(&search);
        }
        break;

    case PGetIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "pool");
            return TCL_ERROR;
        }
        if (GetPool(interp, objv[2], &poolPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (PoolResult(interp, poolPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case PSetIdx:
        if (objc < 3 || (((objc - 3) % 2) != 0)) {
            Tcl_WrongNumArgs(interp, 2, objv, "limit ?opt val opt val...?");
            return TCL_ERROR;
        }
        pool = Tcl_GetString(objv[2]);
	poolPtr = CreatePool(pool);
        savedPool = *poolPtr;
        for (i = 3; i < objc; i += 2) {
            if (Tcl_GetIndexFromObj(interp, objv[i], cfgs, "cfg", 0,
                        (int *) &cfg) != TCL_OK || 
                    Tcl_GetIntFromObj(interp, objv[i+1], &val) != TCL_OK) {
                *poolPtr = savedPool;
                return TCL_ERROR;
            }
            switch (cfg) {
            case PCMinThreadsIdx:
                poolPtr->threads.min = val;
                break;

            case PCMaxThreadsIdx:
                poolPtr->threads.max = val;
                break;

            case PCTimeoutIdx:
                poolPtr->threads.timeout = val;
                break;

            case PCMaxConnsIdx:
                poolPtr->threads.maxconns = val;
                break;
            }
        }
        if (PoolResult(interp, poolPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case PRegisterIdx:
        if (objc != 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "pool server method url");
            return TCL_ERROR;
        }
        if (GetPool(interp, objv[2], &poolPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_UrlSpecificSet(Tcl_GetString(objv[3]),
                Tcl_GetString(objv[4]),
                Tcl_GetString(objv[5]), poolid, poolPtr, 0, NULL);
        break;
    }

    return TCL_OK;
}


static int
AppendPool(Tcl_Interp *interp, char *key, int val)
{
    Tcl_Obj *result = Tcl_GetObjResult(interp);

    if (Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(key, -1))
            != TCL_OK ||
            Tcl_ListObjAppendElement(interp, result, Tcl_NewIntObj(val))
            != TCL_OK) {
        return 0;
    }
    return 1;
}


static int
PoolResult(Tcl_Interp *interp, Pool *poolPtr)
{

    if (!AppendPool(interp, "minthreads", poolPtr->threads.min) ||
        !AppendPool(interp, "maxthreads", poolPtr->threads.max) ||
        !AppendPool(interp, "idle", poolPtr->threads.idle) ||
        !AppendPool(interp, "current", poolPtr->threads.current) ||
        !AppendPool(interp, "maxconns", poolPtr->threads.maxconns) ||
        !AppendPool(interp, "queued", poolPtr->threads.queued) ||
        !AppendPool(interp, "timeout", poolPtr->threads.timeout)) {
    	return TCL_ERROR;
    }
    return TCL_OK;
}


static int
GetPool(Tcl_Interp *interp, Tcl_Obj *objPtr, Pool **poolPtrPtr)
{
    char *pool = Tcl_GetString(objPtr);
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&pools, pool);
    if (hPtr == NULL) {
    Tcl_AppendResult(interp, "no such pool: ", pool, NULL);
    return TCL_ERROR;
    }
    *poolPtrPtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

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
 * pool.c --
 *
 *	Pool memory allocator.  This allocator does nothing more than
 *  	keep track of the allocations and frees to detect possible
 *  	memory leaks.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/oldpools.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

typedef struct OldPool {
    char name[32];
    int  nalloc;
} OldPool;


Ns_Pool *
Ns_PoolCreate(char *name)
{
    OldPool *poolPtr;
    static Ns_Mutex lock;
    static int npools;
    
    poolPtr = ns_calloc(sizeof(OldPool), 1);
    if (name != NULL) {
	strncpy(poolPtr->name, name, 31);
	poolPtr->name[31] = '\0';
    } else {
    	Ns_MutexLock(&lock);
	sprintf(poolPtr->name, "p%d", npools++);
	Ns_MutexUnlock(&lock);
    }
    return (Ns_Pool *) poolPtr;
}


void
Ns_PoolDestroy(Ns_Pool *pool)
{
    OldPool *poolPtr = (OldPool *) pool;

    if (poolPtr->nalloc != 0) {
	fprintf(stderr, "warning: pool[%s]: %d leaks\n",
		poolPtr->name, poolPtr->nalloc);
    }
    ns_free(poolPtr);
}


void *
Ns_PoolAlloc(Ns_Pool *pool, size_t size)
{
    OldPool *poolPtr = (OldPool *) pool;

    if (poolPtr != NULL) {
    	++poolPtr->nalloc;
    }
    return ns_malloc(size);
}


void
Ns_PoolFree(Ns_Pool *pool, void *ptr)
{   
    OldPool *poolPtr = (OldPool *) pool;

    if (ptr == NULL) {
	return;
    }
    if (poolPtr != NULL) {
    	--poolPtr->nalloc;
    }
    ns_free(ptr);
}


void *
Ns_PoolRealloc(Ns_Pool *pool, void *ptr, size_t size)
{
    if (ptr == NULL) {
	return Ns_PoolAlloc(pool, size);
    } else if (size == 0) {
	Ns_PoolFree(pool, ptr);
	return NULL;
    }
    return ns_realloc(ptr, size);
}


void *
Ns_PoolCalloc(Ns_Pool *pool, size_t nelem, size_t elsize)
{
    size_t size;
    void *ptr;

    size = nelem * elsize;
    ptr = Ns_PoolAlloc(pool, size);
    memset(ptr, 0, size);
    return ptr;
}


char *
Ns_PoolStrDup(Ns_Pool *pool, char *old)
{
    char *new;
    size_t size;

    size = strlen(old) + 1;
    new = Ns_PoolAlloc(pool, size);
    strcpy(new, old);
    return new;
}


char *
Ns_PoolStrCopy(Ns_Pool *pool, char *old)
{
    return (old != NULL ? Ns_PoolStrDup(pool, old) : NULL);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadPool --
 *
 *	Return the memory pool for the calling thread.
 *
 * Results:
 *	Pointer to Ns_Pool.
 *
 * Side effects:
 *	Pool is created if necessary.
 *
 *----------------------------------------------------------------------
 */

Ns_Pool *
Ns_ThreadPool(void)
{
    Thread *thisPtr = NsGetThread();

    if (thisPtr->pool == NULL) {
	char name[20];

	sprintf(name, "t%d", thisPtr->tid);
	thisPtr->pool = Ns_PoolCreate(name);
    }

    return thisPtr->pool;
}

/*
 *==========================================================================
 * Thread allocation routines
 *==========================================================================
 */

void *
Ns_ThreadMalloc(size_t size)
{
    return Ns_PoolAlloc(Ns_ThreadPool(), size);
}    

void *
Ns_ThreadRealloc(void *ptr, size_t size)
{   
    return Ns_PoolRealloc(Ns_ThreadPool(), ptr, size);
}

void
Ns_ThreadFree(void *ptr)
{   
    Ns_PoolFree(Ns_ThreadPool(), ptr);
}

void *
Ns_ThreadCalloc(size_t nelem, size_t elsize)
{
    return Ns_PoolCalloc(Ns_ThreadPool(), nelem, elsize);
}

char *
Ns_ThreadStrDup(char *old)
{
    return Ns_PoolStrDup(Ns_ThreadPool(), old);
}

char *
Ns_ThreadStrCopy(char *old)
{
    return Ns_PoolStrCopy(Ns_ThreadPool(), old);
}

/*
 * Backward compatibility wrapper for Ns_ThreadAlloc().
 */

#undef Ns_ThreadAlloc

void *
Ns_ThreadAlloc(size_t size)
{
    return Ns_ThreadMalloc(size);
}

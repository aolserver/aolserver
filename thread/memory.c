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
 * memory.c --
 *
 *	Memory allocation routine wrappers which abort the process on error.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/memory.c,v 1.4 2000/11/06 17:53:50 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

int nsMemPools = 0; /* Should per-thread memory pools be used? This must
		     * be set before the first ns_malloc in a program. */


/*
 *----------------------------------------------------------------------
 *
 * NsAlloc, NsFree --
 *
 *	Simple allocator for thread objects.  These routines are used
 *	internally in the thread library to avoid any conflict with
 *	the pools.  This should not be a performance issue as allocating
 *	memory for these objects is somewhat rare, normally just at
 *	startup and thread create time.
 *
 * Results:
 *	NsAlloc: Pointer to zero'ed memory.
 *	NsFree: None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsAlloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (ptr == NULL) {
	NsThreadAbort("NsAlloc: could not allocate %d bytes", size);
    }
    memset(ptr, 0, size);
    return ptr;
}

void
NsFree(void *ptr)
{
    free(ptr);
}


/*
 *----------------------------------------------------------------------
 *
 * ns_realloc, ns_malloc, ns_calloc, ns_free, ns_strdup, ns_strcopy --
 *
 *	Memory allocation wrappers which either call the platform
 *	versions or the fast pool allocator for a per-thread pool.
 *
 * Results:
 *	As with system functions.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
ns_realloc(void *ptr, size_t size)
{
    if (ptr == NULL) {
	ptr = ns_malloc(size);
    } else if (nsMemPools) {
	ptr = Ns_ThreadRealloc(ptr, size);
    } else {
	ptr = realloc(ptr, size);
    }	
    if (ptr == NULL) {
	NsThreadAbort("ns_realloc: could not allocate %d bytes", size);
    }
    return ptr;
}

void *
ns_malloc(size_t size)
{
    void *ptr;

    if (nsMemPools) {
	ptr = Ns_ThreadMalloc(size);
    } else {
	ptr = malloc(size);
    }
    if (ptr == NULL) {
	NsThreadAbort("ns_malloc: could not allocate %d bytes", size);
    }
    return ptr;
}

void
ns_free(void *ptr)
{
    if (ptr != NULL) {
	if (nsMemPools) {
	    Ns_ThreadFree(ptr);
	} else {
	    free(ptr);
	}
    }
}

void *
ns_calloc(size_t num, size_t esize)
{
    void *new;
    size_t size;

    size = num * esize;
    new = ns_malloc(size);
    memset(new, 0, size);

    return new;
}

char *
ns_strcopy(char *old)
{
    return (old == NULL ? NULL : ns_strdup(old));
}

char *
ns_strdup(char *old)
{
    char *new;

    new = ns_malloc(strlen(old) + 1);
    strcpy(new, old);

    return new;
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
	thisPtr->pool = Ns_PoolCreate(thisPtr->name);
    }
    return thisPtr->pool;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ThreadMalloc, Ns_ThreadRealloc, Ns_ThreadCalloc, Ns_ThreadFree,
 * Ns_ThreadStrDup, Ns_ThreadStrCopy --
 *
 *	Allocate/free memory from the per-thread pool.
 *
 * Results:
 *	See Ns_Pool routines.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
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
 * Backward compatible wrappers.
 */

#ifdef Ns_Malloc
#undef Ns_Malloc
#endif

void *
Ns_Malloc(size_t size)
{
    return ns_malloc(size);
}

#ifdef Ns_Realloc
#undef Ns_Realloc
#endif

void *
Ns_Realloc(void *ptr, size_t size)
{
    return ns_realloc(ptr, size);
}

#ifdef Ns_Calloc
#undef Ns_Calloc
#endif

void *
Ns_Calloc(size_t nelem, size_t elsize)
{
    return ns_calloc(nelem, elsize);
}

#ifdef Ns_Free
#undef Ns_Free
#endif

void 
Ns_Free(void *ptr)
{
    ns_free(ptr);
}

#ifdef Ns_StrDup
#undef Ns_StrDup
#endif

char *
Ns_StrDup(char *str)
{
    return ns_strdup(str);
}

#ifdef Ns_StrCopy
#undef Ns_StrCopy
#endif

char *
Ns_StrCopy(char *str)
{
    return ns_strcopy(str);
}

#ifdef Ns_ThreadAlloc
#undef Ns_ThreadAlloc
#endif

void *
Ns_ThreadAlloc(size_t size)
{
    return Ns_ThreadMalloc(size);
}

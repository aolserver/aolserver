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
 * tclAlloc.c --
 *
 *	Fast cache memory allocator designed to avoid lock
 *  	contention.  The basic strategy is to allocate memory in
 *  	fixed size blocks from block caches.  
 */

#include "tclInt.h"
#include <pthread.h>
#include <errno.h>

/*
 * If range checking is enabled, an additional byte will be allocated
 * to store the magic number at the end of the requested memory.
 */

#ifndef RCHECK
#ifdef  NDEBUG
#define RCHECK		0
#else
#define RCHECK		1
#endif
#endif

/*
 * The following define the number of Tcl_Obj's to allocate/move
 * at a time and the high water mark to prune a per-thread cache.
 * On a 32 bit system, sizeof(Tcl_Obj) = 24 so 800 * 24 = ~16k.
 */
 
#define NOBJALLOC	 800
#define NOBJHIGH	1200

/*
 * The following defines the number of buckets in the bucket
 * cache and those block sizes from (1<<4) to (1<<(3+NBUCKETS))
 */

#define NBUCKETS	  11
#define MAXALLOC	  16284

/*
 * The following union stores accounting information for
 * each block including two small magic numbers and
 * a bucket number when in use or a next pointer when
 * free.  The original requested size (not including
 * the Block overhead) is also maintained.
 */
 
typedef struct Block {
    union {
    	struct Block *next;	  /* Next in free list. */
    	struct {
	    unsigned char magic1; /* First magic number. */
	    unsigned char bucket; /* Bucket block allocated from. */
	    unsigned char unused; /* Padding. */
	    unsigned char magic2; /* Second magic number. */
        } b_s;
    } b_u;
    size_t b_reqsize;		  /* Requested allocation size. */
} Block;
#define b_next		b_u.next
#define b_bucket	b_u.b_s.bucket
#define b_magic1	b_u.b_s.magic1
#define b_magic2	b_u.b_s.magic2
#define MAGIC		0xef

/*
 * The following structure defines a bucket of blocks with
 * various accouting and statistics information.
 */

typedef struct Bucket {
    Block *firstPtr;
    int nfree;
    int nget;
    int nput;
    int nwait;
    int nlock;
    int nrequest;
} Bucket;

/*
 * The following structure defines a cache of buckets and objs.
 */

typedef struct Cache {
    struct  Cache *nextPtr;
    pthread_t      owner;
    int            busy;
    Tcl_Obj       *firstObjPtr;
    int            nobjs;
    int	           nsysalloc;
    Bucket         buckets[NBUCKETS];
} Cache;

/*
 * The following array specifies various per-bucket 
 * limits and locks.  The values are statically initialized
 * to avoid calculating them repeatedly.
 */

struct binfo {
    size_t blocksize;	/* Bucket blocksize. */
    int maxblocks;	/* Max blocks before move to share. */
    int nmove;		/* Num blocks to move to share. */
    pthread_mutex_t lock; /* Share bucket lock. */
} binfo[NBUCKETS] = {
    {   16, 1024, 512, PTHREAD_MUTEX_INITIALIZER},
    {   32,  512, 256, PTHREAD_MUTEX_INITIALIZER},
    {   64,  256, 128, PTHREAD_MUTEX_INITIALIZER},
    {  128,  128,  64, PTHREAD_MUTEX_INITIALIZER},
    {  256,   64,  32, PTHREAD_MUTEX_INITIALIZER},
    {  512,   32,  16, PTHREAD_MUTEX_INITIALIZER},
    { 1024,   16,   8, PTHREAD_MUTEX_INITIALIZER},
    { 2048,    8,   4, PTHREAD_MUTEX_INITIALIZER},
    { 4096,    4,   2, PTHREAD_MUTEX_INITIALIZER},
    { 8192,    2,   1, PTHREAD_MUTEX_INITIALIZER},
    {16284,    1,   0, PTHREAD_MUTEX_INITIALIZER},
};

/*
 * Static functions defined in this file.
 */

static void LockBucket(Cache *cachePtr, int bucket);
static void UnlockBucket(Cache *cachePtr, int bucket);
static void PutBlocks(Cache *cachePtr, int bucket, int nmove);
static int  GetBlocks(Cache *cachePtr, int bucket);
static Block *Ptr2Block(char *ptr);
static char *Block2Ptr(Block *blockPtr, int bucket, unsigned int reqsize);
static void FreeCache(void *arg);
static void MoveObjs(Cache *fromPtr, Cache *toPtr, int nmove);

/*
 * Local variables defined in this file and initialized at
 * startup.
 */

static int initialized = 0;
static pthread_mutex_t listlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t objlock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t key;
static Cache     sharedCache;
static Cache    *sharedPtr = &sharedCache;
static Cache    *firstCachePtr = &sharedCache;


/*
 *----------------------------------------------------------------------
 *
 *  GetCache ---
 *
 *	Gets per-thread memory cache, allocating it if necessary.
 *
 * Results:
 *	Pointer to cache.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static Cache *
GetCache(void)
{
    Cache *cachePtr;
    int err;

    /*
     * Check for first-time initialization.
     */

    if (!initialized) {
	TclpMasterLock();
	if (!initialized) {
    	    err = pthread_key_create(&key, FreeCache);
    	    if (err != 0) {
		panic("alloc: pthread_key_create: %s", strerror(err));
    	    }
	    initialized = 1;
	}
	TclpMasterUnlock();
    }

    /*
     * Get this thread's cache, allocating if necessary.
     */

    cachePtr = pthread_getspecific(key);
    if (cachePtr == NULL) {
    	cachePtr = calloc(1, sizeof(Cache));
    	if (cachePtr == NULL) {
	    panic("alloc: could not allocate new cache");
    	}
    	pthread_mutex_lock(&listlock);
    	cachePtr->nextPtr = firstCachePtr;
    	firstCachePtr = cachePtr;
    	pthread_mutex_unlock(&listlock);
    	cachePtr->owner = pthread_self();
	err = pthread_setspecific(key, cachePtr);
	if (err != 0) {
	    panic("alloc: pthread_setspecific: %s", strerror(err));
	}
    }
    cachePtr->busy = 1;
    return cachePtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  FreeCache --
 *
 *	Flush and delete a cache, removing from all-caches list.
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
FreeCache(void *arg)
{
    Cache *cachePtr = arg;
    Cache **nextPtrPtr;
    register int   bucket;
    int	err;

    /*
     * Reset the cache and continue to finalize the thread until
     * it appears idle.
     */

    if (cachePtr->busy) {
    	err = pthread_setspecific(key, cachePtr);
        if (err != 0) {
	    panic("alloc: pthread_setspecific: %s", strerror(err));
        }
    	Tcl_FinalizeThread();
	cachePtr->busy = 0;
	return;
    }

    /*
     * Flush blocks.
     */

    for (bucket = 0; bucket < NBUCKETS; ++bucket) {
	if (cachePtr->buckets[bucket].nfree > 0) {
	    PutBlocks(cachePtr, bucket, cachePtr->buckets[bucket].nfree);
	}
    }

    /*
     * Flush objs.
     */

    if (cachePtr->nobjs > 0) {
    	pthread_mutex_lock(&objlock);
    	MoveObjs(cachePtr, sharedPtr, cachePtr->nobjs);
    	pthread_mutex_unlock(&objlock);
    }

    /*
     * Remove from pool list.
     */

    pthread_mutex_lock(&listlock);
    nextPtrPtr = &firstCachePtr;
    while (*nextPtrPtr != cachePtr) {
	nextPtrPtr = &(*nextPtrPtr)->nextPtr;
    }
    *nextPtrPtr = cachePtr->nextPtr;
    cachePtr->nextPtr = NULL;
    pthread_mutex_unlock(&listlock);

    free(cachePtr);
}


/*
 *----------------------------------------------------------------------
 *
 *  TclpAlloc --
 *
 *	Allocate memory.
 *
 * Results:
 *	Pointer to memory just beyond Block pointer.
 *
 * Side effects:
 *	May allocate more blocks for a bucket.
 *
 *----------------------------------------------------------------------
 */

char *
TclpAlloc(unsigned int reqsize)
{
    Cache          *cachePtr = GetCache();
    Block         *blockPtr;
    register int   bucket;
    size_t  	   size;
    
    /*
     * Increment the requested size to include room for 
     * the Block structure.  Call malloc() directly if the
     * required amount is greater than the largest block,
     * otherwise pop the smallest block large enough,
     * allocating more blocks if necessary.
     */

    blockPtr = NULL;     
    size = reqsize + sizeof(Block);
#if RCHECK
    ++size;
#endif
    if (size > MAXALLOC) {
	bucket = NBUCKETS;
    	blockPtr = malloc(size);
	if (blockPtr != NULL) {
	    cachePtr->nsysalloc += reqsize;
	}
    } else {
    	bucket = 0;
    	while (binfo[bucket].blocksize < size) {
    	    ++bucket;
    	}
    	if (cachePtr->buckets[bucket].nfree || GetBlocks(cachePtr, bucket)) {
	    blockPtr = cachePtr->buckets[bucket].firstPtr;
	    cachePtr->buckets[bucket].firstPtr = blockPtr->b_next;
	    --cachePtr->buckets[bucket].nfree;
    	    ++cachePtr->buckets[bucket].nget;
	    cachePtr->buckets[bucket].nrequest += reqsize;
	}
    }
    if (blockPtr == NULL) {
    	return NULL;
    }
    return Block2Ptr(blockPtr, bucket, reqsize);
}


/*
 *----------------------------------------------------------------------
 *
 *  TclpFree --
 *
 *	Return blocks to the thread block cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May move blocks to shared cache.
 *
 *----------------------------------------------------------------------
 */

void
TclpFree(char *ptr)
{
    Cache  *cachePtr = GetCache();
    Block *blockPtr;
    int bucket;
 
    /*
     * Get the block back from the user pointer and
     * call system free directly for large blocks.
     * Otherwise, push the block back on the bucket and
     * move blocks to the shared cache if there are now
     * too many free.
     */

    blockPtr = Ptr2Block(ptr);
    bucket = blockPtr->b_bucket;
    if (bucket == NBUCKETS) {
	cachePtr->nsysalloc -= blockPtr->b_reqsize;
	free(blockPtr);
    } else {
	cachePtr->buckets[bucket].nrequest -= blockPtr->b_reqsize;
	blockPtr->b_next = cachePtr->buckets[bucket].firstPtr;
	cachePtr->buckets[bucket].firstPtr = blockPtr;
	++cachePtr->buckets[bucket].nfree;
	++cachePtr->buckets[bucket].nput;
    	if (cachePtr != sharedPtr &&
	    cachePtr->buckets[bucket].nfree > binfo[bucket].maxblocks) {
	    PutBlocks(cachePtr, bucket, binfo[bucket].nmove);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  TclpRealloc --
 *
 *	Re-allocate memory to a larger or smaller size.
 *
 * Results:
 *	Pointer to memory just beyond Block pointer.
 *
 * Side effects:
 *	Previous memory, if any, may be freed.
 *
 *----------------------------------------------------------------------
 */

char *
TclpRealloc(char *ptr, unsigned int reqsize)
{
    Cache *cachePtr = GetCache();
    Block *blockPtr;
    void *new;
    size_t size, min;
    int bucket;

    /*
     * If the block is not a system block and fits in place,
     * simply return the existing pointer.  Otherwise, if the block
     * is a system block and the new size would also require a system
     * block, call realloc() directly.
     */

    blockPtr = Ptr2Block(ptr);
    size = reqsize + sizeof(Block);
#if RCHECK
    ++size;
#endif
    bucket = blockPtr->b_bucket;
    if (bucket != NBUCKETS) {
	if (bucket > 0) {
	    min = binfo[bucket-1].blocksize;
	} else {
	    min = 0;
	}
	if (size > min && size <= binfo[bucket].blocksize) {
	    cachePtr->buckets[bucket].nrequest -= blockPtr->b_reqsize;
	    cachePtr->buckets[bucket].nrequest += reqsize;
	    return Block2Ptr(blockPtr, bucket, reqsize);
	}
    } else if (size > MAXALLOC) {
	cachePtr->nsysalloc -= blockPtr->b_reqsize;
	cachePtr->nsysalloc += reqsize;
	blockPtr = realloc(blockPtr, size);
	if (blockPtr == NULL) {
	    return NULL;
	}
	return Block2Ptr(blockPtr, NBUCKETS, reqsize);
    }

    /*
     * Finally, perform an expensive malloc/copy/free.
     */

    new = TclpAlloc(reqsize);
    if (new != NULL) {
	if (reqsize > blockPtr->b_reqsize) {
	    reqsize = blockPtr->b_reqsize;
	}
    	memcpy(new, ptr, reqsize);
    	TclpFree(ptr);
    }
    return new;
}


/*
 *----------------------------------------------------------------------
 *
 * TclThreadAllocObj --
 *
 *	Allocate a Tcl_Obj from the per-thread cache.
 *
 * Results:
 *	Pointer to uninitialized Tcl_Obj.
 *
 * Side effects:
 *	May move Tcl_Obj's from shared cached or allocate new Tcl_Obj's
 *  	if list is empty.
 *
 *----------------------------------------------------------------------
 */

Tcl_Obj *
TclThreadAllocObj(void)
{
    register Cache *cachePtr = GetCache();
    register int nmove;
    register Tcl_Obj *objPtr;
    Tcl_Obj *newObjsPtr;

    /*
     * Get this thread's obj list structure and move
     * or allocate new objs if necessary.
     */
     
    if (cachePtr->nobjs == 0) {
    	pthread_mutex_lock(&objlock);
	nmove = sharedPtr->nobjs;
	if (nmove > 0) {
	    if (nmove > NOBJALLOC) {
		nmove = NOBJALLOC;
	    }
	    MoveObjs(sharedPtr, cachePtr, nmove);
	}
    	pthread_mutex_unlock(&objlock);
	if (cachePtr->nobjs == 0) {
	    cachePtr->nobjs = nmove = NOBJALLOC;
	    newObjsPtr = malloc(sizeof(Tcl_Obj) * nmove);
	    if (newObjsPtr == NULL) {
		panic("alloc: could not allocate %d new objects", nmove);
	    }
	    while (--nmove >= 0) {
		objPtr = &newObjsPtr[nmove];
		objPtr->internalRep.otherValuePtr = cachePtr->firstObjPtr;
		cachePtr->firstObjPtr = objPtr;
	    }
	}
    }

    /*
     * Pop the first object.
     */

    objPtr = cachePtr->firstObjPtr;
    cachePtr->firstObjPtr = objPtr->internalRep.otherValuePtr;
    --cachePtr->nobjs;
    return objPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * TclThreadFreeObj --
 *
 *	Return a free Tcl_Obj to the per-thread cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May move free Tcl_Obj's to shared list upon hitting high
 *  	water mark.
 *
 *----------------------------------------------------------------------
 */

void
TclThreadFreeObj(Tcl_Obj *objPtr)
{
    Cache *cachePtr = GetCache();

    /*
     * Get this thread's list and push on the free Tcl_Obj.
     */
     
    objPtr->internalRep.otherValuePtr = cachePtr->firstObjPtr;
    cachePtr->firstObjPtr = objPtr;
    
    /*
     * If the number of free objects has exceeded the high
     * water mark, move some blocks to the shared list.
     */
     
    ++cachePtr->nobjs;
    if (cachePtr->nobjs > NOBJHIGH) {
	pthread_mutex_lock(&objlock);
	MoveObjs(cachePtr, sharedPtr, NOBJALLOC);
	pthread_mutex_unlock(&objlock);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetMemoryInfo --
 *
 *	Return a list-of-lists of memory stats.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	List appended to given dstring.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_GetMemoryInfo(Tcl_DString *dsPtr)
{
    Cache *cachePtr;
    char buf[200];
    int n;

    pthread_mutex_lock(&listlock);
    cachePtr = firstCachePtr;
    while (cachePtr != NULL) {
	Tcl_DStringStartSublist(dsPtr);
	if (cachePtr == sharedPtr) {
    	    Tcl_DStringAppendElement(dsPtr, "shared");
	} else {
	    sprintf(buf, "thread%d", (int) cachePtr->owner);
    	    Tcl_DStringAppendElement(dsPtr, buf);
	}
	for (n = 0; n < NBUCKETS; ++n) {
    	    sprintf(buf, "%d %d %d %d %d %d %d",
		binfo[n].blocksize,
		cachePtr->buckets[n].nfree,
		cachePtr->buckets[n].nget,
		cachePtr->buckets[n].nput,
		cachePtr->buckets[n].nrequest,
		cachePtr->buckets[n].nlock,
		cachePtr->buckets[n].nwait);
	    Tcl_DStringAppendElement(dsPtr, buf);
	}
	Tcl_DStringEndSublist(dsPtr);
	    cachePtr = cachePtr->nextPtr;
    }
    pthread_mutex_unlock(&listlock);
}


/*
 *----------------------------------------------------------------------
 *
 * MoveObjs --
 *
 *	Move Tcl_Obj's between caches.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static void
MoveObjs(Cache *fromPtr, Cache *toPtr, int nmove)
{
    register Tcl_Obj *objPtr;

    toPtr->nobjs += nmove;
    fromPtr->nobjs -= nmove;
    while (--nmove >= 0) {
	objPtr = fromPtr->firstObjPtr;
	fromPtr->firstObjPtr = objPtr->internalRep.otherValuePtr;
	objPtr->internalRep.otherValuePtr = toPtr->firstObjPtr;
	toPtr->firstObjPtr = objPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  Block2Ptr, Ptr2Block --
 *
 *	Convert between internal blocks and user pointers.
 *
 * Results:
 *	User pointer or internal block.
 *
 * Side effects:
 *	Invalid blocks will abort the server.
 *
 *----------------------------------------------------------------------
 */

static char *
Block2Ptr(Block *blockPtr, int bucket, unsigned int reqsize) 
{
    register void *ptr;

    blockPtr->b_magic1 = blockPtr->b_magic2 = MAGIC;
    blockPtr->b_bucket = bucket;
    blockPtr->b_reqsize = reqsize;
    ptr = ((void *) (blockPtr + 1));
#if RCHECK
    ((unsigned char *)(ptr))[reqsize] = MAGIC;
#endif
    return (char *) ptr;
}

static Block *
Ptr2Block(char *ptr)
{
    register Block *blockPtr;

    blockPtr = (((Block *) ptr) - 1);
    if (blockPtr->b_magic1 != MAGIC
#if RCHECK
	|| ((unsigned char *) ptr)[blockPtr->b_reqsize] != MAGIC
#endif
	|| blockPtr->b_magic2 != MAGIC) {
	panic("alloc: invalid block: %p: %x %x %x\n",
	    blockPtr, blockPtr->b_magic1, blockPtr->b_magic2,
	    ((unsigned char *) ptr)[blockPtr->b_reqsize]);
    }
    return blockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  LockBucket, UnlockBucket --
 *
 *	Set/unset the lock to access a bucket in the shared cache.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *	Lock activity and contention are monitored globally and on
 *  	a per-cache basis.
 *
 *----------------------------------------------------------------------
 */

static void
LockBucket(Cache *cachePtr, int bucket)
{
    int err;

    err = pthread_mutex_trylock(&binfo[bucket].lock);
    if (err == EBUSY) {
	err = pthread_mutex_lock(&binfo[bucket].lock);
	if (err != 0) {
	    panic("alloc: pthread_mutex_lock: %s", strerror(err));
	}
	++cachePtr->buckets[bucket].nwait;
	++sharedPtr->buckets[bucket].nwait;
    } else if (err != 0) {
	panic("alloc: pthread_mutex_trylock: %s", strerror(err));
    }
    ++cachePtr->buckets[bucket].nlock;
    ++sharedPtr->buckets[bucket].nlock;
}


static void
UnlockBucket(Cache *cachePtr, int bucket)
{
    int err;

    err = pthread_mutex_unlock(&binfo[bucket].lock);
    if (err != 0) {
	panic("alloc: pthread_mutex_unlock: %s", strerror(err));
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  PutBlocks --
 *
 *	Return unused blocks to the shared cache.
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
PutBlocks(Cache *cachePtr, int bucket, int nmove)
{
    register Block *lastPtr, *firstPtr;
    register int n = nmove;

    /*
     * Before acquiring the lock, walk the block list to find
     * the last block to be moved.
     */

    firstPtr = lastPtr = cachePtr->buckets[bucket].firstPtr;
    while (--n > 0) {
	lastPtr = lastPtr->b_next;
    }
    cachePtr->buckets[bucket].firstPtr = lastPtr->b_next;
    cachePtr->buckets[bucket].nfree -= nmove;

    /*
     * Aquire the lock and place the list of blocks at the front
     * of the shared cache bucket.
     */

    LockBucket(cachePtr, bucket);
    lastPtr->b_next = sharedPtr->buckets[bucket].firstPtr;
    sharedPtr->buckets[bucket].firstPtr = firstPtr;
    sharedPtr->buckets[bucket].nfree += nmove;
    UnlockBucket(cachePtr, bucket);
}


/*
 *----------------------------------------------------------------------
 *
 *  GetBlocks --
 *
 *	Get more blocks for a bucket.
 *
 * Results:
 *	1 if blocks where allocated, 0 otherwise.
 *
 * Side effects:
 *	Cache may be filled with available blocks.
 *
 *----------------------------------------------------------------------
 */

static int
GetBlocks(Cache *cachePtr, int bucket)
{
    register Block *blockPtr;
    register int n;
    register size_t size;

    /*
     * First, atttempt to move blocks from the shared cache.  Note
     * the potentially dirty read of nfree before acquiring the lock
     * which is a slight performance enhancement.  The value is
     * verified after the lock is actually acquired.
     */
     
    if (cachePtr != sharedPtr && sharedPtr->buckets[bucket].nfree > 0) {
	LockBucket(cachePtr, bucket);
	if (sharedPtr->buckets[bucket].nfree > 0) {

	    /*
	     * Either move the entire list or walk the list to find
	     * the last block to move.
	     */

	    n = binfo[bucket].nmove;
	    if (n >= sharedPtr->buckets[bucket].nfree) {
		cachePtr->buckets[bucket].firstPtr = sharedPtr->buckets[bucket].firstPtr;
		cachePtr->buckets[bucket].nfree = sharedPtr->buckets[bucket].nfree;
		sharedPtr->buckets[bucket].firstPtr = NULL;
		sharedPtr->buckets[bucket].nfree = 0;
	    } else {
		blockPtr = sharedPtr->buckets[bucket].firstPtr;
		cachePtr->buckets[bucket].firstPtr = blockPtr;
		sharedPtr->buckets[bucket].nfree -= n;
		cachePtr->buckets[bucket].nfree = n;
		while (--n > 0) {
    		    blockPtr = blockPtr->b_next;
		}
		sharedPtr->buckets[bucket].firstPtr = blockPtr->b_next;
		blockPtr->b_next = NULL;
	    }
	}
	UnlockBucket(cachePtr, bucket);
    }
    
    if (cachePtr->buckets[bucket].nfree == 0) {

	/*
	 * If no blocks could be moved from shared, first look for a
	 * larger block in this cache to split up.
	 */

    	blockPtr = NULL;
	n = NBUCKETS;
	while (--n > bucket) {
    	    if (cachePtr->buckets[n].nfree > 0) {
		size = binfo[n].blocksize;
		blockPtr = cachePtr->buckets[n].firstPtr;
		cachePtr->buckets[n].firstPtr = blockPtr->b_next;
		--cachePtr->buckets[n].nfree;
		break;
	    }
	}

	/*
	 * Otherwise, allocate a big new block directly.
	 */

	if (blockPtr == NULL) {
	    size = MAXALLOC;
	    blockPtr = malloc(size);
	    if (blockPtr == NULL) {
		return 0;
	    }
	}

	/*
	 * Split the larger block into smaller blocks for this bucket.
	 */

	n = size / binfo[bucket].blocksize;
	cachePtr->buckets[bucket].nfree = n;
	cachePtr->buckets[bucket].firstPtr = blockPtr;
	while (--n > 0) {
	    blockPtr->b_next = (Block *) 
		((char *) blockPtr + binfo[bucket].blocksize);
	    blockPtr = blockPtr->b_next;
	}
	blockPtr->b_next = NULL;
    }
    return 1;
}

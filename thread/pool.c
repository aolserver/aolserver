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
 * pool.c --
 *
 *	Fast pool memory allocator designed to avoid lock
 *  	contention.  The basic strategy is to allocate memory in
 *  	fixed size blocks from block caches.  
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/pool.c,v 1.11 2000/11/09 00:48:41 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

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
    unsigned int nfree;
    unsigned int nget;
    unsigned int nput;
    unsigned int nwait;
    unsigned int nlock;
    unsigned int nrequest;
} Bucket;

/*
 * The following structure defines a pool of buckets.
 * The actual size of the buckets array is grown to the
 * run-time set nbuckets.
 */

typedef struct Pool {
    char     name[NS_THREAD_NAMESIZE];
    unsigned int nsysalloc;
    Bucket   buckets[1];
} Pool;

/*
 * The following structure array specifies various per-bucket 
 * limits and locks.  The values are initialized at startup
 * to avoid calculating them repeatedly.
 */

struct binfo {
    size_t  blocksize;	/* Bucket blocksize. */
    int	    maxblocks;	/* Max blocks before move to share. */
    int     nmove;	/* Num blocks to move to share. */
    void   *lock;	/* Share bucket lock. */
    unsigned int nlock;	/* Share bucket total lock count. */
    unsigned int nwait;	/* Share bucket lock waits. */
} *binfo;

/*
 * Static functions defined in this file.
 */

static void LockBucket(Pool *poolPtr, int bucket);
static void UnlockBucket(Pool *poolPtr, int bucket);
static void PutBlocks(Pool *poolPtr, int bucket, int nmove);
static int  GetBlocks(Pool *poolPtr, int bucket);
static Block *Ptr2Block(void *ptr);
static void  *Block2Ptr(Block *blockPtr, int bucket, int reqsize);

/*
 * Local variables defined in this file and initialized at
 * startup.
 */

static int	nbuckets;  /* Number of buckets. */
static Pool    *sharedPtr; /* Pool to which blocks are flushed. */
static int	maxalloc;  /* Max block allocation size. */

/*
 * The following global variable can be set to a different value
 * before the first allocation.
 */ 

int nsMemNumBuckets = 11;  /* Default: Allocate blocks up to 16k. */


/*
 *----------------------------------------------------------------------
 *
 *  Ns_PoolCreate ---
 *
 *	Create a new memory pool
 *
 * Results:
 *	Pointer to pool.
 *
 * Side effects:
 *	Bucket limits, locks, and shared pool are initialized on
 *	the first call.
 *
 *----------------------------------------------------------------------
 */

Ns_Pool *
Ns_PoolCreate(char *name)
{
    static size_t poolsize;
    Pool *poolPtr;
    int i;

    if (sharedPtr == NULL) {
	Ns_MasterLock();
	if (sharedPtr == NULL) {
	    nbuckets = nsMemNumBuckets;
	    if (nbuckets < 1) {
		nbuckets = 1;	/* Min: 16 bytes */
	    } else if (nbuckets > 13) {
		nbuckets = 13;	/* Max: 64k */
	    }
	    binfo = NsAlloc(nbuckets * sizeof(struct binfo));
	    maxalloc = 1 << (nbuckets + 3);
	    for (i = 0; i < nbuckets; ++i) {
		binfo[i].lock = NsLockAlloc();
		binfo[i].blocksize = 1 << (i+4);
		binfo[i].maxblocks = maxalloc / binfo[i].blocksize;
		binfo[i].nmove = (binfo[i].maxblocks + 1) / 2;
	    }
	    poolsize = sizeof(Pool) + (sizeof(Bucket) * (nbuckets - 1));
	    sharedPtr = NsAlloc(poolsize);
	    strcpy(sharedPtr->name, "-shared-");
	}
	Ns_MasterUnlock();
    }
    poolPtr = NsAlloc(poolsize);
    if (name != NULL) {
	strncpy(poolPtr->name, name, sizeof(poolPtr->name)-1);
    }
    return (Ns_Pool *) poolPtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  Ns_PoolFlush --
 *
 *	Return all free blocks to the shared pool.
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
Ns_PoolFlush(Ns_Pool *pool)
{
    register int   bucket;
    Pool *poolPtr = (Pool *) pool;

    for (bucket = 0; bucket < nbuckets; ++bucket) {
	if (poolPtr->buckets[bucket].nfree > 0) {
	    PutBlocks(poolPtr, bucket, poolPtr->buckets[bucket].nfree);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  Ns_PoolDestroy --
 *
 *	Flush and delete a pool.
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
Ns_PoolDestroy(Ns_Pool *pool)
{
    Ns_PoolFlush(pool);
    NsFree(pool);
}


/*
 *----------------------------------------------------------------------
 *
 *  Ns_PoolAlloc --
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

void *
Ns_PoolAlloc(Ns_Pool *pool, size_t reqsize)
{
    Pool          *poolPtr = (Pool *) pool;
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
    if (size > maxalloc) {
	bucket = nbuckets;
    	blockPtr = malloc(size);
	if (blockPtr != NULL) {
	    poolPtr->nsysalloc += reqsize;
	}
    } else {
    	bucket = 0;
    	while (binfo[bucket].blocksize < size) {
    	    ++bucket;
    	}
    	if (poolPtr->buckets[bucket].nfree || GetBlocks(poolPtr, bucket)) {
	    blockPtr = poolPtr->buckets[bucket].firstPtr;
	    poolPtr->buckets[bucket].firstPtr = blockPtr->b_next;
	    --poolPtr->buckets[bucket].nfree;
    	    ++poolPtr->buckets[bucket].nget;
	    poolPtr->buckets[bucket].nrequest += reqsize;
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
 *  Ns_PoolFree --
 *
 *	Return blocks to the thread block pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May move blocks to shared pool.
 *
 *----------------------------------------------------------------------
 */

void
Ns_PoolFree(Ns_Pool *pool, void *ptr)
{
    Pool  *poolPtr = (Pool *) pool;
    Block *blockPtr;
    int bucket;
 
    /*
     * Get the block back from the user pointer and
     * call system free directly for large blocks.
     * Otherwise, push the block back on the bucket and
     * move blocks to the shared pool if there are now
     * too many free.
     */

    blockPtr = Ptr2Block(ptr);
    bucket = blockPtr->b_bucket;
    if (bucket == nbuckets) {
	poolPtr->nsysalloc -= blockPtr->b_reqsize;
	free(blockPtr);
    } else {
	poolPtr->buckets[bucket].nrequest -= blockPtr->b_reqsize;
	blockPtr->b_next = poolPtr->buckets[bucket].firstPtr;
	poolPtr->buckets[bucket].firstPtr = blockPtr;
	++poolPtr->buckets[bucket].nfree;
	++poolPtr->buckets[bucket].nput;
    	if (poolPtr != sharedPtr &&
	    poolPtr->buckets[bucket].nfree > binfo[bucket].maxblocks) {
	    PutBlocks(poolPtr, bucket, binfo[bucket].nmove);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  Ns_PoolRealloc --
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

void *
Ns_PoolRealloc(Ns_Pool *pool, void *ptr, size_t reqsize)
{
    Pool *poolPtr = (Pool *) pool;
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
    if (bucket != nbuckets) {
	if (bucket > 0) {
	    min = binfo[bucket-1].blocksize;
	} else {
	    min = 0;
	}
	if (size > min && size <= binfo[bucket].blocksize) {
	    poolPtr->buckets[bucket].nrequest -= blockPtr->b_reqsize;
	    poolPtr->buckets[bucket].nrequest += reqsize;
	    return Block2Ptr(blockPtr, bucket, reqsize);
	}
    } else if (size > maxalloc) {
	poolPtr->nsysalloc -= blockPtr->b_reqsize;
	poolPtr->nsysalloc += reqsize;
	blockPtr = realloc(blockPtr, size);
	if (blockPtr == NULL) {
	    return NULL;
	}
	return Block2Ptr(blockPtr, nbuckets, reqsize);
    }

    /*
     * Finally, perform an expensive malloc/copy/free.
     */

    new = Ns_PoolAlloc(pool, reqsize);
    if (new != NULL) {
	if (reqsize > blockPtr->b_reqsize) {
	    reqsize = blockPtr->b_reqsize;
	}
    	memcpy(new, ptr, reqsize);
    	Ns_PoolFree(pool, ptr);
    }
    return new;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PoolCalloc --
 *
 *	Allocate zero-filled memory from a pool.
 *
 * Results:
 *	Pointer to allocated, zero'ed memory.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

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


/*
 *----------------------------------------------------------------------
 *
 * Ns_PoolStrDup, Ns_PoolStrCopy --
 *
 *	Dup (copy) a string.
 *
 * Results:
 *	Pointer string dup (copy).
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

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
 *  Ns_PoolStats --
 *
 *	Return allocation statistics for a memory pool.  If pool is
 *	NULL, stats on the shared pool are returned instead.
 *
 * Results:
 *	Pointer to per-thread Ns_PoolInfo structure with stats or
 *	NULL if the shared pool isn't initialized.
 *
 * Side effects:
 *	Returned Ns_PoolInfo structure will be overwritten on next
 *	call by this thread.
 *
 *----------------------------------------------------------------------
 */

Ns_PoolInfo *
Ns_PoolStats(Ns_Pool *pool)
{
    Ns_PoolInfo *infoPtr;
    Pool *poolPtr = (Pool *) pool;
    Bucket bucket;
    int i, size;
    static Ns_Tls tls;

    if (poolPtr == NULL) {
	poolPtr = sharedPtr;
    }
    if (poolPtr == NULL) {
	return NULL;
    }

    size = sizeof(Ns_PoolInfo) + (sizeof(Ns_PoolBucketInfo) * (nbuckets - 1));
    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, ns_free);
	}
	Ns_MasterUnlock();
    }
    infoPtr = Ns_TlsGet(&tls);
    if (infoPtr == NULL) {
	infoPtr = ns_malloc(size);
	Ns_TlsSet(&tls, infoPtr);
    }
    infoPtr->name = poolPtr->name;
    infoPtr->nbuckets = nbuckets;
    infoPtr->nsysalloc = poolPtr->nsysalloc;
    for (i = 0; i < nbuckets; ++i) {
	infoPtr->buckets[i].blocksize  = binfo[i].blocksize;
	bucket = poolPtr->buckets[i];
	infoPtr->buckets[i].nfree = bucket.nfree;
	infoPtr->buckets[i].nrequest = bucket.nrequest;
	infoPtr->buckets[i].nlock = bucket.nlock;
	infoPtr->buckets[i].nwait = bucket.nwait;
    }
    return infoPtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  Ns_ThreadPoolStats --
 *
 *	Return Ns_PoolStats for a thread's pool.  Stats for the 
 *	current thread are returned if thread is NULL.
 *
 * Results:
 *	Results of Ns_PoolStats or NULL on no pool for the given
 *	thread.
 *
 * Side effects:
 *	See Ns_PoolStats.
 *
 *----------------------------------------------------------------------
 */

Ns_PoolInfo *
Ns_ThreadPoolStats(Ns_Thread *thread)
{
    Thread *thrPtr = (Thread *) *thread;

    if (thrPtr == NULL) {
	thrPtr = NsGetThread();
    }
    if (thrPtr->pool == NULL) {
	return NULL;
    }
    return Ns_PoolStats(thrPtr->pool);
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

static void *
Block2Ptr(Block *blockPtr, int bucket, int reqsize) 
{
    register void *ptr;

    blockPtr->b_magic1 = blockPtr->b_magic2 = MAGIC;
    blockPtr->b_bucket = bucket;
    blockPtr->b_reqsize = reqsize;
    ptr = ((void *) (blockPtr + 1));
#if RCHECK
    ((unsigned char *)(ptr))[reqsize] = MAGIC;
#endif
    return ptr;
}

static Block *
Ptr2Block(void *ptr)
{
    register Block *blockPtr;

    blockPtr = (((Block *) ptr) - 1);
    if (blockPtr->b_magic1 != MAGIC
#if RCHECK
	|| ((unsigned char *) ptr)[blockPtr->b_reqsize] != MAGIC
#endif
	|| blockPtr->b_magic2 != MAGIC) {
	NsThreadAbort("Ns_Pool: invalid block: %p", blockPtr);
    }
    return blockPtr;
}


/*
 *----------------------------------------------------------------------
 *
 *  LockBucket, UnlockBucket --
 *
 *	Set/unset the lock to access a bucket in the shared pool.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *	Lock activity and contention are monitored globally and on
 *  	a per-pool basis.
 *
 *----------------------------------------------------------------------
 */

static void
LockBucket(Pool *poolPtr, int bucket)
{
    if (!NsLockTry(binfo[bucket].lock)) {
	NsLockSet(binfo[bucket].lock);
	++poolPtr->buckets[bucket].nwait;
	++binfo[bucket].nwait;
    }
    ++poolPtr->buckets[bucket].nlock;
    ++binfo[bucket].nlock;
}


static void
UnlockBucket(Pool *poolPtr, int bucket)
{
    NsLockUnset(binfo[bucket].lock);
}


/*
 *----------------------------------------------------------------------
 *
 *  PutBlocks --
 *
 *	Return unused blocks to the shared pool.
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
PutBlocks(Pool *poolPtr, int bucket, int nmove)
{
    register Block *lastPtr, *firstPtr;
    register int n = nmove;

    /*
     * Before acquiring the lock, walk the block list to find
     * the last block to be moved.
     */

    firstPtr = lastPtr = poolPtr->buckets[bucket].firstPtr;
    while (--n > 0) {
	lastPtr = lastPtr->b_next;
    }
    poolPtr->buckets[bucket].firstPtr = lastPtr->b_next;
    poolPtr->buckets[bucket].nfree -= nmove;

    /*
     * Aquire the lock and place the list of blocks at the front
     * of the shared pool bucket.
     */

    LockBucket(poolPtr, bucket);
    lastPtr->b_next = sharedPtr->buckets[bucket].firstPtr;
    sharedPtr->buckets[bucket].firstPtr = firstPtr;
    sharedPtr->buckets[bucket].nfree += nmove;
    UnlockBucket(poolPtr, bucket);
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
 *	Pool may be filled with available blocks.
 *
 *----------------------------------------------------------------------
 */

static int
GetBlocks(Pool *poolPtr, int bucket)
{
    register Block *blockPtr;
    register int n;
    register size_t size;

    /*
     * First, atttempt to move blocks from the shared pool.  Note
     * the potentially dirty read of nfree before acquiring the lock
     * which is a slight performance enhancement.  The value is
     * verified after the lock is actually acquired.
     */
     
    if (poolPtr != sharedPtr && sharedPtr->buckets[bucket].nfree > 0) {
	LockBucket(poolPtr, bucket);
	if (sharedPtr->buckets[bucket].nfree > 0) {

	    /*
	     * Either move the entire list or walk the list to find
	     * the last block to move.
	     */

	    n = binfo[bucket].nmove;
	    if (n >= sharedPtr->buckets[bucket].nfree) {
		poolPtr->buckets[bucket].firstPtr = sharedPtr->buckets[bucket].firstPtr;
		poolPtr->buckets[bucket].nfree = sharedPtr->buckets[bucket].nfree;
		sharedPtr->buckets[bucket].firstPtr = NULL;
		sharedPtr->buckets[bucket].nfree = 0;
	    } else {
		blockPtr = sharedPtr->buckets[bucket].firstPtr;
		poolPtr->buckets[bucket].firstPtr = blockPtr;
		sharedPtr->buckets[bucket].nfree -= n;
		poolPtr->buckets[bucket].nfree = n;
		while (--n > 0) {
    		    blockPtr = blockPtr->b_next;
		}
		sharedPtr->buckets[bucket].firstPtr = blockPtr->b_next;
		blockPtr->b_next = NULL;
	    }
	}
	UnlockBucket(poolPtr, bucket);
    }
    
    if (poolPtr->buckets[bucket].nfree == 0) {

	/*
	 * If no blocks could be moved from shared, first look for a
	 * larger block in this pool to split up.
	 */

    	blockPtr = NULL;
	n = nbuckets;
	while (--n > bucket) {
    	    if (poolPtr->buckets[n].nfree > 0) {
		size = binfo[n].blocksize;
		blockPtr = poolPtr->buckets[n].firstPtr;
		poolPtr->buckets[n].firstPtr = blockPtr->b_next;
		--poolPtr->buckets[n].nfree;
		break;
	    }
	}

	/*
	 * Otherwise, allocate a big new block directly.
	 */

	if (blockPtr == NULL) {
	    size = maxalloc;
	    blockPtr = malloc(size);
	    if (blockPtr == NULL) {
		return 0;
	    }
	}

	/*
	 * Split the larger block into smaller blocks for this bucket.
	 */

	n = size / binfo[bucket].blocksize;
	poolPtr->buckets[bucket].nfree = n;
	poolPtr->buckets[bucket].firstPtr = blockPtr;
	while (--n > 0) {
	    blockPtr->b_next = (Block *) 
		((char *) blockPtr + binfo[bucket].blocksize);
	    blockPtr = blockPtr->b_next;
	}
	blockPtr->b_next = NULL;
    }
    return 1;
}

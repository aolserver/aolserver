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
 *	Support for pools-based replacement for ns_malloc/ns_free.
 *  	The goal of this dynamic memory allocator is to avoid lock
 *  	contention.  The basic strategy is to allocate memory in
 *  	fixed size blocks from per-thread block caches.  
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/pool.c,v 1.7 2000/10/23 14:33:58 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"
#include <assert.h>

/*
 * The following structure specifies various per-bucket runtime
 * limits.  The values are statically defined to avoid calculating
 * them repeatedly.
 */

struct {
    size_t  blocksize;
    int     maxblocks;
    int     nmove;
} limits[NBUCKETS] = {
    {16, 2048, 1024},
    {32, 1024, 512},
    {64, 512, 256},
    {128, 256, 128},
    {256, 128, 64},
    {512, 64, 32},
    {1024, 32, 16},
    {2048, 16, 8},
    {4096, 8, 4},
    {8192, 4, 2},
    {16384, 2, 1}
};

#define MAXALLOC	16384
#define BLOCKSZ(i)  	(limits[i].blocksize)
#define MAXBLOCKS(i)	(limits[i].maxblocks)
#define NMOVE(i)    	(limits[i].nmove)

#if 0

/*
 * Calculated equivalent of the limits above.  Using these macros
 * instead appears to slow the code down about 20%.
 */

#define MAXALLOC    	(1<<(NBUCKETS+3))
#define BLOCKSZ(i)	(1<<(i+4))
#define MAXBLOCKS(i)	((MAXALLOC/BLOCKSZ(i)) * 2)
#define NMOVE(i)	(MAXBLOCKS(i)/2)

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
    	struct Block *next;
    	struct {
	    unsigned char magic1;
	    unsigned char bucket;
	    unsigned char unused;
	    unsigned char magic2;
        } b_s;
    } b_u;
    size_t b_size;
} Block;
#define b_next		b_u.next
#define b_bucket	b_u.b_s.bucket
#define b_magic1	b_u.b_s.magic1
#define b_magic2	b_u.b_s.magic2
#define MAGIC		0xef
#define SYSBUCKET	0xff

/*
 * Local variables and inlined macros defined in this file
 */

static void PutBlocks(Pool *poolPtr, int bucket, int nmove);
static int GetBlocks(Pool *poolPtr, int bucket);
static Pool 	sharedPool;
static int	initialized;
static Ns_Mutex bucketLocks[NBUCKETS];

#define GETTHREADPOOL()	(&(NsGetThread())->memPool)
#define GETPOOL()	(initialized ? GETTHREADPOOL() : &sharedPool)
#define BLOCK2PTR(bp)	((void *) (bp + 1))
#define PTR2BLOCK(ptr)	(((Block *) ptr) - 1)

/*
 * If range checking is enabled, an additional byte will be allocated
 * to store the magic number at the end of the request memory.
 * CHECKBLOCK and SETBLOCK will then verify this byte to detect
 * possible overwrite.
 */

#ifndef RCHECK
#ifdef NDEBUG
#define RCHECK		0
#else
#define RCHECK		1
#endif
#endif

#if RCHECK

#define SETBLOCK(bp,i,sz) 			\
    bp->b_magic1 = bp->b_magic2 = MAGIC;	\
    bp->b_bucket = i;				\
    bp->b_size = sz;				\
    ((unsigned char *)(bp+1))[sz] = MAGIC

#define CHECKBLOCK(bp) \
    (bp->b_magic1==MAGIC && bp->b_magic2==MAGIC && \
     ((unsigned char *)(bp+1))[bp->b_size]==MAGIC)

#else

#define SETBLOCK(bp,i,sz) 			\
    bp->b_magic1 = bp->b_magic2 = MAGIC;	\
    bp->b_bucket = i;				\
    bp->b_size = sz

#define CHECKBLOCK(bp) 				\
    (bp->b_magic1==MAGIC && bp->b_magic2==MAGIC)

#endif

#define POPBLOCK(pp,i,bp) \
    bp = pp->firstPtr[i];\
    pp->firstPtr[i] = bp->b_next;\
    --pp->nfree[i];\
    ++pp->nused[i]

#define PUSHBLOCK(pp,bp,i) \
    bp->b_next = pp->firstPtr[i];\
    pp->firstPtr[i] = bp;\
    ++pp->nfree[i];\
    --pp->nused[i]

#define UNLOCKBUCKET(i) Ns_MutexUnlock(&bucketLocks[i])
#define LOCKBUCKET(i)	Ns_MutexLock(&bucketLocks[i])


/*
 *----------------------------------------------------------------------
 *
 *  NsInitPools ---
 *
 *	Initialize the memory pools on first call to CreateThread.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	All further ns_malloc/ns_free will be through per-thread pools.
 *
 *----------------------------------------------------------------------
 */

void
NsInitPools(void)
{
    int i;

    if (!initialized) {
	char name[32];

    	/*
	 * Call Ns_MutexInit's before setting initialized because
	 * Ns_MutexInit will call ns_malloc().
	 */
    	
	for (i = 0; i < NBUCKETS; ++i) {
	    Ns_MutexInit(&bucketLocks[i]);
	    sprintf(name, "%d", i);
	    Ns_MutexSetName2(&bucketLocks[i], "nsmalloc", name);
	}
	initialized = 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  NsPoolFlush --
 *
 *	Return all free blocks to the shared pool at thread exit.
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
NsPoolFlush(Pool *poolPtr)
{
    register int   bucket;

    for (bucket = 0; bucket < NBUCKETS; ++bucket) {
	if (poolPtr->nfree[bucket] > 0) {
	    PutBlocks(poolPtr, bucket, poolPtr->nfree[bucket]);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsPoolDump --
 *
 *	Dump stats about current pool to open file.
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
NsPoolDump(Pool *poolPtr, FILE *fp)
{
    int nfree[NBUCKETS], nused[NBUCKETS];
    int i, size;
    unsigned long nbused;
    unsigned long nbfree;

    if (poolPtr == NULL) {
	poolPtr = &sharedPool;
    }
    nbused = nbfree = 0;
    memcpy(nfree, poolPtr->nfree, sizeof(nfree));
    memcpy(nused, poolPtr->nused, sizeof(nused));
    for (i = 0; i < NBUCKETS; ++i) {
	size = BLOCKSZ(i);
	fprintf(fp, " %d:%d:%d", size, nused[i], nfree[i]);
	nbused += nused[i] * size;
	nbfree += nfree[i] * size;
    }
    fprintf(fp, " %ld:%ld\n", nbused, nbfree);
}


/*
 *----------------------------------------------------------------------
 *
 *  NsPoolMalloc --
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
NsPoolMalloc(size_t reqsize)
{
    Pool          *poolPtr;
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
	bucket = SYSBUCKET;
    	blockPtr = malloc(size);
    } else {
    	poolPtr = GETPOOL();
    	bucket = 0;
    	while (BLOCKSZ(bucket) < size) {
    	    ++bucket;
    	}
    	if (poolPtr->nfree[bucket] || GetBlocks(poolPtr, bucket)) {
	    POPBLOCK(poolPtr, bucket, blockPtr);
	}
    }
    if (blockPtr == NULL) {
    	return NULL;
    }
    SETBLOCK(blockPtr, bucket, reqsize);
    return BLOCK2PTR(blockPtr);
}


/*
 *----------------------------------------------------------------------
 *
 *  NsPoolFree --
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
NsPoolFree(void *ptr)
{
    Pool *poolPtr;
    Block *blockPtr;
    int bucket;
 
    /*
     * Get the block back from the user pointer and
     * call system free directly for large blocks.
     * Otherwise, push the block back on the bucket and
     * move blocks to the shared pool if there are now
     * too many free.
     */

    blockPtr = PTR2BLOCK(ptr);
#ifndef NDEBUG
    assert(CHECKBLOCK(blockPtr));
#else
    if (!CHECKBLOCK(blockPtr)) {
	return;
    }
#endif
    bucket = blockPtr->b_bucket;
    if (bucket == SYSBUCKET) {
	free(blockPtr);
    } else {
    	poolPtr = GETPOOL();
    	PUSHBLOCK(poolPtr, blockPtr, bucket);
    	if (poolPtr != &sharedPool &&
	    poolPtr->nfree[bucket] > MAXBLOCKS(bucket)) {
	    PutBlocks(poolPtr, bucket, NMOVE(bucket));
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 *  NsPoolRealloc --
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
NsPoolRealloc(void *ptr, size_t reqsize)
{
    Block *blockPtr;
    void *new;
    size_t size, min;
    int bucket;

    /*
     * First, get and block from the user pointer and verify it.
     */

    blockPtr = PTR2BLOCK(ptr);
#ifndef NDEBUG
    assert(CHECKBLOCK(blockPtr));
#else
    if (!CHECKBLOCK(blockPtr)) {
    	return NULL;
    }
#endif

    /*
     * Next, if the block is not a system block and fits in place,
     * simply return the existing pointer.  Otherwise, if the block
     * is a system block and the new size would also require a system
     * block, call realloc() directly.
     */

    size = reqsize + sizeof(Block);
#if RCHECK
    ++size;
#endif
    bucket = blockPtr->b_bucket;
    if (bucket != SYSBUCKET) {
	if (bucket > 0) {
	    min = BLOCKSZ(bucket - 1);
	} else {
	    min = 0;
	}
	if (size > min && size <= BLOCKSZ(bucket)) {
	    SETBLOCK(blockPtr, bucket, reqsize);
	    return ptr;
	}
    } else if (size > MAXALLOC) {
	blockPtr = realloc(blockPtr, size);
	if (blockPtr == NULL) {
	    return NULL;
	}
	SETBLOCK(blockPtr, SYSBUCKET, reqsize);
	return BLOCK2PTR(blockPtr);
    }

    /*
     * Finally, perform an expensive malloc/copy/free.
     */

    new = NsPoolMalloc(reqsize);
    if (new != NULL) {
	if (reqsize > blockPtr->b_size) {
	    reqsize = blockPtr->b_size;
	}
    	memcpy(new, ptr, reqsize);
    	NsPoolFree(ptr);
    }
    return new;
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
     * Before aquiring the lock, walk the block list to find
     * the last block to be moved.
     */

    firstPtr = lastPtr = poolPtr->firstPtr[bucket];
    while (--n > 0) {
	lastPtr = lastPtr->b_next;
    }
    poolPtr->firstPtr[bucket] = lastPtr->b_next;
    poolPtr->nfree[bucket] -= nmove;

    /*
     * Aquire the lock and place the list of blocks at the front
     * of the shared pool bucket.
     */

    LOCKBUCKET(bucket);
    lastPtr->b_next = sharedPool.firstPtr[bucket];
    sharedPool.firstPtr[bucket] = firstPtr;
    sharedPool.nfree[bucket] += nmove;
    UNLOCKBUCKET(bucket);
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
    register int n, blksz, nblks;
    size_t size;

    /*
     * First, atttempt to move blocks from the shared pool.  Note
     * the potentially dirty read of nfree before aquiring the lock
     * which is a slight performance enhancement.  The value is
     * verified after the lock is actually aquired.
     */
     
    if (poolPtr != &sharedPool && sharedPool.nfree[bucket] > 0) {
	LOCKBUCKET(bucket);
	if (sharedPool.nfree[bucket] > 0) {

	    /*
	     * Either move the entire list or walk the list to find
	     * the last block to move.
	     */

	    n = NMOVE(bucket);
	    if (n >= sharedPool.nfree[bucket]) {
		poolPtr->firstPtr[bucket] = sharedPool.firstPtr[bucket];
		poolPtr->nfree[bucket] = sharedPool.nfree[bucket];
		sharedPool.firstPtr[bucket] = NULL;
		sharedPool.nfree[bucket] = 0;
	    } else {
		blockPtr = sharedPool.firstPtr[bucket];
		poolPtr->firstPtr[bucket] = blockPtr;
		sharedPool.nfree[bucket] -= n;
		poolPtr->nfree[bucket] = n;
		while (--n > 0) {
    		    blockPtr = blockPtr->b_next;
		}
		sharedPool.firstPtr[bucket] = blockPtr->b_next;
		blockPtr->b_next = NULL;
	    }
	}
	UNLOCKBUCKET(bucket);
    }
    
    if (poolPtr->nfree[bucket] == 0) {

	/*
	 * If no blocks could be moved from shared, first look for a
	 * larger block in this pool to split up.
	 */

    	blockPtr = NULL;
	n = NBUCKETS;
	while (--n > bucket) {
    	    if (poolPtr->nfree[n] > 0) {
		size = BLOCKSZ(n);
		POPBLOCK(poolPtr, n, blockPtr);
		--poolPtr->nused[n]; /* NB: Not really in use. */
		break;
	    }
	}

	/*
	 * Otherwise, allocate a big new block directly.
	 */

	if (blockPtr == NULL) {
	    size = MAXALLOC;
#ifdef WIN32
	    blockPtr = malloc(size);
	    if (blockPtr == NULL) {
		return 0;
	    }
#else
	    blockPtr = sbrk(size);
	    if (blockPtr == (Block *) -1) {
		return 0;
	    }
#endif
	}

	/*
	 * Split the larger block into smaller blocks for this bucket.
	 */

	blksz = BLOCKSZ(bucket);
	nblks = size / blksz;
	poolPtr->nfree[bucket] = nblks;
	poolPtr->firstPtr[bucket] = blockPtr;
	while (--nblks > 0) {
	    blockPtr->b_next = (Block *) ((char *) blockPtr + blksz);
	    blockPtr = blockPtr->b_next;
	}
	blockPtr->b_next = NULL;
    }
    
    return 1;
}

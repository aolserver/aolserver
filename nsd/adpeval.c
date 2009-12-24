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
 * adpeval.c --
 *
 *	ADP string and file eval.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpeval.c,v 1.50 2009/12/24 19:50:07 dvrsn Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a cached ADP page result.  A cached
 * object is created by executing the non-cached code and saving the
 * resulting output which may include embedded non-cached components
 * (see NsTclAdpIncludeObjCmd for details).
 */

typedef struct AdpCache {
    int	      	   refcnt;	/* Current interps using cached results. */ 
    Ns_Time	   expires;	/* Expiration time of cached results. */
    AdpCode	   code;	/* ADP code for cached result. */
} AdpCache;

/*
 * The following structure defines a shared page in the ADP cache.  The
 * size of the object is extended to include the filename bytes.
 */

typedef struct Page {
    NsServer	  *servPtr;	/* Page server context (reg tags, etc.) */
    Tcl_HashEntry *hPtr;	/* Entry in shared table of all pages. */
    time_t    	   mtime;	/* Original modify time of file. */
    off_t     	   size;	/* Original size of file. */
    int		   flags;	/* Flags used on last compile, e.g., SAFE. */
    int	      	   refcnt;	/* Refcnt of current interps using page. */
    int		   evals;	/* Count of page evaluations. */
    int		   locked;	/* Page locked for cache update. */
    int		   cacheGen;	/* Cache generation id. */
    AdpCache	  *cachePtr;	/* Cached output. */
    AdpCode	   code;	/* ADP code blocks. */
    char	   file[1];	/* First known filename for stats reporting. */
} Page;

/*
 * The following structure holds per-interp script byte codes.  The
 * size of the object is extended based on the number of script objects.
 */

typedef struct Objs {
    int nobjs;			/* Number of scripts objects. */
    Tcl_Obj *objs[1];		/* Scripts to be compiled and reused. */
} Objs;

/*
 * The following structure defines a per-interp page entry with
 * a pointer to the shared Page and private Objs for cached and
 * non-cached page results.
 */

typedef struct InterpPage {
    Page     *pagePtr;		/* Pointer to shared page text. */
    Objs     *objs;		/* Non-cache ADP code script. */
    int	      cacheGen;		/* Cache generation id. */
    Objs     *cacheObjs;	/* Cache results ADP code scripts. */
} InterpPage;

/*
 * Local functions defined in this file.
 */

static Page *ParseFile(NsInterp *itPtr, char *file, struct stat *stPtr,
		       int flags);
static int AdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int flags,
		   char *resvar);
static int AdpExec(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *file,
		   AdpCode *codePtr, Objs *objsPtr, Tcl_DString *outputPtr);
static int AdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *file,
		     Ns_Time *ttlPtr, int flags, Tcl_DString *outputPtr);
static int AdpDebug(NsInterp *itPtr, char *ptr, int len, int nscript);
static void DecrCache(AdpCache *cachePtr);
static Objs *AllocObjs(int nobjs);
static void FreeObjs(Objs *objsPtr);
static void AdpTrace(NsInterp *itPtr, char *ptr, int len);
static Ns_Callback FreeInterpPage;


/*
 *----------------------------------------------------------------------
 *
 * NsAdpEval, NsAdpSource --
 *
 *	Evaluate an ADP string or file and return the output
 *	as the interp result.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Variable named by resvar, if any, is updated with results of
 *	Tcl interp before being replaced with ADP output.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int flags, char *resvar)
{
    return AdpEval(itPtr, objc, objv, flags, resvar);
}

int
NsAdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int flags, char *resvar)
{
    return AdpEval(itPtr, objc, objv, (flags | ADP_EVAL_FILE), resvar);
}

static int
AdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int flags, char *resvar)
{
    Tcl_Interp	     *interp = itPtr->interp;
    AdpCode	      code;
    Tcl_DString       output;
    Tcl_Obj	     *objPtr;
    int               result;
    char	     *obj0;
    
    /*
     * If the ADP object is a file, simply source it. Otherwise, parse
     * the script as a temporary ADP code object and execute it directly.
     */

    Tcl_DStringInit(&output);
    obj0 = Tcl_GetString(objv[0]);
    if (flags & ADP_EVAL_FILE) {
    	result = AdpSource(itPtr, objc, objv, obj0, NULL, flags, &output);
    } else {
    	NsAdpParse(&code, itPtr->servPtr, obj0, flags);
    	result = AdpExec(itPtr, objc, objv, NULL, &code, NULL, &output);
    	NsAdpFreeCode(&code);
    }

    /*
     * Set the interp result with the ADP output, saving the last interp
     * result first if requested.
     */

    if (result == TCL_OK) {
	if (resvar != NULL) {
	    objPtr = Tcl_GetObjResult(interp);
	    if (Tcl_SetVar2Ex(interp, resvar, NULL, objPtr, TCL_LEAVE_ERR_MSG)
			      == NULL) {
		result = TCL_ERROR;
	    }
	}
	if (result == TCL_OK) {
	    objPtr = Tcl_NewStringObj(output.string, output.length);
	    Tcl_SetObjResult(interp, objPtr);
	}
    }
    Tcl_DStringFree(&output);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpInclude --
 *
 *	Evaluate an ADP file, utilizing per-thread byte-code pages.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output is either left in current ADP buffer.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpInclude(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *file,
		Ns_Time *ttlPtr)
{
    Ns_DString *outputPtr;
    int flags = itPtr->adp.flags;

    /*
     * If an ADP execution is already active, use the current output
     * buffer. Otherwise, use the top-level buffer in the ADP struct.
     */

    if (itPtr->adp.framePtr != NULL) {
	outputPtr = itPtr->adp.framePtr->outputPtr;
    } else {
	outputPtr = &itPtr->adp.output;
    }
    return AdpSource(itPtr, objc, objv, file, ttlPtr, flags, outputPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpInit, NsAdpFree --
 *
 *	Initialize or free the NsInterp ADP data structures.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpInit(NsInterp *itPtr)
{
    Tcl_DStringInit(&itPtr->adp.output);
    NsAdpReset(itPtr);
}

void
NsAdpFree(NsInterp *itPtr)
{
    if (itPtr->adp.cache != NULL) {
	Ns_CacheDestroy(itPtr->adp.cache);
    }
    Tcl_DStringFree(&itPtr->adp.output);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpReset --
 *
 *	Reset the NsInterp ADP data structures for the next
 *	execution request.
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
NsAdpReset(NsInterp *itPtr)
{
    itPtr->adp.exception = ADP_OK;
    itPtr->adp.debugLevel = 0;
    itPtr->adp.debugInit = 0;
    itPtr->adp.debugFile = NULL;
    itPtr->adp.chan = NULL;
    itPtr->adp.conn = NULL;
    itPtr->adp.bufsize = itPtr->servPtr->adp.bufsize;
    itPtr->adp.flags = itPtr->servPtr->adp.flags;
    Tcl_DStringTrunc(&itPtr->adp.output, 0);
}

/*
 *----------------------------------------------------------------------
 *
 * AdpSource --
 *
 *	Execute ADP code in a file with results returned in given
 *	dstring.
 *
 * Results:
 *	TCL_ERROR if the file could not be parsed, result of
 *	AdpExec otherwise.
 *
 * Side effects:
 *	Page text and ADP code results may be cached up to given time
 *	limit, if any.
 *
 *----------------------------------------------------------------------
 */

static int
AdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *file,
       Ns_Time *ttlPtr, int flags, Tcl_DString *outputPtr)
{
    NsServer  *servPtr = itPtr->servPtr;
    Tcl_Interp *interp = itPtr->interp;
    Tcl_HashEntry *hPtr;
    struct stat st;
    Ns_DString tmp, path;
    InterpPage *ipagePtr;
    Page *pagePtr, *oldPagePtr;
    AdpCache *cachePtr;
    AdpCode *codePtr;
    Ns_Time now;
    Ns_Entry *ePtr;
    Objs *objsPtr;
    int new, cacheGen;
    char *p, *key;
    FileKey ukey;
    int result;

    ipagePtr = NULL;
    pagePtr = NULL; 
    result = TCL_ERROR;   /* assume error until accomplished success */
    Ns_DStringInit(&tmp);
    Ns_DStringInit(&path);
    key = (char *) &ukey;
    
    /*
     * Construct the full, normalized path to the ADP file.
     */

    if (!Ns_PathIsAbsolute(file)) {
	file = Ns_MakePath(&tmp, itPtr->adp.cwd, file, NULL);
    }
    file = Ns_NormalizePath(&path, file);
    Ns_DStringTrunc(&tmp, 0);

    /*
     * Check for TclPro debugging.
     */

    if (itPtr->adp.debugLevel > 0) {
	++itPtr->adp.debugLevel;
    } else if ((flags & ADP_DEBUG) &&
	itPtr->adp.debugFile != NULL &&
	(p = strrchr(file, '/')) != NULL &&
	Tcl_StringMatch(p+1, itPtr->adp.debugFile)) {
	Ns_Set *hdrs;
	char *host, *port, *procs;

	hdrs = Ns_ConnGetQuery(itPtr->conn);
	host = Ns_SetIGet(hdrs, "dhost");
	port = Ns_SetIGet(hdrs, "dport");
	procs = Ns_SetIGet(hdrs, "dprocs");
	if (NsAdpDebug(itPtr, host, port, procs) != TCL_OK) {
	    Ns_ConnReturnNotice(itPtr->conn, 200, "Debug Init Failed",
				(char *) Tcl_GetStringResult(interp));
	    itPtr->adp.exception = ADP_ABORT;
	    goto done;
	}
    }

    if (itPtr->adp.cache == NULL) {
	Ns_DStringPrintf(&tmp, "nsadp:%p", itPtr);
#ifdef _WIN32
	itPtr->adp.cache = Ns_CacheCreateSz(tmp.string, TCL_STRING_KEYS,
				itPtr->servPtr->adp.cachesize, FreeInterpPage);
#else
	itPtr->adp.cache = Ns_CacheCreateSz(tmp.string, FILE_KEYS,
				itPtr->servPtr->adp.cachesize, FreeInterpPage);
#endif
	Ns_DStringTrunc(&tmp, 0);
    }

    /*
     * Verify the file is an existing, ordinary file and get page code.
     */

    if (stat(file, &st) != 0) {
	Tcl_AppendResult(interp, "could not stat \"",
	    file, "\": ", Tcl_PosixError(interp), NULL);
    } else if (S_ISREG(st.st_mode) == 0) {
    	Tcl_AppendResult(interp, "not an ordinary file: ", file, NULL);
    } else {
	/*
	 * Check for valid code in interp page cache.
	 */
	 
#ifdef _WIN32
	key = file;
#else
	ukey.dev = st.st_dev;
	ukey.ino = st.st_ino;
#endif
        ePtr = Ns_CacheFindEntry(itPtr->adp.cache, key);
	if (ePtr != NULL) {
    	    ipagePtr = Ns_CacheGetValue(ePtr);
    	    if (ipagePtr->pagePtr->mtime != st.st_mtime
			|| ipagePtr->pagePtr->size != st.st_size
			|| ipagePtr->pagePtr->flags != flags) {
		Ns_CacheFlushEntry(ePtr);
		ipagePtr = NULL;
	    }
	}
	if (ipagePtr == NULL) {
	    /*
	     * Find or create valid page in server table.
	     */
	     
    	    Ns_MutexLock(&servPtr->adp.pagelock);
	    hPtr = Tcl_CreateHashEntry(&servPtr->adp.pages, key, &new);
	    while (!new && (pagePtr = Tcl_GetHashValue(hPtr)) == NULL) {
		/* NB: Wait for other thread to read/parse page. */
		Ns_CondWait(&servPtr->adp.pagecond, &servPtr->adp.pagelock);
		hPtr = Tcl_CreateHashEntry(&servPtr->adp.pages, key, &new);
	    }
	    if (!new && (pagePtr->mtime != st.st_mtime
			|| pagePtr->size != st.st_size
			|| pagePtr->flags != flags)) {
		/* NB: Clear entry to indicate read/parse in progress. */
		Tcl_SetHashValue(hPtr, NULL);
		pagePtr->hPtr = NULL;
		new = 1;
	    }
	    if (new) {
		Ns_MutexUnlock(&servPtr->adp.pagelock);
		pagePtr = ParseFile(itPtr, file, &st, flags);
		Ns_MutexLock(&servPtr->adp.pagelock);
		if (pagePtr == NULL) {
		    Tcl_DeleteHashEntry(hPtr);
		} else {
#ifdef _WIN32
            	    if (pagePtr->mtime != st.st_mtime
				|| pagePtr->size != st.st_size)
#else
            	    if (ukey.dev != st.st_dev || ukey.ino != st.st_ino)
#endif
		    {
			/* NB: File changed between stat above and ParseFile. */
		    	Tcl_DeleteHashEntry(hPtr);
#ifndef _WIN32
                	ukey.dev = st.st_dev;
		    	ukey.ino = st.st_ino;
#endif
		    	hPtr = Tcl_CreateHashEntry(&servPtr->adp.pages, key,
						   &new);
		    	if (!new) {
			    oldPagePtr = Tcl_GetHashValue(hPtr);
			    oldPagePtr->hPtr = NULL;
		    	}
		    }
		    pagePtr->hPtr = hPtr;
		    Tcl_SetHashValue(hPtr, pagePtr);
		}
		Ns_CondBroadcast(&servPtr->adp.pagecond);
	    }
	    if (pagePtr != NULL) {
	    	++pagePtr->refcnt;
	    }
	    Ns_MutexUnlock(&servPtr->adp.pagelock);
	    if (pagePtr != NULL) {
	    	ipagePtr = ns_malloc(sizeof(InterpPage));
		ipagePtr->pagePtr = pagePtr;
		ipagePtr->cacheGen = 0;
		ipagePtr->objs = AllocObjs(pagePtr->code.nscripts);
		ipagePtr->cacheObjs = NULL;
        	ePtr = Ns_CacheCreateEntry(itPtr->adp.cache, key, &new);
		if (!new) {
		    Ns_CacheUnsetValue(ePtr);
		}
        	Ns_CacheSetValueSz(ePtr, ipagePtr,
				   (size_t) ipagePtr->pagePtr->size);
	    }
	}
    }
    
    /*
     * If valid page was found, evaluate it in a new call frame.
     */
         
    if (ipagePtr != NULL) {
	pagePtr = ipagePtr->pagePtr;
	if (ttlPtr == NULL || (flags & ADP_NOCACHE)) {
	   cachePtr = NULL;
	} else {
	    Ns_MutexLock(&servPtr->adp.pagelock);

	    /*
	     * First, wait for an initial cache if already executing.
	     */

	    while ((cachePtr = pagePtr->cachePtr) == NULL && pagePtr->locked) {
		Ns_CondWait(&servPtr->adp.pagecond, &servPtr->adp.pagelock);
	    }

	    /*
	     * Next, if a cache exists and isn't locked, check expiration.
	     */

	    if (cachePtr != NULL && !pagePtr->locked) {
		Ns_GetTime(&now);
		if (Ns_DiffTime(&cachePtr->expires, &now, NULL) < 0) {
		    pagePtr->locked = 1;
		    cachePtr = NULL;
		}
	    }

	    /*
	     * Create the cached page if necessary.
	     */

	    if (cachePtr == NULL) {
	    	Ns_MutexUnlock(&servPtr->adp.pagelock);
		codePtr = &pagePtr->code;
		++itPtr->adp.refresh;
		result = AdpExec(itPtr, objc, objv, file, codePtr,
				 ipagePtr->objs, &tmp);
		--itPtr->adp.refresh;
		if (result == TCL_OK) {
		    cachePtr = ns_malloc(sizeof(AdpCache));
		    NsAdpParse(&cachePtr->code, itPtr->servPtr, tmp.string,
			       flags);
		    Ns_GetTime(&cachePtr->expires);
		    Ns_IncrTime(&cachePtr->expires, ttlPtr->sec, ttlPtr->usec);
	    	    cachePtr->refcnt = 1;
		}
		Ns_DStringTrunc(&tmp, 0);
	    	Ns_MutexLock(&servPtr->adp.pagelock);
		if (cachePtr != NULL) {
		    if (pagePtr->cachePtr != NULL) {
		    	DecrCache(pagePtr->cachePtr);
		    }
		    ++pagePtr->cacheGen;
		    pagePtr->cachePtr = cachePtr;
		}
		pagePtr->locked = 0;
		Ns_CondBroadcast(&servPtr->adp.pagecond);
	    }
	    cacheGen = pagePtr->cacheGen;
	    ++cachePtr->refcnt;
	    Ns_MutexUnlock(&servPtr->adp.pagelock);
	}
	if (cachePtr == NULL) {
	   codePtr = &pagePtr->code;
	   objsPtr = ipagePtr->objs;
	} else {
	    codePtr = &cachePtr->code;
	    if (ipagePtr->cacheObjs != NULL && cacheGen != ipagePtr->cacheGen) {
		FreeObjs(ipagePtr->cacheObjs);
		ipagePtr->cacheObjs = NULL;
	    }
	    if (ipagePtr->cacheObjs == NULL) {
		ipagePtr->cacheObjs = AllocObjs(AdpCodeScripts(codePtr));
		ipagePtr->cacheGen = cacheGen;
	    }
	    objsPtr = ipagePtr->cacheObjs;
	}
	result = AdpExec(itPtr, objc, objv, file, codePtr, objsPtr, outputPtr);
	Ns_MutexLock(&servPtr->adp.pagelock);
	++ipagePtr->pagePtr->evals;
	if (cachePtr != NULL) {
	    DecrCache(cachePtr);
	}
	Ns_MutexUnlock(&servPtr->adp.pagelock);
    }
    if (itPtr->adp.debugLevel > 0) {
	--itPtr->adp.debugLevel;
    }

done:
    Ns_DStringFree(&path);
    Ns_DStringFree(&tmp);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpDebug --
 *
 *	Initialize the debugger by calling the debug init proc with
 *	the hostname and port of the debugger and a pattern of procs
 *	to auto-instrument.
 *
 * Results:
 *	TCL_OK if debugger initialized, TCL_ERROR otherwise.
 *
 * Side effects:
 *	Interp is marked for delete on next deallocation.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpDebug(NsInterp *itPtr, char *host, char *port, char *procs)
{
    Tcl_Interp *interp = itPtr->interp;
    Tcl_DString ds;
    int code;

    code = TCL_OK;
    if (!itPtr->adp.debugInit) {
	itPtr->delete = 1;
	Tcl_DStringInit(&ds);
	Tcl_DStringAppendElement(&ds, itPtr->servPtr->adp.debuginit);
	Tcl_DStringAppendElement(&ds, procs ? procs : "");
	Tcl_DStringAppendElement(&ds, host ? host : "");
	Tcl_DStringAppendElement(&ds, port ? port : "");
	code = Tcl_EvalEx(interp, ds.string, ds.length, 0);
        Tcl_DStringFree(&ds);
	if (code != TCL_OK) {
	    NsAdpLogError(itPtr);
	    return TCL_ERROR;
	}

	/*
	 * Link the ADP output buffer result to a global variable
	 * which can be monitored with a variable watch.
	 */

	if (Tcl_LinkVar(interp, "ns_adp_output",
			(char *) &itPtr->adp.output.string,
		TCL_LINK_STRING | TCL_LINK_READ_ONLY) != TCL_OK) {
	    NsAdpLogError(itPtr);
	}

	itPtr->adp.debugInit = 1;
	itPtr->adp.debugLevel = 1;
    }
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpStatsCmd --
 *
 *	Implement the ns_adp_stats command to return stats on cached
 *	ADP pages.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpStatsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    FileKey *keyPtr;
    char buf[200];
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    Page *pagePtr;

    Ns_MutexLock(&servPtr->adp.pagelock);
    hPtr = Tcl_FirstHashEntry(&servPtr->adp.pages, &search);
    while (hPtr != NULL) {
	pagePtr = Tcl_GetHashValue(hPtr);
	keyPtr = (FileKey *) Tcl_GetHashKey(&servPtr->adp.pages, hPtr);
	Tcl_AppendElement(interp, pagePtr->file);
	sprintf(buf, "dev %ld ino %ld mtime %ld refcnt %d evals %d "
		     "size %ld blocks %d scripts %d",
		(long) keyPtr->dev, (long) keyPtr->ino, (long) pagePtr->mtime,
		pagePtr->refcnt, pagePtr->evals, (long) pagePtr->size,
		pagePtr->code.nblocks, pagePtr->code.nscripts);
	Tcl_AppendElement(interp, buf);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&servPtr->adp.pagelock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ParseFile --
 *
 *	Read and parse text from a file.  The code is complicated
 *	somewhat to account for changing files.
 *
 * Results:
 *	Pointer to new Page structure or NULL on error.
 *
 * Side effects:
 *	Error message will be left in interp on failure.
 *
 *----------------------------------------------------------------------
 */

static Page *
ParseFile(NsInterp *itPtr, char *file, struct stat *stPtr, int flags)
{
    Tcl_Interp *interp = itPtr->interp;
    Tcl_Encoding encoding;
    Tcl_DString utf;
    char *page, *buf;
    int fd, n, trys;
    size_t size;
    Page *pagePtr;
    
    fd = open(file, O_RDONLY | O_BINARY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    file, "\": ", Tcl_PosixError(interp), NULL);
	return NULL;
    }

    pagePtr = NULL;
    buf = NULL;
    trys = 0;
    do {
	/*
	 * fstat the open file to ensure it has not changed or been
	 * replaced since the original stat.
	 */

	if (fstat(fd, stPtr) != 0) {
	    Tcl_AppendResult(interp, "could not fstat \"", file,
	   	"\": ", Tcl_PosixError(interp), NULL);
	    goto done;
	}
    	size = stPtr->st_size;
    	buf = ns_realloc(buf, size + 1);

	/*
	 * Attempt to read +1 byte to catch the file growing.
	 */

    	n = read(fd, buf, size + 1);
    	if (n < 0) {
	    Tcl_AppendResult(interp, "could not read \"", file,
	    	"\": ", Tcl_PosixError(interp), NULL);
	    goto done;
	}
	if (n != size) {
	    /*
	     * File is not expected size, rewind and fstat/read again.
	     */
	
	    if (lseek(fd, (off_t) 0, SEEK_SET) != 0) {
	    	Tcl_AppendResult(interp, "could not lseek \"", file,
	    	    "\": ", Tcl_PosixError(interp), NULL);
	        goto done;
	    }
	    Ns_ThreadYield();
	}
    } while (n != size && ++trys < 10);

    if (n != size) {
	Tcl_AppendResult(interp, "inconsistant file: ", file, NULL);
    } else {
	buf[n] = '\0';
	Tcl_DStringInit(&utf);
	encoding = Ns_GetFileEncoding(file);
	if (encoding == NULL) {
	    page = buf;
	} else {
	    Tcl_ExternalToUtfDString(encoding, buf, n, &utf);
	    page = utf.string;
	}
	pagePtr = ns_malloc(sizeof(Page) + strlen(file));
	strcpy(pagePtr->file, file);
	pagePtr->servPtr = itPtr->servPtr;
	pagePtr->flags = flags;
	pagePtr->refcnt = 0;
	pagePtr->evals = 0;
	pagePtr->locked = 0;
	pagePtr->cacheGen = 0;
	pagePtr->cachePtr = NULL;
	pagePtr->mtime = stPtr->st_mtime;
	pagePtr->size = stPtr->st_size;
	NsAdpParse(&pagePtr->code, itPtr->servPtr, page, flags);
	Tcl_DStringFree(&utf);
    }

done:
    ns_free(buf);
    close(fd);
    return pagePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpLogError --
 *
 *	Log an ADP error, possibly invoking the log handling ADP
 *	file if configured.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on log handler.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpLogError(NsInterp *itPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    Ns_Conn *conn = itPtr->conn; 
    Ns_DString ds;
    Tcl_Obj *objv[2];
    AdpFrame *framePtr;
    char *adp, *inc, *dot, *err;
    int i, len;
    
    framePtr = itPtr->adp.framePtr;
    Ns_DStringInit(&ds);

    if (framePtr != NULL) {
        Ns_DStringPrintf(&ds, "\n    at line %d of ",
                        framePtr->line + Tcl_GetErrorLine(interp));
    }
    inc = "";
    while (framePtr != NULL) {
	if (framePtr->file != NULL) {
	    Ns_DStringPrintf(&ds, "%sadp file \"%s\"", inc, framePtr->file);
	    if (framePtr->ident != NULL) {
		Ns_DStringPrintf(&ds, " {%s}", Tcl_GetString(framePtr->ident));
	    }
	} else {
	    adp = Tcl_GetStringFromObj(framePtr->objv[0], &len);
	    dot = "";
	    if (len > 150) {
		len = 150;
		dot = "...";
	    }
	    while ((adp[len] & 0xC0) == 0x80) {
		/* NB: Avoid truncating multi-byte UTF-8 character. */
		len--;
		dot = "...";
	    }
	    Ns_DStringPrintf(&ds, "%sadp script:\n\"%.*s%s\"",
			     inc, len, adp, dot);
	}
	framePtr = framePtr->prevPtr;
	inc = "\n    included from ";
    }
    if (conn != NULL && (itPtr->adp.flags & ADP_DETAIL)) {
	Ns_DStringPrintf(&ds, "\n    while processing connection #%d:\n%8s%s",
			 Ns_ConnId(conn), "",
			 conn->request->line);
	for (i = 0; i < Ns_SetSize(conn->headers); ++i) {
	    Ns_DStringPrintf(&ds, "\n        %s: %s",
			     Ns_SetKey(conn->headers, i),
			     Ns_SetValue(conn->headers, i));
	}
    }
    Tcl_AddErrorInfo(interp, ds.string);
    err = Ns_TclLogError(interp);
    if (itPtr->adp.flags & ADP_DISPLAY) {
	Ns_DStringTrunc(&ds, 0);
	Ns_DStringAppend(&ds, "<br><pre>\n");
	Ns_QuoteHtml(&ds, err);
	Ns_DStringAppend(&ds, "\n<br></pre>\n");
	NsAdpAppend(itPtr, ds.string, ds.length);
    }
    Ns_DStringFree(&ds);
    adp = itPtr->servPtr->adp.errorpage;
    if (adp != NULL && itPtr->adp.errorLevel == 0) {
	++itPtr->adp.errorLevel;
	objv[0] = Tcl_NewStringObj(adp, -1);
	Tcl_IncrRefCount(objv[0]);
	objv[1] = Tcl_GetVar2Ex(interp, "errorInfo", NULL, TCL_GLOBAL_ONLY);
	if (objv[1] == NULL) {
	    objv[1] = Tcl_GetObjResult(interp);
	}
	(void) NsAdpInclude(itPtr, 2, objv, adp, NULL);
	Tcl_DecrRefCount(objv[0]);
	--itPtr->adp.errorLevel;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AdpExec --
 *
 *	Execute ADP code.
 *
 * Results:
 *	TCL_OK unless there is an ADP error exception, stack overflow,
 *	or script error when the ADP_STRICT option is set.
 *	
 * Side Effects:
 *	Depends on page.
 *
 *----------------------------------------------------------------------
 */

static int
AdpExec(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *file,
	AdpCode *codePtr, Objs *objsPtr, Tcl_DString *outputPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    AdpFrame frame;
    Ns_DString cwd;
    Tcl_Obj *objPtr;
    int nscript, nblocks, result, len, i;
    char *ptr, *slash, *savecwd;

    /*
     * Check for stack overflow.
     */

    if (Ns_CheckStack() == NS_BREAK) {
	Tcl_SetResult(interp, "adp stack overflow", TCL_STATIC);
	return TCL_ERROR;
    }

    /*
     * Setup the new call frame.
     */

    Ns_DStringInit(&cwd);
    frame.file = file;
    frame.objc = objc;
    frame.objv = objv;
    frame.outputPtr = outputPtr;
    frame.ident = NULL;
    savecwd = itPtr->adp.cwd;
    if (file != NULL && (slash = strrchr(file, '/')) != NULL) {
    	Ns_DStringNAppend(&cwd, file, slash - file);
	itPtr->adp.cwd = cwd.string;
    }
    frame.prevPtr = itPtr->adp.framePtr;
    itPtr->adp.framePtr = &frame;
    itPtr->adp.depth++;

    /*
     * Execute the ADP by copying text blocks directly to the output
     * stream and evaluating script blocks.
     */

    ptr = AdpCodeText(codePtr);
    nblocks = AdpCodeBlocks(codePtr);
    nscript = 0;
    result = TCL_OK;
    for (i = 0; itPtr->adp.exception == ADP_OK && i < nblocks; ++i) {
	frame.line = AdpCodeLine(codePtr, i);
	len = AdpCodeLen(codePtr, i);
	if (itPtr->adp.flags & ADP_TRACE) {
	    AdpTrace(itPtr, ptr, len);
	}
	if (len > 0) {
	    result = NsAdpAppend(itPtr, ptr, len);
	} else {
	    len = -len;
	    if (itPtr->adp.debugLevel > 0) {
    	        result = AdpDebug(itPtr, ptr, len, nscript);
	    } else if (objsPtr == NULL) {
		result = Tcl_EvalEx(interp, ptr, len, 0);
	    } else {
		objPtr = objsPtr->objs[nscript];
		if (objPtr == NULL) {
		    objPtr = Tcl_NewStringObj(ptr, len);
		    Tcl_IncrRefCount(objPtr);
		    objsPtr->objs[nscript] = objPtr;
		}
    	        result = Tcl_EvalObjEx(interp, objPtr, 0);
	    }
	    ++nscript;
	}

	/*
	 * Log an error message and optionally break from this ADP
	 * call frame unless the error was generated to signal
	 * and ADP exception.
	 */
	
	if (result != TCL_OK && itPtr->adp.exception == ADP_OK) {
	    if (!(itPtr->adp.flags & ADP_ERRLOGGED)) {
	    	NsAdpLogError(itPtr);
	    }
	    if (itPtr->adp.flags & ADP_STRICT) {
    	    	itPtr->adp.flags |= ADP_ERRLOGGED;
		break;
	    }
	}
	ptr += len;
    }

    /*
     * Clear the return exception and reset result.
     */

    switch (itPtr->adp.exception) {
    case ADP_OK:
	break;
    case ADP_RETURN:
	itPtr->adp.exception = ADP_OK;
	/* FALLTHROUGH */
    default: 
	result = TCL_OK;
	break;
    }

    /*
     * Restore the previous call frame.
     */

    itPtr->adp.framePtr = frame.prevPtr;
    itPtr->adp.cwd = savecwd;
    if (frame.ident != NULL) {
	Tcl_DecrRefCount(frame.ident);
    }
    Ns_DStringFree(&cwd);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * AdpDebug --
 *
 *	Evaluate an ADP script block with the TclPro debugger.
 *
 * Results:
 *	Depends on script.
 *
 * Side effects:
 *	A unique temp file with header comments and the script is
 *	created and sourced, the effect of which is TclPro will
 *	instrument the code on the fly for single-step debugging.
 *
 *----------------------------------------------------------------------
 */

static int
AdpDebug(NsInterp *itPtr, char *ptr, int len, int nscript)
{
    int code, fd;
    Tcl_Interp *interp = itPtr->interp;
    int level = itPtr->adp.debugLevel;
    char *file = Tcl_GetString(itPtr->adp.framePtr->objv[0]);
    char buf[10], debugfile[255];
    Ns_DString ds;

    code = TCL_ERROR;
    Ns_DStringInit(&ds);
    sprintf(buf, "%d", level);
    Ns_DStringVarAppend(&ds,
	"#\n"
	"# level: ", buf, "\n", NULL);
    sprintf(buf, "%d", nscript);
    Ns_DStringVarAppend(&ds,
	"# chunk: ", buf, "\n"
	"# file:  ", file, "\n"
	"#\n\n", NULL);
    Ns_DStringNAppend(&ds, ptr, len);
    sprintf(debugfile, P_tmpdir "/adp%d.%d.XXXXXX", level, nscript);
    if (mktemp(debugfile) == NULL) {
	Tcl_SetResult(interp, "could not create adp debug file", TCL_STATIC);
    } else {
	fd = open(debugfile, O_WRONLY|O_TRUNC|O_CREAT, 0644);
	if (fd < 0) {
	    Tcl_AppendResult(interp, "could not create adp debug file \"",
		debugfile, "\": ", Tcl_PosixError(interp), NULL);
	} else {
	    if (write(fd, ds.string, (size_t)ds.length) < 0) {
		Tcl_AppendResult(interp, "write to \"", debugfile,
		    "\" failed: ", Tcl_PosixError(interp), NULL);
	    } else {
		Ns_DStringTrunc(&ds, 0);
		Ns_DStringVarAppend(&ds, "source ", debugfile, NULL);
		code = Tcl_EvalEx(interp, ds.string, ds.length, 0);
	    }
	    close(fd);
	    unlink(debugfile);
	}
    }
    Ns_DStringFree(&ds);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeInterpPage --
 *
 *  	Free a per-interp page cache entry.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeInterpPage(void *arg)
{
    InterpPage *ipagePtr = arg;
    Page *pagePtr = ipagePtr->pagePtr;
    NsServer *servPtr = pagePtr->servPtr;

    FreeObjs(ipagePtr->objs);
    Ns_MutexLock(&servPtr->adp.pagelock);
    if (--pagePtr->refcnt == 0) {
	if (pagePtr->hPtr != NULL) {
	    Tcl_DeleteHashEntry(pagePtr->hPtr);
	}
	if (pagePtr->cachePtr != NULL) {
    	    FreeObjs(ipagePtr->cacheObjs);
	    DecrCache(pagePtr->cachePtr);
	}
	NsAdpFreeCode(&pagePtr->code);
	ns_free(pagePtr);
    }
    Ns_MutexUnlock(&servPtr->adp.pagelock);
    ns_free(ipagePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AllocObjs --
 *
 *  	Allocate new page script objects.
 *
 * Results:
 *	Pointer to new objects.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Objs *
AllocObjs(int nobjs)
{
    Objs *objsPtr;

    objsPtr = ns_calloc(1, sizeof(Objs) + (nobjs * sizeof(Tcl_Obj *)));
    objsPtr->nobjs = nobjs;
    return objsPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeObjs --
 *
 *  	Free page objects, decrementing ref counts as needed.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeObjs(Objs *objsPtr)
{
    int i;

    for (i = 0; i < objsPtr->nobjs; ++i) {
	if (objsPtr->objs[i] != NULL) {
	    Tcl_DecrRefCount(objsPtr->objs[i]);
	}
    }
    ns_free(objsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * DecrCache --
 *
 *  	Decrement ref count of a cache entry, potentially freeing
 *	the cache.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Will free cache on last reference count.
 *
 *----------------------------------------------------------------------
 */

static void
DecrCache(AdpCache *cachePtr)
{
    if (--cachePtr->refcnt == 0) {
	NsAdpFreeCode(&cachePtr->code);
	ns_free(cachePtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AdpTrace --
 *
 *	Trace execution of an ADP page.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Dumps tracing info, possibly truncated, via Ns_Log.
 *
 *----------------------------------------------------------------------
 */

static void
AdpTrace(NsInterp *itPtr, char *ptr, int len)
{
    char type;

    if (len >= 0) {
	type = 'T';
    } else {
	type = 'S';
	len = -len;
    }
    if (len > itPtr->servPtr->adp.tracesize) {
	len = itPtr->servPtr->adp.tracesize;
    }
    Ns_Log(Notice, "adp[%d%c]: %.*s", itPtr->adp.depth, type, len, ptr);
}

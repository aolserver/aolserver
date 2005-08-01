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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpeval.c,v 1.35 2005/08/01 20:27:22 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a cached ADP page result.
 */

typedef struct AdpCache {
    int	      	   refcnt;
    Ns_Time	   expires;
    AdpCode	   code;
} AdpCache;

/*
 * The following structure defines a shared page in the ADP cache.  The
 * bytes for the filename as well as the block lengths array and page text 
 * referenced in the code structure are allocated with the Page struct
 * as one contiguous block of memory.
 */

typedef struct Page {
    NsServer	  *servPtr;
    Tcl_HashEntry *hPtr;
    time_t    	   mtime;
    off_t     	   size;
    int	      	   refcnt;
    int		   evals;
    int		   locked;
    int		   cgen;
    AdpCache	  *cachePtr;
    AdpCode	   code;
    char	   file[1];
} Page;

/*
 * The following structure holds per-interp script byte codes.
 */

typedef struct Objs {
    int nobjs;
    Tcl_Obj *objs[1];
} Objs;

/*
 * The following structure defines a per-interp page entry with
 * a pointer to the shared Page and private Objs for cached and
 * non-cached page results.
 */

typedef struct InterpPage {
    Page     *pagePtr;
    int	      cgen;
    Objs     *objs;
    Objs     *cobjs;
} InterpPage;

/*
 * Local functions defined in this file.
 */

static Page *ParseFile(NsInterp *itPtr, char *file, struct stat *stPtr);
static void AdpLogError(NsInterp *itPtr);
static int AdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[],
		char *resvar, int safe, int file);
static int AdpRun(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[],
		Tcl_DString *outputPtr, Ns_Time *ttlPtr);
static int AdpEval(NsInterp *itPtr, AdpCode *codePtr, Objs *objsPtr);
static int AdpDebug(NsInterp *itPtr, char *ptr, int len, int nscript);
static void PushFrame(NsInterp *itPtr, AdpFrame *framePtr, char *file, int objc,
	  	      Tcl_Obj *objv[], Tcl_DString *outputPtr);
static void PopFrame(NsInterp *itPtr);
static void DecrCache(AdpCache *cachePtr);
static Objs *AllocObjs(int nobjs);
static void FreeObjs(Objs *objsPtr);
static void AdpTrace(NsInterp *itPtr, char *ptr, int len);
static Ns_Callback FreeInterpPage;


/*
 *----------------------------------------------------------------------
 *
 * NsAdpEval --
 *
 *	Evaluate an ADP string.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	String is parsed and evaluated at current Tcl level in a
 *	new ADP call frame.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpEval(NsInterp *itPtr, int objc, Tcl_Obj *objv[], int safe, char *resvar)
{
    return AdpSource(itPtr, objc, objv, resvar, safe ? ADP_SAFE : 0, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpSource, NsAdpInclude --
 *
 *	Evaluate an ADP file, utilizing per-thread byte-code pages.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output is either left in the ADP buffer (NsAdpInclude) or
 *	moved to the interp result (NsAdpSource).
 *
 *----------------------------------------------------------------------
 */

int
NsAdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *resvar)
{
    return AdpSource(itPtr, objc, objv, resvar, 0, 1);
}


int
NsAdpInclude(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[],
		Ns_Time *ttlPtr)
{
    Tcl_DString *dsPtr = &itPtr->adp.output;

    return AdpRun(itPtr, file, objc, objv, dsPtr, ttlPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AdpSource --
 *
 *	Evaluate ADP code, either in a string or file, with the output
 *	returned as the interp result.
 *
 * Results:
 *	Tcl result from AdpRun or AdpEval.
 *
 * Side effects:
 *	Variable named by resvar, if any, is updated with results of
 *	Tcl interp before being replaced with ADP output.
 *
 *----------------------------------------------------------------------
 */

static int
AdpSource(NsInterp *itPtr, int objc, Tcl_Obj *objv[], char *resvar,
	  int safe, int file)
{
    Tcl_Interp	     *interp;
    AdpCode	      code;
    AdpFrame          frame;
    Tcl_DString       output;
    Tcl_Obj	     *objPtr;
    int               result;
    char	     *obj0;
    
    /*
     * Push a frame, execute the code, and then move any result to the
     * interp from the local output buffer.
     */

    Tcl_DStringInit(&output);
    obj0 = Tcl_GetString(objv[0]);
    if (file) {
    	result = AdpRun(itPtr, obj0, objc, objv, &output, 0);
    } else {
    	PushFrame(itPtr, &frame, NULL, objc, objv, &output);
    	NsAdpParse(&code, itPtr->servPtr, obj0, safe ? ADP_SAFE : 0);
    	result = AdpEval(itPtr, &code, NULL);
    	PopFrame(itPtr);
    	NsAdpFreeCode(&code);
    }
    if (result == TCL_OK) {
        /*
         * If the caller has supplied a variable for the adp's result value,
         * then save the interp's result there prior to overwritting it.
         */

	interp = itPtr->interp;
	objPtr = Tcl_GetObjResult(interp);
        if (resvar != NULL && Tcl_SetVar2Ex(interp, resvar, NULL, objPtr,
					    TCL_LEAVE_ERR_MSG) == NULL) {
            result = TCL_ERROR;
        } else { 
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
 * AdpRun --
 *
 *	Execute ADP code in a file with results returned in given
 *	dstring.
 *
 * Results:
 *	TCL_OK if file could be parsed and executated, TCL_ERROR otherwise.
 *	Note the result of the scripts executed is ignored.
 *
 * Side effects:
 *	Page text and ADP code results may be cached up to given time
 *	limit, if any.
 *
 *----------------------------------------------------------------------
 */

static int
AdpRun(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[],
       Tcl_DString *outputPtr, Ns_Time *ttlPtr)
{
    NsServer  *servPtr = itPtr->servPtr;
    Tcl_Interp *interp = itPtr->interp;
    Tcl_HashEntry *hPtr;
    struct stat st;
    Ns_DString tmp, path;
    AdpFrame frame;
    InterpPage *ipagePtr;
    Page *pagePtr, *oldPagePtr;
    AdpCache *cachePtr;
    AdpCode *codePtr;
    Ns_Time now;
    Ns_Entry *ePtr;
    Objs *objsPtr;
    int new, len, cgen;
    char *p, *key;
    FileKey ukey;
    int status;

    ipagePtr = NULL;
    pagePtr = NULL; 
    status = TCL_ERROR;   /* assume error until accomplished success */
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
    } else if ((servPtr->adp.flags & ADP_DEBUG) &&
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
			|| ipagePtr->pagePtr->size != st.st_size) {
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
			|| pagePtr->size != st.st_size)) {
		/* NB: Clear entry to indicate read/parse in progress. */
		Tcl_SetHashValue(hPtr, NULL);
		pagePtr->hPtr = NULL;
		new = 1;
	    }
	    if (new) {
		Ns_MutexUnlock(&servPtr->adp.pagelock);
		pagePtr = ParseFile(itPtr, file, &st);
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
		ipagePtr->cgen = 0;
		ipagePtr->objs = AllocObjs(pagePtr->code.nscripts);
		ipagePtr->cobjs = NULL;
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
	if (ttlPtr == NULL) {
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
		Ns_DString buf;
		Ns_DStringInit(&buf);
		len = outputPtr->length;
	    	Ns_MutexUnlock(&servPtr->adp.pagelock);
		codePtr = &pagePtr->code;
    		PushFrame(itPtr, &frame, file, objc, objv, &buf);
		++itPtr->adp.refresh;
		status = AdpEval(itPtr, codePtr, ipagePtr->objs);
		--itPtr->adp.refresh;
    		PopFrame(itPtr);
		cachePtr = ns_malloc(sizeof(AdpCache));
		NsAdpParse(&cachePtr->code, itPtr->servPtr, buf.string, 0);
		Ns_DStringFree(&buf);
		Ns_GetTime(&cachePtr->expires);
		Ns_IncrTime(&cachePtr->expires, ttlPtr->sec, ttlPtr->usec);
	    	cachePtr->refcnt = 1;
	    	Ns_MutexLock(&servPtr->adp.pagelock);
		if (pagePtr->cachePtr != NULL) {
		    DecrCache(pagePtr->cachePtr);
		}
		++pagePtr->cgen;
		pagePtr->cachePtr = cachePtr;
		pagePtr->locked = 0;
		Ns_CondBroadcast(&servPtr->adp.pagecond);
	    }
	    cgen = pagePtr->cgen;
	    ++cachePtr->refcnt;
	    Ns_MutexUnlock(&servPtr->adp.pagelock);
	}
	if (cachePtr == NULL) {
	   codePtr = &pagePtr->code;
	   objsPtr = ipagePtr->objs;
	} else {
	    codePtr = &cachePtr->code;
	    if (ipagePtr->cobjs != NULL && cgen != ipagePtr->cgen) {
		FreeObjs(ipagePtr->cobjs);
		ipagePtr->cobjs = NULL;
	    }
	    if (ipagePtr->cobjs == NULL) {
		ipagePtr->cobjs = AllocObjs(AdpCodeScripts(codePtr));
		ipagePtr->cgen = cgen;
	    }
	    objsPtr = ipagePtr->cobjs;
	}
    	PushFrame(itPtr, &frame, file, objc, objv, outputPtr);
	status = AdpEval(itPtr, codePtr, objsPtr);
    	PopFrame(itPtr);
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
    return status;
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
	    Ns_TclLogError(interp);
	    return TCL_ERROR;
	}

	/*
	 * Link the ADP output buffer result to a global variable
	 * which can be monitored with a variable watch.
	 */

	if (Tcl_LinkVar(interp, "ns_adp_output",
			(char *) &itPtr->adp.output.string,
		TCL_LINK_STRING | TCL_LINK_READ_ONLY) != TCL_OK) {
	    Ns_TclLogError(interp);
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
 * PushFrame --
 *
 *	Push an ADP call frame on the ADP stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The given AdpFrame is initialized with the current context,
 *	constructing a new current working directory from the given
 *	filename if not NULL.  If this is the first frame on the stack,
 *	ADP options are set to their defaults.
 *
 *----------------------------------------------------------------------
 */

static void
PushFrame(NsInterp *itPtr, AdpFrame *framePtr, char *file, int objc,
	  Tcl_Obj *objv[], Tcl_DString *outputPtr)
{
    char    *slash;

    framePtr->objc = objc;
    framePtr->objv = objv;
    framePtr->outputPtr = outputPtr;
    framePtr->prevPtr = itPtr->adp.framePtr;
    Ns_DStringInit(&framePtr->cwdbuf);
    framePtr->savecwd = itPtr->adp.cwd;
    if (file != NULL && (slash = strrchr(file, '/')) != NULL) {
    	Ns_DStringNAppend(&framePtr->cwdbuf, file, slash - file);
	itPtr->adp.cwd = framePtr->cwdbuf.string;
    }
    itPtr->adp.framePtr = framePtr;
    if (itPtr->adp.depth++ == 0) {
	itPtr->adp.exception = ADP_OK;
	itPtr->adp.debugLevel = 0;
	itPtr->adp.debugInit = 0;
	itPtr->adp.debugFile = NULL;
	itPtr->adp.bufsize = itPtr->servPtr->adp.bufsize;
	itPtr->adp.flags = (itPtr->servPtr->adp.flags & (ADP_GZIP|ADP_TRACE));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PopFrame --
 *
 *	Pops the current AdpFrame from the ADP context stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Previous AdpFrame, if any, is restored.  If this is the last
 *	frame on the stack, output is flushed.
 *
 *----------------------------------------------------------------------
 */

static void
PopFrame(NsInterp *itPtr)
{
    AdpFrame *framePtr;

    framePtr = itPtr->adp.framePtr;
    itPtr->adp.framePtr = framePtr->prevPtr;;
    itPtr->adp.cwd = framePtr->savecwd;
    Ns_DStringFree(&framePtr->cwdbuf);
    if (--itPtr->adp.depth == 0) {
	NsAdpFlush(itPtr, 0);
    }
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
ParseFile(NsInterp *itPtr, char *file, struct stat *stPtr)
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
	pagePtr->refcnt = 0;
	pagePtr->evals = 0;
	pagePtr->locked = 0;
	pagePtr->cgen = 0;
	pagePtr->cachePtr = NULL;
	pagePtr->mtime = stPtr->st_mtime;
	pagePtr->size = stPtr->st_size;
	NsAdpParse(&pagePtr->code, itPtr->servPtr, page, 0);
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
 * AdpLogError --
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

static void
AdpLogError(NsInterp *itPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    Ns_Conn *conn = itPtr->conn; 
    Ns_DString ds;
    Tcl_Obj *objv[2];
    AdpFrame *framePtr;
    char *file;
    int i;
    
    framePtr = itPtr->adp.framePtr;
    Ns_DStringInit(&ds);
    Ns_DStringPrintf(&ds, "\n    at line %d in ",
		     framePtr->line + interp->errorLine);
    while (framePtr != NULL) {
	Ns_DStringPrintf(&ds, "adp %.40s", Tcl_GetString(framePtr->objv[0]));
	framePtr = framePtr->prevPtr;
	if (framePtr != NULL) {
	    Ns_DStringAppend(&ds, "\n    included from ");
	}
    }
    if (conn != NULL && (itPtr->servPtr->adp.flags & ADP_DETAIL)) {
	Ns_DStringPrintf(&ds, "\n    while processing connection #%d:\n%8s%s",
			 Ns_ConnId(conn), "",
			 conn->request->line);
	for (i = 0; i < Ns_SetSize(conn->headers); ++i) {
	    Ns_DStringPrintf(&ds, "\n%8s%s: %s", "",
			     Ns_SetKey(conn->headers, i),
			     Ns_SetValue(conn->headers, i));
	}
    }
    Tcl_AddErrorInfo(interp, ds.string);
    Ns_TclLogError(interp);
    Ns_DStringFree(&ds);

    file = itPtr->servPtr->adp.errorpage;
    if (file != NULL && itPtr->adp.errorLevel == 0) {
	++itPtr->adp.errorLevel;
	objv[0] = Tcl_NewStringObj(file, -1);
	Tcl_IncrRefCount(objv[0]);
	objv[1] = Tcl_GetVar2Ex(interp, "errorInfo", NULL, TCL_GLOBAL_ONLY);
	if (objv[1] == NULL) {
	    objv[1] = Tcl_GetObjResult(interp);
	}
	(void) NsAdpInclude(itPtr, file, 2, objv, NULL);
	Tcl_DecrRefCount(objv[0]);
	--itPtr->adp.errorLevel;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AdpEval --
 *
 *	Evaluate page code.
 *
 * Results:
 *	A Tcl status code.
 *
 * Side Effects:
 *	Depends on page.
 *
 *----------------------------------------------------------------------
 */

static int
AdpEval(NsInterp *itPtr, AdpCode *codePtr, Objs *objsPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    AdpFrame *framePtr = itPtr->adp.framePtr;
    Tcl_Obj *objPtr;
    int nscript, nblocks, result, len, i;
    char *ptr;

    ptr = AdpCodeText(codePtr);
    nblocks = AdpCodeBlocks(codePtr);
    nscript = 0;
    result = TCL_OK;
    for (i = 0; itPtr->adp.exception == ADP_OK && i < nblocks; ++i) {
	framePtr->line = AdpCodeLine(codePtr, i);
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
	if (result != TCL_OK && itPtr->adp.exception == ADP_OK) {
    	    AdpLogError(itPtr);
	}
	ptr += len;
    }
    if (itPtr->adp.exception == ADP_RETURN) {
	itPtr->adp.exception = ADP_OK;
	result = TCL_OK;
    }
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
    	    FreeObjs(ipagePtr->cobjs);
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
    if (len > 40) {
	len = 40;
    }
    Ns_Log(Notice, "adp[%d%c]: %.*s", itPtr->adp.depth, type, len, ptr);
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpeval.c,v 1.22 2003/03/05 19:56:57 mpagenva Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

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
    char	  *file;
    AdpCode	   code;
} Page;

/*
 * The following structure defines a per-interp page entry with
 * a pointer to the shared Page and private script Tcl_Obj's.
 */

typedef struct InterpPage {
    Page     *pagePtr;
    Tcl_Obj  *objs[0];
} InterpPage;

/*
 * The following structure maintains an ADP call frame.  PushFrame
 * is used to save the previous state of the per-thread NsInterp
 * structure in a Frame allocated on the stack and PopFrame restores
 * the previous state from the Frame.
 */

typedef struct Frame {
    int                objc;
    Tcl_Obj          **objv;
    char              *cwd;
    Ns_DString         cwdBuf;
    Tcl_DString	      *outputPtr;
} Frame;

/*
 * Local functions defined in this file.
 */

static Page *ParseFile(NsInterp *itPtr, char *file, struct stat *stPtr);
static void PushFrame(NsInterp *itPtr, Frame *framePtr, char *file, 
		      int objc, Tcl_Obj *objv[], Tcl_DString *outputPtr);
static void PopFrame(NsInterp *itPtr, Frame *framePtr);
static void LogError(NsInterp *itPtr, int nscript);
static int AdpRun(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[],
		Tcl_DString *outputPtr);
static int AdpEval(NsInterp *itPtr, AdpCode *codePtr, Tcl_Obj *objs[]);
static void ParseFree(AdpParse *parsePtr);
static int AdpDebug(NsInterp *itPtr, char *ptr, int len, int nscript);
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
    AdpParse	      parse;
    Frame             frame;
    Tcl_DString       output;
    int               result;
    Tcl_Obj          *resPtr;
    bool              lcl_resp = NS_FALSE;
    
    /*
     * Push a frame, execute the code, and then move any result to the
     * interp from the local output buffer.
     */
     
    Tcl_DStringInit(&output);

    /*
     * If we are not acting within a context which has set up the adp
     * output buffer, then hook the responsePtr to our lcl buffer
     * for the processing of this request.  It will be unhooked after.
     */
    if (itPtr->adp.responsePtr == NULL) {
        itPtr->adp.responsePtr = &output;
        lcl_resp = NS_TRUE;
    }

    PushFrame(itPtr, &frame, NULL, objc, objv, &output);
    NsAdpParse(&parse, itPtr->servPtr, Tcl_GetString(objv[0]), safe);
    result = AdpEval(itPtr, &parse.code, NULL);
    PopFrame(itPtr, &frame);

    if (lcl_resp) {
        itPtr->adp.responsePtr = NULL;
    }

    if (result == TCL_OK) {
        /*
         * If the caller has supplied a variable for the adp's result value,
         * then save the interp's result there prior to overwritting it.
         */
        if (resvar != NULL) {
            resPtr = Tcl_GetObjResult(itPtr->interp);
            if (Tcl_SetVar2Ex(itPtr->interp, resvar, NULL, resPtr,
                              TCL_LEAVE_ERR_MSG) == NULL) {
                return TCL_ERROR;
            }
        }

        Tcl_SetResult(itPtr->interp, output.string, TCL_VOLATILE);
    }

    Tcl_DStringFree(&output);
    ParseFree(&parse);
    return result;
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
    Tcl_DString output;
    int code;
    Tcl_Obj     *resPtr;
    bool         lcl_resp = NS_FALSE;

    /*
     * Direct output to a local buffer.
     */

    Tcl_DStringInit(&output);

    /*
     * If we are not acting within a context which has set up the adp
     * output buffer, then hook the responsePtr to our lcl buffer
     * for the processing of this request.  It will be unhooked after.
     */
    if (itPtr->adp.responsePtr == NULL) {
        itPtr->adp.responsePtr = &output;
        lcl_resp = NS_TRUE;
    }

    code = AdpRun(itPtr, Tcl_GetString(objv[0]), objc, objv, &output);

    if (lcl_resp) {
        itPtr->adp.responsePtr = NULL;
    }

    if (code == TCL_OK) {
        /*
         * If the caller has supplied a variable for the adp's result value,
         * then save the interp's result there prior to overwritting it.
         */
        if (resvar != NULL) {
            resPtr = Tcl_GetObjResult(itPtr->interp);
            if (Tcl_SetVar2Ex(itPtr->interp, resvar, NULL, resPtr,
                              TCL_LEAVE_ERR_MSG) == NULL) {
                return TCL_ERROR;
            }
        }

	Tcl_DStringResult(itPtr->interp, &output);
    }
    Tcl_DStringFree(&output);
    return code;
}

int
NsAdpInclude(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[])
{
    /*
     * Direct output to the ADP response buffer.
     */

    if (itPtr->adp.responsePtr == NULL) {
	Tcl_SetResult(itPtr->interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    return AdpRun(itPtr, file, objc, objv, itPtr->adp.responsePtr);
}

static int
AdpRun(NsInterp *itPtr, char *file, int objc, Tcl_Obj *objv[],
       Tcl_DString *outputPtr)
{
    NsServer  *servPtr = itPtr->servPtr;
    Tcl_Interp *interp = itPtr->interp;
    Tcl_HashEntry *hPtr;
    struct stat st;
    Ns_DString tmp, path;
    Frame frame;
    InterpPage *ipagePtr;
    Page *pagePtr, *oldPagePtr;
    Ns_Entry *ePtr;
    int new, n;
    char *p, *key;
    FileKey ukey;

    ipagePtr = NULL;
    pagePtr = NULL;    
    Ns_DStringInit(&tmp);
    Ns_DStringInit(&path);
    key = (char *) &ukey;
    
    /*
     * Construct the full, normalized path to the ADP file.
     */

    if (Ns_PathIsAbsolute(file)) {
	Ns_NormalizePath(&path, file);
    } else {
	Ns_MakePath(&tmp, itPtr->adp.cwd, file, NULL);
	Ns_NormalizePath(&path, tmp.string);
	Ns_DStringTrunc(&tmp, 0);
    }
    file = path.string;

    /*
     * Check for TclPro debugging.
     */

    if (itPtr->adp.debugLevel > 0) {
	++itPtr->adp.debugLevel;
    } else if (servPtr->adp.enabledebug != NS_FALSE &&
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
	Ns_DStringPrintf(&tmp, "nsadp:%s:%p", itPtr->servPtr->server, itPtr);
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
    	    if (ipagePtr->pagePtr->mtime != st.st_mtime || ipagePtr->pagePtr->size != st.st_size) {
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
	    if (!new && (pagePtr->mtime != st.st_mtime || pagePtr->size != st.st_size)) {
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
            if (pagePtr->mtime != st.st_mtime || pagePtr->size != st.st_size) {
#else
            if (ukey.dev != st.st_dev || ukey.ino != st.st_ino) {
#endif
			/* NB: File changed between stat above and ParseFile. */
		    	Tcl_DeleteHashEntry(hPtr);
#ifndef _WIN32
                ukey.dev = st.st_dev;
		    	ukey.ino = st.st_ino;
#endif
		    	hPtr = Tcl_CreateHashEntry(&servPtr->adp.pages, key, &new);
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
		n = sizeof(Tcl_Obj *) * pagePtr->code.nscripts;
	    	ipagePtr = ns_calloc(1, sizeof(InterpPage) + n);
		ipagePtr->pagePtr = pagePtr;
        	ePtr = Ns_CacheCreateEntry(itPtr->adp.cache, key, &new);
		if (!new) {
		    Ns_CacheUnsetValue(ePtr);
		}
                Ns_CacheSetValueSz(ePtr, ipagePtr, ipagePtr->pagePtr->size);
	    }
	}
    }
    
    /*
     * If valid page was found, evaluate it in a new call frame.
     */
         
    if (ipagePtr != NULL) {
    	PushFrame(itPtr, &frame, file, objc, objv, outputPtr);
	AdpEval(itPtr, &ipagePtr->pagePtr->code, ipagePtr->objs);
    	PopFrame(itPtr, &frame);
	Ns_MutexLock(&servPtr->adp.pagelock);
	++ipagePtr->pagePtr->evals;
	Ns_MutexUnlock(&servPtr->adp.pagelock);
    }
    if (itPtr->adp.debugLevel > 0) {
	--itPtr->adp.debugLevel;
    }

done:
    Ns_DStringFree(&path);
    Ns_DStringFree(&tmp);
    return TCL_OK;
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
	 * Link the ADP response buffer result to a global variable
	 * which can be monitored with a variable watch.
	 */

	if (itPtr->adp.responsePtr != NULL &&
	    Tcl_LinkVar(interp, "ns_adp_output",
			(char *) &itPtr->adp.responsePtr->string,
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
	sprintf(buf, "dev %d ino %d mtime %d refcnt %d evals %d size %d blocks %d scripts %d",
		keyPtr->dev, keyPtr->ino, (int) pagePtr->mtime, pagePtr->refcnt,
		pagePtr->evals, (int) pagePtr->size, pagePtr->code.nblocks,
		pagePtr->code.nscripts);
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
 *	The given Frame is initialized, the current working directory
 *	is determined from the absolute filename (if not NULL), the
 *	previous state of the per-thread NsInterp structure is saved
 *	and then updated with the current call's arguments.
 *
 *----------------------------------------------------------------------
 */

static void
PushFrame(NsInterp *itPtr, Frame *framePtr, char *file, int objc,
	  Tcl_Obj *objv[], Tcl_DString *outputPtr)
{
    char    *slash;

    /*
     * Save current NsInterp state.
     */

    framePtr->cwd = itPtr->adp.cwd;
    framePtr->objc = itPtr->adp.objc;
    framePtr->objv = itPtr->adp.objv;
    framePtr->outputPtr = itPtr->adp.outputPtr;
    itPtr->adp.outputPtr = outputPtr;
    itPtr->adp.objc = objc;
    itPtr->adp.objv = objv;
    ++itPtr->adp.depth;

    /*
     * If file is not NULL it indicates a call from
     * NsAdpSource or NsAdpInclude.  If so, update the
     * current working directory based on the
     * absolute file pathname.
     */

    Ns_DStringInit(&framePtr->cwdBuf);
    if (file != NULL) {
	slash = strrchr(file, '/');
    	Ns_DStringNAppend(&framePtr->cwdBuf, file, slash - file);
    	itPtr->adp.cwd = framePtr->cwdBuf.string;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PopFrame --
 *
 *	Pop a previously pushed ADP call frame from the ADP stack.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Previous state of the per-thread NsInterp structure is restored
 *	and the Frame is free'ed.
 *
 *----------------------------------------------------------------------
 */

static void
PopFrame(NsInterp *itPtr, Frame *framePtr)
{
    /*
     * Restore the previous frame.
     */

    itPtr->adp.objc = framePtr->objc;
    itPtr->adp.objv = framePtr->objv;
    itPtr->adp.cwd = framePtr->cwd;
    itPtr->adp.outputPtr = framePtr->outputPtr;
    --itPtr->adp.depth;
    Ns_DStringFree(&framePtr->cwdBuf);
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
    int fd, n, size, trys;
    Page *pagePtr;
    AdpParse parse;
    
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
	
	    if (lseek(fd, 0, SEEK_SET) != 0) {
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
	NsAdpParse(&parse, itPtr->servPtr, page, 0);
	Tcl_DStringFree(&utf);
	n = parse.hdr.length + parse.text.length + strlen(file) + 1;
	pagePtr = ns_malloc(sizeof(Page) + n);
	pagePtr->servPtr = itPtr->servPtr;
	pagePtr->refcnt = 0;
	pagePtr->evals = 0;
	pagePtr->mtime = stPtr->st_mtime;
	pagePtr->size = stPtr->st_size;
	pagePtr->code.nblocks = parse.code.nblocks;
	pagePtr->code.nscripts = parse.code.nscripts;
	pagePtr->code.len = (int *) (pagePtr + 1);
	pagePtr->code.base = (char *) (pagePtr->code.len + parse.code.nblocks);
	pagePtr->file = pagePtr->code.base + parse.text.length;
	memcpy(pagePtr->code.len, parse.hdr.string, parse.hdr.length);
	memcpy(pagePtr->code.base, parse.text.string, parse.text.length);
	strcpy(pagePtr->file, file);
	ParseFree(&parse);
    }

done:
    ns_free(buf);
    close(fd);
    return pagePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * LogError --
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
LogError(NsInterp *itPtr, int nscript)
{
    Tcl_Interp *interp = itPtr->interp;
    Ns_DString ds;
    Tcl_Obj *objv[2];
    char *file;
    
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, "\n    invoked from within chunk: ");
    Ns_DStringPrintf(&ds, "%d", nscript);
    Ns_DStringAppend(&ds, " of adp: ");
    Ns_DStringAppend(&ds, Tcl_GetString(itPtr->adp.objv[0]));
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
	(void) NsAdpInclude(itPtr, file, 2, objv);
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
AdpEval(NsInterp *itPtr, AdpCode *codePtr, Tcl_Obj **objs)
{
    Tcl_Interp *interp = itPtr->interp;
    Tcl_Obj *objPtr;
    int nscript, result, len, i;
    char *ptr;

    ptr = codePtr->base;
    nscript = 0;
    result = TCL_OK;
    for (i = 0; itPtr->adp.exception == ADP_OK && i < codePtr->nblocks; ++i) {
	len = codePtr->len[i];
	if (len > 0) {
	    Ns_DStringNAppend(itPtr->adp.outputPtr, ptr, len);
	} else {
	    len = -len;
	    if (itPtr->adp.debugLevel > 0) {
    	        result = AdpDebug(itPtr, ptr, len, nscript);
	    } else if (objs == NULL) {
		result = Tcl_EvalEx(interp, ptr, len, 0);
	    } else {
		objPtr = objs[nscript];
		if (objPtr != NULL) {
    	            result = Tcl_EvalObjEx(interp, objPtr, 0);
		} else {
		    objPtr = Tcl_NewStringObj(ptr, len);
		    Tcl_IncrRefCount(objPtr);
    	            result = Tcl_EvalObjEx(interp, objPtr, 0);
		    objs[nscript] = objPtr;
		}
	    }
	    if (result != TCL_OK && result != TCL_RETURN
		    && itPtr->adp.exception == ADP_OK) {
    	    	LogError(itPtr, nscript);
	    }
	    ++nscript;
	}
	ptr += len;
	NsAdpFlush(itPtr);
    }
    if (itPtr->adp.exception == ADP_RETURN) {
	itPtr->adp.exception = ADP_OK;
	result = TCL_OK;
    }
    NsAdpFlush(itPtr);
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
    char *file = Tcl_GetString(itPtr->adp.objv[0]);
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
	    if (write(fd, ds.string, ds.length) < 0) {
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
    int i;

    for (i = 0; i < pagePtr->code.nscripts; ++i) {
	if (ipagePtr->objs[i] != NULL) {
	    Tcl_DecrRefCount(ipagePtr->objs[i]);
	}
    }
    Ns_MutexLock(&servPtr->adp.pagelock);
    if (--pagePtr->refcnt == 0) {
	if (pagePtr->hPtr != NULL) {
	    Tcl_DeleteHashEntry(pagePtr->hPtr);
	}
	ns_free(pagePtr);
    }
    Ns_MutexUnlock(&servPtr->adp.pagelock);
    ns_free(ipagePtr);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseFree --
 *
 *  	Free buffers in an AdpParse structure.
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
ParseFree(AdpParse *parsePtr)
{
    Tcl_DStringFree(&parsePtr->hdr);
    Tcl_DStringFree(&parsePtr->text);
}

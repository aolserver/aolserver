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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpeval.c,v 1.8 2001/06/26 22:09:59 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure defines a block in an ADP as either
 * text or compiled byte code object.
 */

typedef struct Block {
    struct Block *nextPtr;
    Tcl_Obj      *scriptObjPtr;
    int           length;
    char          text[4];
} Block;

/*
 * The following structure defines a parsed file in the ADP cache.
 */

typedef struct Page {
    time_t    mtime;
    off_t     size;
    off_t     length;
    Block    *firstPtr;
    char      chunks[4];
} Page;

/*
 * The following structure maintains an ADP call frame.  PushFrame
 * is used to save the previous state of the per-thread NsInterp
 * structure in a Frame allocated on the stack and PopFrame restores
 * the previous state from the Frame.
 */

typedef struct {
    int                argc;
    char             **argv;
    char              *cwd;
    char	      *file;
    Ns_DString         cwdBuf;
    Tcl_DString	      *outputPtr;
} Frame;

/*
 * The following structure is used as a unique key for compiled pages
 * in the cache.
 */

#ifdef WIN32
#define CACHE_KEYS TCL_STRING_KEYS
#else
typedef struct Key {
    dev_t dev;
    ino_t ino;
} Key;
#define CACHE_KEYS ((sizeof(Key))/(sizeof(int)))
#endif

/*
 * Local functions defined in this file.
 */

static int  ParseFile(NsInterp *itPtr, char *file, size_t size, Ns_DString *);
static void PushFrame(NsInterp *itPtr, Frame *framePtr, char *file, 
		      int argc, char **argv, Tcl_DString *outputPtr);
static void PopFrame(NsInterp *itPtr, Frame *framePtr);
static void LogError(NsInterp *itPtr, int chunk);
static int AdpRun(NsInterp *itPtr, char *file, int argc, char **argv,
		  Tcl_DString *outputPtr);
static int EvalBlocks(NsInterp *itPtr, Block *firstPtr);
static int EvalChunks(NsInterp *itPtr, char *chunks);
static int DebugChunk(NsInterp *itPtr, char *script, int chunk);
static Page *NewPage(Ns_DString *dsPtr, struct stat *stPtr, int blocks);
static Ns_Callback FreePage;


/*
 *----------------------------------------------------------------------
 *
 * NsAdpCache --
 *
 *	Create a new shared ADP cache.
 *
 * Results:
 *	Pointer to Ns_Cache.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
    
Ns_Cache *
NsAdpCache(char *name, int size)
{
    char buf[200];

    sprintf(buf, "nsadp:%s", name);
    return Ns_CacheCreateSz(buf, CACHE_KEYS, size, FreePage);
}


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
NsAdpEval(NsInterp *itPtr, char *string, int argc, char **argv)
{
    Frame             frame;
    Ns_DString        adp, output;
    int               code;
    
    /*
     * Increment the inline eval level to ensure flushing is disabled,
     * push a frame, execute the code, and then move any result to the
     * interp from the output buffer.
     */
     
    Ns_DStringInit(&adp);
    Ns_DStringInit(&output);
    PushFrame(itPtr, &frame, NULL, argc, argv, &output);
    NsAdpParse(itPtr->servPtr, &adp, string);
    code = EvalChunks(itPtr, adp.string);
    PopFrame(itPtr, &frame);
    Tcl_SetResult(itPtr->interp, output.string, TCL_VOLATILE);
    Ns_DStringFree(&output);
    Ns_DStringFree(&adp);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpSource, NsAdpInclude --
 *
 *	Evaluate an ADP file, utilizing the per-thread byte-code
 *	compiled cache or the shared chunk cache if possible.
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
NsAdpSource(NsInterp *itPtr, char *file, int argc, char **argv)
{
    Tcl_DString output;
    int code;

    /*
     * Direct output to a local buffer.
     */

    Tcl_DStringInit(&output);
    code = AdpRun(itPtr, file, argc, argv, &output);
    if (code == TCL_OK) {
	Tcl_DStringResult(itPtr->interp, &output);
    }
    Tcl_DStringFree(&output);
    return code;
}

int
NsAdpInclude(NsInterp *itPtr, char *file, int argc, char **argv)
{
    /*
     * Direct output to the ADP response buffer.
     */

    if (itPtr->adp.responsePtr == NULL) {
	Tcl_SetResult(itPtr->interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    return AdpRun(itPtr, file, argc, argv, itPtr->adp.responsePtr);
}

static int
AdpRun(NsInterp *itPtr, char *file, int argc, char **argv, Tcl_DString *outputPtr)
{
    NsServer  *servPtr = itPtr->servPtr;
    Tcl_Interp *interp = itPtr->interp;
    struct stat st;
    Ns_DString chunks, path;
    Frame frame;
    Page *pagePtr;
    Ns_Entry *ePtr;
    int new, status;
    char *p, *key;
    Ns_Cache *cachePtr;
    
    pagePtr = NULL;
    status = TCL_ERROR;
    Ns_DStringInit(&chunks);
    Ns_DStringInit(&path);
    
    /*
     * Construct the full, normalized path to the ADP file.
     */

    if (Ns_PathIsAbsolute(file)) {
	Ns_NormalizePath(&path, file);
    } else {
	Ns_MakePath(&chunks, itPtr->adp.cwd, file, NULL);
	Ns_NormalizePath(&path, chunks.string);
	Ns_DStringTrunc(&chunks, 0);
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
				interp->result);
	    itPtr->adp.exception = ADP_ABORT;
	    goto done;
	}
    }

    /*
     * Determine the cache to use (if any).
     */
     
    if (itPtr->adp.debugLevel > 0) {
	cachePtr = NULL;
    } else if (!servPtr->adp.threadcache) {
    	cachePtr = servPtr->adp.cache;
    } else {
	if (itPtr->adp.cache == NULL) {
    	    itPtr->adp.cache = NsAdpCache(Ns_ThreadGetName(),
		servPtr->adp.cachesize);
	}
    	cachePtr = itPtr->adp.cache;
    }

    /*
     * Verify the file is an existing, ordinary file and then either
     * parse directly or fetch through the cache.
     */

    if (stat(file, &st) != 0) {
	Tcl_AppendResult(interp, "could not stat \"",
	    file, "\": ", Tcl_PosixError(interp), NULL);
    } else if (S_ISREG(st.st_mode) == 0) {
    	Tcl_AppendResult(interp, "not an ordinary file: ", file,
			 NULL);
    } else if (cachePtr == NULL) {
    
    	/*
	 * Parse directly from file.
	 */
	 
    	status = ParseFile(itPtr, file, st.st_size, &chunks);
    } else {
#ifdef WIN32
	key = file;
#else
    	Key ukey;

	ukey.dev = st.st_dev;
	ukey.ino = st.st_ino;
	key = (char *) &ukey;
#endif
	if (cachePtr != servPtr->adp.cache) {

    	    /*
	     * Fetch from private, free-threaded cache.
	     */

            ePtr = Ns_CacheCreateEntry(cachePtr, key, &new);
            if (!new) {
    		pagePtr = Ns_CacheGetValue(ePtr);
    		if (pagePtr->mtime != st.st_mtime || pagePtr->size != st.st_size) {
		    Ns_CacheUnsetValue(ePtr);
		    new = 1;
		} else {
		    status = TCL_OK;
		}
	    }
	    if (new) {
		status = ParseFile(itPtr, file, st.st_size, &chunks);
		if (status != TCL_OK) {
                    Ns_CacheDeleteEntry(ePtr);
		} else {
		    pagePtr = NewPage(&chunks, &st, 1);
                    Ns_CacheSetValueSz(ePtr, pagePtr, pagePtr->size);
		}
	    }
	} else {

    	    /*
	     * Fetch from shared, interlocked cache.
	     */

            Ns_CacheLock(cachePtr);
            ePtr = Ns_CacheCreateEntry(cachePtr, key, &new);
            if (!new) {
		while (ePtr != NULL && (pagePtr = Ns_CacheGetValue(ePtr)) == NULL) {
		    Ns_CacheWait(cachePtr);
		    ePtr = Ns_CacheFindEntry(cachePtr, key);
		}
		if (pagePtr == NULL) {
		    Tcl_AppendResult(interp, "wait failed for file: ", file, NULL);
		} else if (pagePtr->mtime != st.st_mtime || pagePtr->size != st.st_size) {
		    Ns_CacheUnsetValue(ePtr);
		    new = 1;
		} else {
		    Ns_DStringNAppend(&chunks, pagePtr->chunks, pagePtr->length);
		    status = TCL_OK;
		}
	    }
	    if (new) {
		Ns_CacheUnlock(cachePtr);
		status = ParseFile(itPtr, file, st.st_size, &chunks);
		Ns_CacheLock(cachePtr);
		ePtr = Ns_CacheCreateEntry(cachePtr, key, &new);
		if (status != TCL_OK) {
                    Ns_CacheFlushEntry(ePtr);
		} else {
		    pagePtr = NewPage(&chunks, &st, 0);
                    Ns_CacheSetValueSz(ePtr, pagePtr, pagePtr->size);
        	}
		Ns_CacheBroadcast(cachePtr);
	    }
	    Ns_CacheUnlock(cachePtr);
	}
    }
    
    /*
     * If valid chunks where parsed or copied, push a new call frame, run
     * the chunks, and pop the frame.
     */
         
    if (status == TCL_OK) {
    	PushFrame(itPtr, &frame, file, argc, argv, outputPtr);
        if (cachePtr == NULL || cachePtr == servPtr->adp.cache) {
            status = EvalChunks(itPtr, chunks.string);
        } else {
            status = EvalBlocks(itPtr, pagePtr->firstPtr);
        }
    	PopFrame(itPtr, &frame);
    }
    if (itPtr->adp.debugLevel > 0) {
	--itPtr->adp.debugLevel;
    }

done:
    Ns_DStringFree(&path);
    Ns_DStringFree(&chunks);

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
	code = NsTclEval(itPtr->interp, ds.string);
        Tcl_DStringFree(&ds);
	if (code != TCL_OK) {
	    Ns_TclLogError(itPtr->interp);
	    return TCL_ERROR;
	}

	/*
	 * Link the ADP response buffer result to a global variable
	 * which can be monitored with a variable watch.
	 */

	if (itPtr->adp.responsePtr != NULL &&
	    Tcl_LinkVar(itPtr->interp, "ns_adp_output",
			(char *) &itPtr->adp.responsePtr->string,
		TCL_LINK_STRING | TCL_LINK_READ_ONLY) != TCL_OK) {
	    Ns_TclLogError(itPtr->interp);
	}

	itPtr->adp.debugInit = 1;
	itPtr->adp.debugLevel = 1;
    }
    return code;
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
PushFrame(NsInterp *itPtr, Frame *framePtr, char *file, int argc,
	  char **argv, Tcl_DString *outputPtr)
{
    char    *slash;

    /*
     * Save current NsInterp state.
     */

    framePtr->cwd = itPtr->adp.cwd;
    framePtr->argc = itPtr->adp.argc;
    framePtr->argv = itPtr->adp.argv;
    framePtr->file = itPtr->adp.file;
    framePtr->outputPtr = itPtr->adp.outputPtr;
    itPtr->adp.outputPtr = outputPtr;
    itPtr->adp.argc = argc;
    itPtr->adp.argv = argv;
    itPtr->adp.file = file;
    ++itPtr->adp.depth;

    /*
     * If file is not NULL it indicates a call from
     * AdpProc or AdpIncludeCmd.  If so, update the
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

    itPtr->adp.argc = framePtr->argc;
    itPtr->adp.argv = framePtr->argv;
    itPtr->adp.cwd = framePtr->cwd;
    itPtr->adp.file = framePtr->file;
    itPtr->adp.outputPtr = framePtr->outputPtr;
    --itPtr->adp.depth;
    Ns_DStringFree(&framePtr->cwdBuf);
}


/*
 *----------------------------------------------------------------------
 *
 * ParseFile --
 *
 *	Read and parse text from a file.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ParseFile(NsInterp *itPtr, char *file, size_t size, Ns_DString *dsPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    Tcl_Encoding encoding;
    Tcl_DString ds;
    char *page, *buf;
    int fd, n;
    
    /*
     * Read the file in on big binary chunk.
     */

    fd = open(file, O_RDONLY|O_BINARY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    file, "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    buf = ns_malloc(size+1);
    n = read(fd, buf, size);
    if (n < 0) {
	Tcl_AppendResult(interp, "read() of \"", file,
	    	"\" failed: ", Tcl_PosixError(interp), NULL);
    } else {
	buf[n] = '\0';
	Tcl_DStringInit(&ds);
	encoding = Ns_GetFileEncoding(file);
	if (encoding == NULL) {
	    page = buf;
	} else {
	    Tcl_ExternalToUtfDString(encoding, buf, n, &ds);
	    page = ds.string;
	}
	NsAdpParse(itPtr->servPtr, dsPtr, page);
	Tcl_DStringFree(&ds);
    }
    ns_free(buf);
    close(fd);
    return TCL_OK;
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
LogError(NsInterp *itPtr, int chunk)
{
    Ns_DString ds;
    char *argv[2];
    char *file;
    
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, "\n    invoked from within chunk: ");
    Ns_DStringPrintf(&ds, "%d", chunk);
    Ns_DStringAppend(&ds, " of adp: ");
    Ns_DStringAppend(&ds, itPtr->adp.file ? itPtr->adp.file : "<inline>");
    Tcl_AddErrorInfo(itPtr->interp, ds.string);
    Ns_TclLogError(itPtr->interp);
    Ns_DStringFree(&ds);
    file = itPtr->servPtr->adp.errorpage;
    if (file != NULL && itPtr->adp.errorLevel == 0) {
	++itPtr->adp.errorLevel;
	argv[0] = Tcl_GetVar(itPtr->interp, "errorInfo", TCL_GLOBAL_ONLY);
	if (argv[0] == NULL) {
	    argv[0] = itPtr->interp->result;
	}
	argv[1] = NULL;
	(void) NsAdpInclude(itPtr, file, 1, argv);
	--itPtr->adp.errorLevel;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * EvalChunks --
 *
 *	Evaluate a list of chunks from an ADP.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Depends on script chunks.
 *
 *----------------------------------------------------------------------
 */

static int
EvalChunks(NsInterp *itPtr, char *chunks)
{
    int chunk, n, code;
    register char *ch = chunks;
    Tcl_Interp *interp = itPtr->interp;

    code = TCL_OK;
    chunk = 1;
    while (*ch && itPtr->adp.exception == ADP_OK) {
	n = strlen(ch);
	if (*ch++ == 't') {
	    Ns_DStringNAppend(itPtr->adp.outputPtr, ch, n-1);
	} else {
	    if (itPtr->adp.debugLevel > 0) {
    		code = DebugChunk(itPtr, ch, chunk);
	    } else {
		code = NsTclEval(interp, ch);
	    }
	    if (code != TCL_OK && code != TCL_RETURN && itPtr->adp.exception == ADP_OK) {
    	    	LogError(itPtr, chunk);
	    }
	    ++chunk;
	}
	ch += n;
	NsAdpFlush(itPtr);
    }
    if (itPtr->adp.exception == ADP_RETURN) {
	itPtr->adp.exception = ADP_OK;
	code = TCL_OK;
    }
    NsAdpFlush(itPtr);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * EvalBlocks --
 *
 *	Evaluate a list of blocks.
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
EvalBlocks(NsInterp *itPtr, Block *blockPtr)
{
    Tcl_Interp *interp = itPtr->interp;
    int chunk, code;

    chunk = 0;
    code = TCL_OK;
    while (blockPtr != NULL && itPtr->adp.exception == ADP_OK) {
	if (blockPtr->scriptObjPtr == NULL) {
	    Ns_DStringNAppend(itPtr->adp.outputPtr, blockPtr->text, blockPtr->length);
	} else {
    	    code = Tcl_EvalObjEx(interp, blockPtr->scriptObjPtr, 0);
	    if (code != TCL_OK && code != TCL_RETURN && itPtr->adp.exception == ADP_OK) {
    	    	LogError(itPtr, chunk);
	    }
	    ++chunk;
	}
	blockPtr = blockPtr->nextPtr;
	NsAdpFlush(itPtr);
    }
    if (itPtr->adp.exception == ADP_RETURN) {
	itPtr->adp.exception = ADP_OK;
	code = TCL_OK;
    }
    NsAdpFlush(itPtr);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * DebugChunk --
 *
 *	Evaluate an ADP chunk with the TclPro debugger.
 *
 * Results:
 *	Depends on chunk code.
 *
 * Side effects:
 *	A unique temp file with header comments and the script is
 *	created and sourced, the effect of which is TclPro will
 *	instrument the code on the fly for single-step debugging.
 *
 *----------------------------------------------------------------------
 */

static int
DebugChunk(NsInterp *itPtr, char *script, int chunk)
{
    int code, fd;
    Tcl_Interp *interp = itPtr->interp;
    int level = itPtr->adp.debugLevel;
    char *file = itPtr->adp.file;
    char buf[10], debugfile[255];
    Ns_DString ds;

    code = TCL_ERROR;
    Ns_DStringInit(&ds);
    sprintf(buf, "%d", level);
    Ns_DStringVarAppend(&ds,
	"#\n"
	"# level: ", buf, "\n", NULL);
    sprintf(buf, "%d", chunk);
    Ns_DStringVarAppend(&ds,
	"# chunk: ", buf, "\n"
	"# file:  ", file, "\n"
	"#\n\n", script, NULL);
    sprintf(debugfile, P_tmpdir "/adp%d.%d.XXXXXX", level, chunk);
    if (mktemp(debugfile) == NULL) {
	Tcl_SetResult(interp, "could not create adp debug file", TCL_STATIC);
    } else {
	fd = open(debugfile, O_WRONLY|O_TRUNC|O_CREAT|O_TEXT, 0644);
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
		code = NsTclEval(interp, ds.string);
	    }
	    close(fd);
	}
	unlink(debugfile);
    }
    Ns_DStringFree(&ds);
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NewPage -- 
 *
 *  	Create a new Page, possibly with blocks.
 *
 * Results:
 *	A pointer to new pagePtr.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

Page *
NewPage(Ns_DString *dsPtr, struct stat *stPtr, int blocks)
{
    Page *pagePtr;
    Block *blockPtr, *prevPtr;
    register char *t, cmd;
    int n;

    if (!blocks) {
	pagePtr = ns_malloc(sizeof(Page) + dsPtr->length);
	memcpy(pagePtr->chunks, dsPtr->string, pagePtr->length);
	pagePtr->firstPtr = NULL;
    } else {
	pagePtr = ns_malloc(sizeof(Page));
	pagePtr->firstPtr = NULL;
	t = dsPtr->string;
	while (*t) {
	    cmd = *t++;
	    n = strlen(t);
	    if (cmd == 't') {
		blockPtr = ns_malloc(sizeof(Block) + n);
		blockPtr->scriptObjPtr = NULL;
		blockPtr->length = n;
		memcpy(blockPtr->text, t, n);
	    } else {
		blockPtr = ns_malloc(sizeof(Block));
		blockPtr->length = 0;
		blockPtr->scriptObjPtr = Tcl_NewStringObj(t, n);
		Tcl_IncrRefCount(blockPtr->scriptObjPtr);
	    }
	    blockPtr->nextPtr = NULL;
	    if (pagePtr->firstPtr == NULL) {
		pagePtr->firstPtr = blockPtr;
	    } else {
		prevPtr->nextPtr = blockPtr;
	    }
	    prevPtr = blockPtr;
	    t += n+1;
	}
    }
    pagePtr->mtime = stPtr->st_mtime;
    pagePtr->size = stPtr->st_size;
    pagePtr->length = dsPtr->length + 1;
    return pagePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * FreePage --
 *
 *  	Free previously allocated Page.
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
FreePage(void *arg)
{
    Page *pagePtr = arg;
    Block *blockPtr;

    while ((blockPtr = pagePtr->firstPtr) != NULL) {
	pagePtr->firstPtr = blockPtr->nextPtr;
	if (blockPtr->scriptObjPtr != NULL) {
	    Tcl_DecrRefCount(blockPtr->scriptObjPtr);
	}
	ns_free(blockPtr);
    }
    ns_free(pagePtr);
}

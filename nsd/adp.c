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
 * adp.c --
 *
 *	Support for AOLserver Dynamic Pages.
 *
 * TODO: ns_adp_include -sameframe is dead; should issue a warning
 * you can't debug ns_adp_parse -string "adp"
 *
 * Here's an example of a really complicated setup with multiple parsers:
 *
 * ns_section "ns/server/server1/adp"
 * ns_param   "map"     "/ *.adp"
 * ns_param   "map"     "/ *.utf8-adp"
 * ns_param   "map"     "/unicode/ *.uni-adp"
 *
 * ns_section "ns/server/server1/adp/parsers"
 * ns_param   "utf8"    ".utf8-adp"
 * ns_param   "unicode" ".uni-adp"
 *
 * ns_section "ns/server/server1/modules"
 * ns_param   "nsunicode" "nsunicode.so"
 * ns_param   "nsutf8"    "nsutf8.so"
 *
 * ns_section "ns/server/server1/module/nsunicode"
 * ns_param   "ParserName" "unicode"
 *
 * ns_section "ns/server/server1/module/nsutf8"
 * ns_param   "ParserName" "utf8"
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/adp.c,v 1.2 2000/05/02 14:39:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains an ADP call frame.  PushFrame
 * is used to save the previous state of the per-thread AdpData
 * structure in a Frame allocated on the stack and PopFrame restores
 * the previous state from the Frame.
 */

typedef struct {
    int                argc;
    char             **argv;
    char              *cwd;
    int                length;
    Ns_DString         cwdBuf;
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

static void Parse(char *filename, Ns_DString *out, char *pagein);
static Ns_OpProc AdpProc;
static void (DelAdpData)(void *);
static void ParsePage(Ns_DString *, char *page);
static int  ParseFile(Tcl_Interp *, char *file, size_t size, Ns_DString *);
static void PushFrame(Frame *framePtr, char *file, int argc, char **argv);
static void PopFrame(Frame *framePtr);
static int DebugInit(Tcl_Interp *interp, char *host, char *port,
		     char *procs);
static Ns_TclInterpInitProc EnableCmds;
static void TextChunk(Ns_DString *dsPtr, char *text);
static void SetMimeType(AdpData *adPtr, char *mimeType);

/*
 * Static global variables
 */

static Ns_Tls            adKey;
static Ns_AdpParserProc *defParserProc = NULL;
static Tcl_HashTable     extensionsTable;
static Tcl_HashTable     parsersTable;
static Ns_Cache         *sharedCachePtr;


/*
 * Global variables within this source directory
 */

Ns_ModLogHandle     nsAdpModLogHandle;


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRegisterParser --
 *
 *	Sets the ADP parser.
 *	
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AdpRegisterParser(char *extension, Ns_AdpParserProc *newParserProc)
{
    Tcl_HashEntry *hePtr;
    int            new;
    
    if (Ns_InfoServersStarted() == NS_TRUE) {
	Ns_ModLog(Error, nsAdpModLogHandle,
		  "attempt to register ADP parser after server startup.");
	return NS_ERROR;
    }
    hePtr = Tcl_CreateHashEntry(&parsersTable, extension, &new);
    Tcl_SetHashValue(hePtr, (void *) newParserProc);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpInit --
 *
 *	Initialize the ADP interface.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	ADP processor is register as required, Tcl commands are added, and
 *	a simple cache is initialized.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpInit(void)
{
    Ns_Set *set;
    char *path, *map;
    int i;

    /*
     * register the sub-realm
     */

    NsModLogRegSubRealm("adp", &nsAdpModLogHandle);

    /*
     * Initialize the ADP core.
     */

    Ns_TlsAlloc(&adKey, DelAdpData);
    if (nsconf.adp.cache && !nsconf.adp.threadcache) {
        sharedCachePtr = Ns_CacheCreateSz("adp", CACHE_KEYS,
	    	    	    	    	  nsconf.adp.cachesize, ns_free);
    }

    /*
     * Add the Tcl commands
     */

    Ns_TclInitInterps(nsServer, EnableCmds, NULL);

    /*
     * Register ADP for any requested URLs.
     */

    path = Ns_ConfigPath(nsServer, NULL, "adp", NULL);
    map = NULL;
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	char          *key;

	key = Ns_SetKey(set, i);
	if (!strcasecmp(key, "map")) {
	    map = Ns_SetValue(set, i);
	    Ns_RegisterRequest(nsServer, "GET", map, AdpProc, NULL,
			       NULL, 0);
	    Ns_RegisterRequest(nsServer, "HEAD", map, AdpProc, NULL,
			       NULL, 0);
	    Ns_RegisterRequest(nsServer, "POST", map, AdpProc, NULL,
			       NULL, 0);
	    Ns_ModLog(Notice, nsAdpModLogHandle, "mapped %s", map);
	}
    }
    if (map == NULL) {
	Ns_ModLog(Warning, nsAdpModLogHandle,
		  "no Map configuration - disabled");
    }

    /*
     * Initialize the hash table of parsers
     */

    Tcl_InitHashTable(&parsersTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&extensionsTable, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpParsers --
 *
 *	Load ADP parser mappings. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Registers requests for each parser. 
 *
 *----------------------------------------------------------------------
 */

void
NsAdpParsers(void)
{
    char             *path;
    Tcl_HashEntry    *hePtr;
    int               size = 0;
    Ns_Set           *setPtr;
    int               i, new;
    Ns_AdpParserProc *parserProc;

    path = Ns_ConfigPath(nsServer, NULL, "adp", "parsers", NULL);

    NsAdpFancyInit(nsServer, path);
    Ns_AdpRegisterParser("adp", ParsePage);

    if (path != NULL) {
	setPtr = Ns_ConfigGetSection(path);
	size = Ns_SetSize(setPtr);
    }

    hePtr = Tcl_FindHashEntry(&parsersTable, nsconf.adp.defaultparser);
    parserProc = (Ns_AdpParserProc *) Tcl_GetHashValue(hePtr);
    defParserProc = parserProc;
    
    hePtr = Tcl_CreateHashEntry(&extensionsTable, ".adp", &new);
    Tcl_SetHashValue(hePtr, defParserProc);
    
    for (i=0; i < size; i++) {
	char             *parser;
	char             *ext;

	parser = Ns_SetKey(setPtr, i);
	ext = Ns_SetValue(setPtr, i);

	hePtr = Tcl_FindHashEntry(&parsersTable, parser);
	if (hePtr == NULL) {
	    Ns_ModLog(Notice, nsAdpModLogHandle,
		      "invalid parser '%s'", parser);
	    continue;
	}

	parserProc = (Ns_AdpParserProc *) Tcl_GetHashValue(hePtr);
	hePtr = Tcl_CreateHashEntry(&extensionsTable, ext, &new);
	Tcl_SetHashValue(hePtr, parserProc);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpGetData --
 *
 *	Return the per-thread ADP context structure.
 *
 * Results:
 *	Pointer to AdpData.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

AdpData *
NsAdpGetData(void)
{
    AdpData *adPtr;

    adPtr = (AdpData *) Ns_TlsGet(&adKey);
    if (adPtr == NULL) {
	adPtr = ns_calloc(1, sizeof(AdpData));
        adPtr->mimeType = NULL;
	Ns_DStringInit(&adPtr->output);
	Ns_TlsSet(&adKey, adPtr);
    }

    return adPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpEval --
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

int
NsAdpEval(Tcl_Interp *interp, char *file, char *chunks)
{
    int chunk, n, code, fd;
    register char *ch = chunks;
    Ns_DString ds;
    char buf[10], *script, debugfile[255];
    AdpData *adPtr;

    adPtr = NsAdpGetData();
    if (Ns_CheckStack() != NS_OK) {
        interp->result =  "danger: stack grown too large (recursive adp?)";
	adPtr->exception = ADP_OVERFLOW;
        return TCL_ERROR;
    }

    if (file == NULL) {
	file = "<inlined script>";
    }
    Ns_DStringInit(&ds);
    code = TCL_OK;
    chunk = 1;

    while (*ch && adPtr->exception == ADP_OK) {
	n = strlen(ch);
	if (*ch++ == 't') {
	    Ns_DStringNAppend(&adPtr->output, ch, n-1);
	} else {
	    script = ch;
	    if (adPtr->debugLevel > 0) {
		Ns_DStringTrunc(&ds, 0);
		sprintf(buf, "%d", adPtr->debugLevel);
		Ns_DStringVarAppend(&ds,
		    "#\n"
		    "# level: ", buf, "\n", NULL);
		sprintf(buf, "%d", chunk);
		Ns_DStringVarAppend(&ds,
		    "# chunk: ", buf, "\n"
		    "# file:  ", file, "\n"
		    "#\n\n", ch, NULL);
		sprintf(debugfile, P_tmpdir "/adp%d.%d.XXXXXX",
			adPtr->debugLevel, chunk);
		mktemp(debugfile);
		fd = open(debugfile, O_WRONLY|O_TRUNC|O_CREAT|O_TEXT, 0644);
		if (fd < 0) {
	    	     Ns_ModLog(Error, nsAdpModLogHandle,
			       "could not open %s:  %s", debugfile,
			       strerror(errno));
		} else {
		    write(fd, ds.string, ds.length);
		    close(fd);
		    Ns_DStringTrunc(&ds, 0);
		    Ns_DStringVarAppend(&ds, "source ", debugfile, NULL);
		    script = ds.string;
		}
	    }
    	    code = NsTclEval(interp, script);
	    if (code != TCL_OK &&
		code != TCL_RETURN &&
		adPtr->exception == ADP_OK) {
    	    	NsAdpLogError(interp, file, chunk);
	    }

	    if (adPtr->exception == ADP_RETURN) {
		adPtr->exception = ADP_OK;
		code = TCL_OK;
		if (script != ch) {
		    unlink(debugfile);
		}
		goto done;
		break;
	    }

	    if (script != ch) {
		unlink(debugfile);
	    }
	    ++chunk;
	}
	ch += n;
	NsAdpFlush(adPtr);
    }
    
 done:
    NsAdpFlush(adPtr);
    Ns_DStringFree(&ds);
    
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpStreamOn --
 *
 *	Turn on streaming for the current connection.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None unless streaming is not already enabled in which case
 *	headers and any pending content is immediately sent to the
 *	client.  Note that no content-length header will be sent
 *  	which has the side effect of disabling connection: keep-alive
 *	for the client.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpStreamOn(void)
{
    AdpData *adPtr;

    adPtr = NsAdpGetData();
    if (adPtr->conn == NULL) {
	return;
    }
    if (adPtr->fStream != NS_TRUE) {
    	adPtr->fStream = NS_TRUE;
	Ns_ConnSetRequiredHeaders(adPtr->conn, "text/html", 0);
	Ns_ConnFlushHeaders(adPtr->conn, 200);
    }
    NsAdpFlush(adPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpFlush --
 *
 *	Flush current output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None unless streaming is enabled in which case content is
 *  	written directly out the connection.  Also, output is disabled
 *  	during inlined evalution where the output buffer is used to
 *  	accumulate the inlined evaluation result before being copied
 *  	to the interp.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpFlush(AdpData *adPtr)
{
    if (adPtr->fStream == NS_TRUE &&
	adPtr->evalLevel == 0 &&
	adPtr->output.length > 0) {

	Ns_WriteConn(adPtr->conn, adPtr->output.string,
		     adPtr->output.length);
	Ns_DStringTrunc(&adPtr->output, 0);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpEvalCmd --
 *
 *	Process the Tcl _ns_adp_eval command.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Page string is parsed and evaluated at current Tcl level in a
 *	new ADP call frame.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpEvalCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		char **argv)
{
    AdpData          *adPtr;
    Frame             frame;
    Ns_DString        ds;
    int               code;
    int               offset = 0;
    int               parser = -1;
    Tcl_HashEntry    *hePtr;
    Ns_AdpParserProc *parserProc;
    
    if (argc >= 2) {
	if (!strcmp(argv[1], "-parser")) {
	    offset += 2;
	    parser = 1;
	}
    }
    if (argc < 2 + offset) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?-parser parser? page ?arg ...?\"", NULL);
	return TCL_ERROR;
    }

    if (parser != -1) {
	hePtr = Tcl_FindHashEntry(&parsersTable, argv[parser]);
	if (hePtr == NULL) {
	    Tcl_AppendResult(interp, "invalid parser \"", parser, "\"", NULL);
	    return TCL_ERROR;
	}
	parserProc = (Ns_AdpParserProc *) Tcl_GetHashValue(hePtr);
    } else {
	parserProc = defParserProc;
    }
    
    /*
     * Increment the inline eval level to ensure flushing is disabled,
     * push a frame, execute the code, and then more any result to the
     * interp from the output buffer.
     */
     
    Ns_DStringInit(&ds);
    adPtr = NsAdpGetData();
    ++adPtr->evalLevel;
    PushFrame(&frame, NULL, argc-1-offset, argv+1+offset);
    Parse(NULL, &ds, argv[1]);
    code = NsAdpEval(interp, argv[0], ds.string);
    if (adPtr->output.length > frame.length) {
	Tcl_SetResult(interp, adPtr->output.string + frame.length,
		      TCL_VOLATILE);
	Ns_DStringTrunc(&adPtr->output, frame.length);
    }
    PopFrame(&frame);
    --adPtr->evalLevel;

    Ns_DStringFree(&ds);

    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclIncludeCmd --
 *
 *	Process the Tcl _ns_adp_include and _ns_adp_parse commands.
 *	This routines is the core ADP execution engine.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Parse page for the given file is fetched from the cache and
 *	evaluated using the Tcl76 or Tcl81 code engine.
 *
 *----------------------------------------------------------------------
 */
 
Page *
NsAdpCopyShared(Ns_DString *dsPtr, struct stat *stPtr)
{
    Page *pagePtr;

    pagePtr = ns_malloc(sizeof(Page) + dsPtr->length);
    pagePtr->mtime = stPtr->st_mtime;
    pagePtr->size = stPtr->st_size;
    pagePtr->length = dsPtr->length + 1;
    memcpy(pagePtr->chunks, dsPtr->string, pagePtr->length);
    return pagePtr;
}

int
NsTclIncludeCmd(ClientData parse, Tcl_Interp *interp, int argc,
		char **argv)
{
    struct stat st;
    Ns_DString file, *dsPtr;
    AdpData *adPtr;
    Frame frame;
    Page *pagePtr;
    Ns_Entry *ePtr;
    int new, status;
    char *p, *key;
    Ns_Cache *cachePtr;
    
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file ?args ...?\"", NULL);
	return TCL_ERROR;
    }

    pagePtr = NULL;
    status = TCL_ERROR;
    dsPtr = Ns_DStringPop();
    Ns_DStringInit(&file);
    adPtr = NsAdpGetData();
    
    /*
     * Construct the full, normalized path to the ADP file.
     */

    if (Ns_PathIsAbsolute(argv[1])) {
	Ns_NormalizePath(&file, argv[1]);
    } else {
	Ns_MakePath(dsPtr, adPtr->cwd, argv[1], NULL);
	Ns_NormalizePath(&file, dsPtr->string);
	Ns_DStringTrunc(dsPtr, 0);
    }

    /*
     * Check for TclPro debugging.
     */

    if (adPtr->debugLevel > 0) {
	++adPtr->debugLevel;
    } else if (nsconf.adp.enabledebug != NS_FALSE &&
	adPtr->debugFile != NULL &&
	(p = strrchr(file.string, '/')) != NULL &&
	Tcl_StringMatch(p+1, adPtr->debugFile)) {

	Ns_Conn *conn;
	Ns_Set *hdrs;
	char *host, *port, *procs;

        conn = Ns_TclGetConn(interp);
	hdrs = Ns_ConnGetQuery(conn);
	host = Ns_SetIGet(hdrs, "dhost");
	port = Ns_SetIGet(hdrs, "dport");
	procs = Ns_SetIGet(hdrs, "dprocs");
	if (DebugInit(interp, host, port, procs) != TCL_OK) {
	    Ns_ConnReturnNotice(conn, 200, "Debug Init Failed",
				interp->result);
	    adPtr->exception = ADP_ABORT;
	    goto done;
	}
    }

    /*
     * Determine the cache to use (if any).
     */
     
    if (adPtr->debugLevel > 0) {
	cachePtr = NULL;
    } else if (!nsconf.adp.threadcache) {
    	cachePtr = sharedCachePtr;
    } else {
	if (adPtr->cachePtr == NULL) {
	    char name[30];

    	    sprintf(name, "adpObj.%d", Ns_ThreadId());
    	    adPtr->cachePtr = Ns_CacheCreateSz(name, CACHE_KEYS, nsconf.adp.cachesize,
				(Ns_Callback *) NsAdpFreePrivate);
	}
    	cachePtr = adPtr->cachePtr;
    }

    /*
     * Verify the file is an existing, ordinary file and then either
     * parse directly or fetch through the cache.
     */

    if (stat(file.string, &st) != 0) {
	Tcl_AppendResult(interp, "could not stat \"",
	    file.string, "\": ", Tcl_PosixError(interp), NULL);
    } else if (S_ISREG(st.st_mode) == 0) {
    	Tcl_AppendResult(interp, "not an ordinary file: ", file.string,
			 NULL);
    } else if (cachePtr == NULL) {
    
    	/*
	 * Parse directly from file.
	 */
	 
    	status = ParseFile(interp, file.string, st.st_size, dsPtr);
    } else {
#ifdef WIN32
	key = file.string;
#else
    	Key ukey;

	ukey.dev = st.st_dev;
	ukey.ino = st.st_ino;
	key = (char *) &ukey;
#endif
	if (cachePtr != sharedCachePtr) {

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
		status = ParseFile(interp, file.string, st.st_size, dsPtr);
		if (status != TCL_OK) {
                    Ns_CacheDeleteEntry(ePtr);
		} else {
		    pagePtr = NsAdpCopyPrivate(dsPtr, &st);
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
		    Tcl_AppendResult(interp, "wait failed for file: ", file.string, NULL);
		} else if (pagePtr->mtime != st.st_mtime || pagePtr->size != st.st_size) {
		    Ns_CacheUnsetValue(ePtr);
		    new = 1;
		} else {
		    Ns_DStringNAppend(dsPtr, pagePtr->chunks, pagePtr->length);
		    status = TCL_OK;
		}
	    }
	    if (new) {
		Ns_CacheUnlock(cachePtr);
		status = ParseFile(interp, file.string, st.st_size, dsPtr);
		Ns_CacheLock(cachePtr);
		if (status != TCL_OK) {
                    Ns_CacheDeleteEntry(ePtr);
		} else {
		    pagePtr = NsAdpCopyShared(dsPtr, &st);
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
    	PushFrame(&frame, file.string, argc-1, argv+1);
        if (cachePtr == NULL || cachePtr == sharedCachePtr) {
            status = NsAdpEval(interp, file.string, dsPtr->string);
        } else {
            status = NsAdpRunPrivate(interp, file.string, pagePtr);
        }
    	if (parse && status == TCL_OK &&
	    adPtr->output.length > frame.length) {
	    
	    Tcl_SetResult(interp, adPtr->output.string + frame.length,
		TCL_VOLATILE);
	    Ns_DStringTrunc(&adPtr->output, frame.length);
	}
    	PopFrame(&frame);
	NsAdpFlush(adPtr);
    }
    if (adPtr->debugLevel > 0) {
	--adPtr->debugLevel;
    }

done:
    Ns_DStringFree(&file);
    Ns_DStringPush(dsPtr);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclPutsCmd --
 *
 *	Process the Tcl ns_adp_puts command to append output.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output buffer is extended with given text.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclPutsCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?-nonewline? string\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 3 &&
	strcmp(argv[1], "-nonewline") != 0) {

	Tcl_AppendResult(interp, "invalid flag \"",
	    argv[1], "\": expected -nonewline", NULL);
	return TCL_ERROR;
    }

    adPtr = NsAdpGetData();
    Ns_DStringAppend(&adPtr->output, argv[argc-1]);
    if (argc != 3) {
    	Ns_DStringNAppend(&adPtr->output, "\n", 1);
    }
    NsAdpFlush(adPtr);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclDirCmd --
 *
 *	Process the Tcl ns_adp_dir command to return the current ADP
 *  	directory.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclDirCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    if (adPtr->cwd != NULL && *adPtr->cwd) {   
    	Tcl_SetResult(interp, adPtr->cwd, TCL_VOLATILE);
    } else {
	interp->result = "/";
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclBreakCmd --
 *
 *	Process the Tcl ns_adp_break and ns_adp_abort commands to halt
 *  	page generation.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Break or abort exception is noted and will be handled in
 *  	AdpProc.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclBreakCmd(ClientData clientData, Tcl_Interp *interp, int argc,
	      char **argv)
{
    AdpData *adPtr;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?retval?\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    adPtr->exception = (int) clientData;
    if (argc == 2) {
	Tcl_AppendResult(interp, argv[1], NULL);
    }
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTellCmd --
 *
 *	Process the Tcl ns_adp_tell commands to return the current
 *  	offset within the output buffer.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclTellCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    sprintf(interp->result, "%d", adPtr->output.length);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTruncCmd --
 *
 *	Process the Tcl ns_adp_trunc commands to truncate the output
 *  	buffer to the given length.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	Output buffer is truncated.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclTruncCmd(ClientData ignored, Tcl_Interp *interp, int argc,
	      char **argv)
{
    AdpData *adPtr;
    int length;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?length?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
	length = 0;
    } else {
	if (Tcl_GetInt(interp, argv[1], &length) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (length < 0) {
	    Tcl_AppendResult(interp, "invalid length: ", argv[1], NULL);
	    return TCL_ERROR;
	}
    }
    adPtr = NsAdpGetData();
    Ns_DStringTrunc(&adPtr->output, length);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclDumpCmd --
 *
 *	Process the Tcl ns_adp_dump commands to return the entire text
 *  	of the output buffer.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclDumpCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    Tcl_SetResult(interp, adPtr->output.string, TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclArgcCmd --
 *
 *	Process the Tcl ns_adp_args commands to return the number of
 *  	arguments in the current ADP frame.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclArgcCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    sprintf(interp->result, "%d", adPtr->argc);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclArgvCmd --
 *
 *	Process the Tcl ns_adp_args commands to return an argument (or
 *  	the entire list of arguments) within the current ADP frame.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclArgvCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;
    int i;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?index?\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    if (adPtr->argv != NULL) {
	if (argc == 1) {
    	    for (i = 0; i < adPtr->argc; ++i) {
    		Tcl_AppendElement(interp, adPtr->argv[i]);
    	    }
	} else {
    	    if (Tcl_GetInt(interp, argv[1], &i) != TCL_OK) {
    		return TCL_ERROR;
    	    }
    	    if (i > adPtr->argc) {
    		i = adPtr->argc;
    	    }
    	    Tcl_SetResult(interp, adPtr->argv[i], TCL_VOLATILE);
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclBindCmd --
 *
 *	Process the Tcl ns_adp_bind_args commands to copy arguements
 *  	from the current frame into local variables.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	One or more local variables are created.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclBindCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;
    int i;
    char *arg;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " varName ?varName ...?\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    if (adPtr->argc == 0) {
	Tcl_AppendResult(interp, "not in an ADP", NULL);
	return TCL_ERROR;
    }
    if (adPtr->argc != argc) {
	char buf[sizeof(int) * 3 + 1];

	sprintf(buf, "%d", adPtr->argc - 1);
	Tcl_AppendResult(interp, "wrong # args: this ADP was passed ",
			 buf, " parameters", NULL);
	return TCL_ERROR;
    }
    
    for (i = 1; i < argc; ++i) {
    	if (adPtr->argv != NULL && i < adPtr->argc) {
    	    arg = adPtr->argv[i];
    	} else {
    	    arg = "";
    	}
    	if (Tcl_SetVar(interp, argv[i], arg, TCL_LEAVE_ERR_MSG) == NULL) {
    	    return TCL_ERROR;
    	}
    }

    return TCL_OK;
}   	    


/*
 *----------------------------------------------------------------------
 *
 * ExcepetionCmd --
 *
 *	Process the Tcl ns_adp_exception commands to return the current
 *  	exception state, ok, abort, or break.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclExceptionCmd(ClientData ignored, Tcl_Interp *interp, int argc,
		  char **argv)
{
    AdpData *adPtr;
    char *exception;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?varName?\"", NULL);
	return TCL_ERROR;
    }
    adPtr = NsAdpGetData();
    if (adPtr->exception == ADP_OK) {
	Tcl_SetResult(interp, "0", TCL_STATIC);
    } else {
	Tcl_SetResult(interp, "1", TCL_STATIC);
    }
    if (argc == 2) {
	switch (adPtr->exception) {
	case ADP_OK:
	    exception = "ok";
	    break;
	case ADP_BREAK:
	    exception = "break";
	    break;
	case ADP_ABORT:
	    exception = "abort";
	    break;
	case ADP_OVERFLOW:
	    exception = "overflow";
	    break;
	case ADP_RETURN:
	    exception = "return";
            break;
	default:
	    exception = "unknown";
	    break;
	}
	if (Tcl_SetVar(interp, argv[1], exception, 
		       TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStreamCmd --
 *
 *	Process the Tcl ns_adp_stream commands to enable streaming
 *  	output mode.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See comments for NsAdpStreamOn.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclStreamCmd(ClientData ignored, Tcl_Interp *interp, int argc,
	       char **argv)
{
    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], NULL);
	return TCL_ERROR;
    }
    NsAdpStreamOn();

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclDebugCmd --
 *
 *	Process the Tcl ns_adp_debug command to connect to the TclPro
 *  	debugger if not already connected.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	See comments for DebugInit().
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclDebugCmd(ClientData ignored, Tcl_Interp *interp, int argc,
	      char **argv)
{
    AdpData *adPtr;
    char *host, *port, *procs;

    if (argc > 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?procs? ?host? ?port?\"", NULL);
	return TCL_ERROR;
    }
    procs = (argc > 1) ? argv[1] : NULL;
    host = (argc > 2) ? argv[2] : NULL;
    port = (argc > 3) ? argv[3] : NULL;

    if (DebugInit(interp, host, port, procs) != TCL_OK) {
	Tcl_SetResult(interp, "could not initialize debugger", TCL_STATIC);
	return TCL_ERROR;
    }

    adPtr = NsAdpGetData();
    sprintf(interp->result, "%d", adPtr->debugLevel);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * EnableCmds --
 *
 *	Enable the ADP Tcl commands and create ns_adp_include and
 *	ns_adp_parse commands.
 *
 * Results:
 *	NS_OK.
 *
 * Side effects:
 *	ADP commands are created in the parent Tcl interp.
 *
 *----------------------------------------------------------------------
 */

static int
EnableCmds(Tcl_Interp *interp, void *ignored)
{
    char incWrapper[] =
	"proc ns_adp_include args {\n\teval _ns_adp_include $args\n}";

    Tcl_CreateCommand(interp, "_ns_adp_include",
		      NsTclIncludeCmd, (ClientData) 0, NULL);
    Tcl_GlobalEval(interp, incWrapper);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AdpProc --
 *
 *	Check for a normal file and call Ns_AdpRequest.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

static int
AdpProc(void *parserPtr, Ns_Conn *conn)
{
    Ns_DString file;
    int status;

    Ns_DStringInit(&file);
    Ns_UrlToFile(&file, NULL, conn->request->url);
    if (access(file.string, R_OK) != 0) {
	status = Ns_ConnReturnNotFound(conn);
    } else {
	status = Ns_AdpRequest(conn, file.string);
    }
    Ns_DStringFree(&file);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRequest -
 *
 *  	Invoke a file for an ADP request.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AdpRequest(Ns_Conn *conn, char *file)
{
    Tcl_Interp       *interp;
    AdpData          *adPtr;
    int               status;
    char             *argv[3];
    char             *mimeType;
    Frame             frame;
    Ns_Set           *setPtr;
    
    /*
     * Push a new call frame and execute the file.
     */

    interp = Ns_GetConnInterp(conn);
    adPtr = NsAdpGetData();
    adPtr->conn = conn;
    adPtr->fStream = NS_FALSE;
    if (nsconf.adp.enabledebug &&
	STREQ(conn->request->method, "GET") &&
	(setPtr = Ns_ConnGetQuery(conn)) != NULL) {
	adPtr->debugFile = Ns_SetIGet(setPtr, "debug");
    }
    mimeType = Ns_GetMimeType(file);
    if ((mimeType == NULL) || (strcmp(mimeType, "*/*") == 0)) {
        mimeType = "text/html";
    }
    SetMimeType(adPtr, mimeType);
    argv[0] = "_ns_adp_include";
    argv[1] = nsconf.adp.startpage ? nsconf.adp.startpage : file;
    argv[2] = NULL;
    PushFrame(&frame, file, 0, NULL);

    /*
     * Set the old conn variable for backwards compatibility.
     */

    Tcl_SetVar2(interp, "conn", NULL, NsTclConnId(conn), TCL_GLOBAL_ONLY);
    Tcl_ResetResult(interp);

    /*
     * Ignore error - will be reported in NsAdpEval.
     */

    (void) NsTclIncludeCmd(NULL, interp, 2, argv);

    switch (adPtr->exception) {
	case ADP_ABORT:

	    /*
	     * Abort is normally used after a call to a
	     * ns_return function so no response is sent here.
	     */

	    status = NS_OK;
	    break;

	case ADP_OVERFLOW:
	    Ns_ModLog(Error, nsAdpModLogHandle,
		      "stack overflow:  %s", file);
	    status = Ns_ConnReturnInternalError(conn);
	    break;

	default:
	    if (nsconf.adp.enableexpire) {
		Ns_ConnSetHeaders(conn, "Expires", "now");
	    }
	    if (Ns_ConnResponseStatus(conn) == 0) {
                status = Ns_ConnReturnData(conn, 200,
                                            adPtr->output.string,
                                            adPtr->output.length,
                                            adPtr->mimeType);
	    } else {
                status = NS_OK;
            }
	    break;
    }
    PopFrame(&frame);

    /*
     * Cleanup the per-thead ADP context.
     */

    Ns_DStringTrunc(&adPtr->output, 0);
    adPtr->exception = ADP_OK;
    adPtr->depth = 0;
    adPtr->argc = 0;
    adPtr->argv = NULL;
    adPtr->cwd = NULL;
    adPtr->debugLevel = 0;
    adPtr->debugInit = 0;
    adPtr->debugFile = NULL;
    SetMimeType(adPtr, NULL);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DelAdpData --
 *
 *	AdpData TLS cleanup procedure.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	AdpData structure is free'ed from per-thread memory pool.
 *
 *----------------------------------------------------------------------
 */

static void
DelAdpData(void *arg)
{
    AdpData *adPtr = arg;

    Ns_DStringFree(&adPtr->output);
    if (adPtr->cachePtr != NULL) {
	Ns_CacheDestroy(adPtr->cachePtr);
    }
    if (adPtr->mimeType != NULL) {
        ns_free(adPtr->mimeType);
    }
    ns_free(adPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * ParsePage --
 *
 *	Parse an ADP page string into text/script chunks.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Chunks are parsed into given Ns_DString.
 *
 *----------------------------------------------------------------------
 */

static void
ParsePage(Ns_DString *dsPtr, char *page)
{
    register char *s, *e;
    register char *t = page;

    while ((s = strstr(t, "<%")) != NULL &&
	(e = strstr(s, "%>")) != NULL) {

	*s = '\0';
	TextChunk(dsPtr, t);
	s += 2;
	Ns_DStringNAppend(dsPtr, "s", 1);
	if (*s == '=') {
	    Ns_DStringAppend(dsPtr, "ns_adp_puts -nonewline ");
	    ++s;
	}
	*e = '\0';
	Ns_DStringNAppend(dsPtr, s, e-s+1);
	t = e+2;
    }
    TextChunk(dsPtr, t);
}

static void
TextChunk(Ns_DString *dsPtr, char *text)
{
    Ns_DStringNAppend(dsPtr, "t", 1);
    Ns_DStringNAppend(dsPtr, text, strlen(text)+1);
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
 *	previous state of the per-thread AdpData structure is saved
 *	and then updated with the current call's arguments.
 *
 *----------------------------------------------------------------------
 */

static void
PushFrame(Frame *framePtr, char *file, int argc, char **argv)
{
    register AdpData *adPtr;
    register char    *slash;

    /*
     * Save current AdpData state.
     */

    adPtr = NsAdpGetData();
    framePtr->cwd = adPtr->cwd;
    framePtr->length = adPtr->output.length;
    framePtr->argc = adPtr->argc;
    framePtr->argv = adPtr->argv;
    adPtr->argc = argc;
    adPtr->argv = argv;
    ++adPtr->depth;

    /*
     * If file is not NULL it indicates a call from
     * AdpProc or NsTclIncludeCmd.  If so, update the
     * current working directory based on the
     * absolute file pathname.
     */

    Ns_DStringInit(&framePtr->cwdBuf);
    if (file != NULL) {
	slash = strrchr(file, '/');
    	Ns_DStringNAppend(&framePtr->cwdBuf, file, slash - file);
    	adPtr->cwd = framePtr->cwdBuf.string;
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
 *	Previous state of the per-thread AdpData structure is restored
 *	and the Frame is free'ed.
 *
 *----------------------------------------------------------------------
 */

static void
PopFrame(Frame *framePtr)
{
    register AdpData *adPtr;

    adPtr = NsAdpGetData();
    adPtr->argc = framePtr->argc;
    adPtr->argv = framePtr->argv;
    adPtr->cwd = framePtr->cwd;
    --adPtr->depth;
    Ns_DStringFree(&framePtr->cwdBuf);
}


/*
 *----------------------------------------------------------------------
 *
 * DebugInit --
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

static int
DebugInit(Tcl_Interp *interp, char *host, char *port, char *procs)
{
    AdpData *adPtr;
    Tcl_DString ds;
    int code;

    code = TCL_OK;
    adPtr = NsAdpGetData();
    if (!adPtr->debugInit) {
	Ns_TclMarkForDelete(interp);
	Tcl_DStringInit(&ds);
	Tcl_DStringAppendElement(&ds, nsconf.adp.debuginit);
	Tcl_DStringAppendElement(&ds, procs ? procs : "");
	Tcl_DStringAppendElement(&ds, host ? host : "");
	Tcl_DStringAppendElement(&ds, port ? port : "");
	code = NsTclEval(interp, ds.string);
        Tcl_DStringFree(&ds);
	if (code != TCL_OK) {
	    Ns_TclLogError(interp);
	    return TCL_ERROR;
	}
	if (Tcl_LinkVar(interp, "ns_adp_output",
			(char *) &adPtr->output.string,
		TCL_LINK_STRING | TCL_LINK_READ_ONLY) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
	adPtr->debugInit = 1;
	adPtr->debugLevel = 1;
    }

    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * Parse --
 *
 *	Figure out which parser to use and then parse a page, putting 
 *	the result into the dstring. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Appends to 'out'. If filename is null, it will use the default
 *	parser.
 *
 *----------------------------------------------------------------------
 */

static void
Parse(char *filename, Ns_DString *out, char *pagein)
{
    char             *ext;
    Tcl_HashEntry    *hePtr;
    Ns_AdpParserProc *parserProc;

    if (filename == NULL) {
	filename = "";
    }
    
    ext = strrchr(filename, '.');
    
    if (ext == NULL) {
	ext = "";
    }
    
    hePtr = Tcl_FindHashEntry(&extensionsTable, ext);
    if (hePtr == NULL) {
	parserProc = defParserProc;
    } else {
	parserProc = (Ns_AdpParserProc *) Tcl_GetHashValue(hePtr);
    }
    (*parserProc)(out, pagein);
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
ParseFile(Tcl_Interp *interp, char *file, size_t size, Ns_DString *dsPtr)
{
    int fd, status;
    size_t n;
    char *p;
    
    status = TCL_ERROR;
    fd = open(file, O_RDONLY|O_BINARY);
    if (fd < 0) {
	Tcl_AppendResult(interp, "could not open \"",
	    file, "\": ", Tcl_PosixError(interp), NULL);
    } else {
	p = ns_malloc(size + 1);
	n = read(fd, p, size);
	close(fd);
	if (n < 0) {
	    Tcl_AppendResult(interp, "read() of \"", file,
	    	"\" failed: ", Tcl_PosixError(interp), NULL);
	} else {
	    p[n] = '\0';
	    Parse(file, dsPtr, p);
	    status = TCL_OK;
	}
	ns_free(p);
    }
    return status;
}


void
NsAdpLogError(Tcl_Interp *interp, char *file, int chunk)
{
    Ns_DString ds;
    char *eargv[4];
    AdpData *adPtr;
    
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, "\n    invoked from within chunk: ");
    Ns_DStringPrintf(&ds, "%d", chunk);
    Ns_DStringAppend(&ds, " of adp: ");
    Ns_DStringAppend(&ds, file);
    Tcl_AddErrorInfo(interp, ds.string);
    Ns_TclLogError(interp);
    Ns_DStringFree(&ds);
    adPtr = NsAdpGetData();
    if (nsconf.adp.errorpage != NULL && adPtr->errorLevel == 0) {
	++adPtr->errorLevel;
	eargv[0] = "<error page>";
	eargv[1] = nsconf.adp.errorpage;
	eargv[2] = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
	if (eargv[2] == NULL) {
	    eargv[2] = interp->result;
	}
	eargv[3] = NULL;
	(void) NsTclIncludeCmd(NULL, interp, 3, eargv);
	--adPtr->errorLevel;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpParseCmd --
 *
 *	Process the ns_adp_parse command to evaluate strings or
 *	ADP files at the current call frame level.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	See comments for NsTclAdpEvalCmd and NsTclIncludeCmd.
 *
 *----------------------------------------------------------------------
 */
 
#define NUM_STATIC_ARGS 10

int
NsTclAdpParseCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int         i;
    int         filearg;
    int         isstring = NS_TRUE;
    int         retval;
    char      **pargv, *pargvStatic[NUM_STATIC_ARGS+1];
    int         pargc;
    
    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
              argv[0], " ?switches? arg ?arg1? ?arg2? ?...?\"", NULL);
        return TCL_ERROR;
    }

    filearg = 1;
    for (i = 1; i < argc; i++) {
        if (STRIEQ(argv[i], "-file")) {
            filearg++;
            isstring = NS_FALSE;
        } else if (STRIEQ(argv[i], "-string")) {
            filearg++;
            isstring = NS_TRUE;
        } else if (STRIEQ(argv[i], "-global")) {
            Tcl_SetResult(interp,
		    "deprecated -global switch passed to ns_adp_parse",
		    TCL_STATIC);
            return TCL_ERROR;
        } else if (STRIEQ(argv[i], "-local")) {
            filearg++;
        } else {
            break;
        }
    }

   /*
    * Construct new arguments and call either NsTclAdpEvalCmd for strings
    * or NsTclIncludeCmd (with ClientData set to 1) for files.
    */

    pargc = argc - filearg + 1;
    if (pargc <= NUM_STATIC_ARGS) {
	pargv = pargvStatic;
    } else {
    	pargv = ns_malloc(sizeof(char *) * (pargc + 1));
    }
    pargv[0] = argv[0];
    for (i = 0; i < pargc; i++) {
        pargv[i+1] = argv[filearg++];
    }
    if (isstring) {
        retval = NsTclAdpEvalCmd(NULL, interp, pargc, pargv);
    } else {
        retval = NsTclIncludeCmd((ClientData) 1, interp, pargc, pargv);
    }
    if (pargv != pargvStatic) {
    	ns_free(pargv);
    }
    return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * SetMimeType --
 *
 *	Sets the mime type for this adp context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Updates the mime type for this adp context, using ns_strcopy.
 *      Existing mime type is freed if not NULL.
 *
 *----------------------------------------------------------------------
 */

static void
SetMimeType(AdpData *adPtr, char *mimeType)
{
    if (adPtr->mimeType) {
        ns_free(adPtr->mimeType);
    }
    adPtr->mimeType = ns_strcopy(mimeType);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpMimeCmd --
 *
 *	Process the ns_adp_mime command to set or get the mime type
 *      returned upon completion of the parsed file.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	Potentially updates the mime type for this adp page.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpMimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    AdpData *adPtr;
    
    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
              argv[0], " ?mimeType?\"", NULL);
        return TCL_ERROR;
    }
    
    adPtr = NsAdpGetData();
    if (argc == 2) {
        SetMimeType(adPtr, argv[1]);
    }
    Tcl_SetResult(interp, adPtr->mimeType, TCL_VOLATILE);
    
    return TCL_OK;
}

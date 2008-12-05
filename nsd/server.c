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
 * server.c --
 *
 *	Routines for managing NsServer structures.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/server.c,v 1.47 2008/12/05 08:51:43 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static functions defined in this file. 
 */

static void GetCharsetEncoding(char *path, char *key, char **charsetPtr,
		   	       Tcl_Encoding *encodingPtr);
static NsServer *CreateServer(char *server);
static void RegisterRedirects(char *server);
static void RegisterMaps(char *server, char *type, Ns_OpProc *proc);
static void RegisterMap(char *server, char *type, char *map, Ns_OpProc *proc);

/*
 * Static variables defined in this file. 
 */

static Tcl_HashTable servers;	/* Table of all virtual servers. */
static Tcl_DString serverlist;	/* Tcl list of all virtual servers. */
static NsServer *initPtr;	/* Pointer to server being initialized. */
static NsServer *globalPtr;	/* Pointer to global pseudo-server. */


/*
 *----------------------------------------------------------------------
 *
 * NsGetServers --
 *
 *	Return Tcl list of all servers.
 *
 * Results:
 *	Pointer to server list string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
NsGetServers(void)
{
    return serverlist.string;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitServers --
 *
 *	Server data structures library init.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates the global pseudo-server.
 *
 *----------------------------------------------------------------------
 */

void
NsInitServers(void)
{
    Tcl_DStringInit(&serverlist);
    Tcl_InitHashTable(&servers, TCL_STRING_KEYS);
    globalPtr = CreateServer(NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetServer --
 *
 *	Return the NsServer structure, allocating if necessary.
 *
 * Results:
 *	Pointer to NsServer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NsServer *
NsGetServer(char *server)
{
    Tcl_HashEntry *hPtr;
    NsServer *servPtr = NULL;

    if (server == NULL) {
	servPtr = globalPtr;
    } else {
	hPtr = Tcl_FindHashEntry(&servers, server);
	if (hPtr != NULL) {
	    servPtr = Tcl_GetHashValue(hPtr);
	}
    }
    return servPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetInitServer --
 *
 *	Return the currently initializing server.
 *
 * Results:
 *	Pointer to NsServer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

NsServer *
NsGetInitServer(void)
{
    return initPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitServer --
 *
 *	Create and initialize a new virtual server.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on resulting Tcl and module inits.
 *
 *----------------------------------------------------------------------
 */

void
NsInitServer(char *server, Ns_ServerInitProc *initProc)
{
    Tcl_HashEntry *hPtr;
    NsServer *servPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(&servers, server, &new);
    if (!new) {
	Ns_Log(Error, "duplicate server: %s", server);
	return;
    }
    servPtr = CreateServer(server);
    Tcl_SetHashValue(hPtr, servPtr);
    Tcl_DStringAppendElement(&serverlist, server);

    /*
     * Register the fastpath and ADP requests.  Fastpath is
     * register by default for all URL's.
     */

    RegisterMap(server, "fastpath", "/", Ns_FastPathOp);
    RegisterMaps(server, "fastpath", Ns_FastPathOp);
    RegisterMaps(server, "adp", NsAdpProc);
    RegisterRedirects(server);

    /*
     * Call the given init proc, if any, which may register
     * additional static modules and then load all dynamic and static
     * modules and initialize Tcl.  The order is significant.
     */

    initPtr = servPtr;
    if (initProc != NULL) {
	(*initProc)(server);
    }
    NsLoadModules(server);
    NsTclInitServer(server);
    initPtr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateServer --
 *
 *	Create a new server with all its crazy state.
 *
 * Results:
 *	Pointer to NsServer.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static NsServer *
CreateServer(char *server)
{
    Tcl_Encoding outputEncoding;
    Ns_DString ds;
    NsServer *servPtr;
    char *path, *spath, *dirf, *p;
    Ns_Set *set;
    int i, n;

    Ns_DStringInit(&ds);
    servPtr = ns_calloc(1, sizeof(NsServer));
    servPtr->server = server;

    /*
     * Set some server options.
     */
     
    spath = path = Ns_ConfigGetPath(server, NULL, NULL);
    servPtr->opts.flags = 0;
    servPtr->opts.realm = Ns_ConfigGetValue(path, "realm");
    if (servPtr->opts.realm == NULL) {
    	servPtr->opts.realm = server;
    }
    if (Ns_ConfigGetBool(path, "enableaolpress", &i) && i) {
    	servPtr->opts.flags |= SERV_AOLPRESS;
    }
    if (!Ns_ConfigGetBool(path, "checkmodifiedsince", &i) || i) {
    	servPtr->opts.flags |= SERV_MODSINCE;
    }
    if (!Ns_ConfigGetBool(path, "noticedetail", &i) || i) {
    	servPtr->opts.flags |= SERV_NOTICEDETAIL;
    }
    p = Ns_ConfigGetValue(path, "headercase");
    if (p != NULL && STRIEQ(p, "tolower")) {
    	servPtr->opts.hdrcase = ToLower;
    } else if (p != NULL && STRIEQ(p, "toupper")) {
	servPtr->opts.hdrcase = ToUpper;
    } else {
    	servPtr->opts.hdrcase = Preserve;
    }
    if (!Ns_ConfigGetBool(path, "chunked", &i) || i) {
    	servPtr->opts.flags |= SERV_CHUNKED;
    }
    if (!Ns_ConfigGetBool(path, "gzip", &i) || i) {
    	servPtr->opts.flags |= SERV_GZIP;
    }
    if (!Ns_ConfigGetInt(path, "gzipmin", &i) || i <= 0) {
	i = 4 * 1024;
    }
    servPtr->opts.gzipmin = i;
    if (!Ns_ConfigGetInt(path, "gziplevel", &i) || i < 0 || i > 9) {
	i = 4;
    }
    servPtr->opts.gziplevel = i;

    /*
     * Set the default URL encoding used to decode the request.  The default
     * is no encoding, i.e., assume UTF-8.
     */

    GetCharsetEncoding(path, "urlcharset", NULL, &servPtr->urlEncoding);

    /*
     * Set the default charset for text/ types which do not
     * include the charset= specification, e.g., legacy code with
     * just "text/html".  The connection init code will update
     * output types with the default charset if needed.
     */

    GetCharsetEncoding(path, "outputcharset", &servPtr->defcharset,
		       &outputEncoding);

    /*
     * Set the default input encoding used to decode the query string
     * or form post.  The default is the output encoding, if any, set above.
     */

    GetCharsetEncoding(path, "inputcharset", NULL, &servPtr->inputEncoding);
    if (servPtr->inputEncoding == NULL) {
	servPtr->inputEncoding = outputEncoding;
    }

    /*
     * Set some server limits.
     */
     
    if (!Ns_ConfigGetInt(path, "errorminsize", &servPtr->limits.errorminsize)) {
    	servPtr->limits.errorminsize = 514;
    }
    if (!Ns_ConfigGetInt(path, "connsperthread", &servPtr->limits.connsperthread)) {
    	servPtr->limits.connsperthread = 0;	/* Unlimited */
    }
    
    /*
     * Initialize Tcl module config support.
     */
     
    Ns_MutexInit(&servPtr->tcl.llock);
    Ns_MutexSetName(&servPtr->tcl.llock, "ns:tcl.llock");
    Ns_CondInit(&servPtr->tcl.lcond);
    Ns_RWLockInit(&servPtr->tcl.tlock);
    Ns_CsInit(&servPtr->tcl.olock);
    Ns_MutexInit(&servPtr->tcl.plock);
    Ns_MutexSetName(&servPtr->tcl.plock, "ns:tcl.plock");
    Ns_RWLockInit(&servPtr->tcl.slock);
    Tcl_InitHashTable(&servPtr->tcl.packages, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.once, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->tcl.loops, TCL_ONE_WORD_KEYS);
    Tcl_DStringInit(&servPtr->tcl.modules);

    path = Ns_ConfigGetPath(server, NULL, "tcl", NULL);
    servPtr->tcl.library = Ns_ConfigGetValue(path, "library");
    if (servPtr->tcl.library == NULL) {
	Ns_ModulePath(&ds, server, "tcl", NULL);
	servPtr->tcl.library = Ns_DStringExport(&ds);
    }
    servPtr->tcl.initfile = Ns_ConfigGetValue(path, "initfile");
    if (servPtr->tcl.initfile == NULL) {
	Ns_HomePath(&ds, "bin", "init.tcl", NULL);
	servPtr->tcl.initfile = Ns_DStringExport(&ds);
    }

    /*
     * Initialize Tcl shared variables, sets, and channels interfaces.
     */
     
    if (!Ns_ConfigGetInt(path, "nsvbuckets", &n) || n < 1) {
	n = 8;
    }
    servPtr->nsv.nbuckets = n;
    servPtr->nsv.buckets = NsTclCreateBuckets(server, n);
    Tcl_InitHashTable(&servPtr->share.inits, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->share.vars, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->share.lock, "nstcl:share", server);
    Tcl_InitHashTable(&servPtr->var.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->var.lock, "nstcl:var", server);
    Tcl_InitHashTable(&servPtr->sets.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->sets.lock, "nstcl:sets", server);
    Tcl_InitHashTable(&servPtr->chans.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->chans.lock, "nstcl:chans", server);

    /*
     * Initialize the fastpath.
     */
     
    path = Ns_ConfigGetPath(server, NULL, "fastpath", NULL);
    if (!Ns_ConfigGetBool(path, "cache", &i) || i) {
    	if (!Ns_ConfigGetInt(path, "cachemaxsize", &n)) {
	    n = 5 * 1024 * 1000;
	}
	if (!Ns_ConfigGetInt(path, "cachemaxentry", &i) || i < 0) {
	    i = n / 10;
	}
	servPtr->fastpath.cachemaxentry = i;
    	servPtr->fastpath.cache = NsFastpathCache(server, n);
    }
    if (!Ns_ConfigGetBool(path, "mmap", &servPtr->fastpath.mmap)) {
    	servPtr->fastpath.mmap = 0;
    }
    dirf = Ns_ConfigGetValue(path, "directoryfile");
    if (dirf == NULL) {
    	dirf = Ns_ConfigGetValue(spath, "directoryfile");
    }
    if (dirf != NULL) {
    	dirf = ns_strdup(dirf);
	p = dirf;
	n = 1;
    	while ((p = (strchr(p, ','))) != NULL) {
	    ++n;
	    ++p;
	}
	servPtr->fastpath.dirc = n;
	servPtr->fastpath.dirv = ns_malloc(sizeof(char *) * n);
	for (i = 0; i < n; ++i) {
    	    p = strchr(dirf, ',');
	    if (p != NULL) {
	    	*p++ = '\0';
	    }
	    servPtr->fastpath.dirv[i] = dirf;
	    dirf = p;
	}
    }
    servPtr->fastpath.pageroot = Ns_ConfigGetValue(path, "pageroot");
    if (servPtr->fastpath.pageroot == NULL) {
    	servPtr->fastpath.pageroot = Ns_ConfigGetValue(spath, "pageroot");
	if (servPtr->fastpath.pageroot == NULL) {    	
	    Ns_ModulePath(&ds, server, NULL, "pages", NULL);
	    servPtr->fastpath.pageroot = Ns_DStringExport(&ds);
	}
    }
    p = Ns_ConfigGetValue(path, "directorylisting");
    if (p != NULL && (STREQ(p, "simple") || STREQ(p, "fancy"))) {
	p = "_ns_dirlist";
    }
    servPtr->fastpath.dirproc = Ns_ConfigGetValue(path, "directoryproc");
    if (servPtr->fastpath.dirproc == NULL) {
    	servPtr->fastpath.dirproc = p;
    }
    servPtr->fastpath.diradp = Ns_ConfigGetValue(path, "directoryadp");

    /*
     * Configure the url, proxy and redirect requests.
     */

    Tcl_InitHashTable(&servPtr->request.proxy, TCL_STRING_KEYS);
    Ns_MutexInit(&servPtr->request.plock);
    Ns_MutexSetName2(&servPtr->request.plock, "nsd:proxy", server);
    Tcl_InitHashTable(&servPtr->request.redirect, TCL_ONE_WORD_KEYS);

    /*
     * Initialize ADP.
     */

    path = Ns_ConfigGetPath(server, NULL, "adp", NULL);
    servPtr->adp.errorpage = Ns_ConfigGetValue(path, "errorpage");
    servPtr->adp.startpage = Ns_ConfigGetValue(path, "startpage");
    servPtr->adp.flags = 0;
    if (Ns_ConfigGetBool(path, "enableexpire", &i) && i) {
    	servPtr->adp.flags |= ADP_EXPIRE;
    }
    if (Ns_ConfigGetBool(path, "enabledebug", &i) && i) {
    	servPtr->adp.flags |= ADP_DEBUG;
    }
    if (Ns_ConfigGetBool(path, "safeeval", &i) && i) {
    	servPtr->adp.flags |= ADP_SAFE;
    }
    if (Ns_ConfigGetBool(path, "singlescript", &i) && i) {
    	servPtr->adp.flags |= ADP_SINGLE;
    }
    if (Ns_ConfigGetBool(path, "gzip", &i) && i) {
    	servPtr->adp.flags |= ADP_GZIP;
    }
    if (Ns_ConfigGetBool(path, "trace", &i) && i) {
    	servPtr->adp.flags |= ADP_TRACE;
    }
    if (!Ns_ConfigGetBool(path, "detailerror", &i) || i) {
    	servPtr->adp.flags |= ADP_DETAIL;
    }
    if (Ns_ConfigGetBool(path, "stricterror", &i) && i) {
    	servPtr->adp.flags |= ADP_STRICT;
    }
    if (Ns_ConfigGetBool(path, "displayerror", &i) && i) {
    	servPtr->adp.flags |= ADP_DISPLAY;
    }
    if (Ns_ConfigGetBool(path, "trimspace", &i) && i) {
    	servPtr->adp.flags |= ADP_TRIM;
    }
    if (!Ns_ConfigGetBool(path, "autoabort", &i) || i) {
    	servPtr->adp.flags |= ADP_AUTOABORT;
    }
    servPtr->adp.debuginit = Ns_ConfigGetValue(path, "debuginit");
    if (servPtr->adp.debuginit == NULL) {
    	servPtr->adp.debuginit = "ns_adp_debuginit";
    }
    if (!Ns_ConfigGetInt(path, "tracesize", &i)) {
        i = 40;
    }
    servPtr->adp.tracesize = i;
    if (!Ns_ConfigGetInt(path, "cachesize", &i)) {
	i = 5 * 1024 * 1000;
    }
    servPtr->adp.cachesize = i;
    if (!Ns_ConfigGetInt(path, "bufsize", &i)) {
	i = 1 * 1024 * 1000;
    }
    servPtr->adp.bufsize = i;

    /*
     * Initialize the ADP page and tag tables and locks.
     */

    Tcl_InitHashTable(&servPtr->adp.pages, FILE_KEYS);
    Ns_MutexInit(&servPtr->adp.pagelock);
    Ns_CondInit(&servPtr->adp.pagecond);
    Ns_MutexSetName2(&servPtr->adp.pagelock, "nsadp:pages", server);
    Tcl_InitHashTable(&servPtr->adp.tags, TCL_STRING_KEYS);
    Ns_RWLockInit(&servPtr->adp.taglock);

    return servPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCharsetEncoding --
 *
 *	Get the charset and/or encoding for given server config key.
 *	Will use process-wide config if no server config is present.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will set charsetPtr and/or encodingPtr if not NULL.
 *
 *----------------------------------------------------------------------
 */

static void
GetCharsetEncoding(char *path, char *key, char **charsetPtr,
		   Tcl_Encoding *encodingPtr)
{
    Tcl_Encoding encoding = NULL;
    char *charset;

    charset = Ns_ConfigGetValue(path, key);
    if (charset == NULL) {
	charset = NsParamString(key, NULL);
    }
    if (charset != NULL) {
	encoding = Ns_GetCharsetEncoding(charset);
    }
    if (charsetPtr != NULL) {
    	*charsetPtr = charset;
    }
    if (encodingPtr != NULL) {
	*encodingPtr = encoding;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterMaps --
 *
 *	Register requests for all "map" config lines in given
 *	config path.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See RegisterMap.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterMaps(char *server, char *type, Ns_OpProc *proc)
{
    char *path, *key;
    Ns_Set *set;
    int i;

    path = Ns_ConfigGetPath(server, NULL, type, NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	if (!strcasecmp(key, "map")) {
	    RegisterMap(server, type, Ns_SetValue(set, i), proc);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterMap --
 *
 *	Register requests for a given "map" config line.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will process requests for given URL and standard
 *	methods with given proc.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterMap(char *server, char *type, char *map, Ns_OpProc *proc)
{
    static char *methods[] = {"GET", "HEAD", "POST", NULL};
    static int nmethods = 3;
    char **largv;
    int largc, ttl, i, skip;
    Ns_Time *ttlPtr;

    /*
     * Split the map line which is either a single element for an
     * URL pattern or two elements, URL followed by time to live.
     */
 
    if (Tcl_SplitList(NULL, map, &largc, &largv) == TCL_OK) {
	skip = 0;
	if (largc == 1) {
	    ttlPtr = NULL;
	} else {
	    if (largc != 2 || Tcl_GetInt(NULL, largv[1], &ttl) != TCL_OK) {
		Ns_Log(Error, "adp[%s]: invalid map: %s", server, map);
		skip = 1;
	    } else {
	    	ttlPtr = ns_malloc(sizeof(Ns_Time));
	    	ttlPtr->sec = ttl;
	    	ttlPtr->usec = 0;
	    }
	}
	if (!skip) {
	    /*
	     * Register the request for the default methods.
	     */

	    for (i = 0; i < nmethods; ++i) {
		Ns_RegisterRequest(server, methods[i], largv[0], proc,
				   ns_free, ttlPtr, 0);
	    	Ns_Log(Notice, "%s[%s]: mapped %s %s", type, server,
		       methods[i], map);
	    }
	}
	Tcl_Free((char *) largv);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterRedirects --
 *
 *	Register redirects for given server.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	See Ns_RegisterRedirect.
 *
 *----------------------------------------------------------------------
 */

static void
RegisterRedirects(char *server)
{
    char *path, *key, *url;
    Ns_Set *set;
    int i, status;

    path = Ns_ConfigGetPath(server, NULL, "redirects", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	url = Ns_SetValue(set, i);
	status = atoi(key);
	if (status <= 0 || *url == '\0') {
	    Ns_Log(Error, "return: invalid redirect '%s=%s'", key, url);
	} else {
	    Ns_RegisterRedirect(server, status, url);
	}
    }
}

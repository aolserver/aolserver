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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/server.c,v 1.32 2004/10/26 19:52:27 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static variables defined in this file. 
 */

static NsServer *initServPtr; /* Holds currently initializing server. */


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

    if (server != NULL) {
    	hPtr = Tcl_FindHashEntry(&nsconf.servertable, server);
	if (hPtr != NULL) {
	    return Tcl_GetHashValue(hPtr);
	}
    }
    return NULL;
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
    return initServPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitServer --
 *
 *	Initialize a virtual server and all its crazy state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will later be started.
 *
 *----------------------------------------------------------------------
 */

void
NsInitServer(char *server, Ns_ServerInitProc *initProc)
{
    Tcl_HashEntry *hPtr;
    Ns_DString ds;
    NsServer *servPtr;
    char *path, *spath, *map, *key, *dirf, *p;
    Ns_Set *set;
    int i, j, n, status;

    hPtr = Tcl_CreateHashEntry(&nsconf.servertable, server, &n);
    if (!n) {
	Ns_Log(Error, "duplicate server: %s", server);
	return;
    }
    Tcl_DStringAppendElement(&nsconf.servers, server);   
    servPtr = ns_calloc(1, sizeof(NsServer));
    Tcl_SetHashValue(hPtr, servPtr);
    initServPtr = servPtr;

    /*
     * Create a new NsServer.
     */
     
    Ns_DStringInit(&ds);
    spath = path = Ns_ConfigGetPath(server, NULL, NULL);
    servPtr->server = server;

    /*
     * Set some server options.
     */
     
    servPtr->opts.realm = Ns_ConfigGetValue(path, "realm");
    if (servPtr->opts.realm == NULL) {
    	servPtr->opts.realm = server;
    }
    if (!Ns_ConfigGetBool(path, "enableaolpress", &servPtr->opts.aolpress)) {
    	servPtr->opts.aolpress = 0;
    }
    if (!Ns_ConfigGetBool(path, "checkmodifiedsince", &servPtr->opts.modsince)) {
    	servPtr->opts.modsince = 1;
    }
    if (!Ns_ConfigGetBool(path, "flushcontent", &servPtr->opts.flushcontent)) {
    	servPtr->opts.flushcontent = 0;
    }
    if (!Ns_ConfigGetBool(path, "noticedetail", &servPtr->opts.noticedetail)) {
    	servPtr->opts.noticedetail = 1;
    }
    p = Ns_ConfigGetValue(path, "headercase");
    if (p != NULL && STRIEQ(p, "tolower")) {
    	servPtr->opts.hdrcase = ToLower;
    } else if (p != NULL && STRIEQ(p, "toupper")) {
	servPtr->opts.hdrcase = ToUpper;
    } else {
    	servPtr->opts.hdrcase = Preserve;
    }

    /*
     * Encoding defaults for the server.
     */

    servPtr->encoding.outputCharset = Ns_ConfigGetValue(path, "outputCharset");
    if (servPtr->encoding.outputCharset != NULL) {

        servPtr->encoding.outputEncoding =
            Ns_GetCharsetEncoding(servPtr->encoding.outputCharset);
        if (servPtr->encoding.outputEncoding == NULL) {
            Ns_Fatal("could not find encoding for default output charset \"%s\"",
                     servPtr->encoding.outputCharset);
        }
    } else {
        servPtr->encoding.outputCharset = nsconf.encoding.outputCharset;
        servPtr->encoding.outputEncoding = nsconf.encoding.outputEncoding;
        nsconf.encoding.hackContentTypeP = nsconf.encoding.hackContentTypeP;
    }
    if (servPtr->encoding.outputEncoding != NULL) {
        servPtr->encoding.hackContentTypeP = NS_TRUE;
        Ns_ConfigGetBool(path, "HackContentType",
                         &servPtr->encoding.hackContentTypeP);
    } else {
        nsconf.encoding.hackContentTypeP = NS_FALSE;
    }
    servPtr->encoding.urlCharset = Ns_ConfigGetValue(path, "urlCharset");
    if (servPtr->encoding.urlCharset != NULL) {
        servPtr->encoding.urlEncoding =
            Ns_GetCharsetEncoding(servPtr->encoding.urlCharset);
        if( servPtr->encoding.urlEncoding == NULL ) {
            Ns_Log(Warning,
                   "no encoding found for charset \"%s\" from config",
                   servPtr->encoding.urlCharset);
        }
    } else {
        servPtr->encoding.urlCharset = nsconf.encoding.urlCharset;
        servPtr->encoding.urlEncoding = nsconf.encoding.urlEncoding;
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
     * Initialize Tcl.
     */
     
    path = Ns_ConfigGetPath(server, NULL, "tcl", NULL);
    servPtr->tcl.library = Ns_ConfigGetValue(path, "library");
    if (servPtr->tcl.library == NULL) {
	Ns_ModulePath(&ds, server, "tcl", NULL);
	servPtr->tcl.library = Ns_DStringExport(&ds);
    }
    if (!Ns_ConfigGetBool(path, "oldhttp", &servPtr->tcl.oldhttp)) {
	servPtr->tcl.oldhttp = 1;
    }
    servPtr->tcl.initfile = Ns_ConfigGetValue(path, "initfile");
    if (servPtr->tcl.initfile == NULL) {
	Ns_HomePath(&ds, "bin", "init.tcl", NULL);
	servPtr->tcl.initfile = Ns_DStringExport(&ds);
    }
    servPtr->tcl.modules = Tcl_NewObj();
    Tcl_IncrRefCount(servPtr->tcl.modules);
    Ns_RWLockInit(&servPtr->tcl.lock);
    if (!Ns_ConfigGetInt(path, "nsvbuckets", &n) || n < 1) {
	n = 8;
    }
    servPtr->nsv.nbuckets = n;
    servPtr->nsv.buckets = NsTclCreateBuckets(server, n);
    Tcl_InitHashTable(&servPtr->share.inits, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->share.vars, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->share.lock, "nstcl:share", server);
    Tcl_InitHashTable(&servPtr->var.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->sets.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->sets.lock, "nstcl:sets", server);

    /*
     * Initialize the Tcl detached channel support.
     */

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
    	servPtr->fastpath.cache =  NsFastpathCache(server, n);
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
    path = Ns_ConfigGetPath(server, NULL, "redirects", NULL);
    set = Ns_ConfigGetSection(path);
    Tcl_InitHashTable(&servPtr->request.redirect, TCL_ONE_WORD_KEYS);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	map = Ns_SetValue(set, i);
	status = atoi(key);
	if (status <= 0 || *map == '\0') {
	    Ns_Log(Error, "return: invalid redirect '%s=%s'", key, map);
	} else {
	    Ns_RegisterReturn(status, map);
	}
    }

    /*
     * Register the fastpath requests.
     */
     
    Ns_RegisterRequest(server, "GET", "/", NsFastGet, NULL, servPtr, 0);
    Ns_RegisterRequest(server, "HEAD", "/", NsFastGet, NULL, servPtr, 0);
    Ns_RegisterRequest(server, "POST", "/", NsFastGet, NULL, servPtr, 0);

    /*
     * Initialize ADP.
     */
     
    path = Ns_ConfigGetPath(server, NULL, "adp", NULL);
    servPtr->adp.errorpage = Ns_ConfigGetValue(path, "errorpage");
    servPtr->adp.startpage = Ns_ConfigGetValue(path, "startpage");
    servPtr->adp.flags = 0;
    if (Ns_ConfigGetBool(path, "safeeval", &i) && i) {
    	servPtr->adp.flags |= ADP_SAFE;
    }
    if (Ns_ConfigGetBool(path, "singlescript", &i) && i) {
    	servPtr->adp.flags |= ADP_SINGLE;
    }
    if (!Ns_ConfigGetBool(path, "enableexpire", &servPtr->adp.enableexpire)) {
    	servPtr->adp.enableexpire = 0;
    }
    if (!Ns_ConfigGetBool(path, "enabledebug", &servPtr->adp.enabledebug)) {
    	servPtr->adp.enabledebug = 0;
    }
    servPtr->adp.debuginit = Ns_ConfigGetValue(path, "debuginit");
    if (servPtr->adp.debuginit == NULL) {
    	servPtr->adp.debuginit = "ns_adp_debuginit";
    }
    servPtr->adp.defaultparser = Ns_ConfigGetValue(path, "defaultparser");
    if (servPtr->adp.defaultparser == NULL) {
    	servPtr->adp.defaultparser = "adp";
    }
    if (!Ns_ConfigGetInt(path, "cachesize", &n)) {
	n = 5 * 1024 * 1000;
    }
    servPtr->adp.cachesize = n;

    /*
     * Initialize the page and tag tables and locks.
     */

    Tcl_InitHashTable(&servPtr->adp.pages, FILE_KEYS);
    Ns_MutexInit(&servPtr->adp.pagelock);
    Ns_CondInit(&servPtr->adp.pagecond);
    Ns_MutexSetName2(&servPtr->adp.pagelock, "nsadp:pages", server);
    Tcl_InitHashTable(&servPtr->adp.tags, TCL_STRING_KEYS);
    Ns_RWLockInit(&servPtr->adp.taglock);

    /*
     * Register ADP for any requested URLs.
     */

    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	char **largv;
	int largc, ttl;
	char *methods[] = {"GET", "HEAD", "POST"};

	key = Ns_SetKey(set, i);
	if (!strcasecmp(key, "map")) {
	    map = Ns_SetValue(set, i);
	    if (Tcl_SplitList(NULL, map, &largc, &largv) == TCL_OK) {
		if (largc == 1) {
		    ttl = -1;
		} else if (largc == 2) {
		    (void) Tcl_GetInt(NULL, largv[1], &ttl);
		} else {
		    Ns_Log(Error, "adp[%s]: invalid map: %s", server, map);
		    continue;
		}
		for (j = 0; j < 3; ++j) {
	    	    Ns_RegisterRequest(server, methods[j], largv[0],
					NsAdpProc, NULL, (void *) ttl, 0);
		}
	    	Ns_Log(Notice, "adp[%s]: mapped %s %d", server, map, ttl);
		Tcl_Free((char *) largv);
	    }
	}
    }

    /*
     * Call the static server init proc, if any, which may register
     * static modules.
     */
    
    if (initProc != NULL) {
	(*initProc)(server);
    }

    /*
     * Load modules and initialize Tcl.  The order is significant.
     */

    NsLoadModules(server);
    NsTclInitServer(server);
    initServPtr = NULL;
}

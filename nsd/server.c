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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/server.c,v 1.1 2001/03/12 22:06:14 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static Tcl_HashTable table;


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

    hPtr = Tcl_FindHashEntry(&table, server);
    return (hPtr ? Tcl_GetHashValue(hPtr) : NULL);
}

void
NsStartServers(void)
{
    NsServer *servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	servPtr = Tcl_GetHashValue(hPtr);
	NsStartServer(servPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}

void
NsStopServers(Ns_Time *toPtr)
{
    NsServer *servPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;

    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	servPtr = Tcl_GetHashValue(hPtr);
	NsStopServer(servPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
    hPtr = Tcl_FirstHashEntry(&table, &search);
    while (hPtr != NULL) {
	servPtr = Tcl_GetHashValue(hPtr);
	NsWaitServer(servPtr, toPtr);
	hPtr = Tcl_NextHashEntry(&search);
    }
}


void
NsInitServer(Ns_ServerInitProc *initProc, char *server)
{
    Tcl_HashEntry *hPtr;
    Ns_DString ds;
    static int initialized = 0;
    NsServer *servPtr;
    Conn *connBufPtr;
    Bucket *buckPtr;
    char *path, *spath, *map, *key, *dirf, *p;
    char buf[200];
    Ns_Set *set;
    int i, n, maxconns, status;

    if (!initialized) {
	Tcl_InitHashTable(&table, TCL_STRING_KEYS);
    	Tcl_DStringInit(&nsconf.servers);   
	initialized = 1;
    }
    hPtr = Tcl_CreateHashEntry(&table, server, &n);
    if (!n) {
	Ns_Log(Error, "duplicate server: %s", server);
	return;
    }
    Tcl_DStringAppendElement(&nsconf.servers, server);   
    servPtr = ns_calloc(1, sizeof(NsServer));
    Tcl_SetHashValue(hPtr, servPtr);

    /*
     * Create a new NsServer.
     */
     
    Ns_DStringInit(&ds);
    spath = path = Ns_ConfigPath(server, NULL, NULL);
    servPtr->server = server;
    Ns_MutexSetName2(&servPtr->queue.lock, "nsd:queue", server);
    if (!Ns_ConfigGetInt(path, "threadyield", &servPtr->queue.yield)) {
	servPtr->queue.yield = 0;
    }
    if (!Ns_ConfigGetInt(path, "maxconnections", &maxconns)) {
	maxconns = 100;
    }
    if (!Ns_ConfigGetInt(path, "minthreads", &servPtr->threads.min)) {
	servPtr->threads.min = 0;
    }
    if (!Ns_ConfigGetInt(path, "maxthreads", &servPtr->threads.max)) {
	servPtr->threads.max = 10;
    }
    if (!Ns_ConfigGetInt(path, "threadtimeout", &servPtr->threads.timeout)) {
	servPtr->threads.timeout = 120;
    }

    /*
     * Pre-allocate all available connection structures to avoid having
      to repeatedly allocate and free them at run time and to ensure there
     * is a per-set maximum number of simultaneous connections to handle
     * before Ns_QueueConn begins to return NS_ERROR.
     */

    connBufPtr = ns_malloc(sizeof(Conn) * maxconns);
    for (n = 0; n < maxconns - 1; ++n) {
	connBufPtr[n].nextPtr = &connBufPtr[n+1];
    }
    connBufPtr[n].nextPtr = NULL;
    servPtr->queue.freePtr = &connBufPtr[0];

    /*
     * Determine the minimum and maximum number of threads, adjusting the
     * values as needed.  The threadtimeout value is the maximum number of
     * seconds a thread will wait for a connection before exiting if the
     * current number of threads is above the minimum.
     */

    if (servPtr->threads.max > maxconns) {
	Ns_Log(Warning, "serv: cannot have more maxthreads than maxconns: "
	       "%d max threads adjusted down to %d max connections",
	       servPtr->threads.max, maxconns);
	servPtr->threads.max = maxconns;
    }
    if (servPtr->threads.min > servPtr->threads.max) {
	Ns_Log(Warning, "serv: cannot have more minthreads than maxthreads: "
	       "%d min threads adjusted down to %d max threads",
	       servPtr->threads.min, servPtr->threads.max);
	servPtr->threads.min = servPtr->threads.max;
    }

    /*
     * Set some server options.
     */
     
    servPtr->opts.realm = Ns_ConfigGet(path, "realm");
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
    p = Ns_ConfigGet(path, "headercase");
    if (p != NULL && STRIEQ(p, "tolower")) {
    	servPtr->opts.hdrcase = ToLower;
    } else if (p != NULL && STRIEQ(p, "toupper")) {
	servPtr->opts.hdrcase = ToUpper;
    } else {
    	servPtr->opts.hdrcase = Preserve;
    }

    /*
     * Set some server limits.
     */
     
    if (!Ns_ConfigGetInt(path, "sendfdthreshold", &servPtr->limits.sendfdmin)) {
    	servPtr->limits.sendfdmin = 2048;
    }
    if (!Ns_ConfigGetInt(path, "errorminsize", &servPtr->limits.errorminsize)) {
    	servPtr->limits.errorminsize = 514;
    }
    if (!Ns_ConfigGetInt(path, "maxheaders", &servPtr->limits.maxheaders)) {
    	servPtr->limits.maxheaders = 16 * 1024;
    }
    if (!Ns_ConfigGetInt(path, "maxline", &servPtr->limits.maxline)) {
    	servPtr->limits.maxline = 8 * 1024;
    }
    if (!Ns_ConfigGetInt(path, "maxpost", &servPtr->limits.maxpost)) {
    	servPtr->limits.maxpost = 64 * 1024;
    }
    
    /*
     * Initialize Tcl.
     */
     
    path = Ns_ConfigPath(server, NULL, "tcl", NULL);
    servPtr->tcl.library = Ns_ConfigGet(path, "library");
    if (servPtr->tcl.library == NULL) {
	Ns_ModulePath(&ds, server, "tcl", NULL);
	servPtr->tcl.library = Ns_DStringExport(&ds);
    }
    servPtr->tcl.initfile = Ns_ConfigGet(path, "initfile");
    if (servPtr->tcl.initfile == NULL) {
	Ns_HomePath(&ds, "bin", "init.tcl", NULL);
	servPtr->tcl.initfile = Ns_DStringExport(&ds);
    }
    if (!Ns_ConfigGetInt(path, "nsvbuckets", &n) || n < 1) {
	n = 8;
    }
    servPtr->nsv.nbuckets = n;
    servPtr->nsv.buckets = ns_malloc(sizeof(Bucket) * n);
    while (--n >= 0) {
	sprintf(buf, "nsv:%d", n);
	buckPtr = &servPtr->nsv.buckets[n];
	Tcl_InitHashTable(&buckPtr->arrays, TCL_STRING_KEYS);
	Ns_MutexInit(&buckPtr->lock);
	Ns_MutexSetName2(&buckPtr->lock, buf, server);
    }
    Tcl_InitHashTable(&servPtr->share.inits, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->share.vars, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->share.lock, "ns:share", server);
    Tcl_InitHashTable(&servPtr->var.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->sets.table, TCL_STRING_KEYS);
    Ns_MutexSetName2(&servPtr->sets.lock, "ns:sets", server);

    /*
     * Initialize the fastpath.
     */
     
    path = Ns_ConfigPath(server, NULL, "fastpath", NULL);
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
    dirf = Ns_ConfigGet(path, "directoryfile");
    if (dirf == NULL) {
    	dirf = Ns_ConfigGet(spath, "directoryfile");
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
    servPtr->fastpath.pageroot = Ns_ConfigGet(path, "pageroot");
    if (servPtr->fastpath.pageroot == NULL) {
    	servPtr->fastpath.pageroot = Ns_ConfigGet(spath, "pageroot");
	if (servPtr->fastpath.pageroot == NULL) {    	
	    Ns_ModulePath(&ds, server, NULL, "pages", NULL);
	    servPtr->fastpath.pageroot = Ns_DStringExport(&ds);
	}
    }
    p = Ns_ConfigGet(path, "directorylisting");
    if (p != NULL && (STREQ(p, "simple") || STREQ(p, "fancy"))) {
	p = "_ns_dirlist";
    }
    servPtr->fastpath.dirproc = Ns_ConfigGet(path, "directoryproc");
    if (servPtr->fastpath.dirproc == NULL) {
    	servPtr->fastpath.dirproc = p;
    }
    servPtr->fastpath.diradp = Ns_ConfigGet(path, "directoryadp");

    /*
     * Configure the proxy and redirects requests , e.g., not found 404.
     */

    Tcl_InitHashTable(&servPtr->request.proxy, TCL_STRING_KEYS);

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
    	    hPtr = Tcl_CreateHashEntry(&servPtr->request.redirect, (char *) status, &n);
    	    if (!n) {
		ns_free(Tcl_GetHashValue(hPtr));
    	    }
	    Tcl_SetHashValue(hPtr, ns_strdup(map));
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
     
    path = Ns_ConfigPath(server, NULL, "adp", NULL);
    servPtr->adp.errorpage = Ns_ConfigGet(path, "errorpage");
    servPtr->adp.startpage = Ns_ConfigGet(path, "startpage");
    if (!Ns_ConfigGetBool(path, "enableexpire", &servPtr->adp.enableexpire)) {
    	servPtr->adp.enableexpire = 0;
    }
    if (!Ns_ConfigGetBool(path, "enabledebug", &servPtr->adp.enabledebug)) {
    	servPtr->adp.enabledebug = 0;
    }
    servPtr->adp.debuginit = Ns_ConfigGet(path, "debuginit");
    if (servPtr->adp.debuginit == NULL) {
    	servPtr->adp.debuginit = "ns_adp_debuginit";
    }
    servPtr->adp.defaultparser = Ns_ConfigGet(path, "defaultparser");
    if (servPtr->adp.defaultparser == NULL) {
    	servPtr->adp.defaultparser = "adp";
    }
    if (!Ns_ConfigGetInt(path, "cachesize", &servPtr->adp.cachesize)) {
    	servPtr->adp.cachesize = 5 * 1024 * 1000;
    }
    if (!Ns_ConfigGetBool(path, "threadcache", &servPtr->adp.threadcache)) {
    	servPtr->adp.threadcache = 1;
    }
    if(!servPtr->adp.threadcache && (!Ns_ConfigGetBool(path, "cache", &n) || n)) {
        servPtr->adp.cache = NsAdpCache(server, servPtr->adp.cachesize);
    }

    /*
     * Initialize the fancy parser tags table.
     */

    Tcl_InitHashTable(&servPtr->adp.tags, TCL_STRING_KEYS);
    Ns_MutexInit(&servPtr->adp.lock);
    Ns_MutexSetName2(&servPtr->adp.lock, "nsadp", server);

    /*
     * Register ADP for any requested URLs.
     */

    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	if (!strcasecmp(key, "map")) {
	    map = Ns_SetValue(set, i);
	    Ns_RegisterRequest(server, "GET", map, NsAdpProc, NULL, servPtr, 0);
	    Ns_RegisterRequest(server, "HEAD", map, NsAdpProc, NULL, servPtr, 0);
	    Ns_RegisterRequest(server, "POST", map, NsAdpProc, NULL, servPtr, 0);
	    Ns_Log(Notice, "adp[%s]: mapped %s", server, map);
	}
    }

    NsDbInitServer(server);

    /*
     * Call custom init, if any.
     */

    if (initProc != NULL && (*initProc)(server) != NS_OK) {
	Ns_Fatal("nsmain: Ns_ServerInitProc failed");
    }

    /*
     * Load modules.
     */

    NsLoadModules(server);

    /*
     * Initialize Tcl.
     */

    NsTclInitServer(server);

}

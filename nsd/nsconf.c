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
 * nsconf.c --
 *
 *	Various core configuration.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsconf.c,v 1.7 2000/10/17 17:27:38 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

struct _nsconf nsconf;
int nsConfQuiet;

static void
Log(char *path, char *key, char *value)
{
    if (!nsConfQuiet) Ns_Log(Notice, "conf: [%s]%s = %s", path, key, value);
}


static int
GetInt(char *path, char *key, int def)
{
    int i;

    if (!Ns_ConfigGetInt(path, key, &i) || i < 0) {
	i = def;
    }
    if (!nsConfQuiet) Ns_Log(Notice, "conf: [%s]%s = %d", path, key, i);
    return i;
}


static bool
GetBool(char *path, char *key, int def)
{
    int i;

    if (!Ns_ConfigGetBool(path, key, &i)) {
	i = def;
    }
    Log(path, key, i ? "on" : "off");
    return i;
}


static char *
GetString(char *path, char *key, char *def)
{
    char *s;

    s = Ns_ConfigGet(path, key);
    if (s == NULL) {
	s = def;
    }
    Log(path, key, s ? s : "\"\"");
    return s;
}


static char *
GetString2(char *path, char *key, Ns_DString *dsPtr)
{
    char *s;

    s = GetString(path, key, dsPtr->string);
    if (s == dsPtr->string) {
	s = Ns_DStringExport(dsPtr);
    }
    return s;
}


static char *
GetFile(char *path, char *key)
{
    char *file;
    
    file = GetString(path, key, NULL);
    if (file != NULL && access(file, R_OK) != 0) {
    	Ns_Log(Error, "conf: access(%s, R_OK) failed: %s",
	       file, strerror(errno));
	Ns_Log(Error, "conf: [%s]%s reset to NULL",
	       path, key);
	file = NULL;
    }
    return file;
}


#define PARAMS "ns/parameters"

void
NsConfInit(void)
{
    char *s, *p, *path, *olddf, *oldp;
    int i;
    Ns_DString ds, pds;
    char *ignored[] = {"creatmode", "mkdirmode", "faststart", "quotesize",
		       "quotewarningperiod", "checkstats",
		       "checkstatsinterval", "logqueryontclerror",
		       "connectionyield", NULL};
    
    Ns_DStringInit(&ds);
    Ns_DStringInit(&pds);

    /*
     * log.c
     */
     
    nsconf.log.expanded = GetBool(PARAMS, "logexpanded", 0);
    nsconf.log.dev = GetBool(PARAMS, "dev", 0);
    nsconf.log.debug = GetBool(PARAMS, "debug", 0);
    nsconf.log.maxback = GetInt(PARAMS, "maxbackup", 10);

    /*
     * libnsthread.a
     */

    i = GetInt(PARAMS, "stacksize", nsThreadStackSize);
    nsThreadStackSize = GetInt("ns/threads", "stacksize", i);
    nsThreadMutexMeter = GetBool("ns/threads", "mutexmeter", 0);
    

    /*
     * nsmain.c
     */
         
    nsconf.shutdowntimeout = GetInt(PARAMS, "shutdowntimeout", 20);
    nsconf.bufsize = GetInt(PARAMS, "iobufsize", 16000);

    /*
     * sched.c
     */

    nsconf.sched.maxelapsed = GetInt(PARAMS, "schedmaxelapsed", 2);

    /*
     * binder.c, win32.c
     */

    nsconf.backlog = GetInt(PARAMS, "listenbacklog", 32);
    
    /*
     * dns.c
     */
     
    nsconf.dns.cache = GetBool(PARAMS, "dnscache", 1);
    nsconf.dns.timeout = GetInt(PARAMS, "dnscachetimeout", 60) * 60;
    nsconf.dstring.maxentries = GetInt(PARAMS, "dstringcachemaxentries", 10);
    nsconf.dstring.maxsize = GetInt(PARAMS, "dstringcachemaxsize", 3*1024);

    /*
     * exec.c
     */
     
    nsconf.exec.checkexit = GetBool(PARAMS, "checkexitcode", 1);

    /*
     * keepalive.c
     */
     
    nsconf.keepalive.timeout = GetInt(PARAMS, "keepalivetimeout", 30);
    if (nsconf.keepalive.timeout > 0) {
	nsconf.keepalive.enabled = 1;
    }
    nsconf.keepalive.maxkeep = GetInt(PARAMS, "maxkeepalive", 100);

    /*
     * return.c, serv.c, conn.c
     */

    /*
     * Configuration values come from the server-specific
     * config section (e.g., ns/server/server1) even though
     * only a single server runs in AOLserver 3.0+.  This is
     * for backwards compatibility with pre-3.0 config file
     * versions and so a single config file can be used
     * with multiple server processes.
     */

    Ns_DStringTrunc(&pds, 0);
    path = Ns_DStringVarAppend(&pds, "ns/server/", nsServer, NULL);
    nsconf.serv.realm = GetString(path, "realm", nsServer);
    nsconf.serv.aolpress = GetBool(path, "enableaolpress", 0);
    nsconf.serv.sendfdmin = GetInt(path, "sendfdthreshold", 2048);
    nsconf.serv.maxconns = GetInt(path, "maxconnections", 100);
    nsconf.serv.maxdropped = GetInt(path, "maxdropped", 0);
    /* NB: DirectoryFile, DirectoryListing, and Pages in old location. */
    olddf = GetString(path, "directoryfile", NULL);
    oldp = GetString(path, "pageroot", NULL);
    s = GetString(path, "directorylisting", "none");
    if (STREQ(s, "simple") || STREQ(s, "fancy")) {
	s = "_ns_dirlist";
    } else {
	s = NULL;
    }
    nsconf.fastpath.dirproc = GetString(path, "directoryproc", s);
    nsconf.fastpath.diradp = GetString(path, "directoryadp", NULL);
    if (GetBool(path, "globalstats", 0)) {
	nsconf.serv.stats |= STATS_GLOBAL;
    }
    nsconf.serv.maxurlstats = GetInt(path, "maxurlstats", 1000);
    if (nsconf.serv.maxurlstats > 0 && GetBool(path, "urlstats", 0)) {
	nsconf.serv.stats |= STATS_PERURL;
    }
    nsconf.serv.errorminsize = GetInt(path,  "errorminsize", 514);
    nsconf.serv.noticedetail = GetBool(path, "noticedetail", 1);

    /*
     * ConnsPerThread specifies the maximum number of connections for
     * a thread to handle before exiting perhaps to benefit from some
     * TLS garbage collection.  A value <= 0 disables this feature.
     */

    nsconf.serv.connsperthread = GetInt(path, "connsperthread", 0);
    nsconf.serv.minthreads = GetInt(path, "minthreads", 0);
    nsconf.serv.maxthreads = GetInt(path, "maxthreads", 20);
    nsconf.serv.threadtimeout = GetInt(path, "threadtimeout", 120);
    nsconf.conn.maxheaders = GetInt(path, "maxheaders", 16384);
    nsconf.conn.maxline = GetInt(path, "maxline", 8192);
    nsconf.conn.maxpost = GetInt(path, "maxpost", 65536);
    nsconf.conn.flushcontent = GetBool(path, "flushcontent", 0);
    nsconf.conn.modsince = GetBool(path, "checkmodifiedsince", 1);
    s = GetString(path, "headercase", "preserve");
    if (STRIEQ(s, "tolower")) {
    	nsconf.conn.hdrcase = ToLower;
    } else if (STRIEQ(s, "toupper")) {
	nsconf.conn.hdrcase = ToUpper;
    } else {
    	if (!STRIEQ(s, "preserve")) {
	    Ns_Log(Error, "conf: "
		   "[%s]headercase = %s invalid - set to preserve", path, s);
    	}
    	nsconf.conn.hdrcase = Preserve;
    }

    /*
     * fastpath.c
     */

    Ns_DStringTrunc(&pds, 0);
    path = Ns_DStringVarAppend(&pds, "ns/server/", nsServer, "/fastpath", NULL);
    nsconf.fastpath.mmap = GetBool(path, "mmap", 0);
    nsconf.fastpath.cache = GetBool(path, "cache", 1);
    if (nsconf.fastpath.cache) {
    	nsconf.fastpath.cachesize = GetInt(path, "cachemaxsize", 5000*1024);
	nsconf.fastpath.cachemaxentry = GetInt(path, "cachemaxentry", 8192);
    }
    s = GetString(path, "directoryfile", olddf);
    if (s != NULL) {
    	s = ns_strdup(s);
    	nsconf.fastpath.dirc = 1;
	p = s;
    	while ((p = (strchr(p, ','))) != NULL) {
	    ++nsconf.fastpath.dirc;
	    ++p;
	}
	nsconf.fastpath.dirv = ns_malloc(sizeof(char *) * nsconf.fastpath.dirc);
	for (i = 0; i < nsconf.fastpath.dirc; ++i) {
    	    p = strchr(s, ',');
	    if (p != NULL) {
	    	*p++ = '\0';
	    }
	    nsconf.fastpath.dirv[i] = s;
	    s = p;
	}
    }
    if (oldp != NULL) {
    	nsconf.fastpath.pageroot = GetString(path, "pageroot", oldp);
    } else {
    	Ns_DStringTrunc(&ds, 0);
    	Ns_ModulePath(&ds, nsServer, NULL, "pages", NULL);
	nsconf.fastpath.pageroot = GetString2(path, "pageroot", &ds);
    }

    /*
     * tclinit.c
     */
     
    Ns_DStringTrunc(&pds, 0);
    path = Ns_DStringVarAppend(&pds, "ns/server/", nsServer, "/tcl", NULL);
    nsconf.tcl.autoclose = GetBool(path, "autoclose", 1);
    nsconf.tcl.debug = GetBool(path, "debug", 0);    
    Ns_DStringTrunc(&ds, 0);
    Ns_ModulePath(&ds, nsServer, "tcl", NULL);
    nsconf.tcl.library = GetString2(path, "library", &ds);
    Ns_DStringTrunc(&ds, 0);
    Ns_HomePath(&ds, "modules", "tcl", NULL);
    nsconf.tcl.sharedlibrary = GetString2(path, "sharedlibrary", &ds);
    nsconf.tcl.statlevel = GetInt(path, "statlevel", 0);
    nsconf.tcl.statmaxbuf = GetInt(path, "statmaxbuf", 1000);
    nsconf.tcl.nsvbuckets = GetInt(path, "nsvbuckets", 8);

    /*
     * adp.c, adpfancy.c
     */

    Ns_DStringTrunc(&pds, 0);
    path = Ns_DStringVarAppend(&pds, "ns/server/", nsServer, "/adp", NULL);
    nsconf.adp.errorpage = GetFile(path, "errorpage");
    nsconf.adp.startpage = GetFile(path, "startpage");
    nsconf.adp.enableexpire = GetBool(path, "enableexpire", 0);
    nsconf.adp.enabledebug = GetBool(path, "enabledebug", 0);
    nsconf.adp.debuginit = GetString(path, "debuginit", "ns_adp_debuginit");
    nsconf.adp.defaultparser = GetString(path, "defaultparser", "adp");
    nsconf.adp.taglocks = GetBool(path, "taglocks", 0);

    /*
     * ADP supports three caching options:
     *
     * 1.  Disabled:  No caching, files read and parsed on each connection.
     *     This uses no cache memory but would likley result in higher load.
     *
     * 2.  Shared:  Single, interlocked cache for all threads.  For Tcl 8.2,
     *     this disables the byte-code caching code in tclNsd.c.  This is
     *     option uses less memory but, although unlikely, could result
     *     in lock contention.  This is the default for 7.6.
     *
     * 3.  Private:  Each thread reads, parses, and caches files, enabling
     *     bytes codes for Tcl 8.2.  This option uses more memory and could,
     *     although unlikley, result in lower cache hit rates and higher
     *     load.  This is the default for 8.2.
     */

    nsconf.adp.cache = GetBool(path, "cache", 1);
    if (nsconf.adp.cache) {
	i = (*nsTclVersion == '7' ? 0 : 1);
	nsconf.adp.threadcache = GetBool(path, "threadcache", i);
	nsconf.adp.cachesize = GetInt(path, "cachesize", 5000*1024);
    }
    
    for (i = 0; ignored[i] != NULL; ++i) {
	if (Ns_ConfigGet(PARAMS, ignored[i]) != NULL) {
	    Ns_Log(Warning, "conf: ignored 2.x option: %s", ignored[i]);
	}
    }

    Ns_DStringFree(&ds);
}

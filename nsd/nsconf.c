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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsconf.c,v 1.29 2002/10/14 23:20:33 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include "nsconf.h"

static int GetInt(char *key, int def);
static int GetBool(char *key, int def);
struct _nsconf nsconf;


/*
 *----------------------------------------------------------------------
 *
 * NsInitConf --
 *
 *	Initialize core elements of the nsconf structure at startup.
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
NsInitConf(void)
{
    Ns_DString addr;
    static char cwd[PATH_MAX];
    extern char *nsBuildDate; /* NB: Declared in stamp.c */

    Ns_ThreadSetName("-main-");

    /*
     * At library load time the server is considered started. 
     * Normally it's marked stopped immediately by Ns_Main unless
     * libnsd is being used for some other, non-server program.
     */
     
    nsconf.state.started = 1;
    Ns_MutexInit(&nsconf.state.lock);
    Ns_MutexSetName(&nsconf.state.lock, "nsd:conf");

    nsconf.build	 = nsBuildDate;
    nsconf.name          = NSD_NAME;
    nsconf.version       = NSD_VERSION;
    nsconf.tcl.version	 = TCL_VERSION;
    time(&nsconf.boot_t);
    nsconf.pid = getpid();
    nsconf.home = getcwd(cwd, sizeof(cwd));
    nsconf.tcl.objcmds = 1;
    if (gethostname(nsconf.hostname, sizeof(nsconf.hostname)) != 0) {
        strcpy(nsconf.hostname, "localhost");
    }
    Ns_DStringInit(&addr);
    if (Ns_GetAddrByHost(&addr, nsconf.hostname)) {
    	strcpy(nsconf.address, addr.string);
    }
    Ns_DStringFree(&addr);

    Tcl_InitHashTable(&nsconf.sections, TCL_STRING_KEYS);
    Tcl_DStringInit(&nsconf.servers);
    Tcl_InitHashTable(&nsconf.servertable, TCL_STRING_KEYS);
}


/*
 *----------------------------------------------------------------------
 *
 * NsConfUpdate --
 *
 *	Update various elements of the nsconf structure now that
 *	the config script has been evaluated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Various, depending on config.
 *
 *----------------------------------------------------------------------
 */

void
NsConfUpdate(void)
{
    int i;
    Ns_DString ds;
    
    NsUpdateEncodings();
    NsUpdateMimeTypes();

    Ns_DStringInit(&ds);

    /*
     * libnsthread
     */

    if (!Ns_ConfigGetInt(NS_CONFIG_THREADS, "stacksize", &i)) {
    	i = GetInt("stacksize", THREAD_STACKSIZE_INT);
    }
    Ns_ThreadStackSize(i);

    /*
     * log.c
     */
    
    if (GetBool("logusec", LOG_USEC_BOOL)) {
	nsconf.log.flags |= LOG_USEC;
    }
    if (GetBool("logroll", LOG_ROLL_BOOL)) {
	nsconf.log.flags |= LOG_ROLL;
    }
    if (GetBool("logexpanded", LOG_EXPANDED_BOOL)) {
	nsconf.log.flags |= LOG_EXPAND;
    }
    if (GetBool("debug", LOG_DEBUG_BOOL)) {
	nsconf.log.flags |= LOG_DEBUG;
    }
    if (GetBool("logdev", LOG_DEV_BOOL)) {
	nsconf.log.flags |= LOG_DEV;
    }
    if (!GetBool("lognotice", LOG_NOTICE_BOOL)) {
	nsconf.log.flags |= LOG_NONOTICE;
    }
    nsconf.log.maxback  = GetInt("logmaxbackup", LOG_MAXBACK_INT);
    nsconf.log.maxlevel = GetInt("logmaxlevel", LOG_MAXLEVEL_INT);
    nsconf.log.maxbuffer  = GetInt("logmaxbuffer", LOG_MAXBUFFER_INT);
    nsconf.log.flushint  = GetInt("logflushinterval", LOG_FLUSHINT_INT);
    nsconf.log.file = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS, "serverlog");
    if (nsconf.log.file == NULL) {
	nsconf.log.file = "server.log";
    }
    if (Ns_PathIsAbsolute(nsconf.log.file) == NS_FALSE) {
	Ns_HomePath(&ds, "log", nsconf.log.file, NULL);
	nsconf.log.file = Ns_DStringExport(&ds);
    }

    /*
     * nsmain.c
     */
         
    nsconf.shutdowntimeout = GetInt("shutdowntimeout", SHUTDOWNTIMEOUT);

    /*
     * sched.c
     */

    nsconf.sched.maxelapsed = GetInt("schedmaxelapsed", SCHED_MAXELAPSED_INT);

    /*
     * binder.c, win32.c
     */

    nsconf.backlog = GetInt("listenbacklog", BACKLOG);
    
    /*
     * dns.c
     */
     
    if (GetBool("dnscache", DNS_CACHE_BOOL)) {
	int max = GetInt("dnscachemaxentries", 100);
	i = GetInt("dnscachetimeout", DNS_TIMEOUT_INT);
	if (max > 0 && i > 0) {
	    i *= 60; /* NB: Config minutes, seconds internally. */
	    NsEnableDNSCache(i, max);
	}
    }

    /*
     * keepalive.c
     */
     
    nsconf.keepalive.timeout = GetInt("keepalivetimeout", KEEPALIVE_TIMEOUT_INT);
    if (nsconf.keepalive.timeout > 0) {
	nsconf.keepalive.enabled = 1;
    }
    nsconf.keepalive.maxkeep = GetInt("maxkeepalive", KEEPALIVE_MAXKEEP_INT);

    /*
     * tclinit.c
     */
     
    Ns_HomePath(&ds, "modules", "tcl", NULL);
    nsconf.tcl.sharedlibrary = Ns_DStringExport(&ds);

    /*
     * tclcmds.c
     */

    if (GetBool("objcmds", 1)) {
	nsconf.tcl.objcmds = 1;
    }
    
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * GetInt, GetBool --
 *
 *	Helper routines for getting int or bool config values, using
 *	default values if necessary.
 *
 * Results:
 *	Int value of 1/0 bool.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetInt(char *key, int def)
{
    int i;

    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, key, &i) || i < 0) {
	i = def;
    }
    return i;
}

static bool
GetBool(char *key, int def)
{
    int i;

    if (!Ns_ConfigGetBool(NS_CONFIG_PARAMETERS, key, &i)) {
	i = def;
    }
    return i;
}

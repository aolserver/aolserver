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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsconf.c,v 1.16 2001/03/26 15:32:26 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include "nsconf.h"

#define NS_CONFIG_PARAMETERS "ns/parameters"
#define NS_CONFIG_THREADS "ns/threads"

struct _nsconf nsconf;

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


void
NsConfInit(void)
{
    int i;
    Ns_DString ds;
    
    Tcl_DStringInit(&nsconf.servers);

    /*
     * libnsthread.a
     */

    if (!Ns_ConfigGetInt(NS_CONFIG_THREADS, "stacksize", &i)) {
    	i = GetInt("stacksize", THREAD_STACKSIZE_INT);
    }
    nsThreadStackSize   = i;
    if (!Ns_ConfigGetBool(NS_CONFIG_THREADS, "mutexmeter", &i)) {
	i = THREAD_MUTEXMETER_BOOL;
    }
    nsThreadMutexMeter  = i;

    /*
     * log.c
     */
     
    nsconf.log.expanded = GetBool("logexpanded", LOG_EXPANDED_BOOL);
    nsconf.log.dev      = GetBool("dev", LOG_DEV_BOOL);
    nsconf.log.debug    = GetBool("debug", LOG_DEBUG_BOOL);
    nsconf.log.maxback  = GetInt("maxbackup", LOG_MAXBACK_INT);

    /*
     * nsmain.c
     */
         
    nsconf.shutdowntimeout = GetInt("shutdowntimeout", SHUTDOWNTIMEOUT);
    nsconf.startuptimeout  = GetInt("startuptimeout", STARTUPTIMEOUT);

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
	    NsEnableDNSCache(i * 60, max);
	}
    }

    /*
     * exec.c
     */
     
    nsconf.exec.checkexit = GetBool("checkexitcode", EXEC_CHECKEXIT_BOOL);

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
     
    Ns_DStringInit(&ds);
    Ns_HomePath(&ds, "modules", "tcl", NULL);
    nsconf.tcl.sharedlibrary = Ns_DStringExport(&ds);
    
    Ns_DStringFree(&ds);
}

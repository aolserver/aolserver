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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsconf.c,v 1.40 2005/08/05 18:45:28 shmooved Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define THREAD_STACKSIZE	(128*1024)
#define SCHED_MAXELAPSED	2
#define SHUTDOWNTIMEOUT		20
#define LISTEN_BACKLOG		32
#define TCL_INITLCK		0
#define HTTP_MAJOR		1
#define HTTP_MINOR		1

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
     * Set various core environment variables.
     */

    nsconf.build	 = nsBuildDate;
    nsconf.name          = NSD_NAME;
    nsconf.version       = NSD_VERSION;
    nsconf.tcl.version	 = TCL_VERSION;
    nsconf.http.major    = HTTP_MAJOR;
    nsconf.http.minor    = HTTP_MINOR;
    time(&nsconf.boot_t);
    nsconf.pid = getpid();
    nsconf.home = getcwd(cwd, sizeof(cwd));
    if (gethostname(nsconf.hostname, sizeof(nsconf.hostname)) != 0) {
        strcpy(nsconf.hostname, "localhost");
    }
    Ns_DStringInit(&addr);
    if (Ns_GetAddrByHost(&addr, nsconf.hostname)) {
        strcpy(nsconf.address, addr.string);
    } else {
        strcpy(nsconf.address, "0.0.0.0");
    }
    Ns_DStringFree(&addr);

    /*
     * Set various default values.
     */

    nsconf.shutdowntimeout = SHUTDOWNTIMEOUT;
    nsconf.sched.maxelapsed = SCHED_MAXELAPSED;
    nsconf.backlog = LISTEN_BACKLOG;
    nsconf.http.major = HTTP_MAJOR;
    nsconf.http.minor = HTTP_MINOR;
    nsconf.tcl.lockoninit = TCL_INITLCK;
    
    /*
     * At library load time the server is considered started. 
     * Normally it's marked stopped immediately by Ns_Main unless
     * libnsd is being used for some other, non-server program.
     */
     
    Ns_MutexSetName(&nsconf.state.lock, "nsd:state");
    nsconf.state.started = 1;
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
    int stacksize;
    Ns_DString ds;
    
    Ns_DStringInit(&ds);
    Ns_HomePath(&ds, "modules", "tcl", NULL);
    nsconf.tcl.sharedlibrary = Ns_DStringExport(&ds);

    nsconf.shutdowntimeout = NsParamInt("shutdowntimeout", SHUTDOWNTIMEOUT);
    nsconf.sched.maxelapsed = NsParamInt("schedmaxelapsed", SCHED_MAXELAPSED);
    nsconf.backlog = NsParamInt("listenbacklog", LISTEN_BACKLOG);
    nsconf.http.major = (unsigned) NsParamInt("httpmajor", HTTP_MAJOR);
    nsconf.http.minor = (unsigned) NsParamInt("httpmajor", HTTP_MINOR);
    nsconf.tcl.lockoninit = NsParamBool("tclinitlock", TCL_INITLCK);

    if (!Ns_ConfigGetInt(NS_CONFIG_THREADS, "stacksize", &stacksize)) {
    	stacksize = NsParamInt("stacksize", THREAD_STACKSIZE);
    }
    Ns_ThreadStackSize(stacksize);

    NsLogConf();
    NsEnableDNSCache();
    NsUpdateEncodings();
    NsUpdateMimeTypes();
}


/*
 *----------------------------------------------------------------------
 *
 * NsParamInt, NsParamBool, NsParamString --
 *
 *	Helper routines for getting int, bool, or string paramaters
 *	from the ns/parameters config section, returning defaults
 *	if no config is set.
 *
 * Results:
 *	Config value or default.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsParamInt(char *key, int def)
{
    int i;

    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, key, &i) || i < 0) {
	i = def;
    }
    return i;
}

bool
NsParamBool(char *key, int def)
{
    int i;

    if (!Ns_ConfigGetBool(NS_CONFIG_PARAMETERS, key, &i)) {
	i = def;
    }
    return i;
}

char *
NsParamString(char *key, char *def)
{
    char *val;

    val = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS, key);
    if (val == NULL) {
	val = def;
    }
    return val;
}

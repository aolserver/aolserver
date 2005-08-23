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
 * info.c --
 *
 *	Ns_Info* API and ns_info command support.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/info.c,v 1.19 2005/08/23 21:41:31 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

extern char *nsBuildDate;
static Ns_ThreadArgProc ThreadArgProc;


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoHomePath --
 *
 *	Return the home dir. 
 *
 * Results:
 *	Home dir. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoHomePath(void)
{
    return nsconf.home;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServerName --
 *
 *	Return the server name. 
 *
 * Results:
 *	Server name 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoServerName(void)
{
    return nsconf.name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServerVersion --
 *
 *	Returns the server version 
 *
 * Results:
 *	String server version. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoServerVersion(void)
{
    return nsconf.version;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoConfigFile --
 *
 *	Returns path to config file. 
 *
 * Results:
 *	Path to config file. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoConfigFile(void)
{
    return nsconf.config;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoPid --
 *
 *	Returns server's PID 
 *
 * Results:
 *	PID (tread like pid_t) 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoPid(void)
{
    return nsconf.pid;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoNameOfExecutable --
 *
 *	Returns the name of the nsd executable.  Quirky name is from Tcl.
 *
 * Results:
 *	Name of executable, string.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoNameOfExecutable(void)
{
    return nsconf.nsd;
}


/*
 *----------------------------------------------------------------------
 *
 *   --
 *
 *	Return platform name 
 *
 * Results:
 *	Platform name, string. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoPlatform(void)
{

#if defined(__linux)
    return "linux";
#elif defined(__FreeBSD__)
    return "freebsd";
#elif defined(__OpenBSD__)
    return "openbsd";
#elif defined(__sgi)
    return "irix";
#elif defined(__sun)

#if defined(__i386)
    return "solaris/intel";
#else
    return "solaris";
#endif

#elif defined(__alpha)
    return "OSF/1 - Alpha";
#elif defined(__hp10)
    return "hp10";
#elif defined(__hp11)
    return "hp11";
#elif defined(__unixware)
    return "UnixWare";
#elif defined(__APPLE__)
    return "osx";
#elif defined(_WIN32)
    return "win32";
#else
    return "?";
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoUptime --
 *
 *	Returns time server has been up. 
 *
 * Results:
 *	Seconds server has been running.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoUptime(void)
{
    return (int) difftime(time(NULL), nsconf.boot_t);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoBootTime --
 *
 *	Returns time server started. 
 *
 * Results:
 *	Treat as time_t. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoBootTime(void)
{
    return nsconf.boot_t;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoHostname --
 *
 *	Return server hostname 
 *
 * Results:
 *	Hostname 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoHostname(void)
{
    return nsconf.hostname;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoAddress --
 *
 *	Return server IP address
 *
 * Results:
 *	Primary (first) IP address of this machine.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoAddress(void)
{
    return nsconf.address;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoBuildDate --
 *
 *	Returns time server was compiled. 
 *
 * Results:
 *	String build date and time. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoBuildDate(void)
{
    return nsconf.build;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoShutdownPending --
 *
 *	Boolean: is a shutdown pending? 
 *
 * Results:
 *	NS_TRUE: yes, NS_FALSE: no 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoShutdownPending(void)
{
    int stopping;

    Ns_MutexLock(&nsconf.state.lock);
    stopping = nsconf.state.stopping;
    Ns_MutexUnlock(&nsconf.state.lock);
    return stopping;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoStarted --
 *
 *	Boolean: has the server started up all the way yet? 
 *
 * Results:
 *	NS_TRUE: yes, NS_FALSE: no 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoStarted(void)
{
    int             started;

    Ns_MutexLock(&nsconf.state.lock);
    started = nsconf.state.started;
    Ns_MutexUnlock(&nsconf.state.lock);
    return started;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoServersStarted --
 *
 *	Compatability function, same as Ns_InfoStarted 
 *
 * Results:
 *	See Ns_InfoStarted 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_InfoServersStarted(void)
{
    return Ns_InfoStarted();
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoLabel --
 *
 *	Returns version information about this build. 
 *
 * Results:
 *	A string version name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoLabel(void)
{
    return NSD_LABEL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoTag --
 *
 *	Returns CVS tag of this build (can be meaningless).
 *
 * Results:
 *	A string version name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoTag(void)
{
    return NSD_TAG;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInfoObjCmd --
 *
 *	Implements ns_info. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclInfoObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    char *server;
    char *elog;
    Tcl_DString ds;
    static CONST char *opts[] = {
	"address", "argv0", "boottime", "builddate", "callbacks",
	"config", "home", "hostname", "label", "locks", "log",
	"major", "minor", "name", "nsd", "pageroot", "patchlevel",
	"pid", "platform", "pools", "scheduled", "server", "servers",
	"sockcallbacks", "tag", "tcllib", "threads", "uptime",
	"version", "winnt", NULL
    };
    enum {
	IAddressIdx, IArgv0Idx, IBoottimeIdx, IBuilddateIdx, ICallbacksIdx,
	IConfigIdx, IHomeIdx, hostINameIdx, ILabelIdx, ILocksIdx, ILogIdx,
	IMajorIdx, IMinorIdx, INameIdx, INsdIdx, IPageRootIdx, IPatchLevelIdx,
	IPidIdx, IPlatformIdx, IPoolsIdx, IScheduledIdx, IServerIdx, IServersIdx,
	sockICallbacksIdx, ITagIdx, ITclLibIdx, IThreadsIdx, IUptimeIdx,
	IVersionIdx, IWinntIdx,
    } _nsmayalias opt;

    if (objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    switch (opt) {
    case IArgv0Idx:
	Tcl_SetResult(interp, nsconf.argv0, TCL_STATIC);
	break;

    case INsdIdx:
	Tcl_SetResult(interp, nsconf.nsd, TCL_STATIC);
	break;

    case INameIdx:
        Tcl_SetResult(interp, Ns_InfoServerName(), TCL_STATIC);
	break;

    case IConfigIdx:
	Tcl_SetResult(interp, Ns_InfoConfigFile(), TCL_STATIC);
	break;

    case ICallbacksIdx:
    	NsGetCallbacks(&ds);
	Tcl_DStringResult(interp, &ds);
	break;

    case sockICallbacksIdx:
    	NsGetSockCallbacks(&ds);
	Tcl_DStringResult(interp, &ds);
	break;

    case IScheduledIdx:
    	NsGetScheduled(&ds);
	Tcl_DStringResult(interp, &ds);
	break;

    case ILocksIdx:
	Ns_MutexList(&ds);
	Tcl_DStringResult(interp, &ds);
	break;

    case IThreadsIdx:
	Ns_ThreadList(&ds, ThreadArgProc);
	Tcl_DStringResult(interp, &ds);
	break;

    case IPoolsIdx:
#if !defined(_WIN32) && defined(USE_THREAD_ALLOC) && (STATIC_BUILD == 0)
	Tcl_GetMemoryInfo(&ds); /* As of Tcl8.4.1 this is not exported. */
	Tcl_DStringResult(interp, &ds);
#endif
	break;

    case ILogIdx:
    elog = Ns_InfoErrorLog();
	Tcl_SetResult(interp, elog == NULL ? "STDOUT" : elog, TCL_STATIC);
	break;

    case IPlatformIdx:
	Tcl_SetResult(interp, Ns_InfoPlatform(), TCL_STATIC);
	break;

    case hostINameIdx:
	Tcl_SetResult(interp, Ns_InfoHostname(), TCL_STATIC);
	break;

    case IAddressIdx:
	Tcl_SetResult(interp, Ns_InfoAddress(), TCL_STATIC);
	break;

    case IUptimeIdx:
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_InfoUptime()));
	break;

    case IBoottimeIdx:
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_InfoBootTime()));
	break;

    case IPidIdx:
	Tcl_SetObjResult(interp, Tcl_NewIntObj(Ns_InfoPid()));
	break;

    case IMajorIdx:
	Tcl_SetObjResult(interp, Tcl_NewIntObj(NS_MAJOR_VERSION));
	break;

    case IMinorIdx:
	Tcl_SetObjResult(interp, Tcl_NewIntObj(NS_MINOR_VERSION));
	break;

    case IVersionIdx:
	Tcl_SetResult(interp, NS_VERSION, TCL_STATIC);
	break;

    case IPatchLevelIdx:
	Tcl_SetResult(interp, NS_PATCH_LEVEL, TCL_STATIC);
	break;

    case IHomeIdx:
	Tcl_SetResult(interp, Ns_InfoHomePath(), TCL_STATIC);
	break;

    case IWinntIdx:
#ifdef _WIN32
    Tcl_SetResult(interp, "1", TCL_STATIC);
#else
	Tcl_SetResult(interp, "0", TCL_STATIC);
#endif
	break;

    case ILabelIdx:
	Tcl_SetResult(interp, Ns_InfoLabel(), TCL_STATIC);
	break;

    case IBuilddateIdx:
	Tcl_SetResult(interp, Ns_InfoBuildDate(), TCL_STATIC);
	break;

    case ITagIdx:
	Tcl_SetResult(interp, Ns_InfoTag(), TCL_STATIC);
	break;

    case IServersIdx:
	Tcl_SetResult(interp, NsGetServers(), TCL_STATIC);
	break;

    case IServerIdx:
	if (NsTclGetServer(itPtr, &server) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_SetResult(interp, itPtr->servPtr->server, TCL_STATIC);
        break;

    case ITclLibIdx:
    case IPageRootIdx:
	if (opt == ITclLibIdx) {
	    Tcl_SetResult(interp, itPtr->servPtr->tcl.library, TCL_STATIC);
	} else {
	    Tcl_SetResult(interp, itPtr->servPtr->fastpath.pageroot, TCL_STATIC);
	}
        break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLibraryCmd --
 *
 *	Implements ns_library. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclLibraryCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    char *lib;
    Ns_DString ds;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " library ?module?\"", NULL);
	return TCL_ERROR;
    }
    if (STREQ(argv[1], "private")) {
    	lib = itPtr->servPtr->tcl.library;
    } else if (STREQ(argv[1], "shared")) {
    	lib = nsconf.tcl.sharedlibrary;
    } else {
	Tcl_AppendResult(interp, "unknown library \"",
	   argv[1], "\": should be private or shared", NULL);
	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, lib, argv[2], NULL);
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


static void
ThreadArgProc(Tcl_DString *dsPtr, void *proc, void *arg)
{
    Ns_GetProcInfo(dsPtr, proc, arg);
}

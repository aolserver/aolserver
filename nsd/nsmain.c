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
 * nsmain.c --
 *
 *	AOLserver Ns_Main() startup routine.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsmain.c,v 1.41 2002/07/08 02:50:55 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file.
 */

static void UsageError(char *msg);
static void StatusMsg(int state);
static char *FindConfig(char *config);


/*
 *----------------------------------------------------------------------
 *
 * Ns_Main --
 *
 *	The AOLserver startup routine called from main().  The
 *	separation from main() is for the benefit of nsd.dll on NT
 *	and also to allow a custom, static linked server init.
 *
 *	Startup is somewhat complicated to ensure certain things happen
 *	in the correct order, e.g., setting the malloc/ns_malloc flag
 *	before the first ns_malloc and forking the binder before
 *	initializing the thread library (which is a problem for Linux
 * 	and perhaps other platforms).  This routine is also an unusual
 *	case of mixing much specific code for Unix and NT.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Many - read comments below.
 *
 *----------------------------------------------------------------------
 */

int
Ns_Main(int argc, char **argv)
{
    int            i, fd;
    char          *config;
    Ns_Time 	   timeout;
    char	   buf[PATH_MAX];
    int		   uid = 0;
    int		   gid = 0;
    int		   debug = 0;
    int	   	   mode = 0;
    char	  *root = NULL;
    char	  *garg = NULL;
    char	  *uarg = NULL;
    char	  *bindargs = NULL;
    char	  *bindfile = NULL;
    char	  *procname = NULL;
    char	  *server = NULL;
    Ns_Set	  *servers;
    struct rlimit  rl;

    /*
     * Set up configuration defaults and initial values.
     */

    nsconf.argv0         = argv[0];

    /*
     * AOLserver requires file descriptor 0 be open on /dev/null to
     * ensure the server never blocks reading stdin.
     */
     
    fd = open("/dev/null", O_RDONLY);
    if (fd > 0) {
    	dup2(fd, 0);
	close(fd);
    }

    /*     
     * Also, file descriptors 1 and 2 may not be open if the server
     * is starting from /etc/init.  If so, open them on /dev/null
     * as well because the server will assume they're open during
     * initialization.  In particular, the log file will be duped
     * to fd's 1 and 2 which would cause problems if 1 and 2
     * were open on something important, e.g., the underlying
     * SGI sproc-based threads library arena file.
     */

    fd = open("/dev/null", O_WRONLY);
    if (fd > 0 && fd != 1) {
	close(fd);
    }
    fd = open("/dev/null", O_WRONLY);
    if (fd > 0 && fd != 2) {
	close(fd);
    }

    /*
     * Parse the command line arguments.
     */

    opterr = 0;
    while ((i = getopt(argc, argv, "hpzifVl:s:t:kKdr:u:g:b:B:")) != -1) {
        switch (i) {
	case 'l':
	    sprintf(buf, "TCL_LIBRARY=%s", optarg);
	    putenv(buf);
	    break;
	case 'h':
	    UsageError(NULL);
	    break;
	case 'f':
	case 'i':
	case 'V':
	    if (mode != 0) {
		UsageError("only one of -i, -f, or -V may be specified");
	    }
	    mode = i;
	    break;
        case 's':
	    if (server != NULL) {
		UsageError("multiple -s <server> options");
	    }
	    server = optarg;
            break;
	case 't':
	    if (nsconf.config != NULL) {
		UsageError("multiple -t <file> options");
	    }
            nsconf.config = optarg;
            break;
        case 'p':
        case 'z':
	    /* NB: Ignored. */
            break;
	case 'b':
	    bindargs = optarg;
	    break;
	case 'B':
	    bindfile = optarg;
	    break;
	case 'r':
	    root = optarg;
	    break;
	    break;
        case 'd':
	    debug = 1;
            break;
	case 'g':
	    garg = optarg;
	    break;
	case 'u':
	    uarg = optarg;
	    break;
	case ':':
	    sprintf(buf, "option -%c requires a parameter", optopt);
            UsageError(buf);
	    break;
        default:
	    sprintf(buf, "invalid option: -%c", optopt);
            UsageError(buf);
            break;
        }
    }
    if (mode == 'V') {
        printf("AOLserver/%s (%s)\n", NSD_VERSION, Ns_InfoLabel()); 
	printf("   CVS Tag:         %s\n", Ns_InfoTag());
	printf("   Built:           %s\n", Ns_InfoBuildDate());
	printf("   Tcl version:     %s\n", nsconf.tcl.version);
	printf("   Platform:        %s\n", Ns_InfoPlatform());
        return 0;
    } else if (nsconf.config == NULL) {
        UsageError("required -t <config> option not specified");
    }

    /*
     * Find the absolute config pathname and read the config data
     * before a possible chroot().
     */

    nsconf.config = FindConfig(nsconf.config);
    config = NsConfigRead(nsconf.config);

    /*
     * Verify the uid/gid args.
     */

    if (uarg != NULL) {
	uid = Ns_GetUid(uarg);
	gid = Ns_GetUserGid(uarg);
	if (uid < 0) {
	    uid = atoi(uarg);
	}
	if (uid == 0) {
	    Ns_Fatal("nsmain: invalid user '%s'", uarg);
	}
    }
    if (garg != NULL) {
	gid = Ns_GetGid(garg);
	if (gid < 0) {
	    gid = atoi(garg);
	    if (gid == 0) {
		Ns_Fatal("nsmain: invalid group '%s'", garg);
	    }
	}
    }

    /*
     * AOLserver uses select() extensively so adjust the open file
     * limit to be no greater than FD_SETSIZE on Unix.  It's possible
     * you could define FD_SETSIZE to a larger value and recompile but
     * this has not been verified.  Note also that on some platforms
     * (e.g., Solaris, SGI o32) you're still left with the hardcoded
     * limit of 255  open streams due to the definition of the FILE
     * structure with an unsigned char member for the file descriptor.
     * Note this limit must be set now to ensure it's inherited by
     * all future threads on certain platforms such as SGI and Linux.
     */

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
	Ns_Log(Warning, "nsmain: getrlimit(RLIMIT_NOFILE) failed: '%s'",
	       strerror(errno));
    } else {
	if (rl.rlim_max > FD_SETSIZE) {
	    rl.rlim_max = FD_SETSIZE;
	}
	if (rl.rlim_cur != rl.rlim_max) {
    	    rl.rlim_cur = rl.rlim_max;
    	    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
	        Ns_Log(Warning, "nsmain: "
		       "setrlimit(RLIMIT_NOFILE, %d) failed: '%s'",
		       rl.rlim_max, strerror(errno));
	    } 
	}
    }

    /*
     * Initialize the binder now, before a possible setuid from root
     * or chroot which may hide /etc/resolv.conf required to
     * bind to one or more ports given on the command line and/or
     * pre-bind file.
     */

    NsPreBind(bindargs, bindfile);

    /*
     * Chroot() if requested before setuid from root.
     */

    if (root != NULL) {
	if (chroot(root) != 0) {
	    Ns_Fatal("nsmain: chroot(%s) failed: '%s'", root, strerror(errno));
	}
	if (chdir("/") != 0) {
	    Ns_Fatal("nsmain: chdir(/) failed: '%s'", strerror(errno));
	}
	nsconf.home = "/";
    }

    /*
     * If root, determine and change to the run time user and/or group.
     */

    if (getuid() == 0) {
	if (uid == 0) {
	    Ns_Fatal("nsmain: server will not run as root; "
		     "must specify '-u username' parameter");
	}
	if (gid != 0 && gid != getgid() && setgid(gid) != 0) {
	    Ns_Fatal("nsmain: setgid(%d) failed: '%s'", gid, strerror(errno));
	}
	if (setuid(uid) != 0) {
	    Ns_Fatal("nsmain: setuid(%d) failed: '%s'", uid, strerror(errno));
	}
    }

    /*
     * Fork into the background and create a new session if running 
     * in daemon mode.
     */

    if (mode == 0) {
	i = ns_fork();
	if (i < 0) {
	    Ns_Fatal("nsmain: fork() failed: '%s'", strerror(errno));
	}
	if (i > 0) {
	    return 0;
	}
	nsconf.pid = getpid();
	setsid();
    }

    /*
     * Finally, block all signals for the duration of startup to ensure any
     * new threads inherit the blocked state.
     */

    NsBlockSignals(debug);

    /*
     * Initialize Tcl and eval the config file.
     */

    Tcl_FindExecutable(argv[0]);
    NsTclInitObjs();
    nsconf.nsd = (char *) Tcl_GetNameOfExecutable();
    NsConfigEval(config, argc, argv, optind);
    ns_free(config);

    /*
     * Ensure servers where defined.
     */

    servers = Ns_ConfigGetSection("ns/servers");
    if (servers == NULL || Ns_SetSize(servers) == 0) {
	Ns_Fatal("nsmain: no servers defined");
    }

    /*
     * If a single server was specified, ensure it exists
     * and update the pointer to the config string (the
     * config server strings are considered the unique
     * server "handles").
     */

    if (server != NULL) {
	i = Ns_SetFind(servers, server);
	if (i < 0) {
	    Ns_Fatal("nsmain: no such server '%s'", server);
	}
	server = Ns_SetKey(servers, i);
    }

    /*
     * Set the procname used for the pid file and NT service name.
     */

    procname = (server ? server : Ns_SetKey(servers, 0));

    /*
     * Verify and change to the home directory.
     */
     
    nsconf.home = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS, "home");
    if (nsconf.home == NULL) {
	Ns_Fatal("nsmain: missing: [%s]home", NS_CONFIG_PARAMETERS);
    }
    if (chdir(nsconf.home) != 0) {
	Ns_Fatal("nsmain: chdir(%s) failed: '%s'", nsconf.home, strerror(errno));
    }

    /*
     * Update core config values.
     */

    NsConfUpdate();

    /*
     * Open the log file now that the home directory and runtime
     * user id have been set.
     */
     
    if (mode != 'f') {
    	NsLogOpen();
    }

    /*
     * Log the first startup message which should be the first
     * output to the open log file.
     */
     
    StatusMsg(0);

    /*
     * Log the current open file limit.
     */
     
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
	Ns_Log(Warning, "nsmain: "
	       "getrlimit(RLIMIT_NOFILE) failed: '%s'", strerror(errno));
    } else {
	Ns_Log(Notice, "nsmain: "
	       "max files: FD_SETSIZE = %d, rl_cur = %d, rl_max = %d",
	       FD_SETSIZE, rl.rlim_cur, rl.rlim_max);
    }

    /*
     * Initialize the core.
     */

    NsCreatePidFile(procname);

    /*
     * Initialize the virtual servers.
     */

    if (server != NULL) {
    	NsInitServer(server);
    } else {
	for (i = 0; i < Ns_SetSize(servers); ++i) {
	    server = Ns_SetKey(servers, i);
    	    NsInitServer(server);
	}
    }

    /*
     * Run pre-startups and start the servers.
     */

    NsRunPreStartupProcs();
    NsStartServers();

    /*
     * Signal startup is complete.
     */

    StatusMsg(1);
    Ns_MutexLock(&nsconf.state.lock);
    nsconf.state.started = 1;
    Ns_CondBroadcast(&nsconf.state.cond);
    Ns_MutexUnlock(&nsconf.state.lock);

    /*
     * Run any post-startup procs.
     */

    NsRunStartupProcs();

    /*
     * Start the drivers now that the server appears
     * ready.
     */

    NsStartDrivers();
    NsStopBinder();

    /*
     * Once the listening thread is started, this thread will just
     * endlessly wait for Unix signals, calling NsRunSignalProcs()
     * whenever SIGHUP arrives.
     */

    NsHandleSignals();

    /*
     * Print a "server shutting down" status message and
     * then set the nsconf.stopping flag for any threads calling
     * Ns_InfoShutdownPending() and then determine an absolute
     * timeout for all systems to complete shutown.
     */

    StatusMsg(2);
    Ns_MutexLock(&nsconf.state.lock);
    nsconf.state.stopping = 1;
    if (nsconf.shutdowntimeout < 0) {
	nsconf.shutdowntimeout = 0;
    }
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, nsconf.shutdowntimeout, 0);
    Ns_MutexUnlock(&nsconf.state.lock);

    /*
     * First, stop the drivers and keepalive threads.
     */

    NsStopDrivers();
    NsStopServers(&timeout);

    /*
     * Next, start and then wait for other systems to shutdown
     * simultaneously.  Note that if there was a timeout
     * waiting for the connection threads to exit, the following
     * waits will generally timeout as well.
     */

    NsStartSchedShutdown(); 
    NsStartSockShutdown();
    NsStartShutdownProcs();
    NsWaitSchedShutdown(&timeout);
    NsWaitSockShutdown(&timeout);
    NsWaitShutdownProcs(&timeout);

    /*
     * Finally, execute the exit procs directly and print a final
     * "server exiting" status message and exit.  Note that
     * there is not timeout check for the exit procs so they
     * should be well behaved.
     */

    NsRunAtExitProcs();
    NsRemovePidFile(procname);
    StatusMsg(3);
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_WaitForStartup --
 *
 *	Blocks until the server has completed loading modules, 
 *	sourcing Tcl, and is ready to begin normal operation. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_WaitForStartup(void)
{
    Ns_MutexLock(&nsconf.state.lock);
    while (!nsconf.state.started) {
        Ns_CondWait(&nsconf.state.cond, &nsconf.state.lock);
    }
    Ns_MutexUnlock(&nsconf.state.lock);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclShutdownCmd --
 *
 *	Implements ns_shutdown. 
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
NsTclShutdownCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int timeout;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?timeout?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
	timeout = nsconf.shutdowntimeout;
    } else  if (Tcl_GetInt(interp, argv[1], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(timeout));
    Ns_MutexLock(&nsconf.state.lock);
    nsconf.shutdowntimeout = timeout;
    Ns_MutexUnlock(&nsconf.state.lock);
    NsSendSignal(SIGTERM);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclShutdownObjCmd --
 *
 *	Implements ns_shutdown as obj command. 
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
NsTclShutdownObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    int timeout;

    if (objc != 1 && objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?timeout?");
	return TCL_ERROR;
    }
    if (objc == 1) {
	timeout = nsconf.shutdowntimeout;
    } else  if (Tcl_GetIntFromObj(interp, objv[1], &timeout) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetIntObj(Tcl_GetObjResult(interp), timeout);
    Ns_MutexLock(&nsconf.state.lock);
    nsconf.shutdowntimeout = timeout;
    Ns_MutexUnlock(&nsconf.state.lock);
    NsSendSignal(SIGTERM);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * StatusMsg --
 *
 *	Print a status message to the log file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
StatusMsg(int state)
{
    char *what;

    switch (state) {
    case 0:
	what = "starting";
	break;
    case 1:
	what = "running";
	break;
    case 2:
	what = "stopping";
	break;
    case 3:
	what = "exiting";
	break;
    }
    Ns_Log(Notice, "nsmain: %s/%s %s",
	   Ns_InfoServerName(), Ns_InfoServerVersion(), what);
    if (state < 2) {
        Ns_Log(Notice, "nsmain: security info: uid=%d, euid=%d, gid=%d, egid=%d",
	       getuid(), geteuid(), getgid(), getegid());
    }
}


/*
 *----------------------------------------------------------------------
 *
 * UsageError --
 *
 *	Print a command line usage error message and exit.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server exits.
 *
 *----------------------------------------------------------------------
 */

static void
UsageError(char *msg)
{
    if (msg != NULL) {
	fprintf(stderr, "\nError: %s\n", msg);
    }
    fprintf(stderr, "\n"
	    "Usage: %s [-h|V] [-i|f] [-b] [-u <user>] "
		"[-g <group>] [-r <path>] [-b <address:port>|-B <file>] "
		"[-s <server>] -t <file>\n"
	    "\n"
	    "  -h  help (this message)\n"
	    "  -V  version and release information\n"
	    "  -i  inittab mode\n"
	    "  -f  foreground mode\n"
	    "  -d  debugger-friendly mode (ignore SIGINT)\n"
	    "  -u  run as <user>\n"
	    "  -g  run as <group>\n"
	    "  -r  chroot to <path>\n"
	    "  -b  bind <address:port>\n"
	    "  -B  bind address:port list from <file>\n"
	    "  -s  use server named <server> in config file\n"
	    "  -t  read config from <file> (REQUIRED)\n"
	    "\n", nsconf.argv0);
    exit(msg ? 1 : 0);
}
    

/*
 *----------------------------------------------------------------------
 *
 * FindConfig --
 *
 *	Find the absolute pathname to the config and clean it up.
 *
 * Results:
 *	ns_malloc'ed string with clean path.
 *
 * Side effects:
 *	Config path is "normalized".
 *
 *----------------------------------------------------------------------
 */

char *
FindConfig(char *config)
{
    Ns_DString ds1, ds2;
    char cwd[PATH_MAX];

    Ns_DStringInit(&ds1);
    Ns_DStringInit(&ds2);
    if (!Ns_PathIsAbsolute(config) && getcwd(cwd, sizeof(cwd)) != NULL) {
	Ns_MakePath(&ds2, cwd, config, NULL);
        config = ds2.string;
    }
    Ns_NormalizePath(&ds1, config);
    config = Ns_DStringExport(&ds1);
    Ns_DStringFree(&ds2);
    return config;
}

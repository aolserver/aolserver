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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsmain.c,v 1.22.2.3 2001/04/04 00:13:15 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include "nsconf.h"

#ifdef WIN32
#define DEVNULL "nul:"
#else
#define DEVNULL "/dev/null"
#endif

/*
 * Local functions defined in this file.
 */

static void UsageError(char *msg);
static void StatusMsg(int state);
static char *FindConfig(char *config);

/*
 * The following global variable specifies the name
 * of the single running server.
 */
 
char *nsServer;

/*
 * The following strucuture is used to maintain and
 * signal the top level states of the server.
 */

struct {
    int started;
    int stopping;
    int shutdowntimeout;
    Ns_Mutex lock;
    Ns_Cond cond;
} status;


/*
 *----------------------------------------------------------------------
 *
 * Ns_Main --
 *
 *	The AOLserver startup routine called from main().  The
 *	separation from main() is for the benefit of nsd.dll on NT.
 *	It will also be used later for static linking of modules.
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
Ns_Main(int argc, char **argv, Ns_ServerInitProc *initProc)
{
    int            i, fd;
    char          *config;
    char	  *server = NULL;
    Ns_Time 	   timeout;
    char	   cwd[PATH_MAX];
    Ns_DString	   addr;
#ifndef WIN32
    int		   uid = 0;
    int		   gid = 0;
    int		   kill = 0;
    int		   debug = 0;
    char	  *root = NULL;
    char	  *garg = NULL;
    char	  *uarg = NULL;
    char	  *bindargs = NULL;
    char	  *bindfile = NULL;
    struct rlimit  rl;
#endif
    static int	   mode = 0;

    /*
     * When run as a Win32 service, Ns_Main will be re-entered
     * in the service main thread.  In this case, jump past
     * the point where the initial thread blocked when
     * connected to the service control manager.
     */
     
#ifdef WIN32
    if (mode == 'S') {
    	goto contservice;
    }
#endif

    /*
     * Set up configuration defaults and initial values.
     */
    nsconf.argv0         = argv[0];
    nsconf.log.debug     = LOG_DEBUG_BOOL;
    nsconf.log.dev       = LOG_DEV_BOOL;
    nsconf.log.expanded  = LOG_EXPANDED_BOOL;
    nsconf.log.maxback   = LOG_MAXBACK_INT;
    nsconf.name          = NSD_NAME;
    nsconf.version       = NSD_VERSION;
    nsconf.quiet         = SERV_QUIET_BOOL;
    nsconf.config        = NULL;

    /*
     * AOLserver requires file descriptor 0 be open on /dev/null to
     * ensure the server never blocks reading stdin.
     */
     
    fd = open(DEVNULL, O_RDONLY|O_TEXT);
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

    fd = open(DEVNULL, O_WRONLY|O_TEXT);
    if (fd > 0 && fd != 1) {
	close(fd);
    }
    fd = open(DEVNULL, O_WRONLY|O_TEXT);
    if (fd > 0 && fd != 2) {
	close(fd);
    }

    /*
     * Parse the command line arguments.
     */

#ifdef WIN32
#define POPTS	"IRS"
#else
#define POPTS	"kKdr:u:g:b:B:"
#endif

    opterr = 0;
    while ((i = getopt(argc, argv, "qhpzifVs:t:c:" POPTS)) != -1) {
        switch (i) {
	case 'h':
	    UsageError(NULL);
	    break;
	case 'q':
	    nsconf.quiet = NS_FALSE;
	    break;
	case 'f':
	case 'i':
	case 'V':
#ifdef WIN32
	case 'I':
	case 'R':
	case 'S':
#endif
	    if (mode != 0) {
		UsageError("only one of -i, -f, -V, -I, -R, or -S may be specified");
	    }
	    mode = i;
	    break;
        case 's':
	    if (server != NULL) {
		UsageError("multiple -s <server> options");
	    }
	    server = optarg;
            break;
        case 'c':
	    fprintf(stderr, "\nWARNING: -c option is deprecated.  Use translate-ini to convert to tcl (-t) format.\n\n");
	case 't':
	    if (nsconf.config != NULL) {
		UsageError("multiple -t/-c <file> options");
	    }
            nsconf.config = optarg;
	    nsconf.configfmt = i;
            break;
        case 'z':
	    nsMemPools = 1;
            break;
        case 'p':
	    nsMemPools = 0;
            break;
#ifndef WIN32
	case 'b':
	    bindargs = optarg;
	    break;
	case 'B':
	    bindfile = optarg;
	    break;
	case 'r':
	    root = optarg;
	    break;
	case 'K':
	case 'k':
	    if (kill != 0) {
		UsageError("only one of -k or -K may be specified");
	    }
	    kill = i;
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
#endif
	case ':':
	    sprintf(cwd, "option -%c requires a parameter", optopt);
            UsageError(cwd);
	    break;
        default:
	    sprintf(cwd, "invalid option: -%c", optopt);
            UsageError(cwd);
            break;
        }

    }
    if (mode == 'V') {
        printf("AOLserver/%s (%s)\n", NSD_VERSION, Ns_InfoLabel()); 
	printf("   CVS Tag:         %s\n", Ns_InfoTag());
	printf("   Built:           %s\n", Ns_InfoBuildDate());
	printf("   Tcl version:     %s\n", nsTclVersion);
	printf("   Thread library:  %s\n", NsThreadLibName());
	printf("   Platform:        %s\n", Ns_InfoPlatform());
        return 0;
    } else if (nsconf.config == NULL) {
        UsageError("required -c/-t <config> option not specified");
    }

    /*
     * Now that zippy malloc may have been set, it's safe to call
     * some API's to initialize various info useful during config eval.
     */

    Ns_ThreadSetName("-main-");

    time(&nsconf.boot_t);
    nsconf.pid = getpid();
    nsconf.home = getcwd(cwd, sizeof(cwd));
    if (gethostname(nsconf.hostname, sizeof(nsconf.hostname)) != 0) {
        strcpy(nsconf.hostname, "localhost");
    }
    Ns_DStringInit(&addr);
    if (Ns_GetAddrByHost(&addr, nsconf.hostname)) {
    	strcpy(nsconf.address, addr.string);
    }
    Ns_DStringFree(&addr);

    /*
     * Find the absolute config pathname and read the config data.
     */

    nsconf.config = FindConfig(nsconf.config);
    config = NsConfigRead(nsconf.config);

#ifndef WIN32

    /*
     * Verify the uid/gid args.
     */

    if (garg != NULL) {
	gid = Ns_GetGid(garg);
	if (gid < 0) {
	    gid = atoi(garg);
	    if (gid == 0) {
		Ns_Fatal("nsmain: invalid group '%s'", garg);
	    }
	}
    }
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
     * On Unix, parse non-Tcl style config files in case the
     * Unix uid/gid are specified.
     */

    if (nsconf.configfmt == 'c') {
    	NsConfigParse(nsconf.config, config);
    }

    /*
     * Initialize the binder now, before a possible setuid from root
     * or chroot which may hide /etc/resolv.conf required to
     * bind to one or more ports given on the command line and/or
     * pre-bind file.
     */

    NsInitBinder(bindargs, bindfile);

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
	char *param;

	if (uid == 0 && nsconf.configfmt == 'c') {
	    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, "uid", &uid)) {
	    	param = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "user");
		if (param != NULL && (uid = Ns_GetUid(param)) < 0) {
		    Ns_Fatal("nsmain: no such user '%s'", param);
		}
	    }
	}
	if (uid == 0) {
	    Ns_Fatal("nsmain: server will not run as root; "
		     "must specify '-u username' parameter");
	}
	if (gid == 0 && nsconf.configfmt == 'c') {
	    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, "gid", &gid)) {
	    	param = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "group");
	    	if (param != NULL && (gid = Ns_GetGid(param)) < 0) {
		    Ns_Fatal("nsmain: no such group '%s'", param);
		}
	    }
	}

	/*
	 * Before setuid, fork the background binder process to
	 * listen on ports which were not pre-bound above.
	 */

	NsForkBinder();

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

#endif

    /*
     * Initialize Tcl and eval the config file.
     */

    nsconf.nsd = NsTclFindExecutable(argv[0]);
    if (nsconf.configfmt == 't') {
    	NsConfigEval(config);
    }
    ns_free(config);

    /*
     * Determine the server to run.
     */

    if (server != NULL) {
	if (Ns_ConfigGet(NS_CONFIG_SERVERS, server) == NULL) {
	    Ns_Fatal("nsmain: no such server '%s'", server);
	}
    } else {
	Ns_Set *set;

	set = Ns_ConfigGetSection(NS_CONFIG_SERVERS);
	if (set == NULL || Ns_SetSize(set) != 1) {
	    Ns_Fatal("nsmain: no server specified: "
		     "specify '-s' parameter or specify "
		     NS_CONFIG_SERVERS " in config file");
	}
	server = Ns_SetKey(set, 0);
    }
    nsconf.server = nsServer = server;

    /*
     * Verify and change to the home directory.
     */
     
    nsconf.home = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "home");
    if (nsconf.home == NULL) {
	Ns_Fatal("nsmain: missing: [%s]home", NS_CONFIG_PARAMETERS);
    }
    if (chdir(nsconf.home) != 0) {
	Ns_Fatal("nsmain: chdir(%s) failed: '%s'", nsconf.home, strerror(errno));
    }

#ifdef WIN32

    /*
     * On Win32, first perform some additional cleanup of
     * home, ensuring forward slashes and lowercase.
     */

    nsconf.home = getcwd(cwd, sizeof(cwd));
    if (nsconf.home == NULL) {
	Ns_Fatal("nsmain: getcwd failed: '%s'", strerror(errno));
    }
    while (*nsconf.home != '\0') {
	if (*nsconf.home == '\\') {
	    *nsconf.home = '/';
	} else if (isupper(*nsconf.home)) {
	    *nsconf.home = tolower(*nsconf.home);
	}
	++nsconf.home;
    }
    nsconf.home = cwd;

    /*
     * Then, connect to the service control manager if running
     * as a service (see service comment above).
     */

    if (mode == 'I' || mode == 'R' || mode == 'S') {
	int status;

	Ns_ThreadSetName("-service-");
	switch (mode) {
	case 'I':
	    status = NsInstallService(server);
	    break;
	case 'R':
	    status = NsRemoveService(server);
	    break;
	case 'S':
    	    status = NsConnectService(initProc);
	    break;
	}
	return (status == NS_OK ? 0 : 1);
    }

    contservice:

#endif

    Ns_MutexSetName2(&status.lock, "ns", "status");

    /*
     * Open the log file now that the home directory and runtime
     * user id have been set.
     */
     
    if (mode != 'f') {
    	NsLogOpen();
    }

    /*
     * On Unix, kill the currently running server if requested.
     */
     
#ifndef WIN32
    if (kill != 0) {
    	i = NsGetLastPid(server);
	if (i > 0) {
    	    NsKillPid(i);
	}
	if (kill == 'K') {
	    return 0;
	}
    }
#endif

    /*
     * Log the first startup message which should be the first
     * output to the open log file.
     */
     
    StatusMsg(0);

#ifndef WIN32

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

#endif

    /*
     * Now, call several server initialization routines.  Most of
     * these routines simply initialize various data structures but
     * some are quite interesting, e.g., NsTclInit() and NsDbInit().
     * Also, what happens in NsLoadModules() depends of course on
     * the modules loaded, e.g., detached threads, scheduled procs,
     * registered URL service procs, etc.
     *
     * Tcl-wise, this is how server startup goes:
     *
     * foreach module
     *    load .so
     * eval private top level files
     * eval top level shared where not in private
     * foreach module
     *    eval private module files
     *    eval shared module files where not in private
     */

    NsConfInit(server);
    NsCreatePidFile(server);
    NsTclInit();
    NsInitMimeTypes();
    NsInitEncodings();
    NsInitReturn(server);
    NsInitProxyRequests();
    NsInitFastpath(server);
    NsDbInit(server);
    NsAdpInit(server);
    if (initProc != NULL && (*initProc)(server) != NS_OK) {
	Ns_Fatal("nsmain: Ns_ServerInitProc failed");
    }
    NsLoadModules(server);
    NsAdpParsers(server);    

    /*
     * Eval top level shared/private files, then
     * modules' shared/private files.
     */
    
    NsTclInitScripts();
    
    /*
     * Now that the core server is initialized, stop the child binder
     * process (privileged listening ports should not be created at
     * run time), start the server listening thread, print the
     * "server started" status message, signal the server is
     * started for any threads waiting in Ns_WaitForStartup() (e.g.,
     * threads blocked in Ns_TclAllocateInterp()), and then run
     * any procedures scheduled to run after startup.
     */

    NsRunPreStartupProcs();
    NsTclRunInits();
    NsStartServer(server);
    NsStartKeepAlive();

    /*
     * Signal startup is complete.
     */

    StatusMsg(1);
    Ns_MutexLock(&status.lock);
    status.shutdowntimeout = nsconf.shutdowntimeout;
    status.started = 1;
    Ns_CondBroadcast(&status.cond);
    Ns_MutexUnlock(&status.lock);

    /*
     * Run any post-startup procs.
     */

    NsRunStartupProcs();

    /*
     * Wait for the conn threads to warmup and
     * the sock and sched callbacks to appear idle.
     */

    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, nsconf.startuptimeout, 0);
    NsWaitServerWarmup(&timeout);
    NsWaitSockIdle(&timeout);
    NsWaitSchedIdle(&timeout);

    /*
     * Start the drivers now that the server appears
     * ready.
     */

    NsStartDrivers(server);
#ifndef WIN32
    NsStopBinder();
#endif

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
    Ns_MutexLock(&status.lock);
    status.stopping = 1;
    if (status.shutdowntimeout < 0) {
	status.shutdowntimeout = 0;
    }
    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, status.shutdowntimeout, 0);
    Ns_MutexUnlock(&status.lock);

    /*
     * First, stop the drivers and keepalive threads.
     */

    NsStopDrivers();
    NsStopKeepAlive();
    NsStopServer(&timeout);

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
    NsRemovePidFile(server);
    StatusMsg(3);
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StopServer --
 *
 *	Signal the server to shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will begin shutdown.
 *
 *----------------------------------------------------------------------
 */

void
Ns_StopServer(char *server)
{
    Ns_Log(Warning, "nsmain: immediate server shutdown requested");
    NsSendSignal(NS_SIGTERM);
}


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
#elif defined(MACOSX)
    return "osx";
#elif defined(WIN32)
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
    return nsBuildDate;
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
    Ns_MutexLock(&status.lock);
    while (!status.started) {
        Ns_CondWait(&status.cond, &status.lock);
    }
    Ns_MutexUnlock(&status.lock);
    return NS_OK;
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

    Ns_MutexLock(&status.lock);
    stopping = status.stopping;
    Ns_MutexUnlock(&status.lock);
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

    Ns_MutexLock(&status.lock);
    started = status.started;
    Ns_MutexUnlock(&status.lock);
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
    sprintf(interp->result, "%d", timeout);
    Ns_MutexLock(&status.lock);
    status.shutdowntimeout = timeout;
    Ns_MutexUnlock(&status.lock);
    NsSendSignal(NS_SIGTERM);
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
	   Ns_InfoServerName(), Ns_InfoVersion(), what);
#ifndef WIN32
    if (state < 2) {
        Ns_Log(Notice, "nsmain: security info: uid=%d, euid=%d, gid=%d, egid=%d",
	       getuid(), geteuid(), getgid(), getegid());
    }
#endif
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

#ifdef WIN32
#define OPTS      "[-I|R]"
#define OPTS_ARGS ""
#else
#define OPTS      "[-d]"
#define OPTS_ARGS "[-u <user>] [-g <group>] [-r <path>]"
#endif

static void
UsageError(char *msg)
{
    if (msg != NULL) {
	fprintf(stderr, "\nError: %s\n", msg);
    }
    fprintf(stderr, "\n"
	    "Usage: %s [-h|V] [-i|f] [-z] [-q] " OPTS " " OPTS_ARGS " [-b <address:port>|-B <file>] [-s <server>] -t <file>\n"
	    "\n"
	    "  -h  help (this message)\n"
	    "  -V  version and release information\n"
	    "  -i  inittab mode\n"
	    "  -f  foreground mode\n"
	    "  -z  zippy memory allocator\n"
	    "  -q  non-quiet startup\n"
#ifdef WIN32
	    "  -I  Install win32 service\n"
	    "  -R  Remove win32 service\n"
#else
	    "  -d  debugger-friendly mode (ignore SIGINT)\n"
	    "  -u  run as <user>\n"
	    "  -g  run as <group>\n"
	    "  -r  chroot to <path>\n"
#endif
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

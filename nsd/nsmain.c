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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nsmain.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

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
 * The follwing global variable specifies the name
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
    Ns_Time 	   timeout;
    char	   cwd[PATH_MAX], *addr;
    struct hostent *he;
    struct in_addr ia;
#ifndef WIN32
    int		   uid = 0;
    int		   gid = 0;
    int		   kill = 0;
    int		   debug = 0;
    char	  *root = NULL;
    struct rlimit  rl;
#endif
    static int	   mode = 0;


    /*
     * Set up logging defaults (for Ns_Log at startup time).
     */
    nsconf.log.dev       = NS_FALSE;
    nsconf.log.debug     = NS_FALSE;
    nsconf.log.expanded  = NS_FALSE;
    nsconf.log.maxback   = 10;

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

#ifndef WIN32
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
	Ns_Log(Warning, "getrlimit(RLIMIT_NOFILE) failed: %s", strerror(errno));
    } else {
	if (rl.rlim_max > FD_SETSIZE) {
	    rl.rlim_max = FD_SETSIZE;
	}
	if (rl.rlim_cur != rl.rlim_max) {
    	    rl.rlim_cur = rl.rlim_max;
    	    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
	        Ns_Log(Warning, "setrlimit(RLIMIT_NOFILE, %d) failed: %s",
	              rl.rlim_max, strerror(errno));
	    } 
	}
    }
#endif

    /*
     * Set defaults and then parse the command line arguments.
     */

    nsconf.argv0 = argv[0];
    nsconf.name = NSD_NAME;
    nsconf.version = NSD_VERSION;
    time(&nsconf.boot_t);
    nsconf.pid = getpid();
    nsconf.home = getcwd(cwd, sizeof(cwd));
    if (gethostname(nsconf.hostname, sizeof(nsconf.hostname)) != 0) {
        strcpy(nsconf.hostname, "localhost");
    }
    addr = "";
    he = gethostbyname(nsconf.hostname);
    if (he != NULL && he->h_addr_list[0] != NULL) {
	memcpy(&ia.s_addr, he->h_addr_list[0], sizeof(ia.s_addr));
	addr = inet_ntoa(ia);
    }
    strcpy(nsconf.address, addr ? addr : "");
    
#ifdef WIN32
#define POPTS	"IRS"
#else
#define POPTS	"kKdr:u:g:"
#endif

    opterr = 0;
    while ((i = getopt(argc, argv, "hpzifVs:t:c:" POPTS)) != -1) {
        switch (i) {
	case 'h':
	    UsageError(NULL);
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
	    if (nsServer != NULL) {
		UsageError("multple -s <server> options");
	    }
	    nsServer = optarg;
            break;
	case 't':
        case 'c':
	    if (nsconf.config != NULL) {
		UsageError("multiple -t/-c <config> options");
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
	    gid = Ns_GetGid(optarg);
	    if (gid < 0) {
		gid = atoi(optarg);
		if (gid == 0) {
		    Ns_Fatal("invalid group: %s", optarg);
	    	}
	    }
	    break;
	case 'u':
	    uid = Ns_GetUid(optarg);
	    if (uid < 0) {
		uid = atoi(optarg);
	    }
	    if (uid == 0) {
		Ns_Fatal("invalid user: %s", optarg);
	    }
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
        printf("AOLserver version %s\n", NSD_VERSION); 
	printf("   Platform:        %s\n", Ns_InfoPlatform());
	printf("   Built:           %s\n", Ns_InfoBuildDate());
	printf("   Tcl version      %s\n", nsTclVersion);
	printf("   Thread library:  %s\n", NsThreadLibName());
        return 0;
    } else if (nsconf.config == NULL) {
        UsageError("required -c/-t <config> option not specified");
    }

    /*
     * Find the absolute config pathname and read the config data.
     */

    nsconf.config = FindConfig(nsconf.config);
    config = NsConfigRead(nsconf.config);

#ifndef WIN32    

    /*
     * On Unix, parse non-Tcl style config files in case the the
     * Unix uid/gid are specified.  Then, chroot() if requested,
     * fork the binder process and setuid() if starting as root,
     * and then fork into the background and create a new session
     * if running in daemon mode.
     */

    if (nsconf.configfmt == 'c') {
    	NsConfigParse(nsconf.config, config);
    }
    if (root != NULL) {
	if (chroot(root) != 0) {
	    Ns_Fatal("chroot(%s) failed: %s", root, strerror(errno));
	}
	if (chdir("/") != 0) {
	    Ns_Fatal("chdir(/) failed: %s", strerror(errno));
	}
	nsconf.home = "/";
    }
    if (getuid() == 0) {
	char *param;

	if (uid == 0 && nsconf.configfmt == 'c') {
	    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, "uid", &uid)) {
	    	param = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "user");
		if (param != NULL && (uid = Ns_GetUid(param)) < 0) {
		    Ns_Fatal("no such user: %s", param);
		}
	    }
	}
	if (uid == 0) {
	    Ns_Fatal("server will not run as root");
	}
	if (gid == 0 && nsconf.configfmt == 'c') {
	    if (!Ns_ConfigGetInt(NS_CONFIG_PARAMETERS, "gid", &gid)) {
	    	param = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "group");
	    	if (param != NULL && (gid = Ns_GetGid(param)) < 0) {
		    Ns_Fatal("no such group: %s", param);
		}
	    }
	}
	NsForkBinder();
	if (gid != 0 && gid != getgid() && setgid(gid) != 0) {
	    Ns_Fatal("setgid(%d) failed: %s", gid, strerror(errno));
	}
	if (setuid(uid) != 0) {
	    Ns_Fatal("setuid(%d) failed: %s", uid, strerror(errno));
	}
    }
    if (mode == 0) {
	i = fork();
	if (i < 0) {
	    Ns_Fatal("fork() failed: %s", strerror(errno));
	}
	if (i > 0) {
	    return 0;
	}
	nsconf.pid = getpid();
	setsid();
    }
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

    if (nsServer != NULL) {
	if (Ns_ConfigGet(NS_CONFIG_SERVERS, nsServer) == NULL) {
	    Ns_Fatal("no such server: %s", nsServer);
	}
    } else {
	Ns_Set *set;

	set = Ns_ConfigGetSection(NS_CONFIG_SERVERS);
	if (set == NULL || Ns_SetSize(set) != 1) {
	    Ns_Fatal("no server specified: give -s switch or have "
		     NS_CONFIG_SERVERS
		     " section in config file.");
	}
	nsServer = Ns_SetKey(set, 0);
    }
    nsconf.server = nsServer;

    /*
     * Verify and change to the home directory.
     */
     
    nsconf.home = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "home");
    if (nsconf.home == NULL) {
	Ns_Fatal("config: missing: [%s]home", NS_CONFIG_PARAMETERS);
    }
    if (chdir(nsconf.home) != 0) {
	Ns_Fatal("chdir(%s) failed: %s", nsconf.home, strerror(errno));
    }

#ifdef WIN32

    /*
     * On Win32, perform some additional cleanup of
     * home, ensuring forward slashes and lowercase.
     */

    nsconf.home = getcwd(cwd, sizeof(cwd));
    if (nsconf.home == NULL) {
	Ns_Fatal("getcwd failed: %s", strerror(errno));
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
     * On NT, connect to the service control manager if running
     * as a service (see service comment above).
     */

    if (mode == 'I' || mode == 'R' || mode == 'S') {
	int status;

	Ns_ThreadSetName("-service-");
	switch (mode) {
	case 'I':
	    status = NsInstallService();
	    break;
	case 'R':
	    status = NsRemoveService();
	    break;
	case 'S':
    	    status = NsConnectService(initProc);
	    break;
	}
	return (status == NS_OK ? 0 : 1);
    }

    contservice:

#endif

    Ns_ThreadSetName("-main-");
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
    	i = NsGetLastPid();
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
	Ns_Log(Warning, "getrlimit(RLIMIT_NOFILE) failed: %s", strerror(errno));
    } else {
	Ns_Log(Notice, "max files: FD_SETSIZE = %d, rl_cur = %d, rl_max = %d",
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

    NsProcInit();
    NsConfInit();
    NsCreatePidFile();
    NsGetURLInit();
    NsTclInit();
    NsCacheInit();
    NsDNSInit();
    NsInitMimeTypes();
    NsInitReturn();
    NsInitProxyRequests();
    NsInitFastpath();
    NsDbInit();
    NsAdpInit();
    if (initProc != NULL && (*initProc)(nsServer) != NS_OK) {
	Ns_Fatal("Ns_ServerInitProc failed");
    }
    NsLoadModules();
    NsAdpParsers();    

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
    NsStartServer();
    NsStartKeepAlive();
    NsStartDrivers();
#ifndef WIN32
    NsStopBinder();
#endif
    NsTclRunInits();
    StatusMsg(1);
    Ns_MutexLock(&status.lock);
    status.shutdowntimeout = nsconf.shutdowntimeout;
    status.started = 1;
    Ns_CondBroadcast(&status.cond);
    Ns_MutexUnlock(&status.lock);
    NsRunStartupProcs();

    /*
     * Once the listening thread is started, this thread will just
     * endlessly wait for Unix signals, calling NsRunSignalProcs()
     * whenever SIGHUP arrives.
     */

    NsHandleSignals();

    /*
     * Print a "server shutting down" status message and
     * set the nsconf.stopping flag for any threads calling
     * Ns_InfoShutdownPending().
     */

    StatusMsg(2);
    Ns_MutexLock(&status.lock);
    status.stopping = 1;
    Ns_MutexUnlock(&status.lock);

    /*
     * Signal various subsystems to begin shutdown.
     */

    NsStopDrivers();
    NsStartServerShutdown();
    NsStartKeepAliveShutdown(); 
    NsStartSchedShutdown(); 
    NsStartSockShutdown();
    NsStartShutdownProcs();

    /*
     * Wait for all subsystems to complete shutdown.
     */

    Ns_MutexLock(&status.lock);
    if (status.shutdowntimeout < 0) {
	status.shutdowntimeout = 0;
    }
    i = status.shutdowntimeout;
    Ns_MutexUnlock(&status.lock);

    Ns_GetTime(&timeout);
    Ns_IncrTime(&timeout, i, 0);
    NsWaitServerShutdown(&timeout);
    NsWaitKeepAliveShutdown(&timeout);
    NsWaitSchedShutdown(&timeout);
    NsWaitSockShutdown(&timeout);
    NsWaitShutdownProcs(&timeout);

    /*
     * Finally, execute the exit procs directly and print a final
     * "server exiting" status message and exit.
     */

    NsRunAtExitProcs();
    NsRemovePidFile();
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
    Ns_Log(Warning, "immediate server shutdown requested");
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
    static char buildDate[] = __DATE__ " at " __TIME__;

    return (buildDate);
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
    Ns_Log(Notice, "%s/%s %s.", Ns_InfoServerName(), Ns_InfoVersion(), what);
#ifndef WIN32
    if (state < 2) {
        Ns_Log(Notice, "security info: uid=%d, euid=%d, gid=%d, egid=%d",
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
 *	Server exits.  This doesn't use Ns_Fatal because that
 *	relies on ModLog.
 *
 *----------------------------------------------------------------------
 */

#ifdef WIN32
#define SOPTS	"|-I|-R"
#define UOPTS   ""
#else
#define SOPTS	""
#define UOPTS	"[-d] [-k|-K] [-r root] [-u user] [-g group] "
#endif

static void
UsageError(char *msg)
{
    if (msg != NULL) {
	fprintf(stderr, "\nError: %s\n", msg);
    }
    fprintf(stderr,
	"\nUsage:\n\n"
	""
	"%s [-h] [-s server] [-z] " UOPTS "{-i|-f|-V" SOPTS "} {-c|-t} <config>\n\n"
	""
	"Where:\n\n"
	""
	"    -h           Print this usage message and exit.\n"
	"    -i           Run in installed mode, log output to file.\n"
	"    -f           Run in foreground mode, log output to screen.\n"
	"    -V           Print identification of server and exit.\n"
	"    -s <server>  Server to run when > 1 server in config file.\n"
	"    -z           Enable fast memory allocator (see documentation).\n"
#ifdef WIN32
    	"    -I           Install as an NT service."
    	"    -R           Remove installed NT service."
#else
	"    -k           Kill running server (based on pidfile) and continue.\n"
	"    -K           Kill running server (based on pidfile) and terminate.\n"
	"    -d           Ignore SIGINT for debugging on some platforms.\n"
	"    -r <root>    chroot(root) after reading config file (recommended)\n"
	"    -u <user>    setuid() to get given uid/user (normally required)\n"
	"    -g <group>   setgid() to get given gid/group (perhaps useful)\n"
#endif
	"    -c <config>  Path to the config file (.ini format)\n"
	"    -t <config>  Path to the config file (.tcl format)\n\n"
	""
	"Note:  One of {-c|-t} and <config> is required.\n\n", nsconf.argv0);
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

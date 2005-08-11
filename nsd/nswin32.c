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
 * win32.c --
 *
 *	Win32 specific routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/nswin32.c,v 1.14 2005/08/11 23:00:44 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static Ns_Mutex lock;
static Ns_Cond cond;
static Ns_Thread tickThread;
static Ns_ThreadProc ServiceTicker;
static void StopTicker(void);
static void StartTicker(DWORD pending);
static VOID WINAPI ServiceMain(DWORD argc, LPTSTR *argv);
static VOID WINAPI ServiceHandler(DWORD code);
static BOOL WINAPI ConsoleHandler(DWORD code);
static void ReportStatus(DWORD state, DWORD code, DWORD hint);
static void ExitService(void);
static char *GetServiceName(Ns_DString *dsPtr, char *server);
static SERVICE_STATUS_HANDLE hStatus = 0;
static SERVICE_STATUS curStatus;
static Ns_Tls   tls;
static int service;
static int tick;
static int sigpending;

#define SysErrMsg()    (NsWin32ErrMsg(GetLastError()))


/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *	Init routine for the nsd.dll which setups TLS for Win32 errors
 *  	disables thread attach/detach calls.
 *
 * Results:
 *  	TRUE or FALSE.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD why, LPVOID lpReserved)
{
    WSADATA         wsd;

    if (why == DLL_PROCESS_ATTACH) {
	NsThreads_LibInit();
	Ns_TlsAlloc(&tls, ns_free);
	if (WSAStartup(MAKEWORD(1, 1), &wsd) != 0) {
            return FALSE;
        }
        DisableThreadLibraryCalls(hModule);
	Ns_LibInit();
    } else if (why == DLL_PROCESS_DETACH) {
        WSACleanup();
    }
    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * NsWin32ErrMsg --
 *
 *	Get a string message for an error code in either the kernel or
 *  	wsock dll's.
 *
 * Results:
 *  	Pointer to per-thread LocalAlloc'ed memory.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

char *
NsWin32ErrMsg(int err)
{
    char           *msg;

    msg = Ns_TlsGet(&tls);
    if (msg == NULL) {
	msg = ns_malloc(100);
	Ns_TlsSet(&tls, msg);
    }
    sprintf(msg, "win32 error code: %d", err);
    return msg;
}


/*
 *----------------------------------------------------------------------
 *
 * NsConnectService --
 *
 *	Attach to the service control manager at startup.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Service control manager will create a new thread running
 *  	ServiceMain().
 *
 *----------------------------------------------------------------------
 */

int
NsConnectService(void)
{
    SERVICE_TABLE_ENTRY table[2];
    char buf[PATH_MAX];
    char *log, *null;
    int fd;
    BOOL ok;

    /*
     * Open a temporary log, ensuring it's opened
     * on fd 2 for later dup2 in NsLogOpen().
     */

    log = null = "nul:";
    if (GetTempPath(sizeof(buf) - sizeof(NSD_NAME) - 7, buf) > 0) {
	strcat(buf, NSD_NAME ".XXXXXX");
	log = _mktemp(buf);
    }
    if (log == NULL) {
	log = null;
    }
    _fcloseall();
    for (fd = 0; fd < 3; ++fd) {
	close(fd);
    }
    freopen(null, "rt", stdin);
    freopen(log, "wt", stdout);
    freopen(log, "wt", stderr);
    Ns_Log(Notice, "nswin32: connecting to service control manager");
    service = 1;
    table[0].lpServiceName = NSD_NAME;
    table[0].lpServiceProc = ServiceMain;
    table[1].lpServiceName = NULL;
    table[1].lpServiceProc = NULL;
    ok = StartServiceCtrlDispatcher(table);
    if (!ok) {
        Ns_Log(Error, "nswin32: "
	       "failed to contact service control dispatcher: '%s'", SysErrMsg());
    } else if (log != null) {
	unlink(log);
    }
    return (ok ? NS_OK : NS_ERROR);
}


/*
 *----------------------------------------------------------------------
 *
 * NsRemoveService --
 *
 *	Remove a previously installed service.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Service should stop and then disappear from the list in the
 *  	services control panel.
 *
 *----------------------------------------------------------------------
 */

int
NsRemoveService(char *server)
{
    SC_HANDLE       hmgr, hsrv;
    SERVICE_STATUS  status;
    Ns_DString	    name;
    BOOL            ok;

    Ns_DStringInit(&name);
    GetServiceName(&name, server);
    ok = FALSE;
    hmgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hmgr != NULL) {
        hsrv = OpenService(hmgr, name.string, SERVICE_ALL_ACCESS);
        if (hsrv != NULL) {
            ControlService(hsrv, SERVICE_CONTROL_STOP, &status);
            ok = DeleteService(hsrv);
            CloseServiceHandle(hsrv);
        }
        CloseServiceHandle(hmgr);
    }
    if (ok) {
	Ns_Log(Notice, "nswin32: removed service: %s", name.string);
    } else {
	Ns_Log(Error, "nswin32: failed to remove %s service: %s",
	       name.string, SysErrMsg());
    }
    Ns_DStringFree(&name);
    return (ok ? NS_OK : NS_ERROR);
}


/*
 *----------------------------------------------------------------------
 *
 * NsInstallService --
 *
 *	Install as an NT service.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Service should appear in the list in the services control panel.
 *
 *----------------------------------------------------------------------
 */

int
NsInstallService(char *server)
{
    SC_HANDLE       hmgr, hsrv;
    BOOL	    ok;
    char	    nsd[PATH_MAX], config[PATH_MAX];
    Ns_DString	    name, cmd;

    ok = FALSE;
    if (_fullpath(config, nsconf.config, sizeof(config)) == NULL) {
	Ns_Log(Error, "nswin32: invalid config path '%s'", nsconf.config);
    } else if (!GetModuleFileName(NULL, nsd, sizeof(nsd))) {
	Ns_Log(Error, "nswin32: failed to find nsd.exe: '%s'", SysErrMsg());
    } else {
	Ns_DStringInit(&name);
	Ns_DStringInit(&cmd);
	Ns_DStringVarAppend(&cmd, "\"", nsd, "\"",
	    " -S -s ", server, " -t \"", config, "\"", NULL);
	GetServiceName(&name, server);
	Ns_Log(Notice, "nswin32: installing %s service: %s",
	    name.string, cmd.string);
	hmgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (hmgr != NULL) {
	    hsrv = CreateService(hmgr, name.string, name.string,
		SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
		SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
		cmd.string, NULL, NULL, "TcpIp\0", NULL, NULL);
	    if (hsrv != NULL) {
		CloseServiceHandle(hsrv);
		ok = TRUE;
	    }
	    CloseServiceHandle(hmgr);
	}
	if (!ok) {
	    Ns_Log(Error, "nswin32: failed to install service '%s': '%s'",
		   name.string, SysErrMsg());
	}
	Ns_DStringFree(&name);
	Ns_DStringFree(&cmd);
    }
    return (ok ? NS_OK : NS_ERROR);
}


/*
 *----------------------------------------------------------------------
 *
 * NsHandleSignals --
 *
 *	Loop endlessly, processing HUP signals until a TERM
 *  	signal arrives.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
NsHandleSignals(void)
{
    int pending;

    /*
     * If running as a service, stop the ticker thread and report
     * startup complete. Otherwise, register a handler which will
     * initiate an orderly shutdown on Ctrl-C.
     */
     
    if (!service) {
    	SetConsoleCtrlHandler(ConsoleHandler, TRUE);
    } else {
	StopTicker();
	ReportStatus(SERVICE_RUNNING, NO_ERROR, 0);
    }

    Ns_MutexSetName2(&lock, "ns", "signal");
    do {
    	Ns_MutexLock(&lock);
	while (sigpending == 0) {
    	    Ns_CondWait(&cond, &lock);
	}
	pending = sigpending;
	sigpending = 0;
	Ns_MutexUnlock(&lock);
	if (pending & NS_SIGHUP) {
	    NsRunSignalProcs();
	}
    } while (!(pending & NS_SIGTERM));

    /*
     * If running as a service, startup the ticker thread again
     * to keep updating status until shutdown is complete.
     */
     
    if (service) {
	StartTicker(SERVICE_STOP_PENDING);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsSendSignal --
 *
 *	Send a signal to wakeup NsHandleSignals. As on Unix, a signal
 *  	sent multiple times is only received once.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Main thread will wakeup.
 *
 *----------------------------------------------------------------------
 */

void
NsSendSignal(int sig)
{
    switch (sig) {
    case NS_SIGTERM:
    case NS_SIGHUP:
        Ns_MutexLock(&lock);
        sigpending |= sig;
        Ns_CondSignal(&cond);
        Ns_MutexUnlock(&lock);
        break;
    default:
        Ns_Fatal("nswin32: invalid signal: %d", sig);
        break;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ns_socknbclose --
 *
 *	Perform a non-blocking socket close via the socket callback
 *  	thread.  This is only called by a timeout in Ns_SockTimedConnect.
 *
 * Results:
 *  	0 or SOCKET_ERROR.
 *
 * Side effects:
 *  	Socket will be closed when writable.
 *
 *----------------------------------------------------------------------
 */

int
ns_socknbclose(SOCKET sock)
{
    if (Ns_SockCloseLater(sock) != NS_OK) {
	return SOCKET_ERROR;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * ns_sockdup --
 *
 *	Duplicate a socket.  This is used in the old ns_sock Tcl cmds.
 *
 * Results:
 *  	New handle to underlying socket.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

SOCKET
ns_sockdup(SOCKET sock)
{
    HANDLE hp, src, dup;
    
    src = (HANDLE) sock;
    hp = GetCurrentProcess();    
    if (!DuplicateHandle(hp, src, hp, &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
	return INVALID_SOCKET;
    }
    return (SOCKET) dup;
}   


/*
 *----------------------------------------------------------------------
 *
 * link, symlink, kill --
 *
 *  	Stubs for missing Unix routines.  This is done simply to avoid
 *  	more ifdef's in the code.
 *
 * Results:
 *  	-1.
 *
 * Side effects:
 *  	Sets errno to EINVAL.
 *
 *----------------------------------------------------------------------
 */

int
link(char *from, char *to)
{
    errno = EINVAL;
    return -1;
}

int
symlink(char *from, char *to)
{
    errno = EINVAL;
    return -1;
}

int
kill(int pid, int sig)
{
    errno = EINVAL;
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * truncate --
 *
 *	Implement Unix truncate.
 *
 * Results:
 *  	0 if ok or -1 on error.
 *
 * Side effects:
 *  	File is opened, truncated, and closed.
 *
 *----------------------------------------------------------------------
 */

int
truncate(char *file, off_t size)
{
    int fd;

    fd = open(file, O_WRONLY|O_BINARY);
    if (fd < 0) {
	return -1;
    }
    size = _chsize(fd, size);
    close(fd);
    if (size != 0) {
	return -1;
    }
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * ConsoleHandler --
 *
 *	Callback when the Ctrl-C is pressed.
 *
 * Results:
 *  	TRUE.
 *
 * Side effects:
 *  	Shutdown is initiated.
 *
 *----------------------------------------------------------------------
 */

static BOOL WINAPI
ConsoleHandler(DWORD ignored)
{
    SetConsoleCtrlHandler(ConsoleHandler, FALSE);
    NsSendSignal(NS_SIGTERM);
    return TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * GetServiceName --
 *
 *	Construct the service name for the corresponding server.
 *
 * Results:
 *  	Pointer to given dstring string.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static char *
GetServiceName(Ns_DString *dsPtr, char *server)
{
    Ns_DStringVarAppend(dsPtr, NSD_NAME, "-", server, NULL);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * StartTicker, StopTicker --
 *
 *	Start and stop the background ticker thread which keeps the
 *  	service control manager informed of during startup and
 *  	shutdown.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Thread is created or signalled to stop and joined.
 *
 *----------------------------------------------------------------------
 */

static void
StartTicker(DWORD pending)
{
    Ns_MutexLock(&lock);
    tick = 1;
    Ns_MutexUnlock(&lock);
    Ns_ThreadCreate(ServiceTicker, (void *) pending, 0, &tickThread);
}

static void
StopTicker(void)
{
    Ns_MutexLock(&lock);
    tick = 0;
    Ns_CondBroadcast(&cond);
    Ns_MutexUnlock(&lock);
    Ns_ThreadJoin(&tickThread, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * ServiceTicker --
 *
 *	Background ticker created by StartTicker which does nothing
 *  	but send the message repeatedly until signaled to stop.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Service control manager is kept informed of progress.
 *
 *----------------------------------------------------------------------
 */

static void
ServiceTicker(void *arg)
{
    Ns_Time timeout;
    DWORD pending = (DWORD) arg;

    Ns_ThreadSetName("-ticker-");

    Ns_MutexLock(&lock);
    do {
        ReportStatus(pending, NO_ERROR, 2000);
	Ns_GetTime(&timeout);
	Ns_IncrTime(&timeout, 1, 0);
	Ns_CondTimedWait(&cond, &lock, &timeout);
    } while (tick);
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * ServiceMain --
 *
 *	Startup routine created by the service control manager.  This
 *  	routine initializes the structure for reporting status,
 *  	starts the ticker, and then re-enters Ns_Main() where it
 *  	was left off when NsServiceConnect() was called.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Server startup continues.
 *
 *----------------------------------------------------------------------
 */

static VOID WINAPI
ServiceMain(DWORD argc, LPTSTR *argv)
{
    hStatus = RegisterServiceCtrlHandler(argv[0], ServiceHandler);
    if (hStatus == 0) {
        Ns_Fatal("nswin32: RegisterServiceCtrlHandler() failed: '%s'",SysErrMsg());
    }
    curStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    curStatus.dwServiceSpecificExitCode = 0;
    StartTicker(SERVICE_START_PENDING);
    Ns_Main(argc, argv, NULL);
    StopTicker();
    ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 100);
    ReportStatus(SERVICE_STOPPED, 0, 0);
    Ns_Log(Notice, "nswin32: service exiting");
}


/*
 *----------------------------------------------------------------------
 *
 * ServiceHandler --
 *
 *	Callback when the service control manager wants to signal the
 *  	server (i.e., when service is stopped via services control
 *  	panel).
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	Signal may be sent.
 *
 *----------------------------------------------------------------------
 */

static VOID WINAPI
ServiceHandler(DWORD code)
{
    if (code == SERVICE_CONTROL_STOP || code == SERVICE_CONTROL_SHUTDOWN) {
    	NsSendSignal(NS_SIGTERM);
    } else {
        ReportStatus(code, NO_ERROR, 0);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ReportStatus --
 *
 *	Update the service control manager with the current state.
 *
 * Results:
 *  	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static void
ReportStatus(DWORD state, DWORD code, DWORD hint)
{
    static DWORD    check = 1;

    if (state == SERVICE_START_PENDING) {
        curStatus.dwControlsAccepted = 0;
    } else {
        curStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    curStatus.dwCurrentState = state;
    curStatus.dwWin32ExitCode = code;
    curStatus.dwWaitHint = hint;
    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED) {
        curStatus.dwCheckPoint = 0;
    } else {
        curStatus.dwCheckPoint = check++;
    }
    if (hStatus != 0 && SetServiceStatus(hStatus, &curStatus) != TRUE) {
        Ns_Fatal("nswin32: SetServiceStatus(%d) failed: '%s'", state, SysErrMsg());
    }
}

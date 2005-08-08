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
 * nslog.c --
 *
 *	This file implements the access log using NCSA Common Log format.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nslog/nslog.c,v 1.16 2005/08/08 11:32:18 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"
#include <sys/stat.h>	/* mkdir */
#include <ctype.h>	/* isspace */

#define LOG_COMBINED	1
#define LOG_FMTTIME	2
#define LOG_REQTIME	4

typedef struct {
    char	   *module;
    Ns_Mutex	    lock;
    int             fd;
    char           *file;
    char           *rollfmt;
    int		    flags;
    int             maxbackup;
    int             maxlines;
    int             curlines;
    int             suppressquery;
    Ns_DString      buffer;
    char          **extheaders;
} Log;


/*
 * Local functions defined in this file
 */
static Ns_Callback LogRollCallback;
static Ns_Callback LogCloseCallback;
static Ns_TraceProc LogTrace;
static int LogFlush(Log *logPtr, Ns_DString *dsPtr);
static int LogOpen(Log *logPtr);
static int LogRoll(Log *logPtr);
static int LogClose(Log *logPtr);
static void LogConfigExtHeaders(Log *logPtr, char *path);
static Ns_ArgProc LogArg;
static Tcl_CmdProc LogCmd;
static Ns_TclInterpInitProc AddCmds;


/*
 *----------------------------------------------------------------------
 *
 * NsLog_ModInit --
 *
 *	Module initialization routine.
 *
 * Results:
 *	NS_OK.
 *
 * Side effects:
 *	Log file is opened, trace routine is registered, and, if
 *	configured, log file roll signal and scheduled procedures 
 *	are registered.
 *
 *----------------------------------------------------------------------
 */

int
NsLog_ModInit(char *server, char *module)
{
    char 	*path;
    int 	 opt, hour;
    Log		*logPtr;
    static int	 first = 1;

    /*
     * Register the info callbacks just once.
     */

    if (first) {
	Ns_RegisterProcInfo((void *)LogRollCallback, "logroll", LogArg);
	Ns_RegisterProcInfo((void *)LogCloseCallback, "logclose", LogArg);
	first = 0;
    }

    /*
     * Initialize the log buffer.
     */

    logPtr = ns_calloc(1, sizeof(Log));
    logPtr->fd = -1;
    logPtr->module = module;
    Ns_MutexInit(&logPtr->lock);
    Ns_MutexSetName2(&logPtr->lock, "nslog", server);
    Ns_DStringInit(&logPtr->buffer);

    /*
     * Determine the log file name which, if not
     * absolute, is expected to exist in the module
     * specific directory.  The module directory is
     * created if necessary.
     */

    path = Ns_ConfigGetPath(server, module, NULL);
    logPtr->file = Ns_ConfigGetValue(path, "file");
    if (logPtr->file == NULL) {
    	logPtr->file = "access.log";
    }
    if (Ns_PathIsAbsolute(logPtr->file) == NS_FALSE) {
    	Ns_DString 	 ds;

	Ns_DStringInit(&ds);
	Ns_ModulePath(&ds, server, module, NULL, NULL);
	if (mkdir(ds.string, 0755) != 0 && errno != EEXIST
	    && errno != EISDIR) {
	    Ns_Log(Error, "nslog: mkdir(%s) failed: %s", 
		   ds.string, strerror(errno));
	    Ns_DStringFree(&ds);
	    return NS_ERROR;
	}
    	Ns_DStringTrunc(&ds, 0);
	Ns_ModulePath(&ds, server, module, logPtr->file, NULL);
	logPtr->file = Ns_DStringExport(&ds);
    }

    /*
     * Get parameters from configuration file
     */

    logPtr->rollfmt = Ns_ConfigGetValue(path, "rollfmt");
    if (!Ns_ConfigGetInt(path, "maxbuffer", &logPtr->maxlines)) {
	logPtr->maxlines = 0;
    }
    if (!Ns_ConfigGetInt(path, "maxbackup", &logPtr->maxbackup) || logPtr->maxbackup < 1) {
        logPtr->maxbackup = 100;
    }
    if (!Ns_ConfigGetBool(path, "formattedTime", &opt)) {
	opt = 1;
    }
    if (opt) {
	logPtr->flags |= LOG_FMTTIME;
    }
    if (!Ns_ConfigGetBool(path, "logcombined", &opt)) {
        opt = 1;
    }
    if (opt) {
	logPtr->flags |= LOG_COMBINED;
    }
    if (!Ns_ConfigGetBool(path, "logreqtime", &opt)) {
        opt = 0;
    }
    if (opt) {
        logPtr->flags |= LOG_REQTIME;
    }


    if (!Ns_ConfigGetBool(path, "suppressquery", &logPtr->suppressquery)) {
	logPtr->suppressquery = 0;
    }

    /*
     * Schedule various log roll and shutdown options.
     */

    if (!Ns_ConfigGetInt(path, "rollhour", &hour) ||
	hour < 0 || hour > 23) {
	hour = 0;
    }
    if (!Ns_ConfigGetBool(path, "rolllog", &opt)) {
        opt = 1;
    }
    if (opt) {
        Ns_ScheduleDaily((Ns_SchedProc *) LogRollCallback, logPtr, 0, hour, 0, NULL);
    }
    if (!Ns_ConfigGetBool(path, "rollonsignal", &opt)) {
        opt = 0;;
    }
    if (opt) {
        Ns_RegisterAtSignal(LogRollCallback, logPtr);
    }

    LogConfigExtHeaders(logPtr, path);

    /*
     * Open the log and register the trace.
     */

    if (LogOpen(logPtr) != NS_OK) {
	return NS_ERROR;
    }
    Ns_RegisterServerTrace(server, LogTrace, logPtr);
    Ns_RegisterAtShutdown(LogCloseCallback, logPtr);
    Ns_TclInitInterps(server, AddCmds, logPtr);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LogTrace --
 *
 *	Trace routine for appending the log with the current connection
 *	results.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Entry is appended to the open log.
 *
 *----------------------------------------------------------------------
 */

static void 
LogTrace(void *arg, Ns_Conn *conn)
{
    Ns_DString     ds;
    register char *p;
    int            quote, n, status, i;
    char           buf[100];
    Log		  *logPtr = arg;
     
    Ns_Time        now, diff;

    /*
     * Compute the request's elapsed time.
     */

    if (logPtr->flags & LOG_REQTIME) {

        Ns_GetTime(&now);
        Ns_DiffTime(&now, Ns_ConnStartTime(conn), &diff);
    }

    Ns_DStringInit(&ds);

    /*
     * Append the peer address and auth user (if any).
     * Watch for users comming from proxy servers. 
     */

    if (conn->headers && (p = Ns_SetIGet(conn->headers, "X-Forwarded-For"))) {
	Ns_DStringAppend(&ds, p);
    } else {
 	Ns_DStringAppend(&ds, Ns_ConnPeer(conn));
    }
    if (conn->authUser == NULL) {
	Ns_DStringAppend(&ds, " - - ");
    } else {
	p = conn->authUser;
	quote = 0;
	while (*p != '\0') {
	    if (isspace((unsigned char) *p)) {
	    	quote = 1;
	    	break;
	    }
	    ++p;
    	}
	if (quote) {
    	    Ns_DStringVarAppend(&ds, " - \"", conn->authUser, "\" ", NULL);
	} else {
    	    Ns_DStringVarAppend(&ds, " - ", conn->authUser, " ", NULL);
	}
    }

    /*
     * Append a common log format time stamp including GMT offset.
     */

    if (!(logPtr->flags & LOG_FMTTIME)) {
	sprintf(buf, "[%ld]", (long) time(NULL));
    } else {
	Ns_LogTime(buf);
    }
    Ns_DStringAppend(&ds, buf);

    /*
     * Append the request line.
     */

    if (conn->request && conn->request->line) {
	
	if (logPtr->suppressquery) {
	    /*
	     * Don't display query data.
	     * NB: Side-effect is that the real URI is returned, so places
	     * where trailing slash returns "index.html" logs as "index.html".
	     */
	    Ns_DStringVarAppend(&ds, " \"", conn->request->url, "\" ", NULL);
	} else {
	    Ns_DStringVarAppend(&ds, " \"", conn->request->line, "\" ", NULL);
	}
    } else {
	Ns_DStringAppend(&ds, " \"\" ");
    }

    /*
     * Construct and append the HTTP status code and bytes sent.
     */

    n = Ns_ConnResponseStatus(conn);
    sprintf(buf, "%d %u ", n ? n : 200, Ns_ConnContentSent(conn));
    Ns_DStringAppend(&ds, buf);

    if ((logPtr->flags & LOG_COMBINED)) {

	/*
	 * Append the referer and user-agent headers (if any).
	 */

	Ns_DStringAppend(&ds, "\"");
	if ((p = Ns_SetIGet(conn->headers, "referer"))) {
	    Ns_DStringAppend(&ds, p);
	}
	Ns_DStringAppend(&ds, "\" \"");
	if ((p = Ns_SetIGet(conn->headers, "user-agent"))) {
	    Ns_DStringAppend(&ds, p);
	}
	Ns_DStringAppend(&ds, "\"");

    }

    /*
     * Append the request's elapsed time (if enabled).
     */

    if (logPtr->flags & LOG_REQTIME) {

        sprintf(buf, " %d.%06ld", (int)diff.sec, diff.usec);
        Ns_DStringAppend(&ds, buf);
    }


    /*
     * Append the extended headers (if any).
     */
    for (i=0; logPtr->extheaders[i] != NULL; i++) {
	if ((p = Ns_SetIGet(conn->headers, logPtr->extheaders[i]))) {
	    Ns_DStringAppend(&ds, " \"");
	    Ns_DStringAppend(&ds, p);
	    Ns_DStringAppend(&ds, "\"");
	} else {
	    Ns_DStringAppend(&ds, " -");
	}
    }

    /*
     * Append the trailing newline and buffer and/or flush the line.
     */

    status = NS_OK;
    Ns_DStringAppend(&ds, "\n");
    Ns_MutexLock(&logPtr->lock);
    if (logPtr->maxlines <= 0) {
	status = LogFlush(logPtr, &ds);
    } else {
	Ns_DStringNAppend(&logPtr->buffer, ds.string, ds.length);
	if (++logPtr->curlines > logPtr->maxlines) {
	    status = LogFlush(logPtr, &logPtr->buffer);
	    logPtr->curlines = 0;
	}
    }
    Ns_MutexUnlock(&logPtr->lock);
    Ns_DStringFree(&ds);
    if (status != NS_OK) {
	Ns_Log(Error, "nslog: failed to flush log: %s", strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * LogCmd --
 *
 *	Implement the ns_accesslog command.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Depends on command.
 *
 *----------------------------------------------------------------------
 */

static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateCommand(interp, "ns_accesslog", LogCmd, arg, NULL);
    return TCL_OK;
}

static int
LogCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    char *rollfile;
    int status;
    Log *logPtr = arg;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " command ?arg?\"", NULL);
	return TCL_ERROR;
    }
    if (STREQ(argv[1], "file")) {
	Tcl_SetResult(interp, logPtr->file, TCL_STATIC);
    } else if (STREQ(argv[1], "roll")) {
	if (argc != 2 && argc != 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    	argv[0], " ", argv[1], " ?file?\"", NULL);
	    return TCL_ERROR;
	}
    	Ns_MutexLock(&logPtr->lock);
	if (argc == 2) {
	    status = LogRoll(logPtr);
	} else {
	    rollfile = (char*)argv[2];
	    status = NS_OK;
    	    if (access(rollfile, F_OK) == 0) {
		status = Ns_RollFile(rollfile, logPtr->maxbackup);
	    }
	    if (status == NS_OK) {
		if (rename(logPtr->file, rollfile) != 0) {
		    status = NS_ERROR;
		} else {
	    	    LogFlush(logPtr, &logPtr->buffer);
	    	    status = LogOpen(logPtr);
		}
	    }
	}
    	Ns_MutexUnlock(&logPtr->lock);
	if (status != NS_OK) {
	    Tcl_AppendResult(interp, "could not roll \"", logPtr->file,
		"\": ", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}
    } else {
	Tcl_AppendResult(interp, "unknown command \"", argv[1],
	    "\": should be file or roll", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LogArg --
 *
 *	Copy log file as argument for callback query.
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
LogArg(Tcl_DString *dsPtr, void *arg)
{
    Log *logPtr = arg;

    Tcl_DStringAppendElement(dsPtr, logPtr->file);
}


/*
 *----------------------------------------------------------------------
 *
 * LogOpen --
 *
 *      Open the access log, closing previous log if opeopen
 *
 * Results:
 *      NS_OK or NS_ERROR.
 *
 * Side effects:
 *      Log re-opened.
 *
 *----------------------------------------------------------------------
 */

static int
LogOpen(Log *logPtr)
{
    int fd;

    fd = open(logPtr->file, O_APPEND|O_WRONLY|O_CREAT, 0644);
    if (fd < 0) {
    	Ns_Log(Error, "nslog: error '%s' opening '%s'",
	       strerror(errno),logPtr->file);
	return NS_ERROR;
    }
    if (logPtr->fd >= 0) {
	close(logPtr->fd);
    }
    logPtr->fd = fd;
    Ns_Log(Notice, "nslog: opened '%s'", logPtr->file);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LogClose --
 *
 *      Flush and/or close the log.
 *
 * Results:
 *      NS_TRUE or NS_FALSE if log was closed.
 *
 * Side effects:
 *      Buffer entries, if any, are flushed.
 *
 *----------------------------------------------------------------------
 */

static int
LogClose(Log *logPtr)
{
    int status;

    status = NS_OK;
    if (logPtr->fd >= 0) {
	Ns_Log(Notice, "nslog: closing '%s'", logPtr->file);
	status = LogFlush(logPtr, &logPtr->buffer);
	close(logPtr->fd);
	logPtr->fd = -1;
	Ns_DStringFree(&logPtr->buffer);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * LogFlush --
 *
 *	Flush a log buffer to the open log file.  Note:  The mutex
 *	is assumed held during call. 
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will disable the log on error.
 *
 *----------------------------------------------------------------------
 */

static int
LogFlush(Log *logPtr, Ns_DString *dsPtr)
{
    if (dsPtr->length > 0) {
    	if (logPtr->fd >= 0 &&
	    write(logPtr->fd, dsPtr->string, (size_t)dsPtr->length) != dsPtr->length) {
	    Ns_Log(Error, "nslog: "
		   "logging disabled: write() failed: '%s'", strerror(errno));
	    close(logPtr->fd);
	    logPtr->fd = -1;
	}
    	Ns_DStringTrunc(dsPtr, 0);
    }
    if (logPtr->fd < 0) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LogRoll --
 *
 *      Roll and re-open the access log.  This procedure is scheduled
 *      and/or registered at signal catching.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Files are rolled to new names.
 *
 *----------------------------------------------------------------------
 */

static int
LogRoll(Log *logPtr)
{
    int 	status;

    status = NS_OK;
    (void) LogClose(logPtr);
    if (access(logPtr->file, F_OK) == 0) {
    	if (logPtr->rollfmt == NULL) {
	    status = Ns_RollFile(logPtr->file, logPtr->maxbackup);
	} else {
	    Ns_DString  ds;
	    time_t 	now;
	    struct tm  *ptm;
	    char 	timeBuf[512];

	    now = time(NULL);
	    ptm = ns_localtime(&now);
	    strftime(timeBuf, 512, logPtr->rollfmt, ptm);
	    Ns_DStringInit(&ds);
	    Ns_DStringVarAppend(&ds, logPtr->file, ".", timeBuf, NULL);
	    if (access(ds.string, F_OK) == 0) {
		status = Ns_RollFile(ds.string, logPtr->maxbackup);
	    } else if (errno != ENOENT) {
		Ns_Log(Error, "nslog: access(%s, F_OK) failed: '%s'", 
	      	       ds.string, strerror(errno));
		status = NS_ERROR;
	    }
	    if (status == NS_OK && rename(logPtr->file, ds.string) != 0) {
	    	Ns_Log(Error, "nslog: rename(%s, %s) failed: '%s'",
		    logPtr->file, ds.string, strerror(errno));
		status = NS_ERROR;
	    }
	    Ns_DStringFree(&ds);
	    if (status == NS_OK) {
		status = Ns_PurgeFiles(logPtr->file, logPtr->maxbackup);
	    }
	}
    }
    status = LogOpen(logPtr);
    return status;
}    	


/*
 *----------------------------------------------------------------------
 *
 * LogCloseCallback, LogRollCallback -
 *
 *      Close or roll the log.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	See LogClose and LogRoll.
 *
 *----------------------------------------------------------------------
 */

static void
LogCallback(int (proc)(Log *), void *arg, char *desc)
{
    int status;
    Log *logPtr = arg;

    Ns_MutexLock(&logPtr->lock);
    status = (*proc)(logPtr);
    Ns_MutexUnlock(&logPtr->lock);
    if (status != NS_OK) {
	Ns_Log(Error, "nslog: failed: %s '%s': '%s'",
	       desc, logPtr->file, strerror(errno));
    }
}

static void
LogCloseCallback(void *arg)
{
    LogCallback(LogClose, arg, "close");
}

static void
LogRollCallback(void *arg)
{
    LogCallback(LogRoll, arg, "roll");
}


/*
 *----------------------------------------------------------------------
 *
 * LogConfigExtHeaders
 *
 *      Parse the ExtendedHeaders configuration.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *	Sets logPtr->extheaders.
 *
 *----------------------------------------------------------------------
 */

static void
LogConfigExtHeaders(Log *logPtr, char *path)
{
    char *config = Ns_ConfigGetValue(path, "extendedheaders");
    char *p;
    int   i;

    if (config == NULL) {
        logPtr->extheaders = ns_calloc(1, sizeof *logPtr->extheaders);
        logPtr->extheaders[0] = NULL;
	return;
    }

    config = ns_strdup(config);

    /* Figure out how many extended headers there are. */

    for (i = 1, p = config; *p; p++) {
	if (*p == ',') {
	    i++;
	}
    }

    /* Initialize the extended header pointers. */

    logPtr->extheaders = ns_calloc((size_t)(i + 1), sizeof *logPtr->extheaders);

    logPtr->extheaders[0] = config;

    for (i = 1, p = config; *p; p++) {
	if (*p == ',') {
	    *p = '\000';
	    logPtr->extheaders[i++] = p + 1;
	}
    }

    logPtr->extheaders[i] = NULL;

}


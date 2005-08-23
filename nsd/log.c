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
 * log.c --
 *
 *	Manage the server log file.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/log.c,v 1.32 2005/08/23 21:41:31 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following define available flags bits.
 */

#define LOG_ROLL	1
#define LOG_EXPAND	2
#define LOG_DEBUG	4
#define LOG_DEV		8
#define LOG_NONOTICE	16
#define LOG_USEC	32

/*
 * The following struct maintains per-thread
 * cached formatted time strings and log buffers.
 */

typedef struct LogCache {
    int		hold;
    int		count;
    time_t	gtime;
    time_t	ltime;
    char	gbuf[100];
    char	lbuf[100];
    Ns_DString  buffer;
} LogCache;

/*
 * Local functions defined in this file
 */

static int    LogReOpen(void);
static void   Log(Ns_LogSeverity severity, char *fmt, va_list ap);
static LogCache *LogGetCache(void);
static Ns_TlsCleanup LogFreeCache;
static void   LogFlush(LogCache *cachePtr);
static char  *LogTime(LogCache *cachePtr, int gmtoff, long *usecPtr);
static int    LogStart(LogCache *cachePtr, Ns_LogSeverity severity);
static void   LogEnd(LogCache *cachePtr);

/*
 * Static variables defined in this file
 */

static Ns_Tls tls;
static Ns_Mutex lock;
static Ns_LogFlushProc *flushProcPtr;
static Ns_LogProc *nslogProcPtr;
static char *file;
static int flags;
static int maxback;
static int maxlevel;
static int maxbuffer;;


/*
 *----------------------------------------------------------------------
 *
 * NsInitLog --
 *
 *	Initialize the log API and TLS slot.
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
NsInitLog(void)
{
    Ns_MutexSetName(&lock, "ns:log");
    Ns_TlsAlloc(&tls, LogFreeCache);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLogConf --
 *
 *	Config the logging interface.
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
NsLogConf(void)
{
    if (NsParamBool("logusec", 0)) {
	flags |= LOG_USEC;
    }
    if (NsParamBool("logroll", 1)) {
	flags |= LOG_ROLL;
    }
    if (NsParamBool("logexpanded", 0)) {
	flags |= LOG_EXPAND;
    }
    if (NsParamBool("debug", 0)) {
	flags |= LOG_DEBUG;
    }
    if (NsParamBool("logdebug", 0)) {
	flags |= LOG_DEBUG;
    }
    if (NsParamBool("logdev", 0)) {
	flags |= LOG_DEV;
    }
    if (!NsParamBool("lognotice", 1)) {
	flags |= LOG_NONOTICE;
    }
    maxback  = NsParamBool("logmaxbackup", 10);
    maxlevel = NsParamBool("logmaxlevel", INT_MAX);
    maxbuffer  = NsParamBool("logmaxbuffer", 10);
    file = NsParamString("serverlog", "server.log");
    if (!Ns_PathIsAbsolute(file)) {
	Ns_DString ds;

	Ns_DStringInit(&ds);
	Ns_HomePath(&ds, "log", file, NULL);
	file = Ns_DStringExport(&ds);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_InfoErrorLog --
 *
 *	Returns the filename of the log file. 
 *
 * Results:
 *	Log file name or NULL if none. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_InfoErrorLog(void)
{
    return file;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_LogRoll --
 *
 *	Signal handler for SIG_HUP which will roll the files. Also a 
 *	tasty snack from Stuckey's. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will rename the log file and reopen it. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_LogRoll(void)
{
    if (file != NULL) {
        if (access(file, F_OK) == 0) {
            Ns_RollFile(file, maxback);
        }
        Ns_Log(Notice, "log: re-opening log file '%s'", file);
        if (LogReOpen() != NS_OK) {
	    return NS_ERROR;
	}
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Log --
 *
 *	Spit a message out to the server log. 
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
Ns_Log(Ns_LogSeverity severity, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    Log(severity, fmt, ap);
    va_end(ap);
}

/* NB: For binary compatibility with previous releases. */

void
ns_serverLog(Ns_LogSeverity severity, char *fmt, va_list *vaPtr)
{
    Log(severity, fmt, *vaPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Fatal --
 *
 *	Spit a message out to the server log with severity level of 
 *	Fatal, and then terminate the nsd process. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	WILL CAUSE AOLSERVER TO EXIT! 
 *
 *----------------------------------------------------------------------
 */

void
Ns_Fatal(char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    Log(Fatal, fmt, ap);
    va_end(ap);
    if (nsconf.debug) {
	abort();
    }
    _exit(1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_LogTime2, Ns_LogTime --
 *
 *	Copy a local or GMT date and time string useful for common log
 *	format enties (e.g., nslog).
 *
 * Results:
 *	Pointer to given buffer.
 *
 * Side effects:
 *	Will put data into timeBuf, which must be at least 41 bytes 
 *	long. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_LogTime2(char *timeBuf, int gmt)
{
    strcpy(timeBuf, LogTime(LogGetCache(), gmt, NULL));
    return timeBuf;
}

char *
Ns_LogTime(char *timeBuf)
{
    return Ns_LogTime2(timeBuf, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsLogOpen --
 *
 *	Open the log file. Adjust configurable parameters, too. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Configures this module to use the newly opened log file.
 *	If LogRoll is turned on in the config file, then it registers
 *	a signal callback.
 *
 *----------------------------------------------------------------------
 */

void
NsLogOpen(void)
{
    /*
     * Open the log and schedule the signal roll.
     */

    if (LogReOpen() != NS_OK) {
	Ns_Fatal("log: failed to open server log '%s': '%s'", 
		 file, strerror(errno));
    }
    if (flags & LOG_ROLL) {
	Ns_RegisterAtSignal((Ns_Callback *) Ns_LogRoll, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogRollObjCmd --
 *
 *	Implements ns_logroll as obj command. 
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
NsTclLogRollObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (Ns_LogRoll() != NS_OK) {
	Tcl_SetResult(interp, "could not roll server log", TCL_STATIC);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogCtlObjCmd --
 *
 *	Implements ns_logctl command to manage log buffering
 *	and release.
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
NsTclLogCtlObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
	       Tcl_Obj *CONST objv[])
{
    int len;
    LogCache *cachePtr;
    static CONST char *opts[] = {
	"hold",
	"count",
	"get",
	"peek",
	"flush",
	"release",
	"truncate",
	NULL
    };
    enum {
	CHoldIdx,
	CCountIdx,
	CGetIdx,
	CPeekIdx,
	CFlushIdx,
	CReleaseIdx,
	CTruncIdx
    } _nsmayalias opt;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg?");
    	return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    cachePtr = LogGetCache();
    switch (opt) {
    case CHoldIdx:
	cachePtr->hold = 1;
	break;

    case CPeekIdx:
	Tcl_SetResult(interp, cachePtr->buffer.string, TCL_VOLATILE);
	break;

    case CGetIdx:
	Tcl_SetResult(interp, cachePtr->buffer.string, TCL_VOLATILE);
	Ns_DStringFree(&cachePtr->buffer);
        cachePtr->count = 0;
	break;

    case CReleaseIdx:
	cachePtr->hold = 0;
	/* FALLTHROUGH */

    case CFlushIdx:
	LogFlush(cachePtr);
        cachePtr->count = 0;
	break;

    case CCountIdx:
	Tcl_SetIntObj(Tcl_GetObjResult(interp), cachePtr->count);
	break;

    case CTruncIdx:
	len = 0;
	if (objc > 2 && Tcl_GetIntFromObj(interp, objv[2], &len) != TCL_OK) {
	    return TCL_ERROR;
	}
	Ns_DStringTrunc(&cachePtr->buffer, len);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogObjCmd --
 *
 *	Implements ns_log as obj command.
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
NsTclLogObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
	       Tcl_Obj *CONST objv[])
{
    Ns_LogSeverity severity;
    LogCache *cachePtr;
    char *severitystr;
    int i;
    Ns_DString ds;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "severity string ?string ...?");
    	return TCL_ERROR;
    }
    severitystr = Tcl_GetString(objv[1]);
    cachePtr = LogGetCache();
    if (STRIEQ(severitystr, "notice")) {
	severity = Notice;
    } else if (STRIEQ(severitystr, "warning")) {
	severity = Warning;
    } else if (STRIEQ(severitystr, "error")) {
	severity = Error;
    } else if (STRIEQ(severitystr, "fatal")) {
	severity = Fatal;
    } else if (STRIEQ(severitystr, "bug")) {
	severity = Bug;
    } else if (STRIEQ(severitystr, "debug")) {
	severity = Debug;
    } else if (STRIEQ(severitystr, "dev")) {
        severity = Dev;
    } else if (Tcl_GetIntFromObj(NULL, objv[1], &i) == TCL_OK) {
	severity = i;
    } else {
	Tcl_AppendResult(interp, "unknown severity: \"", severitystr,
	    "\": should be notice, warning, error, "
	    "fatal, bug, debug, dev, or integer value", NULL);
	return TCL_ERROR;
    }

    Ns_DStringInit(&ds);

    for (i = 2; i < objc; ++i) {
	Ns_DStringVarAppend(&ds,
	Tcl_GetString(objv[i]), i < (objc-1) ? " " : NULL, NULL);
    }

    Ns_Log(severity, "%s", ds.string);
    Ns_DStringFree(&ds);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Log --
 *
 *	Add an entry to the log file if the severity is not surpressed.
 *	Or call a custom log procedure and let that worry about the
 *	severity.
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	May write immediately or later through buffer.
 *
 *----------------------------------------------------------------------
 */

static void
Log(Ns_LogSeverity severity, char *fmt, va_list ap)
{
    LogCache *cachePtr;

    cachePtr = LogGetCache();
    if (nslogProcPtr == NULL) {
	if (LogStart(cachePtr, severity)) {
	    Ns_DStringVPrintf(&cachePtr->buffer, fmt, ap);
	    LogEnd(cachePtr);
	}
    } else {
	(*nslogProcPtr)(&cachePtr->buffer, severity, fmt, ap);
	++cachePtr->count;
	if (!cachePtr->hold) {
	    LogFlush(cachePtr);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * LogStart --
 *
 *	Start a log entry.
 *
 * Results:
 *	1 if log started and should be written, 0 if given severity
 *	is surpressed.
 *
 * Side effects:
 *	May append log header to given dstring.
 *
 *----------------------------------------------------------------------
 */

static int
LogStart(LogCache *cachePtr, Ns_LogSeverity severity)
{
    char *severityStr, buf[10];
    long usec;

    switch (severity) {
	case Notice:
	    if (flags & LOG_NONOTICE) {
		return 0;
	    }
	    severityStr = "Notice";
	    break;
        case Warning:
	    severityStr = "Warning";
	    break;
	case Error:
	    severityStr = "Error";
	    break;
	case Fatal:
	    severityStr = "Fatal";
	    break;
	case Bug:
	    severityStr = "Bug";
	    break;
	case Debug:
	    if (!(flags & LOG_DEBUG)) {
		return 0;
	    }
	    severityStr = "Debug";
	    break;
	case Dev:
	    if (!(flags & LOG_DEV)) {
		return 0;
	    }
	    severityStr = "Dev";
	    break;
	default:
	    if (severity > maxlevel) {
		return 0;
	    }
	    sprintf(buf, "Level%d", severity);
	    severityStr = buf;
	    break;
    }
    Ns_DStringAppend(&cachePtr->buffer, LogTime(cachePtr, 0, &usec));
    if (flags & LOG_USEC) {
    	Ns_DStringTrunc(&cachePtr->buffer, cachePtr->buffer.length-1);
	Ns_DStringPrintf(&cachePtr->buffer, ".%ld]", usec);
    }
    Ns_DStringPrintf(&cachePtr->buffer, "[%d.%lu][%s] %s: ",
	Ns_InfoPid(), (unsigned long) Ns_ThreadId(), Ns_ThreadGetName(), severityStr);
    if (flags & LOG_EXPAND) {
	Ns_DStringAppend(&cachePtr->buffer, "\n    ");
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * LogEnd --
 *
 *	Complete a log entry and flush if necessary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May write to log.
 *
 *----------------------------------------------------------------------
 */

static void
LogEnd(LogCache *cachePtr)
{
    Ns_DStringNAppend(&cachePtr->buffer, "\n", 1);
    if (flags & LOG_EXPAND) {
	Ns_DStringNAppend(&cachePtr->buffer, "\n", 1);
    }
    ++cachePtr->count;
    if (!cachePtr->hold) {
    	LogFlush(cachePtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * LogFlush --
 *
 *	Flush per-thread log entries to buffer or open file.
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
LogFlush(LogCache *cachePtr)
{
    Ns_DString *dsPtr = &cachePtr->buffer;

    Ns_MutexLock(&lock);
    if (flushProcPtr == NULL) {
	(void) write(2, dsPtr->string, (size_t)dsPtr->length);
    } else {
	(*flushProcPtr)(dsPtr->string, (size_t)dsPtr->length);
    }
    Ns_MutexUnlock(&lock);
    Ns_DStringFree(dsPtr);
    cachePtr->count = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * LogReOpen --
 *
 *	Open the log file name specified in the 'logFile' global. If 
 *	it's successfully opened, make that file the sink for stdout 
 *	and stderr too. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
LogReOpen(void)
{
    int fd; 
    int status;

    status = NS_OK;
    fd = open(file, O_WRONLY|O_APPEND|O_CREAT, 0644);
    if (fd < 0) {
        Ns_Log(Error, "log: failed to re-open log file '%s': '%s'",
	       file, strerror(errno));
        status = NS_ERROR;
    } else {
	/*
	 * Route stderr to the file
	 */
	
        if (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) == -1) {
            fprintf(stdout, "dup2(%s, STDERR_FILENO) failed: %s\n",
		file, strerror(errno));
            status = NS_ERROR;
        }
	
	/*
	 * Route stdout to the file
	 */
	
        if (dup2(STDERR_FILENO, STDOUT_FILENO) == -1) {
            Ns_Log(Error, "log: failed to route stdout to file: '%s'",
		   strerror(errno));
            status = NS_ERROR;
        }
	
	/*
	 * Clean up dangling 'open' reference to the fd
	 */
	
        if (fd != STDERR_FILENO && fd != STDOUT_FILENO) {
            close(fd);
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * LogTime --
 *
 *	Get formatted local or gmt log time from per-thread cache.
 *
 * Results:
 *	Pointer to per-thread buffer.
 *
 * Side effects:
 *	Given usecPtr updated with current microseconds.
 *
 *----------------------------------------------------------------------
 */

static char *
LogTime(LogCache *cachePtr, int gmtoff, long *usecPtr)
{
    time_t   *tp;
    struct tm *ptm;
    int gmtoffset, n, sign;
    char *bp;
    Ns_Time now;

    if (gmtoff) {
	tp = &cachePtr->gtime;
	bp = cachePtr->gbuf;
    } else {
	tp = &cachePtr->ltime;
	bp = cachePtr->lbuf;
    }
    Ns_GetTime(&now);
    if (*tp != now.sec) {
	*tp = now.sec;
	ptm = ns_localtime(&now.sec);
	n = strftime(bp, 32, "[%d/%b/%Y:%H:%M:%S", ptm);
	if (!gmtoff) {
	    bp[n++] = ']';
	    bp[n] = '\0';
	} else {
#ifdef HAVE_TM_GMTOFF
	    gmtoffset = ptm->tm_gmtoff / 60;
#else
	    gmtoffset = -timezone / 60;
	    if (daylight && ptm->tm_isdst) {
		gmtoffset += 60;
	    }
#endif
	    if (gmtoffset < 0) {
		sign = '-';
		gmtoffset *= -1;
	    } else {
		sign = '+';
	    }
	    sprintf(bp + n, 
		    " %c%02d%02d]", sign, gmtoffset / 60, gmtoffset % 60);
	}
    }
    if (usecPtr != NULL) {
	*usecPtr = now.usec;
    }
    return bp;
}


/*
 *----------------------------------------------------------------------
 *
 * LogGetCache --
 *
 *	Get the per-thread Cache struct.
 *
 * Results:
 *	Pointer to per-thread struct.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static LogCache *
LogGetCache(void)
{
    LogCache *cachePtr;

    cachePtr = Ns_TlsGet(&tls);
    if (cachePtr == NULL) {
	cachePtr = ns_calloc(1, sizeof(LogCache));
	Ns_DStringInit(&cachePtr->buffer);
	Ns_TlsSet(&tls, cachePtr);
    }
    return cachePtr;
}


/*
 *----------------------------------------------------------------------
 *
 * LogFreeCache --
 *
 *	TLS cleanup callback to destory per-thread Cache struct.
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
LogFreeCache(void *arg)
{
    LogCache *cachePtr = arg;

    LogFlush(cachePtr);
    Ns_DStringFree(&cachePtr->buffer);
    ns_free(cachePtr);
}


/*
 *----------------------------------------------------------------------
 *      
 * Ns_SetLogFlushProc --
 * 
 *	Set the proc to call when writing the log. You probably want
 *	to have a Ns_RegisterAtShutdown() call too so you can
 *	close/finish up whatever special logging you are doing.
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
Ns_SetLogFlushProc(Ns_LogFlushProc *procPtr)
{
    flushProcPtr = procPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SetNsLogProc --
 *
 *	Set the proc to call when adding a log entry.
 *
 *	There are 2 ways to use this override:
 *
 *	1. In conjunction with the Ns_SetLogFlushProc() to use the
 *	   existing AOLserver buffering and writing system. So when a
 *	   log message is added it is inserted into the log cache and
 *	   flushed later through your log flush override. To use this
 *	   write any logging data to the Ns_DString that is passed into
 *	   the Ns_Log proc.
 *	2. Without calling Ns_SetLogFlushProc() and handle all buffering
 *	   and writing directly. LogFlush() will be called as normal but
 *	   is a no-op because nothing will have been added. Do not write
 *	   into the Ns_DString passed into the Ns_Log proc in this case.
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
Ns_SetNsLogProc(Ns_LogProc *procPtr)
{
    nslogProcPtr = procPtr;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/log.c,v 1.11 2001/12/05 22:46:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following struct maintains cached formatted
 * time buffers.
 */

typedef struct Cache {
    time_t	gtime;
    time_t	ltime;
    char	gbuf[100];
    char	lbuf[100];
} Cache;

/*
 * Local functions defined in this file
 */

static int   LogReOpen(void);
static void  Log(Ns_LogSeverity severity, char *fmt, va_list ap);
static int   LogStart(Ns_DString *dsPtr, Ns_LogSeverity severity);
static void  LogEnd(Ns_DString *dsPtr);
static void  LogWrite(void);
static Ns_Callback LogFlush;
static char *LogTime(int gmtoff);

/*
 * Static variables defined in this file
 */

static Ns_Mutex lock;
static Ns_Cond cond;
static Ns_DString buffer;
static int buffered = 0;
static int flushing = 0;
static int nbuffered = 0;
static int initialized = 0;
static Ns_Tls tls;


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
    return nsconf.log.file;
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
    if (nsconf.log.file != NULL) {
        if (access(nsconf.log.file, F_OK) == 0) {
            Ns_RollFile(nsconf.log.file, nsconf.log.maxback);
        }
        Ns_Log(Notice, "log: re-opening log file '%s'", nsconf.log.file);
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
    _exit(1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_LogTime --
 *
 *	Construct local date and time for log file. 
 *
 * Results:
 *	A string time and date. 
 *
 * Side effects:
 *	Will put data into timeBuf, which must be at least 41 bytes 
 *	long. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_LogTime(char *timeBuf)
{
    strcpy(timeBuf, LogTime(1));
    return timeBuf;
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
     * Initialize log buffering.
     */

    Ns_MutexInit(&lock);
    Ns_CondInit(&cond);
    Ns_MutexSetName(&lock, "ns:log");
    Ns_DStringInit(&buffer);
    if (nsconf.log.flags & LOG_BUFFER) {
	buffered = 1;
	Ns_RegisterAtShutdown(LogFlush, (void *) 1);
	if (nsconf.log.flushint > 0) {
	    Ns_ScheduleProc(LogFlush, (void *) 0, 0, nsconf.log.flushint);
	}
    }

    /*
     * Open the log and schedule the signal roll.
     */

    if (LogReOpen() != NS_OK) {
	Ns_Fatal("log: failed to open server log '%s': '%s'", 
		 nsconf.log.file, strerror(errno));
    }
    if (nsconf.log.flags & LOG_ROLL) {
	Ns_RegisterAtSignal((Ns_Callback *) Ns_LogRoll, NULL);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogRollCmd --
 *
 *	Implements ns_logroll. 
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
NsTclLogRollCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (Ns_LogRoll() != NS_OK) {
	Tcl_SetResult(interp, "could not roll server log", TCL_STATIC);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogCmd --
 *
 *	Implements ns_log.
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
NsTclLogCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_LogSeverity severity;
    Ns_DString ds;
    int i;

    if (argc < 3) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " severity string ?string ...?\"", NULL);
    	return TCL_ERROR;
    }
    if (STRIEQ(argv[1], "notice")) {
	severity = Notice;
    } else if (STRIEQ(argv[1], "warning")) {
	severity = Warning;
    } else if (STRIEQ(argv[1], "error")) {
	severity = Error;
    } else if (STRIEQ(argv[1], "fatal")) {
	severity = Fatal;
    } else if (STRIEQ(argv[1], "bug")) {
	severity = Bug;
    } else if (STRIEQ(argv[1], "debug")) {
	severity = Debug;
    } else if (Tcl_GetInt(interp, argv[1], &i) == TCL_OK) {
	severity = i;
    } else {
        Tcl_AppendResult(interp, "unknown severity \"",
                         argv[1], "\":  should be one of: ",
			 "fatal, error, warning, bug, notice, or debug.",
			 NULL);
    	return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    if (LogStart(&ds, severity)) {
	for (i = 2; i < argc; ++i) {
	    Ns_DStringVarAppend(&ds, argv[i], i > 2 ? " " : NULL, NULL);
	}
	LogEnd(&ds);
    }
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Log --
 *
 *	Add an entry to the log file if the severity is not surpressed.
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
    Ns_DString ds;

    Ns_DStringInit(&ds);
    if (LogStart(&ds, severity)) {
	Ns_DStringVPrintf(&ds, fmt, ap);
	LogEnd(&ds);
    }
    Ns_DStringFree(&ds);
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
LogStart(Ns_DString *dsPtr, Ns_LogSeverity severity)
{
    char *severityStr, buf[10];

    switch (severity) {
	case Notice:
	    if (nsconf.log.flags & LOG_NONOTICE) {
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
	    if (!(nsconf.log.flags & LOG_DEBUG)) {
		return 0;
	    }
	    severityStr = "Debug";
	    break;
	case Dev:
	    if (!(nsconf.log.flags & LOG_DEV)) {
		return 0;
	    }
	    severityStr = "Dev";
	    break;
	default:
	    if (severity > nsconf.log.maxlevel) {
		return 0;
	    }
	    sprintf(buf, "L%d", severity);
	    severityStr = buf;
	    break;
    }
    Ns_DStringPrintf(dsPtr, "%s[%d.%d][%s] %s: ", LogTime(0),
		Ns_InfoPid(), Ns_ThreadId(), Ns_ThreadGetName(), severityStr);
    if (nsconf.log.flags & LOG_EXPAND) {
	Ns_DStringAppend(dsPtr, "\n    ");
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * LogEnd --
 *
 *	Complete a log entry and either append to buffer or write
 *	immediately.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May flush log.
 *
 *----------------------------------------------------------------------
 */

static void
LogEnd(Ns_DString *dsPtr)
{
    Ns_DStringNAppend(dsPtr, "\n", 1);
    if (nsconf.log.flags & LOG_EXPAND) {
	Ns_DStringNAppend(dsPtr, "\n", 1);
    }
    Ns_MutexLock(&lock);
    if (!buffered) {
	Ns_MutexUnlock(&lock);
	(void) write(2, dsPtr->string, dsPtr->length);
    } else {
	while (flushing) {
	    Ns_CondWait(&cond, &lock);
	}
	Ns_DStringNAppend(&buffer, dsPtr->string, dsPtr->length);
	if (nbuffered++ > nsconf.log.maxbuffer) {
	    LogWrite();
	}
	Ns_MutexUnlock(&lock);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * LogFlush --
 *
 *	Flush the buffered log, either from the scheduled proc timeout
 *	or at shutdown.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May write to log and disables buffering at shutdown.
 *
 *----------------------------------------------------------------------
 */

static void
LogFlush(void *arg)
{
    int exit = (int) arg;

    Ns_MutexLock(&lock);

    /*
     * Wait for current flushing, if any, to complete.
     */

    while (flushing) {
	Ns_CondWait(&cond, &lock);
    }
    if (nbuffered > 0) {
	LogWrite();
    }

    /*
     * Disable further buffering at exit time.
     */

    if (exit) {
	buffered = 0;
    }
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * LogWrite --
 *
 *	Write current buffer to log file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will write data.
 *
 *----------------------------------------------------------------------
 */

static void
LogWrite(void)
{
    flushing = 1;
    Ns_MutexUnlock(&lock);
    (void) write(2, buffer.string, buffer.length);
    Ns_DStringSetLength(&buffer, 0);
    Ns_MutexLock(&lock);
    flushing = 0;
    nbuffered = 0;
    Ns_CondBroadcast(&cond);
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
    fd = open(nsconf.log.file, O_WRONLY|O_APPEND|O_CREAT, 0644);
    if (fd < 0) {
        Ns_Log(Error, "log: failed to re-open log file '%s': '%s'",
	       nsconf.log.file, strerror(errno));
        status = NS_ERROR;
    } else {
	/*
	 * Route stderr to the file
	 */
	
        if (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) == -1) {
            fprintf(stdout, "dup2(%s, STDERR_FILENO) failed: %s\n",
		nsconf.log.file, strerror(errno));
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
 *	Get formatted local or gmt log time.
 *
 * Results:
 *	Pointer to per-thread buffer.
 *
 * Side effects:
 *	Buffer is updated if time has changed since check.
 *
 *----------------------------------------------------------------------
 */

static char *
LogTime(int gmtoff)
{
    Cache    *cachePtr;
    time_t   *tp, now = time(NULL);
    struct tm *ptm;
    int gmtoffset, n, sign;
    char *bp;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    Ns_TlsAlloc(&tls, ns_free);
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }

    cachePtr = Ns_TlsGet(&tls);
    if (cachePtr == NULL) {
	cachePtr = ns_calloc(1, sizeof(Cache));
	Ns_TlsSet(&tls, cachePtr);
    }
    if (gmtoff) {
	tp = &cachePtr->gtime;
	bp = cachePtr->gbuf;
    } else {
	tp = &cachePtr->ltime;
	bp = cachePtr->lbuf;
    }
    if (*tp != time(&now)) {
	*tp = now;
	ptm = ns_localtime(&now);
	n = strftime(bp, 32, "[%d/%b/%Y:%H:%M:%S", ptm);
	if (!gmtoff) {
    	    strcat(bp+n, "]");
	} else {
#ifdef NO_TIMEZONE
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
    return bp;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/log.c,v 1.7.4.1 2002/09/17 23:52:03 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define DEFAULT_LEVEL         Notice
#define DEFAULT_FILE          NULL

/*
 * Local functions defined in this file
 */

static void  Log(Ns_LogSeverity severity, char *fmt, va_list *argsPtr);
static int   LogReOpen(void);
static char *LogTime(char *buf);
static int   GetSeverity(Tcl_Interp *interp, char *severityStr,
			 Ns_LogSeverity *severityPtr);

/*
 * Static variables defined in this file
 */
static char     *logFile;
static FILE     *logFileFd;
static Ns_Mutex  lock;


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
    return logFile;
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
    if (logFile != NULL) {
        if (access(logFile, F_OK) == 0) {
            Ns_RollFile(logFile, nsconf.log.maxback);
        }
        Ns_Log(Notice, "log: re-opening log file '%s'", logFile);
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
ns_serverLog(Ns_LogSeverity severity, char *fmt, va_list *vaPtr)
{
    
    if (severity == Debug && nsconf.log.debug == 0) {
	return;
    }
    if (severity == Dev && nsconf.log.dev == 0) {
	return;
    }
    Log(severity, fmt, vaPtr);
}

void
Ns_Log(Ns_LogSeverity severity, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    ns_serverLog(severity, fmt, &ap);
    va_end(ap);
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
    Log(Fatal, fmt, &ap);
    va_end(ap);
    _exit(1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_LogTime2, Ns_LogTime --
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
Ns_LogTime2(char *timeBuf, int gmtoff)
{
    time_t     now;
    struct tm *ptm;
    int gmtoffset, n, sign;

    now = time(NULL);
    ptm = ns_localtime(&now);
    n = strftime(timeBuf, 32, "[%d/%b/%Y:%H:%M:%S", ptm);
    if (!gmtoff) {
    	strcat(timeBuf+n, "]");
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
	sprintf(timeBuf + n, 
		" %c%02d%02d]", sign, gmtoffset / 60, gmtoffset % 60);
    }
    return timeBuf;
}

char *
Ns_LogTime(char *timeBuf)
{
    return Ns_LogTime2(timeBuf, 1);
}



/*
 *----------------------------------------------------------------------
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
    Ns_DString ds;
    int        roll;

    /*
     * Log max backup is the maximum number of log files allowed.  Older files
     * are nuked as newer ones are created
     */
    
    logFile = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "serverlog");
    if (logFile == NULL) {
	logFile = "server.log";
    }
    Ns_DStringInit(&ds);
    if (Ns_PathIsAbsolute(logFile) == NS_FALSE) {
	Ns_HomePath(&ds, "log", logFile, NULL);
	logFile = Ns_DStringExport(&ds);
    }
    if (LogReOpen() != NS_OK) {
	Ns_Fatal("log: failed to open server log '%s': '%s'", 
		 logFile, strerror(errno));
    }
    if (!Ns_ConfigGetBool(NS_CONFIG_PARAMETERS, "logroll", &roll)) {
    	roll = 1;
    }
    if (roll) {
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
	interp->result = "could not roll server log";
	return TCL_ERROR;
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

static int
TclLog(Tcl_Interp *interp, char *sevstr, int msgc, char **msgv)
{
    Ns_LogSeverity severity;
    char *msg;

    if (GetSeverity(interp, sevstr, &severity) != TCL_OK) {
    	return TCL_ERROR;
    }    
    if (msgc == 1) {
    	msg = msgv[0];
    } else {
    	msg = Tcl_Concat(msgc, msgv);
    }
    Ns_Log(severity, "%s", msg);
    if (msg != msgv[0]) {
    	ckfree(msg);
    }
    return TCL_OK;
}

int
NsTclLogCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc < 3) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " severity string ?string ...?\"", NULL);
    	return TCL_ERROR;
    }
    return TclLog(interp, argv[1], argc-2, argv+2);
}


/*
 *----------------------------------------------------------------------
 *
 * Log --
 *
 *	Unconditionally write a line to the log file--this does the 
 *	actual writing. 
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
Log(Ns_LogSeverity severity, char *fmt, va_list *argsPtr)
{
    char *severityStr;
    char  timebuf[100];
    FILE *fp;
    static int initialized;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    Ns_MutexSetName2(&lock, "ns", "log");
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }

    Ns_LogTime2(timebuf, 0);
    switch (severity) {
	case Fatal:
	    severityStr = "Fatal";
	    break;
	case Error:
	    severityStr = "Error";
	    break;
        case Warning:
	    severityStr = "Warning";
	    break;
	case Bug:
	    severityStr = "Bug";
	    break;
	case Notice:
	    severityStr = "Notice";
	    break;
	case Debug:
	    severityStr = "Debug";
	    break;
    	case Dev: 
	    severityStr = "Dev";
	    break;
	default:
	    severityStr = "<Unknown>";
	    break;
    }

    Ns_MutexLock(&lock);
    if ((fp = logFileFd) == NULL) {
	fp = stderr;
    }
    fprintf(fp, "%s[%d.%d][%s] %s: ", timebuf,
		Ns_InfoPid(), Ns_ThreadId(), Ns_ThreadGetName(), severityStr);
    if (nsconf.log.expanded == NS_TRUE) {
	fprintf(fp, "\n    ");
    }
    vfprintf(fp, fmt, *argsPtr);
    if (nsconf.log.expanded == NS_TRUE) {
	fprintf(fp, "\n\n");
    } else {
    	fprintf(fp, "\n");
    }
    fflush(fp);
    Ns_MutexUnlock(&lock);
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
    fd = open(logFile, O_WRONLY|O_APPEND|O_CREAT|O_TEXT, 0644);
    if (fd < 0) {
        Ns_Log(Error, "log: failed to re-open log file '%s': '%s'",
	       logFile, strerror(errno));
        status = NS_ERROR;
    } else {
	/*
	 * Route stderr to the file
	 */
	
        if (fd != STDERR_FILENO && dup2(fd, STDERR_FILENO) == -1) {
            fprintf(stdout, "dup2(%s, STDERR_FILENO) failed: %s\n", logFile,
		    strerror(errno));
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
 * GetSeverity --
 *
 *	Convert a string severity name into an Ns_LogSeverity. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Errors will be appended to interp->result. 
 *
 *----------------------------------------------------------------------
 */

static int
GetSeverity(Tcl_Interp *interp, char *severityStr,
		Ns_LogSeverity *severityPtr)
{
    if (STRIEQ(severityStr, 	   "fatal")) {
	*severityPtr = Fatal;
    } else if (STRIEQ(severityStr, "error")) {
	*severityPtr = Error;
    } else if (STRIEQ(severityStr, "warning")) {
	*severityPtr = Warning;
    } else if (STRIEQ(severityStr, "bug")) {
	*severityPtr = Bug;
    } else if (STRIEQ(severityStr, "notice")) {
	*severityPtr = Notice;
    } else if (STRIEQ(severityStr, "debug")) {
	*severityPtr = Debug;
    } else if (STRIEQ(severityStr, "dev")) {
	*severityPtr = Dev;
    } else {
        Tcl_AppendResult(interp, "unknown severity \"",
                         severityStr, "\":  should be one of: ",
			 "fatal, error, warning, bug, notice, or debug.",
			 NULL);
    	return TCL_ERROR;
    }
    return TCL_OK;
}

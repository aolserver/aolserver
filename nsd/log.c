/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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
 *	Manage the server log file. Also includes the Ns_ModLog API. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/log.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define DEFAULT_LEVEL         Notice
#define DEFAULT_FILE          NULL
#define DEFAULT_REALM         "nsd"

/*
 * Ns_ModLogHandle is an opaque type.  It is a pointer to a ModLog structure.
 */

typedef struct {
    Ns_LogSeverity severity;
    char *realm;
    FILE *fp;
} ModLog; 

ModLog defaultMod = {DEFAULT_LEVEL, DEFAULT_REALM, DEFAULT_FILE};

/*
 * Local functions defined in this file
 */

static void Log(ModLog *mPtr, Ns_LogSeverity severity, char *fmt, va_list *argsPtr);
static int LogReOpen(void);
static char *LogTime(char *buf);
static Tcl_HashTable *GetTable(void);
static int GetSeverity(Tcl_Interp *interp, char *severityStr, Ns_LogSeverity *severityPtr);
static int GetHandle(Tcl_Interp *interp, char *realm, Ns_ModLogHandle *handlePtr);

/*
 * Static variables defined in this file
 */

static char          *logFile;

/*
 * Note that because multiple realms normally the default open file
 * (stderr), the entire logging system is single threaded instead
 * of using a lock per-modlog.
 */

static Ns_Mutex lock;

/*
 * Maps the Ns_LogSeverity enum to integers that relate to their relative
 * severity.  This works around the problem of not being able to re-order
 * the elements of that enum without breaking backwards compatibility.
 * Smaller values have higher severity:
 */

static int severityRank[Dev + 1] = { 
   4, /* Notice    - enum value = 0 */
   3, /* Warning   - enum value = 1 */
   2, /* Error     - enum value = 2 */
   0, /* Fatal     - enum value = 3 */
   1, /* Bug       - enum value = 4 */
   5, /* Debug     - enum value = 5 */
   6  /* Dev       -      value = 6 - for developers - defined in nsd/nsd.h */
};


/*
 * The following global variable if the default Ns_ModLogHandle
 * for Ns_ModLog.
 */

Ns_ModLogHandle nsDefaultModLogHandle = (Ns_ModLogHandle) &defaultMod;
#define GETMOD(h)	((h)?((ModLog *)(h)):&defaultMod)


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
        Ns_Log(Notice, "re-opening log:  %s", logFile);
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
    Log(&defaultMod, severity, fmt, vaPtr);
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
    Log(&defaultMod, Fatal, fmt, &ap);
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
	sprintf(timeBuf + n, " %c%02d%02d]", sign, gmtoffset / 60, gmtoffset % 60);
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
 *
 * Ns_ModLogRegister --
 *
 *	Register a realm name, and get a new log handle back. 
 *
 * Results:
 *	The new modlog handle to use in subsequent calls. 
 *
 * Side effects:
 *	Creates a new realm (allocates memory).
 * 	Realms are global to the server, so if you re-register a realm, you'll
 * 	get the original handle back rather than creating a new realm.
 *
 *----------------------------------------------------------------------
 */

void 
Ns_ModLogRegister(char *realm, Ns_ModLogHandle *handle)
{
    ModLog *mPtr;
    Tcl_HashEntry *hPtr;
    int            new;

    if (realm == NULL) {
	mPtr = &defaultMod;
    } else {
    	hPtr = Tcl_CreateHashEntry(GetTable(), realm, &new);
	if (!new) {
	    mPtr = Tcl_GetHashValue(hPtr);
	} else {
	    mPtr = (ModLog *) ns_malloc(sizeof(ModLog));
	    mPtr->severity = DEFAULT_LEVEL;
	    mPtr->fp = DEFAULT_FILE;
	    mPtr->realm = ns_strcopy(realm);
	    Tcl_SetHashValue(hPtr, mPtr);
	}
    }
    *handle = (Ns_ModLogHandle) mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLogSetThreshold --
 *
 *	Throttle the logging output on a per-module basis.
 *	Pass null handle for 'global' level. Only logging of 'serverity'
 *	and higher will actually be logged.
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
Ns_ModLogSetThreshold(Ns_ModLogHandle handle, Ns_LogSeverity severity)
{
    ModLog *mPtr = GETMOD(handle);

    Ns_MutexLock(&lock);
    mPtr->severity = severity;
    Ns_MutexUnlock(&lock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLogGetThreshold --
 *
 *	Return the logging cut-off level.
 *	Pass NULL handle for 'global' level.
 *
 * Results:
 *	Logging level. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_LogSeverity
Ns_ModLogGetThreshold(Ns_ModLogHandle handle)
{
    Ns_LogSeverity severity;
    ModLog *mPtr = GETMOD(handle);

    Ns_MutexLock(&lock);
    severity = mPtr->severity;
    Ns_MutexUnlock(&lock);
    return severity;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLogLookupHandle --
 *
 *	Given a realm name, return the modlog handle. 
 *
 * Results:
 *	The modlog handle, or NULL if the given realm hasn't been 
 *	registered yet. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_ModLogHandle 
Ns_ModLogLookupHandle(char *realm)
{
    Tcl_HashEntry   *hPtr;
    ModLog *mPtr;

    mPtr = NULL;
    if (realm == NULL) {
	mPtr = &defaultMod;
    } else {
    	hPtr = Tcl_FindHashEntry(GetTable(), realm);
    	if (hPtr != NULL) {
	    mPtr = Tcl_GetHashValue(hPtr);
	}
    }
    return (Ns_ModLogHandle) mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLogLookupRealm --
 *
 *	Given a modlog handle, return the realm name.
 *
 * Results:
 *	A string realm name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ModLogLookupRealm(Ns_ModLogHandle handle)
{
    ModLog *mPtr = GETMOD(handle);

    return mPtr->realm;
}

/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLogRedirect -- 
 *
 *	Redirect logging to a different file.
 *
 * Results:
 *
 *	None.
 *
 * Side effects:
 *
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ModLogRedirect(Ns_ModLogHandle handle, FILE *fp, char *description)
{
    ModLog *mPtr = GETMOD(handle);

    Ns_ModLog(Notice, handle, "%s", description);
    if (fp == NULL) {
	fp = DEFAULT_FILE;
    }
    Ns_MutexLock(&lock);
    if (mPtr->fp != fp) {
	mPtr->fp = fp;
    }
    Ns_MutexUnlock(&lock);
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
	Ns_Fatal("Could not open server log %s:  %s", 
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
 * NsModLogRegSubRealm --
 *
 *	Register a sub-realm in the nsd realm.
 *  
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds a new realm.
 *
 *----------------------------------------------------------------------
 */

void 
NsModLogRegSubRealm(char *subrealm, Ns_ModLogHandle *handlePtr)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, DEFAULT_REALM, ".", subrealm, NULL);
    Ns_ModLogRegister(ds.string, handlePtr);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModLog --
 *
 *	Send a message to the server log, gated by the logging 
 *	threshold for a particular module. 
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
Ns_ModLog(Ns_LogSeverity severity, Ns_ModLogHandle handle, char *fmt, ...)
{
    Ns_LogSeverity debug, dev, lowest;
    ModLog         *mPtr = GETMOD(handle);
    va_list         ap;

    if (severity < Notice || severity > Dev) {
	Ns_Log(Bug, "Unknown severity: %d given to Ns_ModLog", severity);
	severity = Error;
    }
    debug = severityRank[Fatal];
    if (nsconf.log.debug) {
	debug = severityRank[Debug];
    }
    dev = severityRank[Fatal];
    if (nsconf.log.dev) {
	dev =  severityRank[Dev];
    }
    lowest = severityRank[mPtr->severity];
    if (debug > lowest) {
	lowest = debug;
    }
    if (dev > lowest) {
	lowest = dev;
    }
    if (lowest >= severityRank[severity]) {
	va_start(ap, fmt);
	Log(mPtr, severity, fmt, &ap);
	va_end(ap);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLogCmd, NsTclModLogCmd --
 *
 *	Implements ns_log and ns_modlog commands. 
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
TclLog(Tcl_Interp *interp, Ns_ModLogHandle *handle, char *sevstr, int msgc, char **msgv)
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
    Ns_ModLog(severity, handle, "%s", msg);
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
    return TclLog(interp, NULL, argv[1], argc-2, argv+2);
}

int
NsTclModLogCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_ModLogHandle handle;

    if (argc < 4) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
			 argv[0], " severity realm string ?string ...?\"", 
			 NULL);
    	return TCL_ERROR;
    }
    if (GetHandle(interp, argv[2], &handle) != TCL_OK) {
    	return TCL_ERROR;
    }
    return TclLog(interp, handle, argv[1], argc-3, argv+3);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclModLogControl --
 *
 *	Tweak the module logging - add new realms, set and retrieve 
 *	the threshold for those realms, and get a list of all of the 
 *	currently registered logging realms. 
 *
 *
 *     ns_modlogcontrol set_threshold ?realm? severity
 *     ns_modlogcontrol get_threshold ?realm?
 *     ns_modlogcontrol get_realms
 *     ns_modlogcontrol register realm
 *     ns_modlogcontrol redirect realm ?filename?
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclModLogControlCmd(ClientData dummy, Tcl_Interp *interp,
                       int argc, char **argv)
{
    Ns_ModLogHandle handle;

    if (argc < 2 || argc > 4) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " command ?arg? ?arg?", NULL);
    	return TCL_ERROR;
    }
    
    if (STREQ(argv[1], "get_realms")) {
	Tcl_DString ds;
	Tcl_HashEntry *hPtr;
	Tcl_HashSearch search;
	Tcl_HashTable *tablePtr = GetTable();

    	Tcl_DStringInit(&ds);
	hPtr = Tcl_FirstHashEntry(tablePtr, &search);
	while (hPtr != NULL) {
	    Tcl_DStringAppendElement(&ds, Tcl_GetHashKey(tablePtr, hPtr));
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Tcl_DStringResult(interp, &ds);
	
    } else if (STREQ(argv[1], "register")) {
	if (Ns_InfoStarted()) {
	    Tcl_SetResult(interp, "can not register realms after startup", TCL_STATIC);
    	    return TCL_ERROR;
	}
	Ns_ModLogRegister(argv[2], &handle);
	
    } else if (STREQ(argv[1], "set_threshold")) {
    	Ns_LogSeverity severity;
	
    	if (argc != 3 && argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
	    	argv[0], " ", argv[1], " ?realm? value\"", NULL);
	    return TCL_ERROR;
	}
	if (argc == 3) {
	    handle = nsDefaultModLogHandle;
	} else if (GetHandle(interp, argv[2], &handle) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (GetSeverity(interp, argv[argc-1], &severity) != TCL_OK) {
	    return TCL_ERROR;
	}
	Ns_ModLogSetThreshold(handle, severity);
	
    } else if (STREQ(argv[1], "get_threshold")) {
	char *severityStr;

    	if (argc != 2 && argc != 3) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
	    	argv[0], " ", argv[1], " ?realm?\"", NULL);
	    return TCL_ERROR;
	}
	if (argc == 3) {
	    handle = nsDefaultModLogHandle;
	} else if (GetHandle(interp, argv[2], &handle) != TCL_OK) {
	    return TCL_ERROR;
	}

	switch (Ns_ModLogGetThreshold(handle)) {
	    case Fatal: {
		severityStr = "fatal";
		break;
	    }
	    case Error: {
		severityStr = "error";
		break;
	    }
	    case Warning: {
		severityStr = "warning";
		break;
	    }
	    case Bug: {
		severityStr = "bug";
		break;
	    }
	    case Notice: {
		severityStr = "notice";
		break;
	    }
	    case Debug: {
		severityStr = "debug";
		break;
	    }
    	    case Dev: {
		severityStr = "dev";
		break;
	    }
	    default: {
		severityStr = "unknown";
	    }
	}
	Tcl_SetResult(interp, severityStr, TCL_STATIC);

    } else if (STREQ(argv[1], "redirect")) {
    	FILE *fp;
	Ns_DString ds;
        char *file;
	
    	if (argc != 3 && argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
	    	argv[0], " ", argv[1], " realm ?filename?\"", NULL);
	    return TCL_ERROR;
	}
	if (GetHandle(interp, argv[2], &handle) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (argc == 3) {
	    fp = DEFAULT_FILE;
	    file = "<strderr>";
	} else {
	    file = argv[3];
    	    fp = fopen(file, "at");
	    if (fp == NULL) {
		Tcl_AppendResult(interp, "could not open \"", file,
		    "\": ", Tcl_PosixError(interp), NULL);
		return TCL_ERROR;
	    }
	}
	Ns_DStringInit(&ds);
	Ns_DStringVarAppend(&ds, "redirecting to ", file, NULL);
	Ns_ModLogRedirect(handle, fp, ds.string);
	Ns_DStringFree(&ds);

    } else {
	Tcl_AppendResult(interp, "unknown command \"", argv[1], "\": should "
			 "be register, set_threshold, get_threshold, "
			 "get_realms or redirect", NULL);
    	return TCL_ERROR;
    }

    return TCL_OK;
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
Log(ModLog *mPtr, Ns_LogSeverity severity, char *fmt, va_list *argsPtr)
{
    char *severityStr;
    char  timebuf[100];
    FILE *fp;

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
    fp = mPtr->fp;
    if (fp == NULL) {
	fp = stderr;
    }
    fprintf(fp, "%s[%d.%d][%s] %s: ", timebuf,
		Ns_InfoPid(), Ns_ThreadId(), Ns_ThreadGetName(), severityStr);
    if (mPtr != &defaultMod) {
	fprintf(fp, "%s: ", mPtr->realm);
    }
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
        Ns_Log(Error, "could not re-open log %s: %s", logFile, strerror(errno));
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
            Ns_Log(Error, "dup2(STDERR_FILENO, STDOUT_FILENO) failed: %s\n",
		      logFile, strerror(errno));
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
 * GetTable --
 *
 *	Get the log realm table, initializing if necessary.
 *
 * Results:
 *  	Pointer to static Tcl_HashTable.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashTable *
GetTable(void)
{
    static int initialized = 0;
    static Tcl_HashTable table;
    
    if (!initialized) {
    	Tcl_HashEntry *hPtr;
	int new;

    	Tcl_InitHashTable(&table, TCL_STRING_KEYS);
	hPtr = Tcl_CreateHashEntry(&table, defaultMod.realm, &new);
	Tcl_SetHashValue(hPtr, &defaultMod);
	Ns_MutexSetName2(&lock, "ns", "log");
	initialized = 1;
    }
    return &table;
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


/*
 *----------------------------------------------------------------------
 *
 * GetHandle --
 *
 *	Return a valid log handle.
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
GetHandle(Tcl_Interp *interp, char *realm, Ns_ModLogHandle *handlePtr)
{
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(GetTable(), realm);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "no such realm: ", realm, NULL);
	return TCL_ERROR;
    }
    *handlePtr = Tcl_GetHashValue(hPtr);
    return TCL_OK;
}

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
 *	Various sundry logging functions. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nspd/log.c,v 1.5 2003/03/07 18:08:49 vasiljevic Exp $, compiled: " __DATE__ " " __TIME__;

#include "pd.h"

static int      trace = 0;
static FILE    *tracefp = NULL;


/*
 *----------------------------------------------------------------------
 *
 * PdTraceOn --
 *
 *	Turn tracing on. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	May open trace file.
 *
 *----------------------------------------------------------------------
 */

void
PdTraceOn(char *file)
{
    char            errbuf[512];

    Ns_PdLog(Notice, "PdTraceOn: traceon:");
    trace = 1;
    if (tracefp != NULL) {
        fclose(tracefp);
    }
    if ((tracefp = fopen(file, "w")) == NULL) {
        sprintf(errbuf, "Unable to open trace file: %s (%s)",
            file, strerror(errno));
        Ns_PdLog(Error, errbuf);
        Ns_PdSendString(errbuf);
    } else {
        Ns_PdSendString(OK_STATUS);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdTraceOff --
 *
 *	Turn tracing off. 
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
PdTraceOff(void)
{
    Ns_PdLog(Notice, "nspd: traceoff");
    trace = 0;
    if (tracefp != NULL) {
        fclose(tracefp);
        tracefp = NULL;
    }
    Ns_PdSendString(OK_STATUS);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_FatalErrno --
 *
 *	Spit out an error and exit. 
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
Ns_FatalErrno(char *func)
{
    Ns_PdLog(Error, "nspd: %s failed: %s(%d)", func, strerror(errno), errno);
    Ns_PdExit(1);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_FatalSock --
 *
 *	Same as Ns_FatalErrno 
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
Ns_FatalSock(char *func)
{
    Ns_FatalErrno(func);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdLog --
 *
 *	Same as Ns_Log in AOLserver core. 
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
Ns_PdLog(Ns_PdLogMsgType errtype, char *format,...)
{
    va_list ap;
    int     priority;

    if (errtype == Trace) {
        if (trace && tracefp != NULL) {
            va_start(ap, format);
            vfprintf(tracefp, format, ap);
            va_end(ap);
            /* maintain consistency with syslog below, which adds \n */
            fprintf(tracefp, "\n");
            fflush(tracefp);
        }
    } else {
        int typeok = 1;

        switch (errtype) {
        case Error:
            priority = LOG_ERR;
            break;
        case Notice:
            priority = LOG_NOTICE;
            break;
        default:
            priority = LOG_ERR;
            typeok = 0;
            syslog(LOG_ERR, "nspd: unknown error type: %d", errtype);
            break;
        }
        if (typeok) {
            char msgbuf[4096];

            va_start(ap, format);
            vsprintf(msgbuf, format, ap);
            va_end(ap);
            syslog(priority, msgbuf);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * OpenLog --
 *
 *	Open the syslog. 
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
OpenLog(void)
{
    char *syslogident;

    if ((syslogident = strrchr(pdBin, '/')) == NULL) {
        syslogident = pdBin;
    } else {
        syslogident++;
    }
    openlog(syslogident, LOG_PID | LOG_CONS, LOG_DAEMON);
}


/*
 *----------------------------------------------------------------------
 *
 * CloseLog --
 *
 *	Close the syslog. 
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
CloseLog(void)
{
    closelog();
}



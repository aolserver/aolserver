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
 * tclcmds.c --
 *
 *	Implements a lot of Tcl API commands. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclmisc.c,v 1.18 2001/05/18 12:29:53 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int WordEndsInSemi(char *ip);


/*
 *----------------------------------------------------------------------
 *
 * NsTclStripHtmlCmd --
 *
 *	Implements ns_striphtml. 
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
NsTclStripHtmlCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int   intag;     /* flag to see if are we inside a tag */
    int	  intspec;   /* flag to see if we are inside a special char */
    char *inString;  /* copy of input string */
    char *inPtr;     /* moving pointer to input string */
    char *outPtr;    /* moving pointer to output string */

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " page\"", NULL);
        return TCL_ERROR;
    }

    /*
     * Make a copy of the input and point the moving and output ptrs to it.
     */
    inString = ns_strdup(argv[1]);
    inPtr    = inString;
    outPtr   = inString;
    intag    = 0;
    intspec  = 0;

    while (*inPtr != '\0') {

        if (*inPtr == '<') {
            intag = 1;

        } else if (intag && (*inPtr == '>')) {
	    /* inside a tag that closes */
            intag = 0;

        } else if (intspec && (*inPtr == ';')) {
	    /* inside a special character that closes */
            intspec = 0;		

        } else if (!intag && !intspec) {
	    /* regular text */

            if (*inPtr == '&') {
		/* starting a new special character */
                intspec=WordEndsInSemi(inPtr);
	    }

            if (!intspec) {
		/* incr pointer only if we're not in something htmlish */
                *outPtr++ = *inPtr;
	    }
        }
        ++inPtr;
    }

    /* null-terminator */
    *outPtr = '\0';

    Tcl_SetResult(interp, inString, TCL_VOLATILE);
    
    ns_free(inString);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCryptCmd --
 *
 *	Implements ns_crypt. 
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
NsTclCryptCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[NS_ENCRYPT_BUFSIZE];

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " key salt\"", (char *) NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, Ns_Encrypt(argv[1], argv[2], buf), TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHrefsCmd --
 *
 *	Implments ns_hrefs. 
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
NsTclHrefsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *p, *s, *e, *he, save;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " html\"", (char *) NULL);
        return TCL_ERROR;
    }

    p = argv[1];
    while ((s = strchr(p, '<')) && (e = strchr(s, '>'))) {
	++s;
	*e = '\0';
	while (*s && isspace(UCHAR(*s))) {
	    ++s;
	}
	if ((*s == 'a' || *s == 'A') && isspace(UCHAR(s[1]))) {
	    ++s;
	    while (*s && isspace(UCHAR(*s))) {
	    	++s;
	    }
	    if (!strncasecmp(s, "href", 4)) {
		s += 4;
		while (*s && isspace(UCHAR(*s))) {
		    ++s;
		}
		if (*s == '=') {
	    	    ++s;
	    	    while (*s && isspace(UCHAR(*s))) {
	    		++s;
	    	    }
		    he = NULL;
		    if (*s == '\'' || *s == '"') {
			he = strchr(s+1, *s);
			++s;
		    }
		    if (he == NULL) {
			he = s;
			while (!isspace(UCHAR(*he))) {
			    ++he;
			}
		    }
		    save = *he;
		    *he = '\0';
		    Tcl_AppendElement(interp, s);
		    *he = save;
		}
	    }
	}
	*e++ = '>';
	p = e;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocalTimeCmd, NsTclGmTimeCmd --
 *
 *	Implements the ns_gmtime and ns_localtime commands. 
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
TmCmd(ClientData isgmt, Tcl_Interp *interp, int argc, char **argv)
{
    time_t     tt_now = time(NULL);
    char       buf[10];
    struct tm *ptm;

    if (isgmt) {
        if (argc != 1) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                 argv[0], "\"", NULL);
            return TCL_ERROR;
        }
        ptm = ns_gmtime(&tt_now);
    } else {
        static Ns_Mutex lock;
        char *oldTimezone = NULL;

        if (argc > 2) {
            Tcl_AppendResult(interp, "wrong # args: should be \"",
                 argv[0], " ?tz?\"", NULL);
            return TCL_ERROR;
        }

        Ns_MutexLock(&lock);

        if (argc == 2) {
            Ns_DString dsNewTimezone;
            Ns_DStringInit(&dsNewTimezone);

            oldTimezone = getenv("TZ");
            Ns_DStringAppend(&dsNewTimezone, "TZ=");
            Ns_DStringAppend(&dsNewTimezone, argv[1]);

            putenv(dsNewTimezone.string);
            tzset();

            Ns_DStringFree(&dsNewTimezone);
        }

        ptm = ns_localtime(&tt_now);

        if (oldTimezone != NULL) {
            Ns_DString dsNewTimezone;
            Ns_DStringInit(&dsNewTimezone);

            Ns_DStringAppend(&dsNewTimezone, "TZ=");
            Ns_DStringAppend(&dsNewTimezone, oldTimezone);

            putenv(dsNewTimezone.string);

            Ns_DStringFree(&dsNewTimezone);
        }

        Ns_MutexUnlock(&lock);
    }

    sprintf(buf, "%d", ptm->tm_sec);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_min);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_hour);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_mday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_mon);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_year);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_wday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_yday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_isdst);
    Tcl_AppendElement(interp, buf);

    return TCL_OK;
}

int
NsTclGmTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd((ClientData) 1, interp, argc, argv);
}

int
NsTclLocalTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd(NULL, interp, argc, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSleepCmd --
 *
 *	Tcl result. 
 *
 * Results:
 *	See docs. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSleepCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int seconds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
                         argv[0], " seconds\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &seconds) != TCL_OK) {
        return TCL_ERROR;
    }
    if (seconds < 0) {
        interp->result = "#seconds must be >= 0";
        return TCL_ERROR;
    }

    sleep(seconds);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHTUUEncodeCmd --
 *
 *	Implements ns_uuencode 
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
NsTclHTUUEncodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char bufcoded[1 + (4 * 48) / 2];
    int  nbytes;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    nbytes = strlen(argv[1]);
    if (nbytes > 48) {
        Tcl_AppendResult(interp, "invalid string \"",
                         argv[1], "\": must be less than 48 characters", NULL);
        return TCL_ERROR;
    }
    Ns_HtuuEncode((unsigned char *) argv[1], nbytes, bufcoded);
    Tcl_SetResult(interp, bufcoded, TCL_VOLATILE);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HTUUDecodecmd --
 *
 *	Implements ns_uudecode. 
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
NsTclHTUUDecodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int   n;
    char *decoded;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    n = strlen(argv[1]) + 3;
    decoded = ns_malloc(n);
    n = Ns_HtuuDecode(argv[1], (unsigned char *) decoded, n);
    decoded[n] = '\0';
    Tcl_SetResult(interp, decoded, (Tcl_FreeProc *) ns_free);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeCmd --
 *
 *	Implements ns_time. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

#define TIME_SEP ':'

int
NsTclGetObjTime(Tcl_Interp *interp, Tcl_Obj *obj, Ns_Time *timePtr)
{
    return NsTclGetTime(interp, Tcl_GetString(obj), timePtr);
}

int
NsTclGetTime(Tcl_Interp *interp, char *time, Ns_Time *timePtr)
{
    int result, sec, usec;
    char *sep;

    sep = strchr(time, TIME_SEP);
    if (sep != NULL) {
	*sep = '\0';
    }
    result = Tcl_GetInt(interp, time, &sec);
    if (sep != NULL) {
	*sep++ = TIME_SEP;
    }
    if (result == TCL_OK) {
	if (sep == NULL) {
	    usec = 0;
	} else {
	    result = Tcl_GetInt(interp, sep, &usec);
	}
	if (result == TCL_OK) {
	    timePtr->sec = (time_t) sec;
	    timePtr->usec = (long) usec;
	}
    }
    return result;
}

int
NsTclTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    char buf[100], *cmd;
    Ns_Time result, t1, t2;
    int sec, usec, n;

    if (objc < 2) {
	Tcl_SetLongObj(Tcl_GetObjResult(interp), time(NULL));
    } else {
	cmd = Tcl_GetString(objv[1]);
	if (STREQ(cmd, "get")) {
	    Ns_GetTime(&result);
	} else if (STREQ(cmd, "incr")) {
	    if (objc != 4 && objc != 5) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
		    cmd, " incr time sec ?usec?\"", NULL);
		return TCL_ERROR;
	    }
	    usec = 0;
	    if (NsTclGetObjTime(interp, objv[2], &result) != TCL_OK ||
		Tcl_GetIntFromObj(interp, objv[3], &sec) != TCL_OK ||
		(objc == 5 && Tcl_GetIntFromObj(interp, objv[4], &usec) != TCL_OK)) {
		return TCL_ERROR;
	    }
	    Ns_IncrTime(&result, sec, usec);
	} else if (STREQ(cmd, "diff")) {
	    if (objc != 4) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
		    cmd, " diff t1 t2\"", NULL);
		return TCL_ERROR;
	    }
	    if (NsTclGetObjTime(interp, objv[2], &t1) != TCL_OK ||
		NsTclGetObjTime(interp, objv[3], &t2) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Ns_DiffTime(&t1, &t2, &result);
	} else if (STREQ(cmd, "adj")) {
	    if (objc != 3) {
		Tcl_AppendResult(interp, "wrong # args: should be \"",
		    cmd, " adj time\"", NULL);
		return TCL_ERROR;
	    }
	    if (NsTclGetObjTime(interp, objv[2], &result) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Ns_AdjTime(&result);
	} else {
	    Tcl_AppendResult(interp, "unknown command \"", cmd,
		"\": should be get, incr, diff, or adj", NULL);
	    return TCL_ERROR;
	}
	sprintf(buf, "%ld%c%ld", (long) result.sec, TIME_SEP, result.usec);
        Tcl_SetResult(interp, buf, TCL_VOLATILE);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStrftimeCmd --
 *
 *	Implements ns_fmttime. 
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
NsTclStrftimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char   *fmt;
    time_t  time;
    int     i;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &i) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argv[2] != NULL) {
        fmt = argv[2];
    } else {
        fmt = "%c";
    }
    time = i;
    if (strftime(interp->result, TCL_RESULT_SIZE, fmt, 
		 ns_localtime(&time)) == 0) {
        sprintf(interp->result, "invalid time: %d", (int) time);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCrashCmd --
 *
 *	Crash the server to test exception handling.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will segfault.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCrashCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char           *death;

    death = NULL;
    *death = 1;

    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * WordEndsInSemi --
 *
 *	Does this word end in a semicolon or a space? 
 *
 * Results:
 *	1 if semi, 0 if space. 
 *
 * Side effects:
 *	Behavior is undefined if string ends before either space or 
 *	semi. 
 *
 *----------------------------------------------------------------------
 */

static int
WordEndsInSemi(char *ip)
{
    while((*ip != ' ') && (*ip != ';')) {
        ip++;
    }
    if (*ip == ';') {
        return 1;
    } else {
        return 0;
    }
}

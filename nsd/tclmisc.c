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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclmisc.c,v 1.24 2002/07/14 23:10:19 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
 * NsTclCryptObjCmd --
 *
 *	Implements ns_crypt as ObjCommand. 
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
NsTclCryptObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    char buf[NS_ENCRYPT_BUFSIZE];

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "key salt");
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, Ns_Encrypt(Tcl_GetString(objv[1]), Tcl_GetString(objv[2]), buf), TCL_VOLATILE);

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


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocalTimeObjCmd, NsTclGmTimeObjCmd --
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
TmObjCmd(ClientData isgmt, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    time_t     now;
    struct tm *ptm;
    Tcl_Obj   *objPtr[9];

    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, "");
        return TCL_ERROR;
    }
    now = time(NULL);
    if (isgmt) {
        ptm = ns_gmtime(&now);
    } else {
        ptm = ns_localtime(&now);
    }
    objPtr[0] = Tcl_NewIntObj(ptm->tm_sec);
    objPtr[1] = Tcl_NewIntObj(ptm->tm_min);
    objPtr[2] = Tcl_NewIntObj(ptm->tm_hour);
    objPtr[3] = Tcl_NewIntObj(ptm->tm_mday);
    objPtr[4] = Tcl_NewIntObj(ptm->tm_mon);
    objPtr[5] = Tcl_NewIntObj(ptm->tm_year);
    objPtr[6] = Tcl_NewIntObj(ptm->tm_wday);
    objPtr[7] = Tcl_NewIntObj(ptm->tm_yday);
    objPtr[8] = Tcl_NewIntObj(ptm->tm_isdst);
    Tcl_SetListObj(Tcl_GetObjResult(interp), 9, objPtr);
    return TCL_OK;
}

int
NsTclGmTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd((ClientData) 1, interp, argc, argv);
}

int
NsTclGmTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    return TmObjCmd((ClientData) 1, interp, objc, objv);
}

int
NsTclLocalTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd(NULL, interp, argc, argv);
}

int
NsTclLocalTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    return TmObjCmd(NULL, interp, objc, objv);
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
	Tcl_AppendResult(interp, "invalid sections \"", argv[1],
	    "\": shoudl be >= 0", NULL);
        return TCL_ERROR;
    }
    sleep(seconds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSleepObjCmd --
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
NsTclSleepObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Time time;
    struct timeval tv;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "timespec");
        return TCL_ERROR;
    }
    if (Ns_TclGetTimeFromObj(interp, objv[1], &time) != TCL_OK) {
        return TCL_ERROR;
    }
    Ns_AdjTime(&time);
    if (time.sec < 0 || (time.sec == 0 && time.usec < 0)) {
        Tcl_AppendResult(interp, "invalid timespec: ", 
            Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    tv.tv_sec = time.sec;
    tv.tv_usec = time.usec;
    (void) select(0, NULL, NULL, NULL, &tv);
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
 * NsTclHTUUEncodeObjCmd --
 *
 *	Implements ns_uuencode as obj command.
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
NsTclHTUUEncodeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    char bufcoded[1 + (4 * 48) / 2];
    char *string;
    int   nbytes;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "string");
        return TCL_ERROR;
    }
    string = Tcl_GetStringFromObj(objv[1], &nbytes);
    if (nbytes > 48) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid string \"",
                         string, "\": must be less than 48 characters", NULL);
        return TCL_ERROR;
    }
    Ns_HtuuEncode((unsigned char *) string, nbytes, bufcoded);
    Tcl_SetResult(interp, bufcoded, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HTUUDecodeCmd --
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
 * HTUUDecodeObjcmd --
 *
 *	Implements ns_uudecode as obj command. 
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
NsTclHTUUDecodeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    int   n;
    char *string, *decoded;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "string");
        return TCL_ERROR;
    }
    string = Tcl_GetStringFromObj(objv[1], &n);
    n += 3;
    decoded = ns_malloc(n);
    n = Ns_HtuuDecode(string, (unsigned char *) decoded, n);
    decoded[n] = '\0';
    Tcl_SetResult(interp, decoded, (Tcl_FreeProc *) ns_free);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeObjCmd --
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

int
NsTclTimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_Time result, t1, t2;
    static CONST char *opts[] = {
	"adjust", "diff", "get", "incr", "make", "seconds",
	"microseconds", NULL
    };
    enum {
	TAdjustIdx, TDiffIdx, TGetIdx, TIncrIdx, TMakeIdx,
	TSecondsIdx, TMicroSecondsIdx
    } opt;

    if (objc < 2) {
    	Tcl_SetLongObj(Tcl_GetObjResult(interp), time(NULL));
    } else {
	if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
				(int *) &opt) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch (opt) {
	case TGetIdx:
	    Ns_GetTime(&result);
	    break;

	case TMakeIdx:
	    if (objc != 3 && objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "sec ?usec?");
		return TCL_ERROR;
	    }
	    if (Tcl_GetLongFromObj(interp, objv[2], &result.sec) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (objc == 3) {
		result.usec = 0;
	    } else if (Tcl_GetLongFromObj(interp, objv[3], &result.usec) != TCL_OK) {
		return TCL_ERROR;
	    }
	    break;

	case TIncrIdx:
	    if (objc != 4 && objc != 5) {
		Tcl_WrongNumArgs(interp, 2, objv, "time sec ?usec?");
		return TCL_ERROR;
	    }
	    if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK ||
		Tcl_GetLongFromObj(interp, objv[3], &t2.sec) != TCL_OK) {
		return TCL_ERROR;
	    }
	    if (objc == 4) {
		t2.usec = 0;
	    } else if (Tcl_GetLongFromObj(interp, objv[4], &t2.usec) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Ns_IncrTime(&result, t2.sec, t2.usec);
	    break;

	case TDiffIdx:
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "time1 time2");
		return TCL_ERROR;
	    }
	    if (Ns_TclGetTimeFromObj(interp, objv[2], &t1) != TCL_OK ||
		Ns_TclGetTimeFromObj(interp, objv[3], &t2) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Ns_DiffTime(&t1, &t2, &result);
	    break;

	case TAdjustIdx:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "time");
		return TCL_ERROR;
	    }
	    if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Ns_AdjTime(&result);
	    break;

	case TSecondsIdx:
	case TMicroSecondsIdx:
	    if (objc != 3) {
		Tcl_WrongNumArgs(interp, 2, objv, "time");
		return TCL_ERROR;
	    }
	    if (Ns_TclGetTimeFromObj(interp, objv[2], &result) != TCL_OK) {
		return TCL_ERROR;
	    }
	    Tcl_SetLongObj(Tcl_GetObjResult(interp),
			  opt == TSecondsIdx ? result.sec : result.usec);
	    return TCL_OK;
	    break;
	}
    	Ns_TclSetTimeObj(Tcl_GetObjResult(interp), &result);
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
    char   *fmt, buf[200];
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
    if (strftime(buf, sizeof(buf), fmt, ns_localtime(&time)) == 0) {
	Tcl_AppendResult(interp, "invalid time: ", argv[1], NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStrftimeObjCmd --
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
NsTclStrftimeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    char   *fmt, buf[200];
    time_t  time;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "time ?fmt?");
        return TCL_ERROR;
    }
    if (Tcl_GetLongFromObj(interp, objv[1], &time) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objv[2] != NULL) {
        fmt = Tcl_GetString(objv[2]);
    } else {
        fmt = "%c";
    }
    if (strftime(buf, sizeof(buf), fmt, ns_localtime(&time)) == 0) {
        Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid time: ", 
            Tcl_GetString(objv[1]), NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
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

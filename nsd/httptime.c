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
 * time.c --
 *
 *	Manipulate times and dates; this is strongly influenced
 *	by HTSUtils.c from CERN. See also RFC 1123.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/httptime.c,v 1.10 2009/05/14 04:27:05 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int MakeNum(char *s);
static int MakeMonth(char *s);

/*
 * Static variables defined in this file
 */

static char *month_names[12] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static char *weekdays_names[7] =
{ 
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" 
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_Httptime --
 *
 *	Convert a time_t into a time/date format used in HTTP
 *	(see RFC 1123). If passed-in time is null, then the
 *	current time will be used.
 *
 * Results:
 *	The string time, or NULL if error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_HttpTime(Ns_DString *pds, time_t *when)
{
    time_t     now;
    char       buf[40];
    struct tm *tmPtr;

    if (when == NULL) {
        now = time(0);
        when = &now;
    }
    tmPtr = ns_gmtime(when);
    if (tmPtr == NULL) {
        return NULL;
    }

    /*
     * Provide always english names independent of locale setting.
     * The format is RFC 1123: "Sun, 06 Nov 1997 09:12:45 GMT"
     */
    
    snprintf(buf, 40, "%s, %02d %s %04d %02d:%02d:%02d GMT",
             weekdays_names[tmPtr->tm_wday], tmPtr->tm_mday,
             month_names[tmPtr->tm_mon], tmPtr->tm_year + 1900,
             tmPtr->tm_hour, tmPtr->tm_min, tmPtr->tm_sec);

    Ns_DStringAppend(pds, buf);
    return pds->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseHttpTime --
 *
 *	Take a time in one of three formats and convert it to a time_t. 
 *	Formats are: "Thursday, 10-Jun-93 01:29:59 GMT", "Thu, 10 
 *	Jan 1993 01:29:59 GMT", or "Wed Jun  9 01:29:59 1993 GMT"
 *
 * Results:
 *	0 if error, or standard time_t.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

time_t
Ns_ParseHttpTime(char *str)
{
    char      *s;
    struct tm  tm;
    time_t     t;

    if (str == NULL) {
        return 0;
    }

    /*
     * Find the comma after day-of-week
     *
     * Thursday, 10-Jun-93 01:29:59 GMT
     *         ^
     *         +-- s
     *
     * Thu, 10 Jan 1993 01:29:59 GMT
     *    ^
     *    +-- s
     */

    s = strchr(str, ',');
    if (s != NULL) {

	/*
	 * Advance S to the first non-space after the comma
	 * which should be the first digit of the day.
	 */
	
        s++;
        while (*s && *s == ' ') {
            s++;
	}

	/*
	 * Figure out which format it is in. If there is a hyphen, then
	 * it must be the first format.
	 */
	
        if (strchr(s, '-') != NULL) {
            if (strlen(s) < 18) {
                return 0;
            }

	    /*
	     * The format is:
	     *
	     * Thursday, 10-Jun-93 01:29:59 GMT
	     *           ^
	     *           +--s
	     */
	    
            tm.tm_mday = MakeNum(s);
            tm.tm_mon = MakeMonth(s + 3);
            tm.tm_year = MakeNum(s + 7);
            tm.tm_hour = MakeNum(s + 10);
            tm.tm_min = MakeNum(s + 13);
            tm.tm_sec = MakeNum(s + 16);
        } else {
            if ((int) strlen(s) < 20) {
                return 0;
            }

	    /*
	     * The format is:
	     *
	     * Thu, 10 Jan 1993 01:29:59 GMT
	     *      ^
	     *      +--s
	     */
	    
            tm.tm_mday = MakeNum(s);
            tm.tm_mon = MakeMonth(s + 3);
            tm.tm_year = (100 * MakeNum(s + 7) - 1900) + MakeNum(s + 9);
            tm.tm_hour = MakeNum(s + 12);
            tm.tm_min = MakeNum(s + 15);
            tm.tm_sec = MakeNum(s + 18);
        }
    } else {

	/*
	 * No commas, so it must be the third, fixed field, format:
	 *
	 * Wed Jun  9 01:29:59 1993 GMT
	 *
	 * Advance s to the first letter of the month.
	 */
	 
        s = str;
        while (*s && *s == ' ') {
            s++;
	}
        if ((int) strlen(s) < 24) {
            return 0;
        }
        tm.tm_mday = MakeNum(s + 8);
        tm.tm_mon = MakeMonth(s + 4);
        tm.tm_year = MakeNum(s + 22);
        tm.tm_hour = MakeNum(s + 11);
        tm.tm_min = MakeNum(s + 14);
        tm.tm_sec = MakeNum(s + 17);
    }

    /*
     * If there are any impossible values, then return an error.
     */
    
    if (tm.tm_sec < 0 || tm.tm_sec > 59 ||
        tm.tm_min < 0 || tm.tm_min > 59 ||
        tm.tm_hour < 0 || tm.tm_hour > 23 ||
        tm.tm_mday < 1 || tm.tm_mday > 31 ||
        tm.tm_mon < 0 || tm.tm_mon > 11 ||
        tm.tm_year < 70 || tm.tm_year > 120) {
        return 0;
    }
    tm.tm_isdst = 0;
#ifdef HAVE_TIMEGM
    t = timegm(&tm);
#else
    t = mktime(&tm) - timezone;
#endif
    return t;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclParseHttpTimeObjCmd --
 *
 *	Implements ns_parsehttptime as obj command. 
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
NsTclParseHttpTimeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    time_t time;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "httptime");
        return TCL_ERROR;
    }
    time = Ns_ParseHttpTime(Tcl_GetString(objv[1]));
    if (time == 0) {
	Tcl_AppendResult(interp, "invalid time: ",
		Tcl_GetString(objv[1]), NULL);
	return TCL_ERROR;
    }
    Tcl_SetLongObj(Tcl_GetObjResult(interp), time);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHttpTimeObjCmd --
 *
 *	Implements ns_httptime as obj command. 
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
NsTclHttpTimeObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_DString ds;
    int        itime;
    time_t     time;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "time");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &itime) != TCL_OK) {
        return TCL_ERROR;
    }
    time = (time_t) itime;
    Ns_DStringInit(&ds);
    Ns_HttpTime(&ds, &time);
    Tcl_SetResult(interp, Ns_DStringExport(&ds), (Tcl_FreeProc *) ns_free);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * MakeNum --
 *
 *	Convert a one or two-digit day into an integer, allowing a 
 *	space in the first position. 
 *
 * Results:
 *	An integer.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
MakeNum(char *s)
{
    if (*s >= '0' && *s <= '9') {
        return (10 * (*s - '0')) + (*(s + 1) - '0');
    } else {
        return *(s + 1) - '0';
    }
}


/*
 *----------------------------------------------------------------------
 *
 * MakeMonth --
 *
 *	Convert a three-digit abbreviated month name into a number; 
 *	e.g., Jan=0, Feb=1, etc. 
 *
 * Results:
 *	An integral month number. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
MakeMonth(char *s)
{
    int i;

    /*
     * Make sure it's capitalized like this:
     * "Jan"
     */
     
    *s = toupper(*s);
    *(s + 1) = tolower(*(s + 1));
    *(s + 2) = tolower(*(s + 2));

    for (i = 0; i < 12; i++) {
        if (!strncmp(month_names[i], s, 3)) {
            return i;
	}
    }
    return 0;
}


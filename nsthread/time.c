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
 *	Ns_Time support routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/time.c,v 1.3 2003/01/18 19:56:30 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetTime --
 *
 *	Get the current time value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ns_Time structure pointed to by timePtr is updated with currnet
 *	time.
 *
 *----------------------------------------------------------------------
 */

void
Ns_GetTime(Ns_Time *timePtr)
{
#ifdef _WIN32
/*
 * Number of 100 nanosecond units from 1/1/1601 to 1/1/1970
 */
#define EPOCH_BIAS  116444736000000000i64
    union {
	unsigned __int64    i;
	FILETIME	    s;
    } ft;

    GetSystemTimeAsFileTime(&ft.s);
    timePtr->sec = (time_t)((ft.i - EPOCH_BIAS) / 10000000i64);
    timePtr->usec =(long)((ft.i / 10i64) % 1000000i64);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    timePtr->sec = tv.tv_sec;
    timePtr->usec = tv.tv_usec;
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdjTime --
 *
 *	Adjust an Ns_Time so the values are in range.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ns_Time structure pointed to by timePtr is adjusted as needed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_AdjTime(Ns_Time *timePtr)
{
    if (timePtr->usec < 0) {
	timePtr->sec += (timePtr->usec / 1000000L) - 1;
	timePtr->usec = (timePtr->usec % 1000000L) + 1000000L;
    } else if (timePtr->usec > 1000000L) {
	timePtr->sec += timePtr->usec / 1000000L;
	timePtr->usec = timePtr->usec % 1000000L;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DiffTime --
 *
 *	Determine the difference between two Ns_Time structures.
 *
 * Results:
 *	-1, 0, or 1 if t1 is before, same, or after t0.
 *
 * Side effects:
 *	Ns_Time structure pointed to by timePtr is set with difference
 *	between the two given times.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DiffTime(Ns_Time *t1, Ns_Time *t0, Ns_Time *diffPtr)
{
    Ns_Time diff;

    if (diffPtr == NULL) {
	diffPtr = &diff;
    }
    if (t1->usec >= t0->usec) {
	diffPtr->sec = t1->sec - t0->sec;
	diffPtr->usec = t1->usec - t0->usec;
    } else {
	diffPtr->sec = t1->sec - t0->sec - 1;
	diffPtr->usec = 1000000L + t1->usec - t0->usec;
    }
    Ns_AdjTime(diffPtr);
    if (diffPtr->sec < 0) {
	return -1;
    } else if (diffPtr->sec == 0 && diffPtr->usec == 0) {
	return 0;
    } else {
	return 1;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_IncrTime --
 *
 *	Increment the given Ns_Time structure with the given number of
 *	seconds and microseconds.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Ns_Time structure pointed to by timePtr is incremented as needed.
 *
 *----------------------------------------------------------------------
 */

void
Ns_IncrTime(Ns_Time *timePtr, time_t sec, long usec)
{
    timePtr->usec += usec;
    timePtr->sec += sec;
    Ns_AdjTime(timePtr);
}

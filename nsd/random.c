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
 * random.c --
 *
 *	This file implements the "ns_rand" command.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/random.c,v 1.14 2003/01/18 19:24:20 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static Ns_ThreadProc CounterThread;
static unsigned long TrueRand(void);
static unsigned long Roulette(void);

/*
 * Static variables used by Ns_GenSeeds to generate array of random
 * by utilizing the random nature of the thread scheduler.
 */

static volatile unsigned long counter;  /* Counter in counting thread */
static volatile char fRun; /* Flag for counting thread outer loop. */
static volatile char fCount; /* Flag for counting thread inner loop. */
static Ns_Sema sema;	/* Semaphore that controls counting threads. */

/*
 * Critical section around initial and subsequent seed generation.
 */

static Ns_Cs lock;
static volatile int initialized;


/*
 *----------------------------------------------------------------------
 *
 * NsTclRandObjCmd --
 *
 *	This procedure implements the AOLserver Tcl 
 *
 *	    ns_rand ?maximum?
 *
 *	command.  
 *
 * Results:
 *	The Tcl result string contains a random number, either a
 *	double >= 0.0 and < 1.0 or a integer >= 0 and < max.
 *
 * Side effects:
 *	None external.
 *
 * Note:
 *	Interpreters share the static variables which randomizes the
 *	the random numbers even more.
 *
 *----------------------------------------------------------------------
 */

int
NsTclRandObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    double d;
    int max;
    Tcl_Obj *result;

    if (objc > 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?maximum?");
	return TCL_ERROR;
    }
    if (objc == 2) {
    	if (Tcl_GetIntFromObj(interp, objv[1], &max) != TCL_OK) {
	    return TCL_ERROR;
	} else if (max <= 0) {
	    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp), "invalid max \"", 
		    Tcl_GetString(objv[1]), "\": must be > 0", NULL);
	    return TCL_ERROR;
	}
    }
    result = Tcl_GetObjResult(interp);
    d = Ns_DRand();
    if (objc == 1) {
	Tcl_SetDoubleObj(result, d);
    } else {
	Tcl_SetIntObj(result, (int) (d * max));
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DRand --
 * 
 *	Return a random double value between 0 and 1.0.
 * 	
 * Results:
 *  	Random double.
 *
 * Side effects:
 * 	Will generate random seed on first call.
 *
 *----------------------------------------------------------------------
 */

double
Ns_DRand(void)
{
    if (!initialized) {
	Ns_CsEnter(&lock);
	if (!initialized) {
	    unsigned long seed;
	    Ns_GenSeeds(&seed, 1);
#ifdef HAVE_DRAND48
    	    srand48((long) seed);
#elif defined(HAVE_RANDOM)
    	    srandom((unsigned int) seed);
#else
    	    srand((unsigned int) seed);
#endif
	    initialized = 1;
	}
	Ns_CsLeave(&lock);
    }
#if HAVE_DRAND48
    return drand48();
#elif HAVE_RANDOM
    return ((double) random() / (LONG_MAX + 1.0));
#else
    return ((double) rand() / (RAND_MAX + 1.0));
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GenSeeds --
 * 
 *	Calculate an array of random seeds used by both Ns_DRand() and
 *  	the old SSL module.
 * 	
 * Results:
 *  	None.
 *
 * Side effects:
 * 	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_GenSeeds(unsigned long *seedsPtr, int nseeds)
{
    Ns_Thread thr;
    
    Ns_Log(Notice, "random: generating %d seed%s", nseeds,
	nseeds == 1 ? "" : "s");
    Ns_CsEnter(&lock);
    Ns_SemaInit(&sema, 0);
    fRun = 1;
    Ns_ThreadCreate(CounterThread, NULL, 0, &thr);
    while (nseeds-- > 0) {
    	*seedsPtr++ = TrueRand();
    }
    fRun = 0;
    Ns_SemaPost(&sema, 1);
    Ns_ThreadJoin(&thr, NULL);
    Ns_SemaDestroy(&sema);
    Ns_CsLeave(&lock);
}

/*
 *----------------------------------------------------------------------
 *
 * CounterThread --
 *
 *	Generate a random seed.  This routine runs as a separate thread 
 *	where it imcrements a counter some indeterminate number of times. 
 *	The assumption is that this thread runs for a sufficiently long time
 * 	to be preempted an arbitrary number of times by the kernel threads 
 *	scheduler.
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
CounterThread(void *ignored)
{
    while (fRun) {
        Ns_SemaWait(&sema);
        if (fRun) {
	    while (fCount) {
		counter++;
            }
        }
    }
}

/*
 *==========================================================================
 * AT&T Seed Generation Code
 *==========================================================================
 *
 * The authors of this software are Don Mitchell and Matt Blaze.
 *              Copyright (c) 1995 by AT&T.
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software and in all copies of the supporting
 * documentation for such software.
 *
 * This software may be subject to United States export controls.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY.  IN PARTICULAR, NEITHER THE AUTHORS NOR AT&T MAKE ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 */

#define MSEC_TO_COUNT 31  /* Duration of thread counting in milliseconds. */
#define ROULETTE_PRE_ITERS 10

static unsigned long
TrueRand(void)
{
    int i;

    for (i = 0; i < ROULETTE_PRE_ITERS; i++) {
	Roulette();
    }
    return Roulette();
}

static unsigned long
Roulette(void)
{
    static unsigned long ocount, randbuf;
    struct timeval tv;

    counter = 0;
    fCount = 1;
    Ns_SemaPost(&sema, 1);
    tv.tv_sec = (time_t)0;
    tv.tv_usec = MSEC_TO_COUNT * 1000;
    select(0, NULL, NULL, NULL, &tv);
    fCount = 0;
    counter ^= (counter >> 3) ^ (counter >> 6) ^ (ocount);
    counter &= 0x7;
    ocount = counter;
    randbuf = (randbuf<<3) ^ counter;
    return randbuf;
}

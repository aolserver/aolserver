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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/random.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#ifndef WIN32
#ifdef NO_RAND48
#define HAVE_RANDOM 1
#else
#define HAVE_RAND48 1
#endif
#endif

/*
 * Local functions defined in this file
 */

static void CounterThread(void);
static unsigned long raw_truerand(void);

/*
 * These static variables are used the GenerateSeed routine to generate
 * an array of random seeds.
 */

static volatile unsigned long Counter;  /* counter in counting thread */

static volatile char counterRun;	/* flag that controls the outer loop
					 * in the counting thread */

static volatile char keepCounting;	/* flag that controls the inner loop
					 * in the counting thread */

static Ns_Sema counterSema;	/* semaphore that controls the 
					 * counting thread */
/*
 *==========================================================================
 * Exported functions
 *==========================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * NsTclRandCmd --
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
NsTclRandCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    double d;
    int max;
	
    if (argc > 2) {
	Tcl_AppendResult(interp, argv[0], ": wrong number args: should be \"", 
	    argv[0], " ?maximum?\"", (char *) NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
    	if (Tcl_GetInt(interp, argv[1], &max) != TCL_OK) {
	    return TCL_ERROR;
	} else if (max <= 0) {
	    Tcl_AppendResult(interp, "invalid max \"", argv[1],
	    	"\": must be > 0", NULL);
	    return TCL_ERROR;
	}
    }
    d = Ns_DRand();
    if (argc == 1) {
    	Tcl_PrintDouble(interp, d, interp->result);
    } else {
    	sprintf(interp->result, "%d", (int) (d * max));
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
    static int initialized;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    unsigned long seed;
	    Ns_GenSeeds(&seed, 1);
#ifdef HAVE_RAND48
    	    srand48(seed);
#elif defined(HAVE_RANDOM)
    	    srandom((unsigned int) seed);
#else
    	    srand((unsigned int) seed);
#endif
	    initialized = 1;
	}
	Ns_MasterUnlock();
    }
#if HAVE_RAND48
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
    
    Ns_Log(Notice, "Ns_GenSeeds: generating %d random seed(s)...", nseeds);
    Ns_MasterLock();
    Ns_SemaInit(&counterSema, 0);
    counterRun = 1;
    Ns_ThreadCreate((Ns_ThreadProc *) CounterThread, NULL, 0, &thr);
    while (nseeds-- > 0) {
    	*seedsPtr++ = raw_truerand();
    }
    counterRun = 0;
    Ns_SemaPost(&counterSema, 1);
    Ns_MasterUnlock();
    Ns_ThreadJoin(&thr, NULL);
    Ns_SemaDestroy(&counterSema);
    Ns_Log(Notice, "Ns_GenSeeds: seed generation complete.");
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
 *
 * Side effects:
 *	None external. 
 *
 *----------------------------------------------------------------------
 */

static void
CounterThread(void)
{
    while (counterRun) {
        Ns_SemaWait(&counterSema);
        if (counterRun) {
	    while (keepCounting) {
		Counter++;
            }
        }
    }
}

/*
 *==========================================================================
 * AT&T Seed Generation Code
 *==========================================================================
 */

/*
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

#define MSEC_TO_COUNT 31  /* duration of thread counting in milliseconds */
#define ROULETTE_PRE_ITERS 10

static unsigned long roulette(void);

static unsigned long
raw_truerand(void)
{
    int i;

    for (i = 0; i < ROULETTE_PRE_ITERS; i++) {
	roulette();
    }
    return roulette();
}


static unsigned long
roulette(void)
{
    static unsigned long ocount, randbuf;
    struct timeval tv;

    Counter = 0;
    keepCounting = 1;
    Ns_SemaPost(&counterSema, 1);
    tv.tv_sec = (time_t)0;
    tv.tv_usec = MSEC_TO_COUNT * 1000;
    select(0, NULL, NULL, NULL, &tv);
    keepCounting = 0;
    Counter ^= (Counter >> 3) ^ (Counter >> 6) ^ (ocount);
    Counter &= 0x7;
    ocount = Counter;
    randbuf = (randbuf<<3) ^ Counter;
    return randbuf;
}

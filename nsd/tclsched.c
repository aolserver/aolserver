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
 * tclsched.c --
 *
 *	Implement scheduled procs in Tcl. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclsched.c,v 1.1.1.1 2000/05/02 13:48:22 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static Ns_Callback EvalScript;
static Ns_Callback FreeScript;
static Ns_SchedProc FreeSched;
static int ReturnValidId(Tcl_Interp *interp, int id, char *script);
static char *NewScript(char *proc, char *arg);


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclThread --
 *
 *	Run a Tcl script in a new thread. 
 *
 * Results:
 *	NS_OK. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclThread(Tcl_Interp *interp, char *script, Ns_Thread *thrPtr)
{
    Ns_ThreadCreate(NsTclThread, ns_strdup(script), 0, thrPtr);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclDetachedThread --
 *
 *	Run a Tcl script in a detached thread. 
 *
 * Results:
 *	NS_OK. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclDetachedThread(Tcl_Interp *interp, char *script)
{
    return Ns_TclThread(interp, script, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAt --
 *
 *	Implements ns_atsignal, ns_atshutdown, and ns_atexit commands.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

typedef void *(AtProc)(Ns_Callback *, void *);

static int
AtCmd(AtProc *procPtr, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " script | procname ?arg?\"", NULL);
        return TCL_ERROR;
    }
    if (procPtr == Ns_RegisterAtSignal) {
    	(*procPtr) (NsTclSignalProc, NewScript(argv[1], argv[2]));
    } else {
    	(*procPtr) (NsTclCallback, NewScript(argv[1], argv[2]));
    }
    return TCL_OK;
}
    
int
NsTclAtSignalCmd(ClientData ctx, Tcl_Interp *interp, int argc, char **argv)
{
    return AtCmd(Ns_RegisterAtSignal, interp, argc, argv);
}

int
NsTclAtShutdownCmd(ClientData ctx, Tcl_Interp *interp, int argc, char **argv)
{
    return AtCmd(Ns_RegisterShutdown, interp, argc, argv);
}

int
NsTclAtExitCmd(ClientData ctx, Tcl_Interp *interp, int argc, char **argv)
{
    return AtCmd(Ns_RegisterAtExit, interp, argc, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAfterCmd --
 *
 *	Implements ns_after.
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
NsTclAfterCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int id, seconds;
    char *script;

    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " seconds script\"", NULL);
	return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &seconds) != TCL_OK) {
	return TCL_ERROR;
    }
    script = NewScript(argv[2], NULL);
    id = Ns_After(seconds, (Ns_Callback *) NsTclSchedProc, script, FreeScript);
    return ReturnValidId(interp, id, script);
}
   

/*
 *----------------------------------------------------------------------
 *
 * NsTclCancelCmd --
 *
 *	Implements ns_unschedule_proc and ns_cancel commands.
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
NsTclCancelCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int id, ok;
    int cmd = (int) arg;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " id\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &id) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (cmd) {
    case 'u':
    case 'c':
    	ok = Ns_Cancel(id);
	break;
    case 'p':
    	ok = Ns_Pause(id);
	break;
    case 'r':
    	ok = Ns_Resume(id);
	break;
    }
    if (cmd != 'u') {
    	sprintf(interp->result, "%d", ok);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSchedDailyCmd --
 *
 *	Implements ns_schedule_daily. 
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
NsTclSchedDailyCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char    	*script;
    int          flags;
    int          first;
    int          id;
    int          hour, minute;

    /* 12 cases (arg count & number after cmd and -options are handled):
     *      0    1        2        3         4         5          6
     *   * cmd hour     minute   script                              (4 args)/3
     *   * cmd hour     minute   procname                            (4 args)/3
     *   * cmd hour     minute   procname  arg                       (5 args)/4
     *   * cmd -once    hour     minute    script                    (5 args)/3
     *   * cmd -once    hour     minute    procname                  (5 args)/3
     *   * cmd -once    hour     minute    procname  arg             (6 args)/4
     *   * cmd -thread  hour     minute    script                    (5 args)/3
     *   * cmd -thread  hour     minute    procname                  (5 args)/3
     *   * cmd -thread  hour     minute    procname  arg             (6 args)/4
     *   * cmd -once    -thread  hour      minute    script          (6 args)/3
     *   * cmd -once    -thread  hour      minute    procname        (6 args)/3
     *   * cmd -once    -thread  hour      minute    procname  arg   (7 args)/4
     */

    if (argc < 4 || argc > 7) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			 " ?-once? ?-thread? hour minute "
			 "{ script | procname ?arg? }\"", (char *) NULL);
        return TCL_ERROR;
    }

    first = 1;
    flags = 0;

    while (argc--) {
        if (strcmp(argv[first], "-thread") == 0) {
            flags |= NS_SCHED_THREAD;
        } else if (strcmp(argv[first], "-once") == 0) {
            flags |= NS_SCHED_ONCE;
        } else {
	    break;
        }
	first++;
    }

    /*
     * First is now the first argument that is not a switch.
     */

    if (Tcl_GetInt(interp, argv[first++], &hour) != TCL_OK) {
        return TCL_ERROR;
    }
    if (hour < 0 || hour > 23) {
        Tcl_AppendResult(interp, "invalid hour \"", argv[first - 1],
			 "\": should be >= 0 and <= 23", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[first++], &minute) != TCL_OK) {
        return TCL_ERROR;
    }
    if (minute < 0 || minute > 59) {
        Tcl_AppendResult(interp, "invalid minute \"", argv[first - 1],
			 "\": should be >= 0 and <= 59", NULL);
        return TCL_ERROR;
    }

    /*
     * Bear in mind that argc has been changed when counting switches,
     * so assume that there are no switches when reading the 4 here.
     */

    script = NewScript(argv[first], argv[first+1]);
    id = Ns_ScheduleDaily(NsTclSchedProc, script, flags,
			  hour, minute, FreeSched);
    return ReturnValidId(interp, id, script);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSchedWeeklyCmd --
 *
 *	Implements ns_sched_weekly.
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
NsTclSchedWeeklyCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		     char **argv)
{
    char        *script;
    int          flags;
    int          first;
    int          id;
    int          day, hour, minute;

    /* 12 cases (arg count & number after cmd and -options are handled):
     *     0    1        2        3        4     5       6      7
     *  * cmd day      hour     minute  script                       (5 args)/4
     *  * cmd day      hour     minute  proc                         (5 args)/4
     *  * cmd day      hour     minute  proc    arg                  (6 args)/5
     *  * cmd -once    day      hour    minute  script               (6 args)/4
     *  * cmd -once    day      hour    minute  proc                 (6 args)/4
     *  * cmd -once    day      hour    minute  proc    arg          (7 args)/5
     *  * cmd -thread  day      hour    minute  script               (6 args)/4
     *  * cmd -thread  day      hour    minute  proc                 (6 args)/4
     *  * cmd -thread  day      hour    minute  proc    arg          (7 args)/5
     *  * cmd -once    -thread  day     hour    minute  script       (7 args)/4
     *  * cmd -once    -thread  day     hour    minute  proc         (7 args)/4
     *  * cmd -once    -thread  day     hour    minute  proc    arg  (8 args)/5
     */

    if (argc < 5 || argc > 8) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			 " ?-once? ?-thread? day hour minute "
			 "{ script | procname ?arg? }\"", (char *) NULL);
        return TCL_ERROR;
    }

    first = 1;
    flags = 0;

    while (argc--) {
        if (strcmp(argv[first], "-thread") == 0) {
            flags |= NS_SCHED_THREAD;
        } else if (strcmp(argv[first], "-once") == 0) {
            flags |= NS_SCHED_ONCE;
        } else {
	    break;
        }
        first++;
    }

    /*
     * First is now the first argument that is not a switch.
     */

    if (Tcl_GetInt(interp, argv[first++], &day) != TCL_OK) {
        return TCL_ERROR;
    }
    if (day < 0 || day > 6) {
        Tcl_AppendResult(interp, "invalid day \"", argv[first - 1],
			 "\": should be >= 0 and <= 6", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[first++], &hour) != TCL_OK) {
        return TCL_ERROR;
    }
    if (hour < 0 || hour > 23) {
        Tcl_AppendResult(interp, "invalid hour \"", argv[first - 1],
			 "\": should be >= 0 and <= 23", NULL);
        return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[first++], &minute) != TCL_OK) {
        return TCL_ERROR;
    }
    if (minute < 0 || minute > 59) {
        Tcl_AppendResult(interp, "invalid minute \"", argv[first - 1],
			 "\": should be >= 0 and <= 59", NULL);
        return TCL_ERROR;
    }
    script = NewScript(argv[first], argv[first+1]);
    id = Ns_ScheduleWeekly(NsTclSchedProc, script, flags,
    	    	    	   day, hour, minute, FreeSched);
    return ReturnValidId(interp, id, script);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSchedCmd --
 *
 *	Implements ns_schedule_proc. 
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
NsTclSchedCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char        *script;
    int          interval;
    int          flags;
    int          first;
    int          id;

    /* 12 cases (arg count & number after cmd and -options are handled):
     *      0    1        2          3         4
     *   * cmd interval script                               (3 args)/2
     *   * cmd interval procname                             (3 args)/2
     *   * cmd interval procname  arg                        (4 args)/3
     *   * cmd -once    interval  script                     (4 args)/2
     *   * cmd -once    interval  procname                   (4 args)/2
     *   * cmd -once    interval  procname  arg              (5 args)/3
     *   * cmd -thread  interval  script                     (4 args)/2
     *   * cmd -thread  interval  procname                   (4 args)/2
     *   * cmd -thread  interval  procname  arg              (5 args)/3
     *   * cmd -once    -thread   interval  script           (5 args)/2
     *   * cmd -once    -thread   interval  procname         (5 args)/2
     *   * cmd -once    -thread   interval  procname arg     (6 args)/3
     */

    if (argc < 3 || argc > 6) {
        Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
			 " ?-once? ?-thread? interval "
			 "{ script | procname ?arg? }\"", 
			 (char *) NULL);
        return TCL_ERROR;
    }

    first = 1;
    flags = 0;

    while (argc--) {
        if (strcmp(argv[first], "-thread") == 0) {
            flags |= NS_SCHED_THREAD;
        } else if (strcmp(argv[first], "-once") == 0) {
            flags |= NS_SCHED_ONCE;
        } else {
	    break;
	}
        first++;
    }

    /*
     * First is now the first argument that is not a switch.
     */

    if (Tcl_GetInt(interp, argv[first++], &interval) != TCL_OK) {
        return TCL_ERROR;
    }
    script = NewScript(argv[first], argv[first+1]);
    id = Ns_ScheduleProcEx(NsTclSchedProc, script, flags, interval, FreeSched);
    return ReturnValidId(interp, id, script);
}


/*
 *----------------------------------------------------------------------
 *
 * ReturnValidId --
 *
 *	Update the interp result with the given schedule id if valid.
 *	Otherwise, free the script and leave an error in the interp.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
ReturnValidId(Tcl_Interp *interp, int id, char *script)
{
    if (id == NS_ERROR) {
        interp->result = "could not schedule procedure";
	FreeScript(script);
        return TCL_ERROR;
    }
    sprintf(interp->result, "%d", id);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * EvalScript --
 *
 *	This is a callback function that runs scheduled tcl 
 *	procedures registered with ns_schedule_*. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will run a Tcl proc/script. 
 *
 *----------------------------------------------------------------------
 */

static void
EvalScript(void *arg)
{
    Tcl_Interp *interp;
    
    interp = Ns_TclAllocateInterp(NULL);
    if (Tcl_Eval(interp, (char *) arg) != TCL_OK) {
    	Ns_TclLogError(interp);
    }
    Ns_TclDeAllocateInterp(interp);
}


/*
 *----------------------------------------------------------------------
 *
 * NewScript --
 *
 *	Create a new script.
 *
 * Results:
 *	Dynamic memory from Tcl_Concat.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
NewScript(char *proc, char *arg)
{
    char *argv[2];

    argv[0] = proc;
    argv[1] = arg;
    return Tcl_Concat(arg ? 2 : 1, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeScript --
 *
 *	Free a script created with NewScript.
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
FreeScript(void *arg)
{
    ckfree((char *) arg);
}

static void
FreeSched(void *arg, int id)
{
    FreeScript(arg);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSchedProc, NsTclSignalProc, NsTclCallback, NsTclThread --
 *
 *	External wrapper for various Tcl callbacks.
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
NsTclSchedProc(void *arg, int id)
{
    EvalScript(arg);
}

void
NsTclSignalProc(void *arg)
{
    EvalScript(arg);
}

void
NsTclCallback(void *arg)
{
    EvalScript(arg);
    FreeScript(arg);
}

void
NsTclThread(void *arg)
{
    EvalScript(arg);
    FreeScript(arg);
}

void
NsTclArgProc(Tcl_DString *dsPtr, void *arg)
{
     Tcl_DStringAppendElement(dsPtr, (char *) arg);
}


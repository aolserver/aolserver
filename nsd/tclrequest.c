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
 * tclrequest.c --
 *
 *	Routines for Tcl proc and ADP registered requests.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclrequest.c,v 1.4 2002/06/08 14:49:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define ARGS_UNKNOWN (-1)
#define ARGS_FAILED  (-2)

/*
 * The following structure defines the proc callback context.
 */

typedef struct {
    char *name;
    char *args;
    int	  nargs;
} Proc;

/*
 * Local functions defined in this file
 */

static Ns_OpProc ProcRequest;
static Ns_OpProc AdpRequest;
static Ns_FilterProc ProcFilter;
static Proc *NewProc(char *name, char *args);
static Ns_Callback FreeProc;
static void AppendConnId(Tcl_DString *dsPtr, Ns_Conn *conn);
static int RegisterFilter(NsInterp *itPtr, int when, char **argv);
static int GetNumArgs(Tcl_Interp *interp, Proc *procPtr);


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRequest --
 *
 *	Dummy up a direct call to TclProcRequest for a connection.
 *
 * Results:
 *	See TclDoOp.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclRequest(Ns_Conn *conn, char *name)
{
    Proc proc;

    proc.name = name;
    proc.args = NULL;
    proc.nargs = 0;
    return ProcRequest(&proc, conn);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterProcCmd --
 *
 *	Implements ns_register_proc. 
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
NsTclRegisterProcCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int adp)
{
    int         flags, idx;
    Proc       *procPtr;
    NsInterp   *itPtr = arg;
    char       *server, *method, *url;

    if (argc < 4 || argc > 7) {
badargs:
	Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " ?-noinherit? method url proc ?args?\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "-noinherit")) {
	if (argc < 5) {
	    goto badargs;
	}
	flags = NS_OP_NOINHERIT;
	idx = 2;
    } else {
	if (argc == 7) {
	    goto badargs;
	}
	flags = 0;
	idx = 1;
    }
    server = itPtr->servPtr->server;
    method = argv[idx++];
    url = argv[idx++];
    procPtr = NewProc(argv[idx], argv[idx+1]);
    Ns_RegisterRequest(server, method, url, ProcRequest, FreeProc,
	procPtr, flags);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterAdpCmd --
 *
 *	Implements ns_register_adp.
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
NsTclRegisterAdpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv, int adp)
{
    NsInterp   *itPtr = arg;
    char       *server, *method, *url, *file;

    if (argc != 4 && argc != 5) {
badargs:
	Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " ?-noinherit? method url file\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 5 && !STREQ(argv[1], "-noinherit")) {
	goto badargs;
    }
    server = itPtr->servPtr->server;
    method = argv[argc-3];
    url = argv[argc-2];
    file = ns_strdup(argv[argc-1]);
    Ns_RegisterRequest(server, method, url, AdpRequest, ns_free,
	file, argc == 5 ? NS_OP_NOINHERIT : 0);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnRegisterCmd --
 *
 *	Implements ns_unregister_proc and ns_unregister_adp commands.
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
NsTclUnRegisterCmd(ClientData arg, Tcl_Interp *interp, int argc,
		    char **argv)
{
    NsInterp *itPtr = arg;
    char *server = itPtr->servPtr->server;
    
    if (argc != 3 && argc != 4) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " ?-noinherit? method url\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 4 && !STREQ(argv[1], "-noinherit")) {
	Tcl_AppendResult(interp, "unknown flag \"", argv[1],
	    "\": should be -noinherit", NULL);
	return TCL_ERROR;
    }
    Ns_UnRegisterRequest(server, argv[argc-2], argv[argc-1], argc == 3 ? 1 : 0);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterFilter --
 *
 *	Implements ns_register_filter. 
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
NsTclRegisterFilterCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    int    largc;
    char **largv;
    int    when, i;

    if (argc != 5 && argc != 6) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " when method urlPattern script ?arg?\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_SplitList(interp, argv[1], &largc, &largv) != TCL_OK) {
        return TCL_ERROR;
    }
    when = 0;
    if (largc == 0) {
	Tcl_AppendResult(interp, "blank filter when specification", NULL);
    } else {
        for (i = 0; i < largc; ++i) {
            if (STREQ(largv[i], "preauth")) {
                when |= NS_FILTER_PRE_AUTH;
            } else if (STREQ(largv[i], "postauth")) {
                when |= NS_FILTER_POST_AUTH;
            } else if (STREQ(largv[i], "trace")) {
                when |= NS_FILTER_TRACE;
            } else {
                Tcl_AppendResult(interp, "unknown when \"", largv[i],
                    "\": should be preauth, postauth, or trace", NULL);
                when = 0;
                break;
            }
        }
        if (when) {
            RegisterFilter(itPtr, when, argv + 2);
        }
    }
    ckfree((char *) largv);
    if (when != 0) {
	return TCL_OK;
    } else {
	return TCL_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterTraceCmd --
 *
 *	Implements ns_register_trace 
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
NsTclRegisterTraceCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;

    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " method urlPattern script ?arg?\"", NULL);
        return TCL_ERROR;
    }
    return RegisterFilter(itPtr, NS_FILTER_VOID_TRACE, argv + 1);
}


/*
 *----------------------------------------------------------------------
 *
 * AdpRequest --
 *
 *	Ns_OpProc for registered ADP's.
 *
 * Results:
 *	See Ns_AdpRequest.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
AdpRequest(void *arg, Ns_Conn *conn)
{
    return Ns_AdpRequest(conn, (char *) arg);
}


/*
 *----------------------------------------------------------------------
 *
 * RequstProc --
 *
 *	Ns_OpProc for Tcl operations. Enters the header and form 
 *	connection sets, sets the current connection, concats the Tcl 
 *	command and args strings from the arg and evaluates the 
 *	resulting string. 
 *
 * Results:
 *	NS_OK or NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ProcRequest(void *arg, Ns_Conn *conn)
{
    Proc	*procPtr = arg;
    Tcl_Interp  *interp;
    int     	 cnt;
    Tcl_DString  cmd;
    int          retval;

    retval = NS_OK;
    Tcl_DStringInit(&cmd);
    Tcl_DStringAppendElement(&cmd, procPtr->name);
    interp = Ns_GetConnInterp(conn);

    /*
     * Build the procedure arguments.  Now that we don't require the
     * connId parameter, there are three cases to consider:
     *   - no args -> don't add anything after the command name
     *   - one arg -> just add the context (the arg specified at register time)
     *   - two+ args-> (backward compatibility), the connId and the context
     */

    cnt = GetNumArgs(interp, procPtr);
    if (cnt != 0) {
	if (cnt > 1) {
	    AppendConnId(&cmd, conn);
	}
	Tcl_DStringAppendElement(&cmd, procPtr->args ? procPtr->args : "");
    }
    if (Tcl_GlobalEval(interp, cmd.string) != TCL_OK) {
        Ns_TclLogError(interp);
        if (Ns_ConnResetReturn(conn) == NS_OK) {
            retval = Ns_ConnReturnInternalError(conn);
        }
    }
    Tcl_DStringFree(&cmd);
    return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * ProcFilter --
 *
 *	The callback for Tcl filters. Run the script. 
 *
 * Results:
 *	NS_OK, NS_FILTER_RETURN, or NS_FILTER_BREAK.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ProcFilter(void *arg, Ns_Conn *conn, int why)
{
    Proc	        *procPtr = arg;
    Tcl_DString          cmd;
    Tcl_Interp          *interp;
    int                  status;
    int                  cnt;
    const char		*result;

    Tcl_DStringInit(&cmd);

    /*
     * Start building the command with the proc name
     */
    
    Tcl_DStringAppendElement(&cmd, procPtr->name);
    interp = Ns_GetConnInterp(conn);

    /*
     * Now add the optional filter arg...
     */
    
    cnt = GetNumArgs(interp, procPtr);
    if (cnt > 1) {
	if (cnt > 2) {
	    AppendConnId(&cmd, conn);
	}
	Tcl_DStringAppendElement(&cmd, procPtr->args ? procPtr->args : "");
    }

    /*
     * Append the 'why'
     */
    
    switch (why) {
	case NS_FILTER_PRE_AUTH:
	    Tcl_DStringAppendElement(&cmd, "preauth");
	    break;
	case NS_FILTER_POST_AUTH:
	    Tcl_DStringAppendElement(&cmd, "postauth");
	    break;
	case NS_FILTER_TRACE:
	    Tcl_DStringAppendElement(&cmd, "trace");
	    break;
    }

    /*
     * Run the script.
     */
    
    Tcl_AllowExceptions(interp);
    status = Tcl_GlobalEval(interp, cmd.string);
    if (status != TCL_OK) {
	Ns_TclLogError(interp);
    }

    /*
     * Determine the filter result code.
     */

    result = Tcl_GetStringResult(interp);
    if (why == NS_FILTER_VOID_TRACE) {
	status = NS_OK;
    } else if (status != TCL_OK) {
	status = NS_ERROR;
    } else if (STREQ(result, "filter_ok")) {
	status = NS_OK;
    } else if (STREQ(result, "filter_break")) {
	status = NS_FILTER_BREAK;
    } else if (STREQ(result, "filter_return")) {
	status = NS_FILTER_RETURN;
    } else {
	Ns_Log(Warning, "tclfilter: %s return invalid result: %s",
	    procPtr->name, result);
	status = NS_ERROR;
    }
    Tcl_DStringFree(&cmd);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * GetNumArgs --
 *
 *	Get the number of arguments for a given callback, invoked
 *	the first time the callback is used.
 *
 * Results:
 *	Number of args or -1 on error.
 *
 * Side effects:
 *	Will invoke script to determine arg count.
 *
 *----------------------------------------------------------------------
 */

static int
GetNumArgs(Tcl_Interp *interp, Proc *procPtr)
{
    Tcl_Obj *objPtr;
    Tcl_DString ds;

    if (procPtr->nargs == ARGS_UNKNOWN) {
    	Tcl_DStringInit(&ds);
    	Tcl_DStringAppend(&ds, "llength [info args ", -1);
    	Tcl_DStringAppendElement(&ds, procPtr->name);
    	Tcl_DStringAppend(&ds, "]", 1);
	if (Tcl_Eval(interp, ds.string) != TCL_OK) {
	    procPtr->nargs = ARGS_FAILED;
	} else {
	    objPtr = Tcl_GetObjResult(interp);
	    if (Tcl_GetIntFromObj(interp, objPtr, &procPtr->nargs) != TCL_OK) {
	    	procPtr->nargs = ARGS_FAILED;
	    }
	}
	Tcl_DStringFree(&ds);
    }
    return procPtr->nargs;
}


/*
 *----------------------------------------------------------------------
 *
 * RegisterFilter --
 *
 *	Register a Tcl filter. 
 *
 * Results:
 *	TCL_OK. 
 *
 * Side effects:
 *	Will allocate memory for TclContext as well as strdup all the 
 *	arguments. 
 *
 *----------------------------------------------------------------------
 */

static int
RegisterFilter(NsInterp *itPtr, int when, char **argv)
{
    Proc	    *procPtr;
    char	    *server, *method, *url;

    server = itPtr->servPtr->server;
    method = argv[0];
    url = argv[1];
    procPtr = NewProc(argv[2], argv[3]);
    Ns_RegisterFilter(server, method, url, ProcFilter, when, procPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendConnId --
 *
 *	Append the tcl conn handle to a dstring. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Appends to the dstring. 
 *
 *----------------------------------------------------------------------
 */

static void
AppendConnId(Tcl_DString *dsPtr, Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    Tcl_DStringAppendElement(dsPtr, connPtr->idstr);
}


/*
 *----------------------------------------------------------------------
 *
 * NewProc, FreeProc --
 *
 *	Create or delete a Proc structure.
 *
 * Results:
 *	For NewProc, pointer to new Proc.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static Proc *
NewProc(char *name, char *args)
{
    Proc *procPtr;

    procPtr = ns_malloc(sizeof(Proc));
    procPtr->name = ns_strdup(name);
    procPtr->args = ns_strcopy(args);
    procPtr->nargs = ARGS_UNKNOWN;
    return procPtr;
}

static void
FreeProc(void *arg)
{
    Proc *procPtr = arg;
    
    ns_free(procPtr->name);
    if (procPtr->args != NULL) {
	ns_free(procPtr->args);
    }
    ns_free(procPtr);
}

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
 * tclop.c --
 *
 *	This file contains routines for the old CGI-style Tcl interface,
 *	with registered procs and whatnot.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/tclop.c,v 1.6.4.1 2002/10/28 23:15:50 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * This structure is used for translating filter return code strings
 * into numbers.
 */

typedef struct TclFilterReturnCode {
    char *name;
    int status;
} TclFilterReturnCode;

/*
 * This is the context passed to the callback for Tcl-registered filters.
 */

typedef struct {
    char *proc;
    char *args;
    int numSet;
    int numArgs;
} TclContext;

typedef struct AtClose {
    struct AtClose *nextPtr;
    char 	    script[4];
} AtClose;

/*
 * Local functions defined in this file
 */

static Ns_FilterProc TclFilterProc;
static void AppendConnId(Tcl_DString *pds, Ns_Conn *conn);
static int TclDoOp(void *arg, Ns_Conn *conn);
static Ns_Callback FreeCtx;
static Ns_Callback FreeAtClose;
static int RegisterFilter(Tcl_Interp *interp, int when, char **args);

/*
 * Static variables defined in this file
 */

static TclFilterReturnCode TclFilterReturnCodes[] = {
    {"filter_ok", NS_OK}, 
    {"filter_return", NS_FILTER_RETURN},
    {"filter_break", NS_FILTER_BREAK},
    {NULL, 0}};


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclEval --
 *
 *	Execute a tcl script. 
 *
 * Results:
 *	Tcl return code. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclEval(Ns_DString *pds, char *server, char *script)
{
    int         retcode;
    Tcl_Interp *interp;

    retcode = NS_ERROR;

    interp = Ns_TclAllocateInterp(server);
    if (interp != NULL) {
        if (Tcl_GlobalEval(interp, script) == TCL_OK) {
            Ns_DStringAppend(pds, interp->result);
            retcode = NS_OK;
        } else {
            Ns_DStringAppend(pds, Ns_TclLogError(interp));
        }
        Ns_TclDeAllocateInterp(interp);
    }
    return retcode;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclRequest --
 *
 *	Dummy up a direct call to TclDoOp for a connection.
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
Ns_TclRequest(Ns_Conn *conn, char *proc)
{
    TclContext ctx;

    ctx.proc = proc;
    ctx.args = NULL;
    ctx.numArgs = 0;
    ctx.numSet = 1;
    return TclDoOp(&ctx, conn);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclLogError --
 *
 *	Log the global errorInfo variable to the server log. 
 *
 * Results:
 *	Returns a pointer to the errorInfo. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclLogError(Tcl_Interp *interp)
{
    char *errorInfo;

    errorInfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo == NULL) {
        errorInfo = "";
    }
    Ns_Log(Error, "%s\n%s", interp->result, errorInfo);
    return errorInfo;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclLogErrorRequest --
 *
 *	Log both errorInfo and info about the HTTP request that led 
 *	to it. 
 *
 * Results:
 *	Returns a pointer to the errorInfo. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_TclLogErrorRequest(Tcl_Interp *interp, Ns_Conn *conn)
{
    char           *errorInfo;

    errorInfo = Tcl_GetVar(interp, "errorInfo", TCL_GLOBAL_ONLY);
    if (errorInfo == NULL) {
        errorInfo = interp->result;
    }
    Ns_Log(Error, "error for %s %s, "
           "User-Agent: %s, "
           "PeerAddress: %s\n%s", 
           conn->request->method, conn->request->url,
           Ns_SetIGet(conn->headers, "User-Agent"),
           Ns_ConnPeer(conn), errorInfo);
    return errorInfo;
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
NsTclRegisterTraceCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		       char **argv)
{
    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " method urlPattern script ?arg?\"", NULL);
        return TCL_ERROR;
    }
    
    return RegisterFilter(interp, NS_FILTER_VOID_TRACE, argv + 1);
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
NsTclRegisterFilterCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			char **argv)
{
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
            RegisterFilter(interp, when, argv + 2);
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
 * NsTclAtCloseCmd --
 *
 *	Implements ns_atclose. 
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
NsTclAtCloseCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char    *script;
    AtClose *atPtr;

    if (argc < 2 || argc > 3) {
	Tcl_AppendResult (interp, "wrong # args: should be \"",
			  argv[0], " { script | procname ?arg? }\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 2) {
	script = argv[1];
    } else {
	script = Tcl_Concat(2, argv+1);
    }

    /*
     * Push the script onto the head of the atclose list so scripts
     * will be called in reversed order when invoked.
     */

    atPtr = ns_malloc(sizeof(AtClose) + strlen(script));
    strcpy(atPtr->script, script);
    atPtr->nextPtr = NsTclGetData(interp, NS_TCL_ATCLOSE_KEY);
    NsTclSetData(interp, NS_TCL_ATCLOSE_KEY, atPtr, FreeAtClose);
    if (script != argv[1]) {
	ckfree(script);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnRegisterCmd --
 *
 *	Implements ns_unregister_proc. 
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
NsTclUnRegisterCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    int   inherit;
    char *method, *url;
    
    if (argc == 3) {
        inherit = 1;
        method = argv[1];
        url = argv[2];
    } else if ((argc == 4) && STREQ(argv[1], "-noinherit")) {
        inherit = 0;
        method = argv[2];
        url = argv[4];
    } else {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " method url\"", NULL);
        return TCL_ERROR;
    }
    Ns_UnRegisterRequest(Ns_TclInterpServer(interp), method, url, inherit);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRegisterCmd --
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
NsTclRegisterCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int         flags;
    int         next;
    TclContext *tclContext;
    char       *cmd;

    cmd = argv[0];
    if ((argc < 4) || (argc > 7)) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            cmd, " ?-noinherit? method url procName ?args?\"", NULL);
        return TCL_ERROR;
    }
    next = 1;
    flags = 0;
    while (next < argc && argv[next][0] == '-') {
        if (STREQ(argv[next], "-noinherit")) {
            flags |= NS_OP_NOINHERIT;
        } else {
            Tcl_AppendResult(interp, "unknown flag \"", argv[next],
                             "\":  should be -noinherit", NULL);
            return TCL_ERROR;
        }
        ++next;
    }
    --next;
    argc -= next;
    if ((argc < 4) || (argc > 5)) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            cmd, " ?-noinherit? method url procName ?args?\"", NULL);
        return TCL_ERROR;
    }
    argv += next;

    tclContext = ns_malloc(sizeof(TclContext));
    tclContext->proc = ns_strdup(argv[3]);
    tclContext->args = ns_strcopy(argv[4]);
    tclContext->numSet = 0;
    tclContext->numArgs = -1;
    Ns_RegisterRequest(Ns_TclInterpServer(interp), argv[1], argv[2], TclDoOp,
	FreeCtx, tclContext, flags);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FreeAtClose --
 *
 *	Dump a list of AtClose callbacks.
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
FreeAtClose(void *arg)
{
    AtClose *atPtr, *firstPtr;

    firstPtr = arg;
    while (firstPtr != NULL) {
	atPtr = firstPtr;
	firstPtr = atPtr->nextPtr;
	ns_free(atPtr);
    }
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
GetNumArgs(Tcl_Interp *interp, TclContext *ctxPtr)
{
    Tcl_DString ds;

    if (!ctxPtr->numSet) {
    	Tcl_DStringInit(&ds);
    	Tcl_DStringAppend(&ds, "llength [info args ", -1);
    	Tcl_DStringAppend(&ds, ctxPtr->proc, -1);
    	Tcl_DStringAppend(&ds, "]", 1);
	if (Tcl_Eval(interp, ds.string) != TCL_OK ||
	    Tcl_GetInt(interp, interp->result, &ctxPtr->numArgs) != TCL_OK) {
	    ctxPtr->numArgs = -1;
	}
	ctxPtr->numSet = 1;
	Tcl_DStringFree(&ds);
    }
    return ctxPtr->numArgs;
}


/*
 *----------------------------------------------------------------------
 *
 * TclDoOp --
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
TclDoOp(void *arg, Ns_Conn *conn)
{
    Tcl_Interp  *interp;
    TclContext  *ctx = (TclContext *) arg;
    int     	 cnt;
    Tcl_DString  cmd;
    int          retval;

    retval = NS_OK;
    Tcl_DStringInit(&cmd);
    Tcl_DStringAppendElement(&cmd, ctx->proc);
    interp = Ns_GetConnInterp(conn);

    /*
     * Build the procedure arguments.  Now that we don't require the
     * connId parameter, there are three cases to consider:
     *   - no args -> don't add anything after the command name
     *   - one arg -> just add the context (the arg specified at register time)
     *   - two+ args-> (backward compatibility), the connId and the context
     */

    cnt = GetNumArgs(interp, ctx);
    if (cnt == -1 || cnt == 1) {
	Tcl_DStringAppendElement(&cmd, ctx->args ? ctx->args : "");
    } else if (cnt >= 2) {
	AppendConnId(&cmd, conn);
	Tcl_DStringAppendElement(&cmd, ctx->args ? ctx->args : "");
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
 * FreeCtx --
 *
 *	Free a TclContext. 
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
FreeCtx(void *arg)
{
    TclContext *ctxPtr = (TclContext *) arg;
    
    ns_free(ctxPtr->proc);
    ns_free(ctxPtr->args);
    ns_free(ctxPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * TclFilterProc --
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
TclFilterProc(void *arg, Ns_Conn *conn, int why)
{
    TclContext          *filter = (TclContext *) arg;
    Tcl_DString          ds;     /* Holds Tcl command to run */
    Tcl_Interp          *interp;
    int                  status;
    int                  code, argCount;
    TclFilterReturnCode *current;

    Tcl_DStringInit(&ds);
    status = NS_OK;

    /*
     * Start building the command with the proc name
     */
    
    Tcl_DStringAppendElement(&ds, filter->proc);
    interp = Ns_GetConnInterp(conn);
    argCount = GetNumArgs(interp, filter);

    /*
     * Now add the optional filter arg...
     */
    
    if (argCount == -1 || argCount == 2) {
	Tcl_DStringAppendElement(&ds, filter->args ? filter->args : "");
    } else if (argCount >= 3) {
	AppendConnId(&ds, conn);
	Tcl_DStringAppendElement(&ds, filter->args ? filter->args : "");
    }

    /*
     * Append the 'why'
     */
    
    switch (why) {
	case NS_FILTER_PRE_AUTH:
	    Tcl_DStringAppendElement(&ds, "preauth");
	    break;
	case NS_FILTER_POST_AUTH:
	    Tcl_DStringAppendElement(&ds, "postauth");
	    break;
	case NS_FILTER_TRACE:
	    Tcl_DStringAppendElement(&ds, "trace");
	    break;
    }
    Tcl_AllowExceptions(interp);

    /*
     * Run the script.
     */
    
    status = Tcl_GlobalEval(interp, ds.string);
    Tcl_DStringFree(&ds);
    if (why == NS_FILTER_VOID_TRACE) {
	status = NS_OK;
    } else {
	/*
	 * Figure out what was returned and set the status accordingly.
	 */
	
	status = NS_ERROR;
	for (code=0; TclFilterReturnCodes[code].name != NULL; code++) {
	    current = TclFilterReturnCodes + code;
	    if (!strcasecmp(current->name, interp->result)) {
		status = current->status;
		break;
	    }
	}
	if (status == NS_ERROR) {
	    Ns_Log(Error, "tclop: invalid return code from filter proc '%s': "
		   "must be filter_ok, filter_return, or filter_break",
		   interp->result);
	}
    }
    return status;
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
RegisterFilter(Tcl_Interp *interp, int when, char **args)
{
    TclContext      *filter;
    char *method;
    char *URL;

    method = ns_strdup(args[0]);
    URL = ns_strdup(args[1]);
    filter = ns_malloc(sizeof(TclContext));
    filter->numSet = filter->numArgs = 0;
    filter->proc = ns_strdup(args[2]);
    filter->args = ns_strcopy(args[3]);

    Ns_RegisterFilter(Ns_TclInterpServer(interp), method, URL, 
		      TclFilterProc, when, filter);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRunAtclose --
 *
 *	Run the registered at-close scripts. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Will free the atclose scripts after execution. 
 *
 *----------------------------------------------------------------------
 */

void
NsTclRunAtClose(Tcl_Interp *interp)
{
    AtClose       *atPtr, *firstPtr;

    firstPtr = NsTclGetData(interp, NS_TCL_ATCLOSE_KEY);
    if (firstPtr != NULL) {
    	NsTclSetData(interp, NS_TCL_ATCLOSE_KEY, NULL, NULL);
    	atPtr = firstPtr;
    	while (atPtr != NULL) {
	    if (Tcl_GlobalEval(interp, atPtr->script) != TCL_OK) {
	    	Ns_TclLogError(interp);
	    }
	    atPtr = atPtr->nextPtr;
    	}
	FreeAtClose(firstPtr);
    }
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
AppendConnId(Tcl_DString *pds, Ns_Conn *conn)
{
    Tcl_DStringAppendElement(pds, NsTclConnId(conn));
}

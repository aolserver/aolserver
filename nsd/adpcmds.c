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
 * adpcmds.c --
 *
 *	ADP commands.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpcmds.c,v 1.13 2003/03/05 14:40:38 mpagenva Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int ReturnObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv,
			int exception);
static int EvalObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv,
		      int safe);


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpEvalObjCmd, NsTclAdpSafeEvalObjCmd --
 *
 *	(Safe) Evaluate an ADP string.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Page string is parsed and evaluated at current Tcl level in a
 *	new ADP call frame.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpEvalObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    return EvalObjCmd(arg, objc, objv, 0);
}

int
NsTclAdpSafeEvalObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj **objv)
{
    return EvalObjCmd(arg, objc, objv, 1);
}

static int
EvalObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv, int safe)
{
    if (objc < 2) {
	Tcl_WrongNumArgs(itPtr->interp, 1, objv, "page ?args ...?");
	return TCL_ERROR;
    }
    return NsAdpEval(itPtr, objc-1, objv+1, safe, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpIncludeObjCmd --
 *
 *	Process the Tcl _ns_adp_include commands to evaluate an
 *	ADP.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	File evaluated with output going to ADP buffer.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpIncludeObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		      Tcl_Obj **objv)
{
    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "file ?args ...?");
	return TCL_ERROR;
    }
    return NsAdpInclude(arg, Tcl_GetString(objv[1]), objc-1, objv+1);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpParseObjCmd --
 *
 *	Process the ns_adp_parse command to evaluate strings or
 *	ADP files at the current call frame level.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	ADP string or file output is return as Tcl result.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpParseObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    int         isfile, i, safe;
    char       *opt;
    char       *resvarname = NULL;
    char       *cwd = NULL;
    NsInterp   *itPtr = (NsInterp *)arg;
    Tcl_DString tds;
    bool        lcl_bufs = NS_FALSE;
    int         ret_status;
    
    if (objc < 2) {
badargs:
	Tcl_WrongNumArgs(interp, 1, objv,
                         "?-file|-string? ?-savedresult varname? ?-cwd path? arg ?arg ...?");
        return TCL_ERROR;
    }
    isfile = safe = 0;
    for (i = 1; i < objc; ++i) {
	opt = Tcl_GetString(objv[i]);
	if (STREQ(opt, "-global")) {
	    Tcl_SetResult(interp, "option -global unsupported", TCL_STATIC);
	    return TCL_ERROR;
	} else if (STREQ(opt, "-file")) {
	    isfile = 1;
        } else if (STREQ(opt, "-savedresult")) {
	    if (++i < objc) {
                resvarname = Tcl_GetString(objv[i]);
            } else {
                goto badargs;
            }
        } else if (STREQ(opt, "-cwd")) {
	    if (++i < objc) {
                cwd = Tcl_GetString(objv[i]);
            } else {
                goto badargs;
            }
	} else if (STREQ(opt, "-safe")) {
	    safe = 1;
	} else if (!STREQ(opt, "-string") && !STREQ(opt, "-local")) {
	    break;
	}
    }
    if (objc == i) {
	goto badargs;
    }
    objc -= i;
    objv += i;

    /*
     * Check the adp field in the nsInterp, and construct any support
     * Also, set the cwd.
     */
    if (itPtr->adp.typePtr == NULL) {
        Tcl_DStringInit(&tds);
        itPtr->adp.typePtr = &tds;
        lcl_bufs = NS_TRUE;
    }
    if (cwd != NULL) {
        itPtr->adp.cwd = cwd;
    }

    if (isfile) {
        ret_status = NsAdpSource(arg, objc, objv, resvarname);
    } else {
        ret_status = NsAdpEval(arg, objc, objv, safe, resvarname);
    }

    if (lcl_bufs) {
        itPtr->adp.responsePtr = NULL;
        itPtr->adp.typePtr = NULL;
        Tcl_DStringFree(&tds);
    }

    return ret_status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpAppendObjCmd, NsTclAdpPutsObjCmd --
 *
 *	Process the ns_adp_append and ns_adp_puts commands to append
 *	output.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output buffer is extended with given text, including a newline
 *	with ns_adp_puts.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpAppendObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    int i, len;
    char *s;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "string ?string ...?");
	return TCL_ERROR;
    }
    for (i = 1; i < objc; ++i) {
	s = Tcl_GetStringFromObj(objv[i], &len);
	Ns_DStringNAppend(itPtr->adp.outputPtr, s, len);
    }
    NsAdpFlush(itPtr);
    return TCL_OK;
}

int
NsTclAdpPutsObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    char *s;
    int len;

    if (objc != 2 && objc != 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "?-nonewline? string");
	return TCL_ERROR;
    }
    if (objc == 3) {
	s = Tcl_GetString(objv[1]);
	if (!STREQ(s, "-nonewline")) {
	    Tcl_AppendResult(interp, "invalid flag \"",
		s, "\": expected -nonewline", NULL);
	    return TCL_ERROR;
	}
    }
    s = Tcl_GetStringFromObj(objv[objc-1], &len);
    Ns_DStringNAppend(itPtr->adp.outputPtr, s, len);
    if (objc == 2) {
	Ns_DStringNAppend(itPtr->adp.outputPtr, "\n", 1);
    }
    NsAdpFlush(itPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpDirObjCmd --
 *
 *	Process the Tcl ns_adp_dir command to return the current ADP
 *  	directory.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpDirObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		  Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    if (itPtr->adp.cwd != NULL && *itPtr->adp.cwd) {   
    	Tcl_SetResult(interp, itPtr->adp.cwd, TCL_VOLATILE);
    } else {
	Tcl_SetResult(interp, "/", TCL_STATIC);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpReturnObjCmd, NsTclAdpBreakObjCmd, NsTclAdpAbortObjCmd --
 *
 *	Process the Tcl ns_adp_return, ns_adp_break and ns_adp_abort
 *	commands to halt page generation.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Break or abort exception is noted and will be handled in
 *  	AdpProc.
 *
 *----------------------------------------------------------------------
 */
 

int
NsTclAdpReturnObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    return ReturnObjCmd(arg, objc, objv, ADP_RETURN);
}

int
NsTclAdpBreakObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    return ReturnObjCmd(arg, objc, objv, ADP_BREAK);
}

int
NsTclAdpAbortObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    return ReturnObjCmd(arg, objc, objv, ADP_ABORT);
}

static int
ReturnObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv, int exception)
{
    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(itPtr->interp, 1, objv, "?retval?");
	return TCL_ERROR;
    }
    itPtr->adp.exception = exception;
    if (objc == 2) {
	Tcl_SetObjResult(itPtr->interp, objv[1]);
    }
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpTellObjCmd --
 *
 *	Process the Tcl ns_adp_tell commands to return the current
 *  	offset within the output buffer.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpTellObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(itPtr->adp.outputPtr->length));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpTruncObjCmd --
 *
 *	Process the Tcl ns_adp_trunc commands to truncate the output
 *  	buffer to the given length.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	Output buffer is truncated.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpTruncObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    int length;

    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?length?");
	return TCL_ERROR;
    }
    if (objc == 1) {
	length = 0;
    } else {
	if (Tcl_GetIntFromObj(interp, objv[1], &length) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (length < 0) {
	    Tcl_AppendResult(interp, "invalid length: ",
			     Tcl_GetString(objv[1]), NULL);
	    return TCL_ERROR;
	}
    }
    Ns_DStringTrunc(itPtr->adp.outputPtr, length);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpDumpObjCmd --
 *
 *	Process the Tcl ns_adp_dump commands to return the entire text
 *  	of the output buffer.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpDumpObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, itPtr->adp.outputPtr->string, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpArgcObjCmd --
 *
 *	Process the Tcl ns_adp_args commands to return the number of
 *  	arguments in the current ADP frame.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpArgcObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(itPtr->adp.objc));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpArgvObjCmd --
 *
 *	Process the Tcl ns_adp_args commands to return an argument (or
 *  	the entire list of arguments) within the current ADP frame.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpArgvObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    int i;

    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?index?");
	return TCL_ERROR;
    }
    if (objc == 1) {
	Tcl_SetListObj(Tcl_GetObjResult(interp), itPtr->adp.objc,
		       itPtr->adp.objv);
    } else {
    	if (Tcl_GetIntFromObj(interp, objv[1], &i) != TCL_OK) {
    	    return TCL_ERROR;
    	}
        if ((i + 1) <= itPtr->adp.objc) {
    	    Tcl_SetObjResult(interp, itPtr->adp.objv[i]);
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpBindArgsObjCmd --
 *
 *	Process the Tcl ns_adp_bind_args commands to copy arguements
 *  	from the current frame into local variables.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	One or more local variables are created.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpBindArgsObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    int i;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "varName ?varName ...?");
	return TCL_ERROR;
    }
    if (objc != itPtr->adp.objc) {
	Tcl_AppendResult(interp, "invalid #variables", NULL);
	return TCL_ERROR;
    }
    for (i = 1; i < objc; ++i) {
    	if (Tcl_ObjSetVar2(interp, objv[i], NULL, itPtr->adp.objv[i],
			   TCL_LEAVE_ERR_MSG) == NULL) {
    	    return TCL_ERROR;
    	}
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpExcepetionObjCmd --
 *
 *	Process the Tcl ns_adp_exception commands to return the current
 *  	exception state, ok, abort, or break.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpExceptionObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
			Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    char *exception;
    int   bool;

    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?varName?");
	return TCL_ERROR;
    }
    if (itPtr->adp.exception == ADP_OK) {
	bool = 0;
    } else {
	bool = 1;
    }
    Tcl_SetBooleanObj(Tcl_GetObjResult(interp), bool);
    if (objc == 2) {
	switch (itPtr->adp.exception) {
	case ADP_OK:
	    exception = "ok";
	    break;
	case ADP_BREAK:
	    exception = "break";
	    break;
	case ADP_ABORT:
	    exception = "abort";
	    break;
	case ADP_RETURN:
	    exception = "return";
            break;
	default:
	    exception = "unknown";
	    break;
	}
	if (Tcl_ObjSetVar2(interp, objv[1], NULL, Tcl_NewStringObj(exception, -1),
		           TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpStreamObjCmd --
 *
 *	Process the Tcl ns_adp_stream commands to enable streaming
 *  	output mode.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output will flush to client immediately.
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpStreamObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    NsAdpStream(itPtr);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpDebugCmd --
 *
 *	Process the Tcl ns_adp_debug command to connect to the TclPro
 *  	debugger if not already connected.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	See comments for DebugInit().
 *
 *----------------------------------------------------------------------
 */
 
int
NsTclAdpDebugCmd(ClientData arg, Tcl_Interp *interp, int argc,
	      char **argv)
{
    NsInterp *itPtr = arg;
    char *host, *port, *procs, buf[20];

    if (argc > 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?procs? ?host? ?port?\"", NULL);
	return TCL_ERROR;
    }
    procs = (argc > 1) ? argv[1] : NULL;
    host = (argc > 2) ? argv[2] : NULL;
    port = (argc > 3) ? argv[3] : NULL;
    if (NsAdpDebug(itPtr, host, port, procs) != TCL_OK) {
	Tcl_SetResult(interp, "could not initialize debugger", TCL_STATIC);
	return TCL_ERROR;
    }
    sprintf(buf, "%d", itPtr->adp.debugLevel);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpMimeTypeCmd --
 *
 *	Process the ns_adp_mimetype command to set or get the mime type
 *      returned upon completion of the parsed file.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *  	Potentially updates the mime type for this adp page.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpMimeTypeObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		       Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    
    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?mimetype?");
        return TCL_ERROR;
    }
    if (itPtr->adp.typePtr != NULL) {
	if (objc == 2) {
	    NsAdpSetMimeType(itPtr, Tcl_GetString(objv[1]));
	}
	Tcl_SetResult(interp, itPtr->adp.typePtr->string, TCL_VOLATILE);
    }
    return TCL_OK;
}

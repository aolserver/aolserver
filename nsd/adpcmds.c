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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpcmds.c,v 1.8 2001/12/05 22:46:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int ReturnCmd(NsInterp *itPtr, int argc, char **argv, int exception);


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpEvalCmd --
 *
 *	Evaluate an ADP string.
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
NsTclAdpEvalCmd(ClientData arg, Tcl_Interp *interp, int argc,
		char **argv)
{
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " page ?args ...?\"", NULL);
	return TCL_ERROR;
    }
    return NsAdpEval(arg, argv[1], argc-2, argv+2);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpIncludeCmd --
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
NsTclAdpIncludeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be: \"",
	    argv[0], " file ?args ...?\"", NULL);
	return TCL_ERROR;
    }
    return NsAdpInclude(arg, argv[1], argc-2, argv+2);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpParseCmd --
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
NsTclAdpParseCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int         isfile, i;
    char       *string;
    
    if (argc < 2) {
badargs:
        Tcl_AppendResult(interp, "wrong # args: should be \"",
              argv[0], " ?-file|-string? arg ?arg1 arg2 ...?\"", NULL);
        return TCL_ERROR;
    }
    isfile = 0;
    for (i = 1; i < argc; ++i) {
	if (STREQ(argv[i], "-global")) {
	    Tcl_SetResult(interp, "option -global unsupported", TCL_STATIC);
	    return TCL_ERROR;
	}
	if (STREQ(argv[i], "-file")) {
	    isfile = 1;
	} else if (!STREQ(argv[i], "-string") && !STREQ(argv[i], "-local")) {
	    break;
	}
    }
    if (argc == i) {
	goto badargs;
    }
    string = argv[i++];
    argc -= i;
    argv += i;
    if (isfile) {
        return NsAdpSource(arg, string, argc, argv);
    } else {
        return NsAdpEval(arg, string, argc, argv);
    }
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
NsTclAdpAppendObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
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
NsTclAdpPutsObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
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
 * NsTclAdpDirCmd --
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
NsTclAdpDirCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
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
 * ReturnCmd --
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
NsTclAdpReturnCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, argc, argv, ADP_RETURN);
}
int
NsTclAdpBreakCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, argc, argv, ADP_BREAK);
}
int
NsTclAdpAbortCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return ReturnCmd(arg, argc, argv, ADP_ABORT);
}

static int
ReturnCmd(NsInterp *itPtr, int argc, char **argv, int exception)
{
    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(itPtr->interp, "wrong # args: should be \"",
	    argv[0], " ?retval?\"", NULL);
	return TCL_ERROR;
    }
    itPtr->adp.exception = exception;
    if (argc == 2) {
	Tcl_SetResult(itPtr->interp, argv[1], TCL_VOLATILE);
    }
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpTellCmd --
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
NsTclAdpTellCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    char buf[20];

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    sprintf(buf, "%d", itPtr->adp.outputPtr->length);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpTruncCmd --
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
NsTclAdpTruncCmd(ClientData arg, Tcl_Interp *interp, int argc,
	      char **argv)
{
    NsInterp *itPtr = arg;
    int length;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?length?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
	length = 0;
    } else {
	if (Tcl_GetInt(interp, argv[1], &length) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (length < 0) {
	    Tcl_AppendResult(interp, "invalid length: ", argv[1], NULL);
	    return TCL_ERROR;
	}
    }
    Ns_DStringTrunc(itPtr->adp.outputPtr, length);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpDumpCmd --
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
NsTclAdpDumpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, itPtr->adp.outputPtr->string, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpArgcCmd --
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
NsTclAdpArgcCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    char buf[20];

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }
    sprintf(buf, "%d", itPtr->adp.argc+1);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpArgvCmd --
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
NsTclAdpArgvCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    int i;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?index?\"", NULL);
	return TCL_ERROR;
    }
    if (argc == 1) {
   	Tcl_AppendElement(interp, itPtr->adp.file);
    	for (i = 0; i < itPtr->adp.argc; ++i) {
     	    Tcl_AppendElement(interp, itPtr->adp.argv[i]);
    	}
    } else {
    	if (Tcl_GetInt(interp, argv[1], &i) != TCL_OK) {
    	    return TCL_ERROR;
    	}
	if (i == 0) {
   	    Tcl_AppendElement(interp, itPtr->adp.file);
	} else if (--i < itPtr->adp.argc) {
    	    Tcl_SetResult(interp, itPtr->adp.argv[i], TCL_VOLATILE);
	}
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpBindArgsCmd --
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
NsTclAdpBindArgsCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    int i;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " varName ?varName ...?\"", NULL);
	return TCL_ERROR;
    }
    for (i = 0; i < itPtr->adp.argc; ++i) {
    	arg = itPtr->adp.argv[i];
    	if (Tcl_SetVar(interp, argv[i+1], arg, TCL_LEAVE_ERR_MSG) == NULL) {
    	    return TCL_ERROR;
    	}
    }
    return TCL_OK;
}   	    


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpExcepetionCmd --
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
NsTclAdpExceptionCmd(ClientData arg, Tcl_Interp *interp, int argc,
		  char **argv)
{
    NsInterp *itPtr = arg;
    char *exception;

    if (argc != 1 && argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?varName?\"", NULL);
	return TCL_ERROR;
    }
    if (itPtr->adp.exception == ADP_OK) {
	Tcl_SetResult(interp, "0", TCL_STATIC);
    } else {
	Tcl_SetResult(interp, "1", TCL_STATIC);
    }
    if (argc == 2) {
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
	if (Tcl_SetVar(interp, argv[1], exception, 
		       TCL_LEAVE_ERR_MSG) == NULL) {
	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpStreamCmd --
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
NsTclAdpStreamCmd(ClientData arg, Tcl_Interp *interp, int argc,
	       char **argv)
{
    NsInterp *itPtr = arg;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], NULL);
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
NsTclAdpMimeTypeCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    
    if (argc != 1 && argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
              argv[0], " ?mimetype?\"", NULL);
        return TCL_ERROR;
    }
    if (itPtr->adp.typePtr != NULL) {
	if (argc == 2) {
	    NsAdpSetMimeType(itPtr, argv[1]);
	}
	Tcl_SetResult(interp, itPtr->adp.typePtr->string, TCL_VOLATILE);
    }
    return TCL_OK;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adpcmds.c,v 1.28 2006/06/26 00:28:02 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static int ExceptionObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv,
			int exception);
static int EvalObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv,
		      int flags);
static int GetFrame(ClientData arg, AdpFrame **framePtrPtr);
static int GetOutput(ClientData arg, Tcl_DString **dsPtrPtr);


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpIdentObjCmd --
 *
 *	Set RCS/CVS ident string for current file.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpIdentObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    AdpFrame *framePtr;

    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "ident");
	return TCL_ERROR;
    }
    if (GetFrame(arg, &framePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 2) {
    	if (framePtr->ident != NULL) {
	    Tcl_DecrRefCount(framePtr->ident);
    	}
    	framePtr->ident = objv[1];
    	Tcl_IncrRefCount(framePtr->ident);
    }
    if (framePtr->ident != NULL) {
	Tcl_SetObjResult(interp, framePtr->ident);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpCtlObjCmd --
 *
 *	ADP processing control.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Depends on subcommand.
 *
 *----------------------------------------------------------------------
 */

int
NsTclAdpCtlObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		   Tcl_Obj **objv)
{
    Tcl_Channel chan;
    NsInterp *itPtr = arg;
    char *id;
    static CONST char *opts[] = {
	"bufsize", "channel",
	"autoabort", "detailerror", "displayerror", "expire", "gzip",
	"nocache", "safe", "singlescript", "stricterror", "trace",
	"trimspace",
	NULL
    };
    enum {
	CBufSizeIdx, CChanIdx,
	CAbortIdx, CDetailIdx, CDispIdx, CExpireIdx, CGzipIdx,
	CNoCacheIdx, CSafeIdx, CSingleIdx, CStrictIdx, CTraceIdx,
	CTrimIdx
    };
    int opt, flag, old, new;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    switch (opt) {
    case CBufSizeIdx:
	if (objc != 2 && objc !=3 ) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?size?");
            return TCL_ERROR;
	}
	old = itPtr->adp.bufsize;
	if (objc == 3) {
	    if (Tcl_GetIntFromObj(interp, objv[2], &new) != TCL_OK) {
	    	return TCL_ERROR;
	    }
	    if (new < 0) {
		new = 0;
	    }
	    itPtr->adp.bufsize = new;
	}
	Tcl_SetIntObj(Tcl_GetObjResult(interp), old);
	break;

    case CChanIdx:
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "channel");
	    return TCL_ERROR;
	}
	id = Tcl_GetString(objv[2]);
	if (*id == '\0') {
	    if (itPtr->adp.chan != NULL) {
		if (NsAdpFlush(itPtr, 0) != TCL_OK) {
		    return TCL_ERROR;
		}
	    	itPtr->adp.chan = NULL;
	    }
	} else {
	    if (Ns_TclGetOpenChannel(interp, id, 1, 1, &chan) != TCL_OK) {
	    	return TCL_ERROR;
	    }
	    itPtr->adp.chan = chan;
	}
	break;
	
    default:
	/*
	 * Query or update an ADP option.
	 */

	if (objc != 2 && objc !=3 ) {
	    Tcl_WrongNumArgs(interp, 2, objv, "?bool?");
            return TCL_ERROR;
	}
	flag = 0;
    	switch (opt) {
	case CAbortIdx:
	    flag = ADP_AUTOABORT;
	    break;
	case CDetailIdx:
	    flag = ADP_DETAIL;
	    break;
	case CDispIdx:
	    flag = ADP_DISPLAY;
	    break;
	case CExpireIdx:
	    flag = ADP_EXPIRE;
	    break;
	case CGzipIdx:
	    flag = ADP_GZIP;
	    break;
	case CNoCacheIdx:
	    flag = ADP_NOCACHE;
	    break;
	case CSafeIdx:
	    flag = ADP_SAFE;
	    break;
	case CSingleIdx:
	    flag = ADP_SINGLE;
	    break;
	case CStrictIdx:
	    flag = ADP_STRICT;
	    break;
	case CTraceIdx:
	    flag = ADP_TRACE;
	    break;
	case CTrimIdx:
	    flag = ADP_TRIM;
	    break;
	}
	old = (itPtr->adp.flags & flag);
	if (objc == 3) {
	    if (Tcl_GetBooleanFromObj(interp, objv[2], &new) != TCL_OK) {
	    	return TCL_ERROR;
	    }
	    if (new) {
		itPtr->adp.flags |= flag;
	    } else {
		itPtr->adp.flags &= ~flag;
	    }
	}
	Tcl_SetBooleanObj(Tcl_GetObjResult(interp), old);
	break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpCompressObjCmd --
 *
 *	Process the Tcl ns_adp_compress command to enable on-the-fly
 *	gzip compression of ADP response.
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
NsTclAdpCompressObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    NsInterp *itPtr = arg;
    int compress = 1;

    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?boolean?");
        return TCL_ERROR;
    }
    if (objc >= 2
	    && Tcl_GetBooleanFromObj(interp, objv[1], &compress) != TCL_OK) {
        return TCL_ERROR;
    }
    if (compress) {
	itPtr->adp.flags |= ADP_GZIP;
    } else {
	itPtr->adp.flags &= ~ADP_GZIP;
    }
    return TCL_OK;
}


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
    return EvalObjCmd(arg, objc, objv, ADP_SAFE);
}

static int
EvalObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv, int flags)
{
    if (objc < 2) {
	Tcl_WrongNumArgs(itPtr->interp, 1, objv, "page ?args ...?");
	return TCL_ERROR;
    }
    return NsAdpEval(itPtr, objc-1, objv+1, flags, NULL);
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
    NsInterp *itPtr = arg;
    Tcl_DString *dsPtr;
    int i, skip, cache;
    Ns_Time *ttlPtr, ttl;
    char *file;

    if (objc < 2) {
badargs:
	Tcl_WrongNumArgs(interp, 1, objv, "?-cache ttl | -nocache? "
					  "file ?args ...?");
	return TCL_ERROR;
    }
    ttlPtr = NULL;
    skip = cache = 1;
    file = Tcl_GetString(objv[1]);
    if (STREQ(file, "-nocache")) {
	if (objc < 3) {
	    goto badargs;
	}
	cache = 0;
	skip = 2;
    } else if (STREQ(file, "-cache")) {
	if (objc < 4) {
	    goto badargs;
	}
	if (Ns_TclGetTimeFromObj(interp, objv[2], &ttl) != TCL_OK) {
	    return TCL_ERROR;
	}
	Ns_AdjTime(&ttl);
	if (ttl.sec < 0) {
	    Tcl_AppendResult(interp, "invalid ttl: ", Tcl_GetString(objv[2]),
			     NULL);
	    return TCL_ERROR;
	}
	ttlPtr = &ttl;
	skip = 3;
    }
    file = Tcl_GetString(objv[skip]);
    objc -= skip;
    objv += skip;

    /*
     * In cache refresh mode, append include command to the output
     * buffer. It will be compiled into the cached result.
     */

    if (!cache && itPtr->adp.refresh > 0) {
	if (GetOutput(arg, &dsPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
    	Tcl_DStringAppend(dsPtr, "<% ns_adp_include", -1);
    	for (i = 0; i < objc; ++i) {
	    Tcl_DStringAppendElement(dsPtr, Tcl_GetString(objv[i]));
    	}
    	Tcl_DStringAppend(dsPtr, "%>", 2);
	return TCL_OK;
    }
    return NsAdpInclude(arg, objc, objv, file, ttlPtr);
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
    NsInterp   *itPtr = arg;
    int         isfile, i, flags, result;
    char       *opt;
    char       *resvar = NULL;
    char       *cwd = NULL, *savecwd = NULL;
    
    if (objc < 2) {
badargs:
	Tcl_WrongNumArgs(interp, 1, objv,
                         "?-file|-string? ?-savedresult varname? ?-cwd path? "
			 "arg ?arg ...?");
        return TCL_ERROR;
    }
    isfile = flags = 0;
    for (i = 1; i < objc; ++i) {
	opt = Tcl_GetString(objv[i]);
	if (STREQ(opt, "-global")) {
	    Tcl_SetResult(interp, "option -global unsupported", TCL_STATIC);
	    return TCL_ERROR;
	} else if (STREQ(opt, "-file")) {
	    isfile = 1;
        } else if (STREQ(opt, "-savedresult")) {
	    if (++i < objc) {
                resvar = Tcl_GetString(objv[i]);
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
	    flags |= ADP_SAFE;
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

    if (cwd != NULL) {
	savecwd = itPtr->adp.cwd;
	itPtr->adp.cwd = cwd;
    }
    if (isfile) {
        result = NsAdpSource(arg, objc, objv, flags, resvar);
    } else {
        result = NsAdpEval(arg, objc, objv, flags, resvar);
    }
    if (cwd != NULL) {
	itPtr->adp.cwd = savecwd;
    }
    return result;
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
	if (NsAdpAppend(itPtr, s, len) != TCL_OK) {
	    return TCL_ERROR;
	}
    }
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
    if (NsAdpAppend(itPtr, s, len) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 2 && NsAdpAppend(itPtr, "\n", 1) != TCL_OK) {
	return TCL_ERROR;
    }
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
    Tcl_SetResult(interp, itPtr->adp.cwd, TCL_VOLATILE);
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
    return ExceptionObjCmd(arg, objc, objv, ADP_RETURN);
}

int
NsTclAdpBreakObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    return ExceptionObjCmd(arg, objc, objv, ADP_BREAK);
}

int
NsTclAdpAbortObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		    Tcl_Obj **objv)
{
    return ExceptionObjCmd(arg, objc, objv, ADP_ABORT);
}

static int
ExceptionObjCmd(NsInterp *itPtr, int objc, Tcl_Obj **objv, int exception)
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
    Tcl_DString *dsPtr;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    if (GetOutput(arg, &dsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(dsPtr->length));
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
    Tcl_DString *dsPtr;
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
    if (GetOutput(arg, &dsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringTrunc(dsPtr, length);
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
    Tcl_DString *dsPtr;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    if (GetOutput(arg, &dsPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetResult(interp, dsPtr->string, TCL_VOLATILE);
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
    AdpFrame *framePtr;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    if (GetFrame(arg, &framePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(framePtr->objc));
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
    AdpFrame *framePtr;
    int i;

    if (objc > 3) {
	Tcl_WrongNumArgs(interp, 1, objv, "?index? ?default?");
	return TCL_ERROR;
    }
    if (GetFrame(arg, &framePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 1) {
	Tcl_SetListObj(Tcl_GetObjResult(interp), framePtr->objc,
		       framePtr->objv);
    } else {
    	if (Tcl_GetIntFromObj(interp, objv[1], &i) != TCL_OK) {
    	    return TCL_ERROR;
    	}
        if ((i + 1) <= framePtr->objc) {
    	    Tcl_SetObjResult(interp, framePtr->objv[i]);
        } else {
            if (objc == 3) {
                Tcl_SetObjResult(interp, objv[2]);
            }
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
    AdpFrame *framePtr;
    int i;

    if (objc < 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "varName ?varName ...?");
	return TCL_ERROR;
    }
    if (GetFrame(arg, &framePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc != framePtr->objc) {
	Tcl_AppendResult(interp, "invalid #variables", NULL);
	return TCL_ERROR;
    }
    for (i = 1; i < objc; ++i) {
    	if (Tcl_ObjSetVar2(interp, objv[i], NULL, framePtr->objv[i],
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
 * NsTclAdpFlushObjCmd, NsTclAdpCloseObjCmd  --
 *
 *	Flush or close the current ADP output.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Output will flush to client immediately.
 *
 *----------------------------------------------------------------------
 */

static int
AdpFlushObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv, int stream)
{
    NsInterp *itPtr = arg;

    if (objc != 1) {
	Tcl_WrongNumArgs(interp, 1, objv, NULL);
	return TCL_ERROR;
    }
    return NsAdpFlush(itPtr, stream); 
}
 
int
NsTclAdpFlushObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    return AdpFlushObjCmd(arg, interp, objc, objv, 1);
}

int
NsTclAdpCloseObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj **objv)
{
    return AdpFlushObjCmd(arg, interp, objc, objv, 0);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclAdpStreamObjCmd --
 *
 *	Set ADP buffer size to 0, forcing all content to be sent to the
 *	client immediately on each append.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	See NsTclAdpFlushObjCmd.
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
    itPtr->adp.bufsize = 0;
    return NsTclAdpFlushObjCmd(arg, interp, objc, objv);
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
    Ns_Conn *conn = itPtr->conn;
    
    if (objc != 1 && objc != 2) {
	Tcl_WrongNumArgs(interp, 1, objv, "?mimetype?");
        return TCL_ERROR;
    }
    if (conn != NULL) {
	if (objc == 2) {
	    Ns_ConnSetType(conn, Tcl_GetString(objv[1]));
	}
	Tcl_SetResult(interp, Ns_ConnGetType(conn), TCL_VOLATILE);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpAppend --
 *
 *	Append content to the ADP output buffer, flushing the content
 *	if necessary.
 *
 * Results:
 *	TCL_ERROR if append and/or flush failed, TCL_OK otherwise.
 *
 * Side effects:
 *	Will set ADP error flag and leave an error message in
 *	the interp on flush failure.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpAppend(NsInterp *itPtr, char *buf, int len)
{
    Tcl_DString *bufPtr;

    if (GetOutput(itPtr, &bufPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringNAppend(bufPtr, buf, len);
    if (bufPtr->length > (int) itPtr->adp.bufsize
	    && NsAdpFlush(itPtr, 1) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetFrame --
 *
 *	Validate and return the current execution frame.
 *
 * Results:
 *	TCL_ERROR if no active frame, TCL_OK otherwise.
 *
 * Side effects:
 *	Will update given framePtrPtr with current frame.
 *
 *----------------------------------------------------------------------
 */

static int
GetFrame(ClientData arg, AdpFrame **framePtrPtr)
{
    NsInterp *itPtr = arg;

    if (itPtr->adp.framePtr == NULL) {
    	Tcl_SetResult(itPtr->interp, "no active adp", TCL_STATIC);
    	return TCL_ERROR;
    }
    *framePtrPtr = itPtr->adp.framePtr;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetOutput --
 *
 *	Validates and returns current output buffer.
 *
 * Results:
 *	TCL_ERROR if GetFrame fails, TCL_OK otherwise.
 *
 * Side effects:
 *	Will update given dsPtrPtr with buffer.
 *
 *----------------------------------------------------------------------
 */

static int
GetOutput(ClientData arg, Tcl_DString **dsPtrPtr)
{
    AdpFrame *framePtr;

    if (GetFrame(arg, &framePtr) != TCL_OK) {
	return TCL_ERROR;
    }
    *dsPtrPtr = framePtr->outputPtr;
    return TCL_OK;
}

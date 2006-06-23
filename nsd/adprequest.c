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
 * adprequest.c --
 *
 *	ADP connection request support.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adprequest.c,v 1.32 2006/06/23 02:18:20 shmooved Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static functions defined in this file.
 */


/*
 *----------------------------------------------------------------------
 *
 * NsAdpProc --
 *
 *	Check for a normal file and call Ns_AdpRequest.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpProc(void *arg, Ns_Conn *conn)
{
    Ns_Time *ttlPtr = arg;
    Ns_DString file;
    int status;

    Ns_DStringInit(&file);
    Ns_UrlToFile(&file, Ns_ConnServer(conn), conn->request->url);
    status = Ns_AdpRequestEx(conn, file.string, ttlPtr);
    Ns_DStringFree(&file);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRequest, Ns_AdpRequestEx -
 *
 *  	Invoke a file for an ADP request with an optional cache
 *	timeout.
 *
 * Results:
 *	A standard AOLserver request result.
 *
 * Side effects:
 *	Depends on code embedded within page.
 *
 *----------------------------------------------------------------------
 */

int
Ns_AdpRequest(Ns_Conn *conn, char *file)
{
    return Ns_AdpRequestEx(conn, file, NULL);
}

int
Ns_AdpRequestEx(Ns_Conn *conn, char *file, Ns_Time *ttlPtr)
{
    Conn	     *connPtr = (Conn *) conn;
    Tcl_Interp       *interp;
    NsInterp         *itPtr;
    char             *start, *type;
    Ns_Set           *query;
    NsServer	     *servPtr;
    Tcl_Obj	     *objv[2];
    int		      result;
    
    interp = Ns_GetConnInterp(conn);
    itPtr = NsGetInterpData(interp);

    /*
     * Verify the file exists.
     */

    if (access(file, R_OK) != 0) {
	return Ns_ConnReturnNotFound(conn);
    }

    /*
     * Set the output type based on the file type.
     */

    type = Ns_GetMimeType(file);
    if (type == NULL || STREQ(type, "*/*")) {
	type = NSD_TEXTHTML;
    }
    Ns_ConnSetType(conn, type);
    Ns_ConnSetStatus(conn, 200);

    /*
     * Enable TclPro debugging if requested.
     */

    servPtr = connPtr->servPtr;
    if ((itPtr->servPtr->adp.flags & ADP_DEBUG) &&
	STREQ(conn->request->method, "GET") &&
	(query = Ns_ConnGetQuery(conn)) != NULL) {
	itPtr->adp.debugFile = Ns_SetIGet(query, "debug");
    }

    /*
     * Include the ADP with the special start page and null args.
     */

    itPtr->adp.conn = conn;
    start = servPtr->adp.startpage ? servPtr->adp.startpage : file;
    objv[0] = Tcl_NewStringObj(start, -1);
    objv[1] = Tcl_NewStringObj(file, -1);
    Tcl_IncrRefCount(objv[0]);
    Tcl_IncrRefCount(objv[1]);
    result = NsAdpInclude(itPtr, 2, objv, start, ttlPtr);
    Tcl_DecrRefCount(objv[0]);
    Tcl_DecrRefCount(objv[1]);
    if (NsAdpFlush(itPtr, 0) != TCL_OK || result != TCL_OK) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpFlush --
 *
 *	Flush output to connection response buffer. 
 *
 * Results:
 *	TCL_ERROR if flush failed, TCL_OK otherwise.
 *
 * Side effects:
 *  	Output buffer is truncated in all cases.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpFlush(NsInterp *itPtr, int stream)
{
    Ns_Conn *conn;
    Tcl_Interp *interp = itPtr->interp;
    int len, wrote, result = TCL_ERROR, flags = itPtr->adp.flags;
    char *buf;

    /*
     * Verify output context.
     */

    if (itPtr->adp.conn == NULL && itPtr->adp.chan == NULL) {
	Tcl_SetResult(interp, "no adp output context", TCL_STATIC);
	return TCL_ERROR;
    }
    buf = itPtr->adp.output.string;
    len = itPtr->adp.output.length;

    /*
     * If enabled, trim leading whitespace if no content has been sent yet.
     */

    if ((flags & ADP_TRIM) && !(flags & ADP_FLUSHED)) {
	while (len > 0 && isspace(UCHAR(*buf))) {
	    ++buf;
	    --len;
	}
    }

    /*
     * Leave error messages if output is disabled or failed. Otherwise,
     * send data if there's any to send or stream is 0, indicating this
     * is the final flush call.
     */

    Tcl_ResetResult(interp);
    if (itPtr->adp.exception == ADP_ABORT) {
	Tcl_SetResult(interp, "adp flush disabled: adp aborted", TCL_STATIC);
    } else if (len == 0 && stream) {
	result = TCL_OK;
    } else {
	if (itPtr->adp.chan != NULL) {
	    while (len > 0) {
		wrote = Tcl_Write(itPtr->adp.chan, buf, len);
		if (wrote < 0) { 
	    	    Tcl_AppendResult(interp, "write failed: ",
				     Tcl_PosixError(interp), NULL);
		    break;
		}
		buf += wrote;
		len -= wrote;
	    }
	    if (len == 0) {
		result = TCL_OK;
	    }
	} else if (NsTclGetConn(itPtr, &conn) == TCL_OK) {
	    if (conn->flags & NS_CONN_CLOSED) {
		Tcl_SetResult(interp, "adp flush failed: connection closed",
			      TCL_STATIC);
	    } else {
	    	if (flags & ADP_GZIP) {
		    Ns_ConnSetGzipFlag(conn, 1);
	    	}
	    	if (!(flags & ADP_FLUSHED) && (flags & ADP_EXPIRE)) {
		    Ns_ConnCondSetHeaders(conn, "Expires", "now");
	    	}
	    	if (Ns_ConnFlush(itPtr->conn, buf, len, stream) == NS_OK) {
		    result = TCL_OK;
	    	} else {
	    	    Tcl_SetResult(interp,
				  "adp flush failed: connection flush error",
				  TCL_STATIC);
	    	}
	    }
	}
	itPtr->adp.flags |= ADP_FLUSHED;

	/*
	 * Raise an abort exception if autoabort is enabled.
	 */ 

    	if (result != TCL_OK && (flags & ADP_AUTOABORT)) {
	    Tcl_AddErrorInfo(interp, "\n    abort exception raised");
	    NsAdpLogError(itPtr);
	    itPtr->adp.exception = ADP_ABORT;
    	}
    }
    Tcl_DStringTrunc(&itPtr->adp.output, 0);

    if (!stream) {
        NsAdpReset(itPtr);
    }
    return result;
}

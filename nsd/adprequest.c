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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adprequest.c,v 1.19 2005/01/15 23:53:50 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
    Tcl_DString	      rds, tds;
    NsInterp         *itPtr;
    int               result;
    char             *type, *start;
    Ns_Set           *query;
    NsServer	     *servPtr;
    Tcl_Obj	     *objv[2];
    
    /*
     * Verify the file exists.
     */

    if (access(file, R_OK) != 0) {
	return Ns_ConnReturnNotFound(conn);
    }

    /*
     * Get the current connection's interp.
     */

    interp = Ns_GetConnInterp(conn);
    itPtr = NsGetInterp(interp);
    servPtr = itPtr->servPtr;

    /*
     * Set the response type and output buffers.
     */

    Tcl_DStringInit(&rds);
    Tcl_DStringInit(&tds);
    itPtr->adp.responsePtr = &rds;
    itPtr->adp.outputPtr = itPtr->adp.responsePtr;

    /*
     * Determine the output type.  This will set both the input
     * and output encodings by default.
     */

    type = Ns_GetMimeType(file);
    if (type == NULL || (strcmp(type, "*/*") == 0)) {
        type = NSD_TEXTHTML;
    }
    Ns_ConnSetType(conn, type);

    /*
     * Set the old conn variable for backwards compatibility.
     */

    Tcl_SetVar2(interp, "conn", NULL, connPtr->idstr, TCL_GLOBAL_ONLY);
    Tcl_ResetResult(interp);

    /*
     * Enable TclPro debugging if requested.
     */

    if ((servPtr->adp.flags & ADP_DEBUG) &&
	STREQ(conn->request->method, "GET") &&
	(query = Ns_ConnGetQuery(conn)) != NULL) {
	itPtr->adp.debugFile = Ns_SetIGet(query, "debug");
    }

    /*
     * Set default ADP response buffer size.
     */

    itPtr->adp.flags = 0;
    itPtr->adp.bufsize = itPtr->servPtr->adp.bufsize;

    /*
     * Queue the Expires header if enabled.
     */

    if (servPtr->adp.flags & ADP_EXPIRE) {
	Ns_ConnCondSetHeaders(conn, "Expires", "now");
    }

    /*
     * Include the ADP with the special start page and null args.
     */

    start = servPtr->adp.startpage ? servPtr->adp.startpage : file;
    objv[0] = Tcl_NewStringObj(start, -1);
    objv[1] = Tcl_NewStringObj(file, -1);
    Tcl_IncrRefCount(objv[0]);
    Tcl_IncrRefCount(objv[1]);
    if (NsAdpInclude(itPtr, start, 2, objv, ttlPtr) != TCL_OK &&
        itPtr->adp.exception == ADP_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_DecrRefCount(objv[0]);
    Tcl_DecrRefCount(objv[1]);

    /*
     * Flush the output if the connection isn't already closed.
     */

    result = NsAdpFlush(itPtr, 0);
    if (result != TCL_OK) {
	Ns_TclLogError(interp);
    }

    /*
     * Cleanup the per-thead ADP context.
     */

    itPtr->adp.flags = 0;
    itPtr->adp.outputPtr = NULL;
    itPtr->adp.responsePtr = NULL;
    itPtr->adp.exception = ADP_OK;
    itPtr->adp.debugLevel = 0;
    itPtr->adp.debugInit = 0;
    itPtr->adp.debugFile = NULL;
    Tcl_DStringFree(&rds);
    Tcl_DStringFree(&tds);

    if (result != TCL_OK) {
	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsFreeAdp --
 *
 *	Interp delete callback to free ADP resources.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

void
NsFreeAdp(NsInterp *itPtr)
{
    if (itPtr->adp.cache != NULL) {
	Ns_CacheDestroy(itPtr->adp.cache);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpGetBuf --
 *
 *	Return the current ADP buffer.
 *
 * Results:
 *	TCL_OK if there is a valid buffer, TCL_ERROR otherwise.
 *
 * Side effects:
 *	On success, updates given bufPtrPtr with current buffer. On error,
 *	formats and error message in given interp.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpGetBuf(NsInterp *itPtr, Tcl_DString **bufPtrPtr)
{
    if (itPtr->adp.outputPtr == NULL) {
	Tcl_SetResult(itPtr->interp, "no output buffer", TCL_STATIC);
	return TCL_ERROR;
    }
    *bufPtrPtr = itPtr->adp.outputPtr;
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

    if (NsAdpGetBuf(itPtr, &bufPtr) != TCL_OK) {
	return TCL_ERROR;
    }
    Ns_DStringNAppend(bufPtr, buf, len);
    if (bufPtr == itPtr->adp.responsePtr
	    && bufPtr->length > itPtr->adp.bufsize
	    && NsAdpFlush(itPtr, 1) != TCL_OK) {
	return TCL_ERROR;
    }
    return TCL_OK;
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
 *  	None.
 *
 *----------------------------------------------------------------------
 */

int
NsAdpFlush(NsInterp *itPtr, int stream)
{
    Tcl_DString *bufPtr = itPtr->adp.responsePtr;
    int status;

    if (bufPtr != NULL
	    && itPtr->adp.exception != ADP_ABORT
	    && !(itPtr->adp.flags & ADP_ERROR)
	    && (bufPtr->length > 0 || !stream)) {
	if (itPtr->adp.flags & ADP_GZIP) {
	    itPtr->nsconn.flags |= NS_CONN_GZIP;
	}
	status = Ns_ConnFlush(itPtr->conn,
			      bufPtr->string, bufPtr->length, stream);
    	Tcl_DStringTrunc(bufPtr, 0);
	if (status != NS_OK) {
	    itPtr->adp.flags |= ADP_ERROR;
	    Tcl_SetResult(itPtr->interp, "flush failed", TCL_STATIC);
	    return TCL_ERROR;
	}
    }
    return TCL_OK;
}

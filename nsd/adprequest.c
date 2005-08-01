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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adprequest.c,v 1.24 2005/08/01 20:27:22 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

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
    if ((servPtr->adp.flags & ADP_DEBUG) &&
	STREQ(conn->request->method, "GET") &&
	(query = Ns_ConnGetQuery(conn)) != NULL) {
	itPtr->adp.debugFile = Ns_SetIGet(query, "debug");
    }

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
    if (NsAdpInclude(itPtr, start, 2, objv, ttlPtr) != TCL_OK
	    && itPtr->adp.exception == ADP_OK) {
	Ns_TclLogError(interp);
    }
    Tcl_DecrRefCount(objv[0]);
    Tcl_DecrRefCount(objv[1]);
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
    Tcl_DStringFree(&itPtr->adp.output);
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
    Tcl_DString *bufPtr = itPtr->adp.framePtr->outputPtr;

    Ns_DStringNAppend(bufPtr, buf, len);
    if (bufPtr->length > itPtr->adp.bufsize
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
    Ns_Conn *conn;
    Tcl_Interp *interp = itPtr->interp;
    Tcl_DString *bufPtr = &itPtr->adp.output;
    int result = TCL_ERROR;

    if (itPtr->adp.exception != ADP_ABORT
	    && !(itPtr->adp.flags & ADP_ERROR)
	    && (bufPtr->length > 0 || !stream)) {
	if (itPtr->adp.chan != NULL) {
	    int len = bufPtr->length;
	    if (Tcl_Write(itPtr->adp.chan, bufPtr->string, len) != len) {
	    	Tcl_AppendResult(interp, "write failed: ",
				 Tcl_PosixError(interp), NULL);
	    } else {
		result = TCL_OK;
	    }
	} else if (NsTclGetConn(itPtr, &conn) == TCL_OK) {
	    if (itPtr->adp.flags & ADP_GZIP) {
	    	itPtr->conn->flags |= NS_CONN_GZIP;
	    }
	    if (Ns_ConnFlush(itPtr->conn, bufPtr->string,
				  bufPtr->length, stream) == NS_OK) {
		result = TCL_OK;
	    } else {
	    	Tcl_SetResult(interp, "flush failed", TCL_STATIC);
	    }
	}
    }
    Tcl_DStringTrunc(bufPtr, 0);
    if (result != TCL_OK) {
	itPtr->adp.flags |= ADP_ERROR;
    }
    return result;
}

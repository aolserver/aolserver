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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/adprequest.c,v 1.1 2001/03/12 22:06:14 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


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
    Ns_DString file;
    int status;

    Ns_DStringInit(&file);
    Ns_UrlToFile(&file, Ns_ConnServer(conn), conn->request->url);
    if (access(file.string, R_OK) != 0) {
	status = Ns_ConnReturnNotFound(conn);
    } else {
	status = Ns_AdpRequest(conn, file.string);
    }
    Ns_DStringFree(&file);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AdpRequest -
 *
 *  	Invoke a file for an ADP request.
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
    Conn	     *connPtr = (Conn *) conn;
    Tcl_Interp       *interp;
    NsInterp          *itPtr;
    int               status;
    char             *mimetype, *start, *argv[1];
    Ns_Set           *setPtr;
    NsServer	     *servPtr;
    
    /*
     * Get the current connection's interp.
     */

    interp = Ns_GetConnInterp(conn);
    itPtr = NsGetInterp(interp);
    servPtr = itPtr->servPtr;

    /*
     * Set the old conn variable for backwards compatibility.
     */

    Tcl_SetVar2(interp, "conn", NULL, connPtr->idstr, TCL_GLOBAL_ONLY);
    Tcl_ResetResult(interp);

    itPtr->adp.stream = 0;
    if (servPtr->adp.enabledebug &&
	STREQ(conn->request->method, "GET") &&
	(setPtr = Ns_ConnGetQuery(conn)) != NULL) {
	itPtr->adp.debugFile = Ns_SetIGet(setPtr, "debug");
    }
    mimetype = Ns_GetMimeType(file);
    if (mimetype == NULL || (strcmp(mimetype, "*/*") == 0)) {
        mimetype = "text/html";
    }
    NsAdpSetMimeType(itPtr, mimetype);

    /*
     * Include the ADP with the special start page and null args.
     */

    start = servPtr->adp.startpage ? servPtr->adp.startpage : file;
    argv[0] = NULL;
    (void) NsAdpInclude(itPtr, start, 0, argv);

    /*
     * Deal with any possible exceptions.
     */

    switch (itPtr->adp.exception) {
	case ADP_ABORT:

	    /*
	     * Abort is normally used after a call to a
	     * ns_return function so no response is sent here.
	     */

	    status = NS_OK;
	    break;

	case ADP_OVERFLOW:
	    Ns_Log(Error, "adp: stack overflow: '%s'", file);
	    status = Ns_ConnReturnInternalError(conn);
	    break;

	default:
	    if (servPtr->adp.enableexpire) {
		Ns_ConnCondSetHeaders(conn, "Expires", "now");
	    }
	    if (Ns_ConnResponseStatus(conn) == 0) {
                status = Ns_ConnReturnData(conn, 200,
					   itPtr->adp.output.string,
					   itPtr->adp.output.length,
					   itPtr->adp.mimetype);
	    } else {
                status = NS_OK;
            }
	    break;
    }

    /*
     * Cleanup the per-thead ADP context.
     */

    Ns_DStringTrunc(&itPtr->adp.output, 0);
    itPtr->adp.exception = ADP_OK;
    itPtr->adp.depth = 0;
    itPtr->adp.argc = 0;
    itPtr->adp.argv = NULL;
    itPtr->adp.cwd = NULL;
    itPtr->adp.file = NULL;
    itPtr->adp.debugLevel = 0;
    itPtr->adp.debugInit = 0;
    itPtr->adp.debugFile = NULL;
    NsAdpSetMimeType(itPtr, NULL);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpFlush --
 *
 *	Flush current output.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None unless streaming is enabled in which case content is
 *  	written directly out the connection.  Also, output is disabled
 *  	during inlined evalution where the output buffer is used to
 *  	accumulate the inlined evaluation result before being copied
 *  	to the interp.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpFlush(NsInterp *itPtr)
{
    if (itPtr->adp.stream &&
	itPtr->adp.evalLevel == 0 &&
	itPtr->adp.output.length > 0) {

	Ns_WriteConn(itPtr->conn, itPtr->adp.output.string,
		     itPtr->adp.output.length);
	Ns_DStringTrunc(&itPtr->adp.output, 0);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpStream --
 *
 *	Turn streaming mode on.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Headers and current data, if any, are flushed.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpStream(NsInterp *itPtr)
{
    if (!itPtr->adp.stream) {
    	itPtr->adp.stream = 1;
	Ns_ConnSetRequiredHeaders(itPtr->conn, itPtr->adp.mimetype, 0);
	Ns_ConnFlushHeaders(itPtr->conn, 200);
    }
    NsAdpFlush(itPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsAdpSetMimeType --
 *
 *	Sets the mime type for this adp context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Updates the mime type for this adp context, using ns_strcopy.
 *      Existing mime type is freed if not NULL.
 *
 *----------------------------------------------------------------------
 */

void
NsAdpSetMimeType(NsInterp *itPtr, char *mimetype)
{
    if (itPtr->adp.mimetype) {
        ns_free(itPtr->adp.mimetype);
    }
    itPtr->adp.mimetype = ns_strcopy(mimetype);
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
    Ns_DStringFree(&itPtr->adp.output);
    if (itPtr->adp.cache != NULL) {
	Ns_CacheDestroy(itPtr->adp.cache);
    }
    if (itPtr->adp.mimetype != NULL) {
        ns_free(itPtr->adp.mimetype);
    }
}

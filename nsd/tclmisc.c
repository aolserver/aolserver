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
 * tclcmds.c --
 *
 *	Implements a lot of Tcl API commands. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclmisc.c,v 1.5 2000/10/17 19:53:23 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define MULTIPART_FORMDATA "multipart/form-data"
#define BOUNDARY "boundary="
#define CONTENT_DISPOSITION "content-disposition"

/*
 * JPEG markers consist of one or more 0xFF bytes, followed by a marker
 * code byte (which is not an FF).  Here are the marker codes of interest
 * in this program.  (See jdmarker.c for a more complete list.)
 */

#define M_SOF0  0xC0		/* Start Of Frame N */
#define M_SOF1  0xC1		/* N indicates which compression process */
#define M_SOF2  0xC2		/* Only SOF0 and SOF1 are now in common use */
#define M_SOF3  0xC3
#define M_SOF5  0xC5
#define M_SOF6  0xC6
#define M_SOF7  0xC7
#define M_SOF9  0xC9
#define M_SOF10 0xCA
#define M_SOF11 0xCB
#define M_SOF13 0xCD
#define M_SOF14 0xCE
#define M_SOF15 0xCF
#define M_SOI   0xD8		/* Start Of Image (beginning of datastream) */
#define M_EOI   0xD9		/* End Of Image (end of datastream) */
#define M_SOS   0xDA		/* Start Of Scan (begins compressed data) */
#define M_COM   0xFE		/* COMment */

/*
 * Local functions defined in this file
 */

static int WordEndsInSemi(char *ip);
static int ConnReadChar(Ns_Conn *conn, char *buf);
static int ChanPutc(Tcl_Channel chan, char ch);
static int ChanGetc(Tcl_Channel chan);
static int CopyToChan(Ns_Conn *conn, Tcl_Channel chan, char *boundary);
static int CopyToDString(Ns_Conn *conn, Ns_DString *pdsData, char *boundary);
static int GetMultipartFormdata(Ns_Conn *conn, char *key, Tcl_Channel chan,
				Ns_DString *pdsFilename, Ns_Set *formdata);
static int InitShare(Tcl_Interp *interp, char *varName, char *script);
static unsigned int JpegRead2Bytes(Tcl_Channel chan);
static int JpegNextMarker(Tcl_Channel chan);
static int JpegSize(Tcl_Channel chan, int *wPtr, int *hPtr);
static void AppendThread(Ns_ThreadInfo *iPtr, void *arg);
static void AppendMutex(Ns_MutexInfo *iPtr, void *arg);

/*
 * Static variables defined in this file
 */


/*
 *----------------------------------------------------------------------
 *
 * NsTclConnId --
 *
 *	Return the connection ID string.
 *
 * Results:
 *	Pointer to conn string.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

#define CID_PREFIX "cns"

char *
NsTclConnId(Ns_Conn *conn)
{
    Conn *connPtr = (Conn *) conn;

    if (!connPtr->tclConn[0]) {
	sprintf(connPtr->tclConn, CID_PREFIX "%d", connPtr->id);
    }
    return connPtr->tclConn;
}


/*
 *----------------------------------------------------------------------
 *
 * NsIsIdConn --
 *
 *	Given an conn ID, could this be a conn ID? Assumes that conn IDs 
 *	start with "cns" as constructed in NsTclGetConn above.
 *
 * Results:
 *	Boolean. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsIsIdConn(char *connId)
{
    if (connId == NULL || *connId != 'c') {
	return NS_FALSE;
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * WordEndsInSemi --
 *
 *	Does this word end in a semicolon or a space? 
 *
 * Results:
 *	1 if semi, 0 if space. 
 *
 * Side effects:
 *	Behavior is undefined if string ends before either space or 
 *	semi. 
 *
 *----------------------------------------------------------------------
 */

static int
WordEndsInSemi(char *ip)
{
    while((*ip != ' ') && (*ip != ';')) {
        ip++;
    }
    if (*ip == ';') {
        return 1;
    } else {
        return 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ConnReadChar --
 *
 *	Read one character from the conn. Buffering should be done in 
 *	the socket driver to reduce inefficiency here. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Will but a byte in *buf. 
 *
 *----------------------------------------------------------------------
 */

static int
ConnReadChar(Ns_Conn *conn, char *buf)
{
    int bytes;
    
    while ((bytes = Ns_ConnRead(conn, buf, 1)) == 0) ;
    
    if (bytes == 1) {
        return NS_OK;
    } else {
        return NS_ERROR;
    }    
}


/*
 *----------------------------------------------------------------------
 *
 * ChanPutc --
 *
 *	Write a character to a channel. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ChanPutc(Tcl_Channel chan, char ch)
{
    if (Tcl_Write(chan, &ch, 1) != 1) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

static int
ChanGetc(Tcl_Channel chan)
{
    unsigned char buf[1];

    if (Tcl_Read(chan, (char *) buf, 1) != 1) {
	return EOF;
    }
    return (int) buf[0];
}


/*
 *----------------------------------------------------------------------
 *
 * CopyToChan --
 *
 *	Read content until boundary, writing data to channel. 
 *	Assumption: The boundary string cannot contain the character 
 *	it starts with anywhere in the remainder of the string. 
 *	Otherwise this function may keep reading right by the 
 *	boundary. Luckily, the boundaries we pass to it to handle the 
 *	file widget all start with \r and will not contain a \r. 
 *
 * Results:
 *	TCL result. 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CopyToChan(Ns_Conn *conn, Tcl_Channel chan, char *boundary)
{
    int  matchlen, boundarylen, i;
    char ch;

    boundarylen = strlen(boundary);
    matchlen = 0;

    while (ConnReadChar(conn, &ch) == NS_OK) {
      again:
        if (ch == boundary[matchlen]) {
            matchlen++;
            if (matchlen == boundarylen) {
                return NS_OK;
            }
            continue;
        } else if (matchlen == 0) {
	    if (ChanPutc(chan, ch) != TCL_OK) {
                return NS_ERROR;
            }
        } else {
            for (i=0; i<matchlen; i++) {
                if (ChanPutc(chan, boundary[i]) != TCL_OK) {
                    return NS_ERROR;
                }
            }
            matchlen = 0;
            goto again;
        }
    }
    Tcl_Flush(chan);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CopyToDString --
 *
 *	Read content until boundary, copying data to Dstring. 
 *	Assumption: Same as CopytoFP. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will read from channel. 
 *
 *----------------------------------------------------------------------
 */

static int
CopyToDString(Ns_Conn *conn, Ns_DString *pdsData, char *boundary)
{
    int  matchlen, boundarylen;
    char ch;
    
    boundarylen = strlen(boundary);
    matchlen = 0;
    while (ConnReadChar(conn, &ch) == NS_OK) {
      again:
        if (ch == boundary[matchlen]) {
            matchlen++;
            if (matchlen == boundarylen) {
                return NS_OK;
            }
            continue;
        } else if (matchlen == 0) {
            Ns_DStringNAppend(pdsData, &ch, 1);
        } else {
            Ns_DStringNAppend(pdsData, boundary, matchlen);
            matchlen = 0;
            goto again;
        }
    }
    
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetMultipartFormdata --
 *
 *	This function lets you get data from a form that contains a 
 *	Netscape FILE widget. Data associated with the key is written 
 *	to fd, and the rest of the form data is put into formdata. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
GetMultipartFormdata(Ns_Conn *conn, char *key, Tcl_Channel chan,
		     Ns_DString *pdsFilename, Ns_Set *formdata)
{
    Ns_Set     *headers;
    char       *contenttype, *p;
    Ns_DString  dsBoundary, dsLine, dsName;
    int         iRead;
    int         status;

    headers = Ns_ConnHeaders(conn);
    if (headers == NULL) {
        return NS_ERROR;
    }

    /*
     * Content-type: multipart/form-data, boundary=AaB03x
     */

    contenttype = Ns_SetIGet(headers, "content-type");
    
    /*
     * NS_ERROR if mimetype is not multipart/formdata
     */
    
    if ((contenttype == NULL) ||
	(strlen(contenttype) < sizeof(MULTIPART_FORMDATA)) ||
	(strncmp(contenttype, MULTIPART_FORMDATA, 
                    sizeof(MULTIPART_FORMDATA) - 1) != 0)) {
	
        return NS_ERROR;
    }

    /*
     * Look for "boundary=" string in content type.
     */
    
    p = strstr(contenttype, BOUNDARY);
    if (p == NULL) {
        return NS_ERROR;
    }
    p += sizeof(BOUNDARY) - 1;
    Ns_DStringInit(&dsBoundary);
    Ns_DStringVarAppend(&dsBoundary, "\r\n--", p, NULL);
    Ns_DStringInit(&dsLine);
    Ns_DStringInit(&dsName);

    /*
     * First line should be the boundary line
     */
    
    Ns_ConnReadLine(conn, &dsLine, &iRead);
    Ns_DStringTrunc(&dsLine, 0);

    status = NS_OK;

    while (Ns_ConnReadLine(conn, &dsLine, &iRead) == NS_OK) {

	/*
	 * Does this line begin with 'content-disposition'?
	 */
	
        if ((dsLine.length > sizeof(CONTENT_DISPOSITION)) &&
	    strncasecmp(CONTENT_DISPOSITION, dsLine.string, 
			sizeof(CONTENT_DISPOSITION) - 1) == 0) {

	    /*
	     * Snarf the name and filename from the content-
	     * disposition.
	     */
	    
            Ns_DStringTrunc(&dsName, 0);
            p = strstr(dsLine.string, "name=\"");
            if (p != NULL) {
                char *q;
                p += 6;
                q = strstr(p, "\"");
                if (q != NULL) {
                    Ns_DStringNAppend(&dsName, p, q-p);
                }
            }
            if (strcmp(key, dsName.string) == 0) {
                p = strstr(dsLine.string, "filename=\"");
                if (p != NULL) {
                    char *q;
                    p += 10;
                    q = strstr(p, "\"");
                    if (q != NULL) {
                        Ns_DStringNAppend(pdsFilename, p, q-p);
                    }
                }
            }
        } else if (dsLine.length == 0) {
	    /*
	     * This is not a content-disposition line; it is empty.
	     */
	    
            if (strcmp(key, dsName.string) == 0) {
		/*
		 * This is the data that we're looking for, so dump it
		 * out.
		 */
		
                if (CopyToChan(conn, chan, dsBoundary.string) != NS_OK) {
                    status = NS_ERROR;
                    break;
                }
            } else {
		/*
		 * This is not the data that we're looking for, so
		 * stick it the formdata.
		 */
		 
                Ns_DString dsData;
                Ns_DStringInit(&dsData);
                CopyToDString(conn, &dsData, dsBoundary.string);
                Ns_SetPut(formdata, dsName.string, dsData.string);
                Ns_DStringFree(&dsData);
            }
            Ns_DStringTrunc(&dsName, 0);
            Ns_ConnReadLine(conn,&dsLine,&iRead);
            if (strcmp(dsLine.string, "--") == 0) {
                break;
            }
        }
        Ns_DStringTrunc(&dsLine, 0);
    }
    Ns_DStringFree(&dsBoundary);
    Ns_DStringFree(&dsLine);
    Ns_DStringFree(&dsName);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * JpegRead2Bytes --
 *
 *	Read 2 bytes, convert to unsigned int. All 2-byte quantities 
 *	in JPEG markers are MSB first. 
 *
 * Results:
 *	The two byte value, or -1 on error. 
 *
 * Side effects:
 *	Advances file pointer. 
 *
 *----------------------------------------------------------------------
 */

static unsigned int
JpegRead2Bytes(Tcl_Channel chan)
{
    int c1, c2;
    
    c1 = ChanGetc(chan);
    c2 = ChanGetc(chan);
    if (c1 == EOF || c2 == EOF) {
	return -1;
    }
    return (((unsigned int) c1) << 8) + ((unsigned int) c2);
}


/*
 *----------------------------------------------------------------------
 *
 * JpegNextMarker --
 *
 *	Find the next JPEG marker and return its marker code. We 
 *	expect at least one FF byte, possibly more if the compressor 
 *	used FFs to pad the file. There could also be non-FF garbage 
 *	between markers. The treatment of such garbage is 
 *	unspecified; we choose to skip over it but emit a warning 
 *	msg. This routine must not be used after seeing SOS marker, 
 *	since it will not deal correctly with FF/00 sequences in the 
 *	compressed image data... 
 *
 * Results:
 *	The next marker code.
 *
 * Side effects:
 *	Will eat up any duplicate FF bytes.
 *
 *----------------------------------------------------------------------
 */

static int
JpegNextMarker(Tcl_Channel chan)
{
    int c;

    /*
     * Find 0xFF byte; count and skip any non-FFs.
     */
    
    c = ChanGetc(chan);
    while (c != EOF && c != 0xFF) {
	c = ChanGetc(chan);
    }
    if (c != EOF) {
	/*
	 * Get marker code byte, swallowing any duplicate FF bytes.
	 */
	
	do {
	    c = ChanGetc(chan);
	} while (c == 0xFF);
    }

    return c;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclVarCmd --
 *
 *	Implements ns_var; deprecated. (see ns_share)
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclVarCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry        *he;
    Tcl_HashSearch        hs;
    int                   new, code;
    static Ns_Mutex       lock;
    static Tcl_HashTable  table;
    static int            initialized = 0;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
                         argv[0], " command ?args?", NULL);
        return TCL_ERROR;
    }
    code = TCL_OK;

    Ns_MutexLock(&lock);
    if (!initialized) {
	Tcl_InitHashTable(&table, TCL_STRING_KEYS);
	initialized = 1;
    }
    if (STREQ(argv[1], "get") ||
	STREQ(argv[1], "exists") ||
	STREQ(argv[1], "unset")) {
	
        if (argc != 3) {
            Tcl_AppendResult(interp, "wrong # args:  should be \"",
                             argv[0], " ", argv[1], " var\"", NULL);
            code = TCL_ERROR;
        } else {
            he = Tcl_FindHashEntry(&table, argv[2]);
            if (he == NULL) {
                if (argv[1][0] == 'e') {
                    Tcl_SetResult(interp, "0", TCL_STATIC);
                } else {
                    Tcl_AppendResult(interp, "no such variable \"", argv[2],
				     "\"", NULL);
                    code = TCL_ERROR;
                }
            } else {
                if (argv[1][0] == 'e') {
                    Tcl_SetResult(interp, "1", TCL_STATIC);
                } else if (argv[1][0] == 'g') {
                    Tcl_SetResult(interp, (char *) Tcl_GetHashValue(he),
				  TCL_VOLATILE);
                } else {
                    ns_free(Tcl_GetHashValue(he));
                    Tcl_DeleteHashEntry(he);
                }
            }
        }
    } else if (STREQ(argv[1], "set")) {
        if (argc != 4) {
            Tcl_AppendResult(interp, "wrong # args:  should be \"",
                             argv[0], " ", argv[1], " var value\"", NULL);
            code = TCL_ERROR;
        } else {
            he = Tcl_CreateHashEntry(&table, argv[2], &new);
            if (!new) {
                ns_free(Tcl_GetHashValue(he));
            }
            Tcl_SetHashValue(he, ns_strdup(argv[3]));
            Tcl_SetResult(interp, argv[3], TCL_VOLATILE);
        }
    } else if (STREQ(argv[1], "list")) {
        he = Tcl_FirstHashEntry(&table, &hs);
        while (he != NULL) {
            Tcl_AppendElement(interp, (char *) Tcl_GetHashKey(&table, he));
            he = Tcl_NextHashEntry(&hs);
        }
    } else {
        Tcl_AppendResult(interp, "unknown command \"",
                         argv[1],
			 "\": should be get, set, unset, exists, or list",
			 NULL);
        code = TCL_ERROR;
    }
    Ns_MutexUnlock(&lock);

    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclShareCmd --
 *
 *	This procedure is invoked to process the "ns_share" Tcl command.
 *      It links the variables passed in to values that are shared.
 *	NOTE:  This procedure requires the NsTclShareVar() routine
 *	defined somewhere else.
 *
 * Results:
 *	A standard Tcl result value.
 *
 * Side effects:
 *	Very similar to "global"
 *
 *----------------------------------------------------------------------
 */

int
NsTclShareCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
		argv[0], " ?-init script? varName ?varName ...?\"", NULL);
	return TCL_ERROR;
    }
    
    if (STREQ(argv[1], "-init")) {
        if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args: should be \"",
			     argv[0], " -init script varName\"", NULL);
	    return TCL_ERROR;
        }
        if (NsTclShareVar(interp, argv[3]) != TCL_OK ||
    	    InitShare(interp, argv[3], argv[2]) != TCL_OK) {
	    return TCL_ERROR;
	}
    } else {
        for (argc--, argv++; argc > 0; argc--, argv++) {
	    if (NsTclShareVar(interp, *argv) != TCL_OK) {
		return TCL_ERROR;
	    }
        }
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * InitShare --
 *
 *	Helper routine to initialize a shared variable once, invoke
 *	by a call to ns_share -init. 
 *
 * Results:
 *	A standard Tcl result value.
 *
 * Side effects:
 *	Init script is evaluated once.
 *
 *----------------------------------------------------------------------
 */

static int
InitShare(Tcl_Interp *interp, char *varName, char *script)
{
    static Ns_Mutex lock;
    static Ns_Cond cond;
    static Tcl_HashTable sharedVars;
    static int initialized;
    Tcl_HashEntry *hPtr;
    int new, result;

    Ns_MutexLock(&lock);
    if (!initialized) {
	Tcl_InitHashTable(&sharedVars, TCL_STRING_KEYS);
	initialized = 1;
    }
    hPtr = Tcl_CreateHashEntry(&sharedVars, varName, &new);
    if (!new) {
	while (Tcl_GetHashValue(hPtr) == NULL) {
	    Ns_CondWait(&cond, &lock);
	}
        result = TCL_OK;
    } else {
	Ns_MutexUnlock(&lock);
	result = NsTclEval(interp, script);
	Ns_MutexLock(&lock);
	Tcl_SetHashValue(hPtr, (ClientData) 1);
	Ns_CondBroadcast(&cond);
    }
    Ns_MutexUnlock(&lock);
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLibraryCmd --
 *
 *	Implements ns_library. 
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
NsTclLibraryCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " library\"", NULL);
	return TCL_ERROR;
    }
    if (STREQ(argv[1], "private")) {
    	Tcl_SetResult(interp, nsconf.tcl.library, TCL_STATIC);
    } else if (STREQ(argv[1], "shared")) {
    	Tcl_SetResult(interp, nsconf.tcl.sharedlibrary, TCL_STATIC);
    } else {
	Tcl_AppendResult(interp, "unknown library \"",
	   argv[1], "\": should be private or shared", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Implements ns_module. --
 *
 *	Implements ns_module; deprecated. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclModuleCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char        **val;
    static char  *module;
    static char  *library;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " command ?arg?\"", NULL);
        return TCL_ERROR;
    }
    if (STREQ(argv[1], "clear")) {
        ns_free(module);
        ns_free(library);
        module = library = NULL;
    } else {
        if (STREQ(argv[1], "name")) {
            val = &module;
        } else if (STREQ(argv[1], "library")) {
            val = &library;
        } else {
            Tcl_AppendResult(interp, "unknown variable \"",
                             argv[1],
			     "\": should be clear, name, or library", NULL);
            return TCL_ERROR;
        }
        if (argv[2] != NULL) {
            ns_free(*val);
            *val = ns_strdup(argv[2]);
        }
        Tcl_SetResult(interp, *val, TCL_STATIC);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclInfoCmd --
 *
 *	Implements ns_info. 
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
NsTclInfoCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *elog;
    Tcl_DString ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
                         argv[0], " command", NULL);
        return TCL_ERROR;
    }

    Tcl_DStringInit(&ds);
    if (STREQ(argv[1], "argv0")) {
	Tcl_SetResult(interp, nsconf.argv0, TCL_STATIC);
    } else if (STREQ(argv[1], "nsd")) {
	Tcl_SetResult(interp, nsconf.nsd, TCL_STATIC);
    } else if (STREQ(argv[1], "pageroot")) {
	Tcl_SetResult(interp, Ns_PageRoot(NULL), TCL_STATIC);
    } else if (STREQ(argv[1], "server") || STREQ(argv[1], "servers")) {
        Tcl_SetResult(interp, nsServer, TCL_STATIC);
    } else if (STREQ(argv[1], "name")) {
        Tcl_SetResult(interp, Ns_InfoServerName(), TCL_STATIC);
    } else if (STREQ(argv[1], "config")) {
	Tcl_SetResult(interp, Ns_InfoConfigFile(), TCL_STATIC);
    } else if (STREQ(argv[1], "callbacks")) {
    	NsGetCallbacks(&ds);
	Tcl_DStringResult(interp, &ds);
    } else if (STREQ(argv[1], "sockcallbacks")) {
    	NsGetSockCallbacks(&ds);
	Tcl_DStringResult(interp, &ds);
    } else if (STREQ(argv[1], "scheduled")) {
    	NsGetScheduled(&ds);
	Tcl_DStringResult(interp, &ds);
    } else if (STREQ(argv[1], "locks")) {
	Ns_MutexEnum(AppendMutex, &ds);
	Tcl_DStringResult(interp, &ds);
    } else if (STREQ(argv[1], "threads")) {
	Ns_ThreadEnum(AppendThread, &ds);
	Tcl_DStringResult(interp, &ds);
    } else if (STREQ(argv[1], "log")) {
        elog = Ns_InfoErrorLog();
	Tcl_SetResult(interp, elog == NULL ? "STDOUT" : elog, TCL_STATIC);
    } else if (STREQ(argv[1], "platform")) {
	Tcl_SetResult(interp, Ns_InfoPlatform(), TCL_STATIC);
    } else if (STREQ(argv[1], "hostname")) {
	Tcl_SetResult(interp, Ns_InfoHostname(), TCL_STATIC);
    } else if (STREQ(argv[1], "address")) {
	Tcl_SetResult(interp, Ns_InfoAddress(), TCL_STATIC);
    } else if (STREQ(argv[1], "uptime")) {
        sprintf(interp->result, "%d", Ns_InfoUptime());
    } else if (STREQ(argv[1], "boottime")) {
        sprintf(interp->result, "%d", Ns_InfoBootTime());
    } else if (STREQ(argv[1], "pid")) {
        sprintf(interp->result, "%d", Ns_InfoPid());
    } else if (STREQ(argv[1], "version")) {
	Tcl_SetResult(interp, Ns_InfoVersion(), TCL_STATIC);
    } else if (STREQ(argv[1], "home")) {
	Tcl_SetResult(interp, Ns_InfoHome(), TCL_STATIC);
    } else if (STREQ(argv[1], "tcllib")) {
	Tcl_SetResult(interp, Ns_TclLibrary(), TCL_STATIC);
    } else if (STREQ(argv[1], "winnt")) {
#ifdef WIN32
        interp->result = "1";
#else
        interp->result = "0";
#endif
    } else if (STREQ(argv[1], "label")) {
	Tcl_SetResult(interp, Ns_InfoLabel(), TCL_STATIC);
    } else if (STREQ(argv[1], "builddate")) {
	Tcl_SetResult(interp, Ns_InfoBuildDate(), TCL_STATIC);
    } else if (STREQ(argv[1], "tag")) {
	Tcl_SetResult(interp, Ns_InfoTag(), TCL_STATIC);
    } else {
        Tcl_AppendResult(interp, "unknown command \"", argv[1],
                         "\":  should be "
                         "address, "
                         "argv0, "
                         "builddate, "
                         "callbacks, "
                         "config, "
                         "hostname, "
                         "label, "
                         "locks, "
                         "log, "
                         "name, "
                         "pageroot, "
			 "pid, "
                         "platform, "
                         "scheduled, "
                         "server, "
                         "sockcallbacks, "
                         "tag, "
                         "tcllib, "
                         "threads, "
                         "version, "
                         "or winnt", NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStripHtmlCmd --
 *
 *	Implements ns_striphtml. 
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
NsTclStripHtmlCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int   intag;
    int	  intspec;
    char *ip;
    char *op;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " page\"", NULL);
        return TCL_ERROR;
    }
    ip = argv[1];
    op = ip;
    intag = 0;
    intspec = 0;
    while (*ip != '\0') {
        if (*ip == '<') {
            intag = 1;
        } else if (intag && (*ip == '>')) {
            intag = 0;
        } else if (intspec && (*ip == ';')) {
            intspec = 0;		
        } else if (!intag && !intspec) {
            if (*ip == '&') {
                intspec=WordEndsInSemi(ip);
	    }
            if (!intspec) {
                *op++ = *ip;
	    }
        }
        ++ip;
    }
    *op = '\0';
    Tcl_SetResult(interp, argv[1], TCL_VOLATILE);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCryptCmd --
 *
 *	Implements ns_crypt. 
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
NsTclCryptCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[NS_ENCRYPT_BUFSIZE];

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " key salt\"", (char *) NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, Ns_Encrypt(argv[1], argv[2], buf), TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHrefsCmd --
 *
 *	Implments ns_hrefs. 
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
NsTclHrefsCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *p, *s, *e, *he, save;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " html\"", (char *) NULL);
        return TCL_ERROR;
    }

    p = argv[1];
    while ((s = strchr(p, '<')) && (e = strchr(s, '>'))) {
	++s;
	*e = '\0';
	while (*s && isspace(UCHAR(*s))) {
	    ++s;
	}
	if ((*s == 'a' || *s == 'A') && isspace(UCHAR(s[1]))) {
	    ++s;
	    while (*s && isspace(UCHAR(*s))) {
	    	++s;
	    }
	    if (!strncasecmp(s, "href", 4)) {
		s += 4;
		while (*s && isspace(UCHAR(*s))) {
		    ++s;
		}
		if (*s == '=') {
	    	    ++s;
	    	    while (*s && isspace(UCHAR(*s))) {
	    		++s;
	    	    }
		    he = NULL;
		    if (*s == '\'' || *s == '"') {
			he = strchr(s+1, *s);
			++s;
		    }
		    if (he == NULL) {
			he = s;
			while (!isspace(UCHAR(*he))) {
			    ++he;
			}
		    }
		    save = *he;
		    *he = '\0';
		    Tcl_AppendElement(interp, s);
		    *he = save;
		}
	    }
	}
	*e++ = '>';
	p = e;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLocalTimeCmd, NsTclGmTimeCmd --
 *
 *	Implements the ns_gmtime and ns_localtime commands. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
TmCmd(ClientData isgmt, Tcl_Interp *interp, int argc, char **argv)
{
    time_t     tt_now = time(NULL);
    char       buf[10];
    struct tm *ptm;

    if (argc != 1) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], "\"", NULL);
        return TCL_ERROR;
    }
    ptm = isgmt ? ns_gmtime(&tt_now) : ns_localtime(&tt_now);

    sprintf(buf, "%d", ptm->tm_sec);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_min);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_hour);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_mday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_mon);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_year);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_wday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_yday);
    Tcl_AppendElement(interp, buf);
    sprintf(buf, "%d", ptm->tm_isdst);
    Tcl_AppendElement(interp, buf);

    return TCL_OK;
}

int
NsTclGmTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd((ClientData) 1, interp, argc, argv);
}

int
NsTclLocalTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    return TmCmd(NULL, interp, argc, argv);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSleepCmd --
 *
 *	Tcl result. 
 *
 * Results:
 *	See docs. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSleepCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int seconds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
                         argv[0], " seconds\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &seconds) != TCL_OK) {
        return TCL_ERROR;
    }
    if (seconds < 0) {
        interp->result = "#seconds must be >= 0";
        return TCL_ERROR;
    }

    sleep(seconds);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclHTUUEncodeCmd --
 *
 *	Implements ns_uuencode 
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
NsTclHTUUEncodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char bufcoded[1 + (4 * 48) / 2];
    int  nbytes;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    nbytes = strlen(argv[1]);
    if (nbytes > 48) {
        Tcl_AppendResult(interp, "invalid string \"",
                         argv[1], "\": must be less than 48 characters", NULL);
        return TCL_ERROR;
    }
    Ns_HtuuEncode((unsigned char *) argv[1], nbytes, bufcoded);
    Tcl_SetResult(interp, bufcoded, TCL_VOLATILE);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * HTUUDecodecmd --
 *
 *	Implements ns_uudecode. 
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
NsTclHTUUDecodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int   n;
    char *decoded;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    n = strlen(argv[1]) + 3;
    decoded = ns_malloc(n);
    n = Ns_HtuuDecode(argv[1], (unsigned char *) decoded, n);
    decoded[n] = '\0';
    Tcl_SetResult(interp, decoded, (Tcl_FreeProc *) ns_free);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTimeCmd --
 *
 *	Implements ns_time. 
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
NsTclTimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    sprintf(interp->result, "%d", (int) time(NULL));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStrftimeCmd --
 *
 *	Implements ns_fmttime. 
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
NsTclStrftimeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char   *fmt;
    time_t  time;
    int     i;

    if (argc != 2 && argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " string\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[1], &i) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argv[2] != NULL) {
        fmt = argv[2];
    } else {
        fmt = "%c";
    }
    time = i;
    if (strftime(interp->result, TCL_RESULT_SIZE, fmt, 
		 ns_localtime(&time)) == 0) {
        sprintf(interp->result, "invalid time: %d", (int) time);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGetMultipartFormdataCmd --
 *
 *	Implements ns_get_multipart_formdata. 
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
NsTclGetMultipartFormdataCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			char **argv)
{
    Ns_DString   dsFilename;
    Ns_Conn     *conn;
    Tcl_Channel  chan;
    Ns_Set      *formdata;
    int          status;

    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " connId key fileId ?formdataSet?",
                         NULL);
        return TCL_ERROR;
    }
    status = NS_OK;
    conn = Ns_TclGetConn(interp);
    if (Ns_TclGetOpenChannel(interp, argv[3], 1, 1, &chan) == TCL_ERROR) {
        return TCL_ERROR;
    }
    formdata = Ns_SetCreate(NULL);
    status = TCL_OK;
    Ns_DStringInit(&dsFilename);
    if (GetMultipartFormdata(conn, argv[2], chan, &dsFilename,
			     formdata) != NS_OK) {
	
        Tcl_SetResult(interp, "Failed.", TCL_STATIC);
        Ns_SetFree(formdata);
        Ns_DStringFree(&dsFilename);
        status = TCL_ERROR;
    } else {
	/*
	 * If a set was provided, put the data in that. If not, throw it
	 * away.
	 */
	
        if (argc == 5) {
            Ns_TclEnterSet(interp, formdata, 1);
            Tcl_SetVar(interp, argv[4], interp->result, 0);
        } else {
            Ns_SetFree(formdata);
        }
        Tcl_SetResult(interp, Ns_DStringExport(&dsFilename), 
                      (Tcl_FreeProc *) ns_free);
    }
    Ns_DStringFree(&dsFilename);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCrashCmd --
 *
 *	Crash the server to test exception handling.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will segfault.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCrashCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char           *death;

    death = NULL;
    *death = 1;

    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGifSizeCmd --
 *
 *	Implements ns_gifsize. 
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
NsTclGifSizeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv) 
{
    int fd;
    unsigned char  buf[0x300];
    int depth, colormap, dx, dy;

    /*
     * Get the height and width of a GIF
     *
     * Returns a list of 2 elements, width and height 
     */
  
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " gif\"", NULL);
        return TCL_ERROR;
    }

    /*
     * Open the file.
     */
    
    fd = open(argv[1],O_RDONLY|O_BINARY);

    if (fd == -1) {
        Tcl_AppendResult(interp, "Could not open file \"", argv[1],"\"",NULL);
        return TCL_ERROR;
    }

    /*
     * Read the GIF version number
     */
    
    if (read(fd, buf, 6) == -1) {
      badfile:
        close(fd);
        Tcl_AppendResult(interp, "Bad file \"", argv[1],"\"",NULL);
        return TCL_ERROR;
    }

    if (strncmp((char *) buf, "GIF87a", 6) && 
	strncmp((char *) buf, "GIF89a", 6)) {
	
        goto badfile;
    }

    if (read(fd, buf, 7) == -1) {
        goto badfile;
    }

    depth = 1 << ((buf[4] & 0x7) + 1);
    colormap = (buf[4] & 0x80 ? 1 : 0);

    if (colormap) {
        if (read(fd,buf,3*depth) == -1) {
            goto badfile;
        }
    }

  outerloop:
    if (read(fd, buf, 1) == -1) {
        goto badfile;
    }

    if (buf[0] == '!') {
        unsigned char count;
	
        if (read(fd, buf, 1) == -1) {
            goto badfile;
        }
      innerloop:
        if (read(fd, (char *) &count, 1) == -1) {
            goto badfile;
        }
        if (count == 0) {
            goto outerloop;
        }
        if (read(fd, buf, count) == -1) {
            goto badfile;
        }
        goto innerloop;
    } else if (buf[0] != ',') {
        goto badfile;
    }

    if (read(fd,buf,9) == -1) {
        goto badfile;
    }

    dx = 0x100 * buf[5] + buf[4];
    dy = 0x100 * buf[7] + buf[6];

    sprintf((char*)buf,"%d",dx);
    Tcl_AppendElement(interp,(char*)buf);

    sprintf((char*)buf,"%d",dy);
    Tcl_AppendElement(interp,(char*)buf);

    close(fd);

    return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * NsTclJpegSizeCmd --
 *
 *	Implements ns_jpegsize. 
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
NsTclJpegSizeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int   code, w, h;
    Tcl_Channel chan;

    if (argc != 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " file\"", NULL);
	return TCL_ERROR;
    }

    chan = Tcl_OpenFileChannel(interp, argv[1], "r", 0);
    if (chan == NULL) {
	Tcl_AppendResult(interp, "could not open \"",
	    argv[1], "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    code = JpegSize(chan, &w, &h);
    Tcl_Close(interp, chan);

    if (code != TCL_OK) {
    	Tcl_AppendResult(interp, "invalid jpeg file: ", argv[1], NULL);
	return TCL_ERROR;
    }

    sprintf(interp->result, "%d %d", w, h);
    return TCL_OK;
}


static int
JpegSize(Tcl_Channel chan, int *wPtr, int *hPtr)
{
    unsigned int i, w, h;

    if (ChanGetc(chan) == 0xFF && ChanGetc(chan) == M_SOI) {
	while (1) {
	    i = JpegNextMarker(chan);
	    if (i == EOF || i == M_SOS || i == M_EOI) {
	    	break;
	    }
	    if ((i >> 4) == 0xC) {
		if (JpegRead2Bytes(chan) != EOF && ChanGetc(chan) != EOF
		    && (h = JpegRead2Bytes(chan)) != EOF
		    && (w = JpegRead2Bytes(chan)) != EOF) {
		    *wPtr = w;
		    *hPtr = h;
		    return TCL_OK;
		}
		break;
	    }
	    i = JpegRead2Bytes(chan);
	    if (i < 2 || Tcl_Seek(chan, i-2, SEEK_CUR) == -1) {
	    	break;
	    }
	}
    }
    return TCL_ERROR;
}


static void
AppendMutex(Ns_MutexInfo *iPtr, void *arg)
{
    Tcl_DString *dsPtr = arg;
    char buf[100], *owner;

    owner = iPtr->owner;
    Tcl_DStringStartSublist(dsPtr);
    Tcl_DStringAppendElement(dsPtr, iPtr->name);
    Tcl_DStringAppendElement(dsPtr, owner ? owner : "");
    sprintf(buf, " %d %lu %lu", iPtr->id, iPtr->nlock, iPtr->nbusy);
    Tcl_DStringAppend(dsPtr, buf, -1);
    Tcl_DStringEndSublist(dsPtr);
}


static void
AppendThread(Ns_ThreadInfo *iPtr, void *arg)
{
    Tcl_DString *dsPtr = arg;
    char buf[100];

    Tcl_DStringStartSublist(dsPtr);
    Tcl_DStringAppendElement(dsPtr, iPtr->name);
    Tcl_DStringAppendElement(dsPtr, iPtr->parent);
    sprintf(buf, " %d %d %ld", iPtr->tid, iPtr->flags, iPtr->ctime);
    Tcl_DStringAppend(dsPtr, buf, -1);
    Ns_GetProcInfo(dsPtr, (void *) iPtr->proc, iPtr->arg);
    Tcl_DStringEndSublist(dsPtr);
}

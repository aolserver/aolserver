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
 * form.c --
 *
 *      Routines for dealing with HTML FORM's.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/form.c,v 1.1 2001/03/22 21:30:17 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static char *Decode(Ns_DString *dsPtr, char *s);
static int ConnReadChar(Ns_Conn *conn, char *buf);
static int ChanPutc(Tcl_Channel chan, char ch);
static int CopyToChan(Ns_Conn *conn, Tcl_Channel chan, char *boundary);
static int CopyToDString(Ns_Conn *conn, Ns_DString *dsPtr, char *boundary);
static int GetMultipartFormdata(Ns_Conn *conn, char *key, Tcl_Channel chan,
				Ns_DString *dsPtr, Ns_Set *formdata);

#define FORM_URLENCODED	    "application/x-www-form-urlencoded"
#define FORM_MULTIPART	    "multipart/form-data"
#define FORM_BOUNDARY	    "boundary="
#define CONTENT_TYPE	    "content-type"
#define CONTENT_DISPOSITION "content-disposition"


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnGetQuery --
 *
 *	Get the connection query data, either by reading the content 
 *	of a POST request or get it from the query string 
 *
 * Results:
 *	Query data or NULL if error 
 *
 * Side effects:
 *	
 *
 *----------------------------------------------------------------------
 */

Ns_Set  *
Ns_ConnGetQuery(Ns_Conn *conn)
{
    Conn           *connPtr = (Conn *) conn;
    char	   *form, *type;
    int		    len;
    Ns_Set	   *set;
    Ns_DString      ds;
    
    if (connPtr->query == NULL) {
	Ns_DStringInit(&ds);
	set = NULL;
	form = connPtr->request->query;
	type = Ns_SetIGet(connPtr->headers, "content-type");
	len = conn->contentLength;
        if (STREQ(connPtr->request->method, "POST")
	    && connPtr->nContent == 0
	    && len > 0) {
	    if (strstr(type, FORM_URLENCODED) != NULL
		&& len < connPtr->servPtr->limits.maxpost
		&& Ns_ConnCopyToDString(conn, len, &ds) == NS_OK) {
		form = ds.string;
	    } else if (strstr(type, FORM_MULTIPART) != NULL) {
		/* TODO: Handle multipart forms here. */
	    }
	}
        if (set == NULL && form != NULL) {
	    set = Ns_SetCreate(NULL);
	    if (Ns_QueryToSet(form, set) != NS_OK) {
		Ns_SetFree(set);
		set = NULL;
	    }
        }
	Ns_DStringFree(&ds);
	connPtr->query = set;
    }
    return connPtr->query;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_QueryToSet --
 *
 *	Parse query data into an Ns_Set 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will add data to set 
 *
 *----------------------------------------------------------------------
 */

int
Ns_QueryToSet(char *query, Ns_Set *set)
{
    char *p, *k, *v;
    Ns_DString      kds, vds;

    Ns_DStringInit(&kds);
    Ns_DStringInit(&vds);
    p = query;
    while (p != NULL) {
	k = p;
	p = strchr(p, '&');
	if (p != NULL) {
	    *p++ = '\0';
	}
	v = strchr(k, '=');
	if (v != NULL) {
	    *v = '\0';
	}
	k = Decode(&kds, k);
	if (v != NULL) {
	    Decode(&vds, v+1);
	    *v = '=';
	    v = vds.string;
	}
	Ns_SetPut(set, k, v);
    }
    Ns_DStringFree(&kds);
    Ns_DStringFree(&vds);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclParseQueryCmd --
 *
 *	This procedure implements the AOLserver Tcl
 *
 *	    ns_parsequery querystring
 *
 *	command.
 *
 * Results:
 *	The Tcl result is a Tcl set with the parsed name-value pairs from
 *	the querystring argument
 *
 * Side effects:
 *	None external.
 *
 *----------------------------------------------------------------------
 */

int
NsTclParseQueryCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv) 
{
    Ns_Set *set;

    if (argc != 2) {
	Tcl_AppendResult(interp, argv[0], ": wrong number args: should be \"",
	    argv[0], " querystring\"", (char *) NULL);
	return TCL_ERROR;
    }
    set = Ns_SetCreate(NULL);
    if (Ns_QueryToSet(argv[1], set) != NS_OK) {
	Tcl_AppendResult(interp, argv[0], ": could not parse: \"",
	    argv[1], "\"", (char *) NULL);
	Ns_SetFree(set);
	return TCL_ERROR;
    }
    return Ns_TclEnterSet(interp, set, NS_TCL_SET_DYNAMIC);
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
NsTclGetMultipartFormdataCmd(ClientData arg, Tcl_Interp *interp, int argc,
			char **argv)
{
    NsInterp    *itPtr = arg;
    Ns_DString   ds;
    Tcl_Channel  chan;
    Ns_Set      *formdata;
    int          status;

    if (argc != 4 && argc != 5) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " connId key fileId ?formdataSet?",
                         NULL);
        return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    status = NS_OK;
    if (Ns_TclGetOpenChannel(interp, argv[3], 1, 1, &chan) == TCL_ERROR) {
        return TCL_ERROR;
    }
    formdata = Ns_SetCreate(NULL);
    status = TCL_OK;
    Ns_DStringInit(&ds);
    if (GetMultipartFormdata(itPtr->conn, argv[2], chan, &ds,
			     formdata) != NS_OK) {
	
        Tcl_SetResult(interp, "Failed.", TCL_STATIC);
        Ns_SetFree(formdata);
        Ns_DStringFree(&ds);
        status = TCL_ERROR;
    } else {
	/*
	 * If a set was provided, put the data in that. If not, throw it
	 * away.
	 */
	
        if (argc == 5) {
            Ns_TclEnterSet(interp, formdata, NS_TCL_SET_DYNAMIC);
            Tcl_SetVar(interp, argv[4], interp->result, 0);
        } else {
            Ns_SetFree(formdata);
        }
        Tcl_SetResult(interp, Ns_DStringExport(&ds), 
                      (Tcl_FreeProc *) ns_free);
    }
    Ns_DStringFree(&ds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Decode --
 *
 *	Decode a form key or value, converting + to spaces and
 *	UrlDecode'ing the result.
 *
 * Results:
 *	Pointer to dsPtr->string.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
Decode(Ns_DString *dsPtr, char *s)
{
    Ns_DString tmp;

    Ns_DStringInit(&tmp);
    if (strchr(s, '+') != NULL) {
	s = Ns_DStringAppend(&tmp, s);
	while (*s != '\0') {
	    if (*s == '+') {
		*s = ' ';
	    }
	    ++s;
	}
	s = tmp.string;
    }
    Ns_DStringTrunc(dsPtr, 0);
    Ns_UrlDecode(dsPtr, s);
    Ns_DStringFree(&tmp);
    return dsPtr->string;
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
		     Ns_DString *dsPtr, Ns_Set *formdata)
{
    Ns_Set     *headers;
    char       *type, *bound, *p;
    Ns_DString  dsBoundary, dsLine, dsName;
    int         status;

    Ns_DStringInit(&dsBoundary);
    Ns_DStringInit(&dsLine);
    Ns_DStringInit(&dsName);
    status = NS_ERROR;

    /*
     * Verify this is a valid mulitpart/form-data form.
     */

    if ((headers = Ns_ConnHeaders(conn)) == NULL
	|| (type = Ns_SetIGet(headers, CONTENT_TYPE)) == NULL
	|| strstr(type, FORM_MULTIPART) == NULL
	|| (bound = strstr(type, FORM_BOUNDARY)) == NULL) {
	goto done;
    }

    /*
     * Format the complete boundary.
     */

    bound += sizeof(FORM_BOUNDARY) - 1;
    bound = Ns_DStringVarAppend(&dsBoundary, "\r\n--", bound, NULL);

    /*
     * First line should be the boundary.
     */
    
    status = NS_OK;
    Ns_ConnReadLine(conn, &dsLine, NULL);
    Ns_DStringTrunc(&dsLine, 0);
    while (Ns_ConnReadLine(conn, &dsLine, NULL) == NS_OK) {

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
                        Ns_DStringNAppend(dsPtr, p, q-p);
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
		
                if (CopyToChan(conn, chan, bound) != NS_OK) {
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
                CopyToDString(conn, &dsData, bound);
                Ns_SetPut(formdata, dsName.string, dsData.string);
                Ns_DStringFree(&dsData);
            }
            Ns_DStringTrunc(&dsName, 0);
            Ns_ConnReadLine(conn,&dsLine, NULL);
            if (strcmp(dsLine.string, "--") == 0) {
                break;
            }
        }
        Ns_DStringTrunc(&dsLine, 0);
    }

done:    
    Ns_DStringFree(&dsBoundary);
    Ns_DStringFree(&dsLine);
    Ns_DStringFree(&dsName);
    return status;
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

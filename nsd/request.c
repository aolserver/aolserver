/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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
 * request.c --
 *
 *	Functions that implement the Ns_Request type.
 *	Pool memory is used as an optimization.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/request.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define HTTP "HTTP/"

/*
 * Local functions defined in this file.
 */
static void SetUrl(Ns_Request * request, char *url);
static void FreeUrl(Ns_Request * request);


/*
 *----------------------------------------------------------------------
 *
 * Ns_FreeRequest --
 *
 *	Free an Ns_Request structure and all its members. 
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
Ns_FreeRequest(Ns_Request * request)
{
    if (request != NULL) {
        ns_free(request->line);
        ns_free(request->method);
        ns_free(request->protocol);
        ns_free(request->host);
        ns_free(request->query);
        FreeUrl(request);
        ns_free(request);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseRequest --
 *
 *	Parse a request from a browser into an Ns_Request structure. 
 *
 * Results:
 *	A new Ns_Request. 
 *
 * Side effects:
 *	The result is newly-allocated. 
 *
 *----------------------------------------------------------------------
 */

Ns_Request *
Ns_ParseRequest(char *line)
{
    char       *url;
    char       *p;
    Ns_DString  ds;
    Ns_Request *request;

    request = ns_calloc(1, sizeof(Ns_Request));
    Ns_DStringInit(&ds);

    /*
     * Make a copy of the line to chop up. Make sure it isn't blank.
     */
    
    if (line == NULL) {
        goto done;
    }
    Ns_DStringAppend(&ds, line);
    line = Ns_StrTrim(ds.string);
    if (*line == '\0') {
        goto done;
    }

    /*
     * Save the trimmed line for logging purposes.
     */
    
    request->line = ns_strdup(line);

    /*
     * Look for the minimum of method and url.
     */
    
    url = line;
    while (*url != '\0' && !isspace(UCHAR(*url))) {
        ++url;
    }
    if (*url == '\0') {
        goto done;
    }
    *url++ = '\0';
    while (*url != '\0' && isspace(UCHAR(*url))) {
        ++url;
    }
    if (*url == '\0') {
        goto done;
    }
    request->method = ns_strdup(line);


    /*
     * Look for a valid version.
     */
    
    request->version = 0.0;
    p = url + strlen(url);
    while (p-- > url) {
        if (!isdigit(UCHAR(*p)) && *p != '.') {
            break;
        }
    }
    p -= (sizeof(HTTP) - 2);
    if (p >= url) {
        if (strncmp(p, HTTP, sizeof(HTTP) - 1) == 0) {

            /*
             * If atof fails, version will be set to 0 and the server
             * will treat the connection as if it had no HTTP/n.n keyword.
             */
	    
            *p = '\0';
            p += sizeof(HTTP) - 1;
            request->version = atof(p);
        }
    }
    url = Ns_StrTrim(url);
    if (*url == '\0') {
        goto done;
    }

    /*
     * Look for a protocol in the URL.
     */
    
    request->protocol = NULL;
    request->host = NULL;
    request->port = 0;
    if (*url != '/') {
        p = url;
        while (*p != '\0' && *p != '/' && *p != ':') {
            ++p;
        }
        if (*p == ':') {

            /*
             * Found a protocol - copy it and search for host:port.
             */

	    *p++ = '\0';
            request->protocol = ns_strdup(url);
            url = p;
            if ((strlen(url) > 3) && (*p++ == '/')
                && (*p++ == '/') && (*p != '\0') && (*p != '/')) {
                char *h;

                h = p;
                while (*p != '\0' && *p != '/') {
                    ++p;
                }
                if (*p == '/') {
                    *p++ = '\0';
                }
                url = p;
                p = strchr(h, ':');
                if (p != NULL) {
                    *p++ = '\0';
                    request->port = atoi(p);
                }
                request->host = ns_strdup(h);
            }
        }
    }
    SetUrl(request, url);

done:
    if (request->url == NULL) {
        Ns_FreeRequest(request);
        request = NULL;
    }
    Ns_DStringFree(&ds);
    return request;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SkipUrl --
 *
 *	Return a pointer n elements into the request's url. 
 *
 * Results:
 *	The url beginning n elements in. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_SkipUrl(Ns_Request *request, int n)
{
    int skip;

    if (n > request->urlc) {
        return NULL;
    }
    skip = 0;
    while (--n >= 0) {
        skip += strlen(request->urlv[n]) + 1;
    }
    return (request->url + skip);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_SetRequestUrl --
 *
 *	Set the url in a request structure. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Makes a copy of url. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_SetRequestUrl(Ns_Request * request, char *url)
{
    Ns_DString      ds;

    FreeUrl(request);
    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, url);
    SetUrl(request, ds.string);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeUrl --
 *
 *	Free the url in a request. 
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
FreeUrl(Ns_Request * request)
{
    if (request->url != NULL) {
        ns_free(request->url);
        request->url = NULL;
    }
    if (request->urlv != NULL) {
        ns_free(request->urlv[0]);
        ns_free(request->urlv);
        request->urlv = NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SetUrl --
 *
 *	Break up an URL and put it in the request. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Memory allocated for members.
 *
 *----------------------------------------------------------------------
 */

static void
SetUrl(Ns_Request * request, char *url)
{
    Ns_DString  ds1, ds2;
    char       *p;

    Ns_DStringInit(&ds1);
    Ns_DStringInit(&ds2);

    /*
     * Look for a query string at the end of the URL.
     */
    
    p = strchr(url, '?');
    if (p != NULL) {
        *p++ = '\0';
        if (request->query != NULL) {
            ns_free(request->query);
        }
        request->query = ns_strdup(p);
    }

    /*
     * Decode and normalize the URL.
     */

    p = Ns_UrlDecode(&ds1, url);
    if (p == NULL) {
        p = url;
    }
    Ns_NormalizePath(&ds2, p);
    Ns_DStringTrunc(&ds1, 0);

    /*
     * Append a trailing slash to the normalized URL if the original URL
     * ended in slash that wasn't also the leading slash.
     */

    while (*url == '/') {
        ++url;
    }
    if (*url != '\0' && url[strlen(url) - 1] == '/') {
        Ns_DStringAppend(&ds2, "/");
    }
    request->url = ns_strdup(ds2.string);
    Ns_DStringFree(&ds2);

    /*
     * Build the urlv and set urlc.
     */

    p = ns_strdup(request->url + 1);
    Ns_DStringNAppend(&ds1, (char *) &p, sizeof(char *));
    while (*p != '\0') {
        if (*p == '/') {
            *p++ = '\0';
            if (*p == '\0') {
		/*
		 * Stop on a trailing slash.
		 */
		
                break;
            }
            Ns_DStringNAppend(&ds1, (char *) &p, sizeof(char *));
        }
        ++p;
    }
    request->urlc = ds1.length / (sizeof(char *));
    p = NULL;
    Ns_DStringNAppend(&ds1, (char *) &p, sizeof(char *));
    request->urlv = (char **) ns_malloc(ds1.length);
    memcpy(request->urlv, ds1.string, ds1.length);
    Ns_DStringFree(&ds1);
}

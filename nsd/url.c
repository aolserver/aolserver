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
 * url.c --
 *
 *	Parse URLs.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/url.c,v 1.5 2001/03/13 16:46:02 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_RelativeUrl --
 *
 *	If the url passed in is for this server, then the initial 
 *	part of the URL is stripped off. e.g., on a server whose 
 *	location is http://www.foo.com, Ns_RelativeUrl of 
 *	"http://www.foo.com/hello" will return "/hello". 
 *
 * Results:
 *	A pointer to the beginning of the relative url in the 
 *	passed-in url, or NULL if error.
 *
 * Side effects:
 *	Will set errno on error.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_RelativeUrl(char *url, char *location)
{
    char *v;

    if (url == NULL || location == NULL) {
        return NULL;
    }

    /*
     * Ns_Match will return the point in URL where location stops
     * being equal to it because location ends.
     *
     * e.g., if location = "http://www.foo.com" and
     * url="http://www.foo.com/a/b" then after the call,
     * v="/a/b", or NULL if there's a mismatch.
     */
    
    v = Ns_Match(location, url);
    if (v != NULL) {
        url = v;
    }
    while (url[0] == '/' && url[1] == '/') {
        ++url;
    }
    return url;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ParseUrl --
 *
 *	Parse a URL into its component parts 
 *
 * Results:
 *	NS_OK or NS_ERROR 
 *
 * Side effects:
 *	Pointers to the protocol, host, port, path, and "tail" (last 
 *	path element) will be set by reference in the passed-in pointers.
 *	The passed-in url will be modified.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ParseUrl(char *url, char **pprotocol, char **phost,
	    char **pport, char **ppath, char **ptail)
{
    char           *end;

    *pprotocol = NULL;
    *phost = NULL;
    *pport = NULL;
    *ppath = NULL;
    *ptail = NULL;

    /*
     * Set end to the end of the protocol
     * http://www.foo.com:8000/baz/blah/spoo.html
     *     ^
     *     +--end
     */
    
    end = strchr(url, ':');
    if (end != NULL) {
	/*
	 * There is a protocol specified. Clear out the colon.
	 * Set pprotocol to the start of the protocol, and url to
	 * the first character after the colon.
	 *
	 * http\0//www.foo.com:8000/baz/blah/spoo.html
	 * ^   ^ ^
	 * |   | +-- url
	 * |   +-- end
	 * +-------- *pprotocol
	 */
	
        *end = '\0';
        *pprotocol = url;
        url = end + 1;
        if ((*url == '/') &&
            (*(url + 1) == '/')) {

	    /*
	     * There are two slashes, which means a host is specified.
	     * Advance url past that and set *phost.
	     *
	     * http\0//www.foo.com:8000/baz/blah/spoo.html
	     * ^   ^   ^
	     * |   |   +-- url, *phost
	     * |   +-- end
	     * +-------- *pprotocol
	     */
	    
            url = url + 2;

            *phost = url;

	    /*
	     * Look for a port number, which is optional.
	     */
	    
            end = strchr(url, ':');
            if (end != NULL) {
		/*
		 * A port was specified. Clear the colon and
		 * set *pport to the first digit.
		 *
		 * http\0//www.foo.com\08000/baz/blah/spoo.html
		 * ^       ^          ^ ^
		 * |       +-- *phost | +------ url, *pport
		 * +----- *pprotocol  +--- end
		 */
		 
                *end = '\0';
                url = end + 1;
                *pport = url;
            }

	    /*
	     * Move up to the slash which starts the path/tail.
	     * Clear out the dividing slash.
	     *
	     * http\0//www.foo.com\08000\0baz/blah/spoo.html
	     * ^       ^            ^   ^ ^
	     * |       |            |   | +-- url
	     * |       +-- *phost   |   +-- end
	     * +----- *pprotocol    +-- *pport
	     */
	    
            end = strchr(url, '/');
            if (end == NULL) {
		/*
		 * No path or tail specified. Return.
		 */
		
                *ppath = "";
                *ptail = "";
                return NS_OK;
            }
            *end = '\0';
	    url = end + 1;
        } else {
	    /*
	     * The URL must have been an odd one without a hostname.
	     * Move the URL up past the dividing slash.
	     *
	     * http\0/baz/blah/spoo.html
	     * ^   ^  ^
	     * |   |  +-- url
	     * |   +-- end
	     * +-------- *pprotocol
	     */
	     
            url++;
        }

	/*
	 * Set the path to URL and advance to the last slash.
	 * Set ptail to the character after that, or if there is none,
	 * it becomes path and path becomes an empty string.
	 *
	 * http\0//www.foo.com\08000\0baz/blah/spoo.html
	 * ^       ^            ^   ^ ^       ^^
	 * |       |            |   | |       |+-- *ptail
	 * |       |            |   | |       +-- end
	 * |       |            |   | +-- *ppath
	 * |       +-- *phost   |   +-- end
	 * +----- *pprotocol    +-- *pport
	 */
	
        *ppath = url;
        end = strrchr(url, '/');
        if (end == NULL) {
            *ptail = *ppath;
            *ppath = "";
        } else {
            *end = '\0';
            *ptail = end + 1;
        }
    } else {
	/*
	 * This URL does not have a colon. If it begins with a slash, then
	 * separate the tail from the path, otherwise it's all tail.
	 */
	
        if (*url == '/') {
            url++;
            *ppath = url;

	    /*
	     * Find the last slash on the right and everything after that
	     * becomes tail; if there are no slashes then it's all tail
	     * and path is an empty string.
	     */
	    
            end = strrchr(url, '/');
            if (end == NULL) {
                *ptail = *ppath;
                *ppath = "";
            } else {
                *end = '\0';
                *ptail = end + 1;
            }
        } else {

	    /*
	     * Just set the tail, there are no slashes.
	     */
	    
            *ptail = url;
        }
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_AbsoluteUrl --
 *
 *	Construct an URL based on baseurl but with as many parts of 
 *	the incomplete url as possible. 
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_AbsoluteUrl(Ns_DString *dsPtr, char *url, char *base)
{
    char           *protocol, *host, *port, *path, *tail, *baseprotocol,
                   *basehost, *baseport, *basepath, *basetail;
    int             status = NS_OK;

    /*
     * Copy the URL's to allow Ns_ParseUrl to destory them.
     */

    url = ns_strdup(url);
    base = ns_strdup(base);
    Ns_ParseUrl(url, &protocol, &host, &port, &path, &tail);
    Ns_ParseUrl(base, &baseprotocol, &basehost, &baseport, &basepath, &basetail);
    if (baseprotocol == NULL || basehost == NULL || basepath == NULL) {
        status = NS_ERROR;
        goto done;
    }
    if (protocol == NULL) {
        protocol = baseprotocol;
    }
    if (host == NULL) {
        host = basehost;
        port = baseport;
    }
    if (path == NULL) {
        path = basepath;
    }
    Ns_DStringVarAppend(dsPtr, protocol, "://", host, NULL);
    if (port != NULL) {
        Ns_DStringVarAppend(dsPtr, ":", port, NULL);
    }
    if (*path == '\0') {
        Ns_DStringVarAppend(dsPtr, "/", tail, NULL);
    } else {
        Ns_DStringVarAppend(dsPtr, "/", path, "/", tail, NULL);
    }
done:
    ns_free(url);
    ns_free(base);
    return status;
}

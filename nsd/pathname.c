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
 * pathname.c --
 *
 *	Functions that manipulate or return paths. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/pathname.c,v 1.14 2006/04/19 17:48:57 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#define isslash(c)	((c) == '/' || (c) == '\\')

/*
 * Local functions defined in this file.
 */

static char *MakePath(Ns_DString *dest, va_list *pap);


/*
 *----------------------------------------------------------------------
 *
 * Ns_PathIsAbsolute --
 *
 *	Boolean: is the path absolute? 
 *
 * Results:
 *	NS_TRUE if it is, NS_FALSE if not. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PathIsAbsolute(char *path)
{
#ifdef _WIN32
    if (isalpha(*path) && path[1] == ':') {
	path += 2;
    }
#endif
    if (isslash(*path)) {
	return NS_TRUE;
    }
    return NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_NormalizePath --
 *
 *	Remove "..", "." from paths. 
 *
 * Results:
 *	dsPtr->string 
 *
 * Side effects:
 *	Will append to dsPtr. Assumes an absolute path.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_NormalizePath(Ns_DString *dsPtr, char *path)
{
    char end;
    register char *src, *part, *slash;
    Ns_DString tmp;

    Ns_DStringInit(&tmp);
    src = Ns_DStringAppend(&tmp, path);
#ifdef _WIN32
    if (isalpha(*src) && src[1] == ':') {
	if (isupper(*src)) {
	    *src = tolower(*src);
	}
	Ns_DStringNAppend(dsPtr, src, 2);
	src += 2;
    }
#endif

    /*
     * Move past leading slash(es)
     */
    
    while (isslash(*src)) {
	++src;
    }
    do {
	part = src;

	/*
	 * Move to next slash
	 */
	
	while (*src && !isslash(*src)) {
	    ++src;
	}
	end = *src;
	*src++ = '\0';

	if (part[0] == '.' && part[1] == '.' && part[2] == '\0') {

	    /*
	     * There's a "..", so wipe out one path backwards.
	     */
	    
	    slash = strrchr(dsPtr->string, '/');
	    if (slash != NULL) {
		Ns_DStringTrunc(dsPtr, slash - dsPtr->string);
	    }
	} else if (part[0] != '\0' &&
		   (part[0] != '.' || part[1] != '\0')) {

	    /*
	     * There's something non-null and not ".".
	     */

	    Ns_DStringNAppend(dsPtr, "/", 1);
	    Ns_DStringAppend(dsPtr, part);
	}
    } while (end != '\0');

    /*
     * If what remains is an empty string, change it to "/".
     */

    if (dsPtr->string[0] == '\0') {
	Ns_DStringNAppend(dsPtr, "/", 1);
    }
    Ns_DStringFree(&tmp);

    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MakePath --
 *
 *	Append all the elements together with slashes between them. 
 *	Stop at NULL. 
 *
 * Results:
 *	dest->string 
 *
 * Side effects:
 *	Will append to dest. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_MakePath(Ns_DString *dest, ...)
{
    va_list  ap;
    char    *path;

    va_start(ap, dest);
    path = MakePath(dest, &ap);
    va_end(ap);
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_LibPath --
 *
 *	Returns the path where AOLserver libraries exist, with 
 *	varargs appended to it with slashes between each, stopping at 
 *	null arg. 
 *
 * Results:
 *	dest->string
 *
 * Side effects:
 *	Appends to dest. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_LibPath(Ns_DString *dest, ...)
{
    va_list  ap;
    char    *path;

    Ns_HomePath(dest, "lib", NULL);
    va_start(ap, dest);
    path = MakePath(dest, &ap);
    va_end(ap);
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_BinPath --
 *
 *	Returns the path where AOLserver binaries exist, with 
 *	varargs appended to it with slashes between each, stopping at 
 *	null arg. 
 *
 * Results:
 *	dest->string
 *
 * Side effects:
 *	Appends to dest. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_BinPath(Ns_DString *dest, ...)
{
    va_list  ap;
    char    *path;

    Ns_HomePath(dest, "bin", NULL);
    va_start(ap, dest);
    path = MakePath(dest, &ap);
    va_end(ap);
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_HomePath --
 *
 *	Build a path relative to AOLserver's home dir. 
 *
 * Results:
 *	dest->string 
 *
 * Side effects:
 *	Appends to dest. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_HomePath(Ns_DString *dest, ...)
{
    va_list  ap;
    char    *path;

    Ns_MakePath(dest, Ns_InfoHomePath(), NULL);
    va_start(ap, dest);
    path = MakePath(dest, &ap);
    va_end(ap);
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModulePath --
 *
 *	Append a path to dest:
 *	server-home/?servers/hserver?/?modules/hmodule?/...
 *	server and module may both be null.
 *
 * Results:
 *	dest->string 
 *
 * Side effects:
 *	Appends to dest. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ModulePath(Ns_DString *dest, char *server, char *module, ...)
{
    va_list         ap;
    char           *path;

    Ns_HomePath(dest, NULL);
    if (server != NULL) {
       Ns_MakePath(dest, "servers", server, NULL);
    }
    if (module != NULL) {
       Ns_MakePath(dest, "modules", module, NULL);
    }
    va_start(ap, module);
    path = MakePath(dest, &ap);
    va_end(ap);
    return path;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclModulePathObjCmd --
 *
 *	Implements ns_modulepath command; basically a wrapper around 
 *	Ns_ModulePath. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None (deprecated) 
 *
 *----------------------------------------------------------------------
 */

int
NsTclModulePathObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		      Tcl_Obj *CONST objv[])
{
    Ns_DString      ds;
    int		    i;
    char	   *module;

    Ns_DStringInit(&ds);
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "server ?module ...?");
        return TCL_ERROR;
    }
    module = objc > 2 ? Tcl_GetString(objv[2]) : NULL;
    Ns_ModulePath(&ds, Tcl_GetString(objv[1]), module, NULL);
    for (i = 3; i < objc; ++i) {
	Ns_MakePath(&ds, Tcl_GetString(objv[i]), NULL);
    }
    Tcl_DStringResult(interp, &ds);
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * MakePath --
 *
 *	Append the args with slashes between them to dest. 
 *
 * Results:
 *	dest->string 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
MakePath(Ns_DString *dest, va_list *pap)
{
    char *s;
    int len;

    while ((s = va_arg(*pap, char *)) != NULL) {
        if (isalpha(*s) && s[1] == ':') {
            char temp = *(s+2);
            *(s + 2) = 0;
            Ns_DStringNAppend(dest, s, 2);
            *(s + 2) = temp;
            s += 2;
        }
	while (*s) {
	    while (isslash(*s)) {
	        ++s;
	    }
	    if (*s) {
	    	Ns_DStringNAppend(dest, "/", 1);
		len = 0;
		while (s[len] != '\0' && !isslash(s[len])) {
		    ++len;
		}
	    	Ns_DStringNAppend(dest, s, len);
	    	s += len;
	    }
	}
    }
    return dest->string;
}

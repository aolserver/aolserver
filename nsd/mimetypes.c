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
 * mimetypes.c --
 *
 *	Defines standard default mime types. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/mimetypes.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file.
 */

static void AddType(char *ext, char *type);
static char *LowerDString(Ns_DString *dsPtr, char *ext);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable    types;
static char            *defaultType;
static char            *noextType;

/*
 * The default extension matching table.  This should be kept up to date with
 * the client.  Case in the extension is ignored.
 */

static struct exttype {
    char           *ext;
    char           *type;
} typetab[] = {
    { ".adp",   "text/html" },
    { ".ai",    "application/postscript" },
    { ".aif",   "audio/aiff" },
    { ".aifc",  "audio/aiff" },
    { ".aiff",  "audio/aiff" },
    { ".ani",   "application/x-navi-animation" },
    { ".art",   "image/x-art" },
    { ".au",    "audio/basic" },
    { ".avi",   "video/x-msvideo" },
    { ".bin",   "application/x-macbinary" },
    { ".bmp",   "image/bmp" },
    { ".css",   "text/css" },
    { ".csv",   "application/csv" },
    { ".dci",   "text/html" },
    { ".dcr",   "application/x-director" },
    { ".dir",   "application/x-director" },
    { ".dp",    "application/commonground" },
    { ".dxr",   "application/x-director" },
    { ".elm",   "text/plain" },
    { ".eml",   "text/plain" },
    { ".exe",   "application/octet-stream" },
    { ".gbt",   "text/plain" },    
    { ".gif",   "image/gif" },
    { ".gz",    "application/x-compressed" },
    { ".hqx",   "application/mac-binhex40" },
    { ".htm",   "text/html" },
    { ".html",  "text/html" },
    { ".jfif",  "image/jpeg" },
    { ".jpe",   "image/jpeg" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".js",    "application/x-javascript" },
    { ".ls",    "application/x-javascript" },
    { ".map",   "application/x-navimap" },
    { ".mid",   "audio/x-midi" },
    { ".midi",  "audio/x-midi" },
    { ".mocha", "application/x-javascript" },
    { ".mov",   "video/quicktime" },
    { ".mpe",   "video/mpeg" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".nvd",   "application/x-navidoc" },
    { ".nvm",   "application/x-navimap" },
    { ".pbm",   "image/x-portable-bitmap" },
    { ".pdf",   "application/pdf" },
    { ".pgm",   "image/x-portable-graymap" },
    { ".pic",   "image/pict" },
    { ".pict",  "image/pict" },
    { ".pnm",   "image/x-portable-anymap" },
    { ".ps",    "application/postscript" },
    { ".qt",    "video/quicktime" },
    { ".ra",    "audio/x-pn-realaudio" },
    { ".ram",   "audio/x-pn-realaudio" },
    { ".ras",   "image/x-cmu-raster" },
    { ".rgb",   "image/x-rgb" },
    { ".rtf",   "application/rtf" },
    { ".sht",   "text/html" },
    { ".shtml", "text/html" },
    { ".sit",   "application/x-stuffit" },
    { ".snd",   "audio/basic" },
    { ".sql",   "application/x-sql" },
    { ".stl",   "application/x-navistyle" },
    { ".tar",   "application/x-tar" },
    { ".tcl",   "text/plain" },
    { ".text",  "text/plain" },
    { ".tgz",   "application/x-compressed" },
    { ".tif",   "image/tiff" },
    { ".tiff",  "image/tiff" },
    { ".txt",   "text/plain" },
    { ".xbm",   "image/x-xbitmap" },
    { ".xpm",   "image/x-xpixmap" },
    { ".vrml",  "x-world/x-vrml" },
    { ".wav",   "audio/x-wav" },
    { ".wrl",   "x-world/x-vrml" },
    { ".z",     "application/x-compressed" },
    { ".zip",   "application/x-zip-compressed" },
    { NULL,     NULL }
};

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetMimeType --
 *
 *	Guess the mime type based on filename extension. Case is 
 *	ignored. 
 *
 * Results:
 *	A mime type. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_GetMimeType(char *file)
{
    char          *ext;
    Ns_DString     ds;
    Tcl_HashEntry *hePtr;

    ext = strrchr(file, '.');
    if (ext == NULL) {
	return noextType;
    }

    Ns_DStringInit(&ds);
    ext = LowerDString(&ds, ext);
    hePtr = Tcl_FindHashEntry(&types, ext);
    if (hePtr == NULL) {
	return defaultType;
    }
    return Tcl_GetHashValue(hePtr);
}

/*
 *==========================================================================
 * Exported functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * NsInitMimeTypes --
 *
 *	Add default and configured mime types. 
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
NsInitMimeTypes(void)
{
    Ns_Set *setPtr;
    int     i;

    /*
     * Initialize hash table of file extensions.
     */
    Tcl_InitHashTable(&types, TCL_STRING_KEYS);

    /* Add default system types first from above */
    for (i = 0; typetab[i].ext != NULL; ++i) {
        AddType(typetab[i].ext, typetab[i].type);
    }

    setPtr = Ns_ConfigGetSection("ns/mimetypes");
    if (setPtr == NULL) {
	defaultType = noextType = "*/*";
	return;
    }

    defaultType = Ns_SetIGet(setPtr, "default");
    if (defaultType == NULL) {
	defaultType = "*/*";
    }

    noextType = Ns_SetIGet(setPtr, "noextension");
    if (noextType == NULL) {
	noextType = defaultType;
    }

    for (i=0; i < Ns_SetSize(setPtr); i++) {
        AddType(Ns_SetKey(setPtr, i), Ns_SetValue(setPtr, i));
    }
}


/*
 *==========================================================================
 * Exported functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * NsTclGuessTypeCmd --
 *
 *	Implements ns_guesstype. 
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
NsTclGuessTypeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *type;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " filename\"", NULL);
        return TCL_ERROR;
    }
    type = Ns_GetMimeType(argv[1]);
    Tcl_SetResult(interp, type, TCL_VOLATILE);
    
    return TCL_OK;
}


/*
 *==========================================================================
 * Static functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * AddType --
 *
 *	Add a mime type to the global hash table. 
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
AddType(char *ext, char *type)
{
    Ns_DString      ds;
    Tcl_HashEntry  *he;
    int             new;

    Ns_DStringInit(&ds);
    ext = LowerDString(&ds, ext);
    he = Tcl_CreateHashEntry(&types, ext, &new);
    if (new == 0) {
        ns_free(Tcl_GetHashValue(he));
    }
    Tcl_SetHashValue(he, ns_strdup(type));
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * LowerDString --
 *
 *	Append a string to the dstring, converting all alphabetic 
 *	characeters to lowercase. 
 *
 * Results:
 *	dsPtr->string 
 *
 * Side effects:
 *	Appends to dstring.
 *
 *----------------------------------------------------------------------
 */

static char *
LowerDString(Ns_DString *dsPtr, char *ext)
{
    assert(ext != NULL);

    Ns_DStringAppend(dsPtr, ext);
    ext = dsPtr->string;
    while (*ext != '\0') {
        if (isupper(UCHAR(*ext))) {
            *ext = tolower(UCHAR(*ext));
        }
        ++ext;
    }

    return dsPtr->string;
}

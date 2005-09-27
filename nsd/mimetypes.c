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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/mimetypes.c,v 1.13 2005/09/27 02:26:27 shmooved Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#define TYPE_DEFAULT "*/*"

/*
 * Local functions defined in this file.
 */

static void AddType(char *ext, char *type);
static char *LowerDString(Ns_DString *dsPtr, char *ext);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable    types;
static char            *defaultType = TYPE_DEFAULT;
static char            *noextType = TYPE_DEFAULT;
/*
 * The default extension matching table.  This should be kept up to date with
 * the client.  Case in the extension is ignored.
 */

static struct exttype {
    char           *ext;
    char           *type;
} typetab[] = {
    /*
     * Basic text/html types.
     */

    { ".adp",   NSD_TEXTHTML},
    { ".dci",   NSD_TEXTHTML},
    { ".htm",   NSD_TEXTHTML},
    { ".html",  NSD_TEXTHTML},
    { ".sht",   NSD_TEXTHTML},
    { ".shtml", NSD_TEXTHTML},

    /*
     * All other types.
     */

    { ".ai",    "application/postscript" },
    { ".aif",   "audio/aiff" },
    { ".aifc",  "audio/aiff" },
    { ".aiff",  "audio/aiff" },
    { ".ani",   "application/x-navi-animation" },
    { ".art",   "image/x-art" },
    { ".asc",   "text/plain" },
    { ".au",    "audio/basic" },
    { ".avi",   "video/x-msvideo" },
    { ".bcpio", "application/x-bcpio" },
    { ".bin",   "application/octet-stream" },
    { ".bmp",   "image/bmp" },
    { ".cdf",   "application/x-netcdf" },
    { ".cgm",   "image/cgm" },
    { ".class", "application/octet-stream" },
    { ".cpio",  "application/x-cpio" },
    { ".cpt",   "application/mac-compactpro" },
    { ".css",   "text/css" },
    { ".csv",   "application/csv" },
    { ".dcr",   "application/x-director" },
    { ".der",   "application/x-x509-ca-cert" },
    { ".dir",   "application/x-director" },
    { ".dll",   "application/octet-stream" },
    { ".dms",   "application/octet-stream" },
    { ".doc",   "application/msword" },
    { ".dp",    "application/commonground" },
    { ".dvi",   "applications/x-dvi" },
    { ".dwg",   "image/vnd.dwg" },
    { ".dxf",   "image/vnd.dxf" },
    { ".dxr",   "application/x-director" },
    { ".elm",   "text/plain" },
    { ".eml",   "text/plain" },
    { ".etx",   "text/x-setext" },
    { ".exe",   "application/octet-stream" },
    { ".ez",    "application/andrew-inset" },
    { ".fm",    "application/vnd.framemaker" },
    { ".gbt",   "text/plain" },    
    { ".gif",   "image/gif" },
    { ".gtar",  "application/x-gtar" },
    { ".gz",    "application/x-gzip" },
    { ".hdf",   "application/x-hdf" },
    { ".hpgl",  "application/vnd.hp-hpgl" },
    { ".hqx",   "application/mac-binhex40" },
    { ".ice",   "x-conference/x-cooltalk" },
    { ".ief",   "image/ief" },
    { ".igs",   "image/iges" },
    { ".iges",  "image/iges" },
    { ".jfif",  "image/jpeg" },
    { ".jpe",   "image/jpeg" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".js",    "application/x-javascript" },
    { ".kar",   "audio/midi" },
    { ".latex", "application/x-latex" },
    { ".lha",   "application/octet-stream" },
    { ".ls",    "application/x-javascript" },
    { ".lxc",   "application/vnd.ms-excel" },
    { ".lzh",   "application/octet-stream" },
    { ".man",   "application/x-troff-man" },
    { ".map",   "application/x-navimap" },
    { ".me",    "application/x-troff-me" },
    { ".mesh",  "model/mesh" },
    { ".mid",   "audio/x-midi" },
    { ".midi",  "audio/x-midi" },
    { ".mif",   "application/vnd.mif" },
    { ".mocha", "application/x-javascript" },
    { ".mov",   "video/quicktime" },
    { ".movie", "video/x-sgi-movie" },
    { ".mp2",   "audio/mpeg" },
    { ".mp3",   "audio/mpeg" },
    { ".mpe",   "video/mpeg" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".mpga",  "audio/mpeg" },
    { ".ms",    "application/x-troff-ms" },
    { ".msh",   "model/mesh" },
    { ".nc",    "application/x-netcdf" },
    { ".nvd",   "application/x-navidoc" },
    { ".nvm",   "application/x-navimap" },
    { ".oda",   "application/oda" },
    { ".pbm",   "image/x-portable-bitmap" },
    { ".pcl",   "application/vnd.hp-pcl" },
    { ".pclx",  "application/vnd.hp-pclx" },
    { ".pdb",   "chemical/x-pdb" },
    { ".pdf",   "application/pdf" },
    { ".pgm",   "image/x-portable-graymap" },
    { ".pgn",   "application/x-chess-pgn" },
    { ".pic",   "image/pict" },
    { ".pict",  "image/pict" },
    { ".pnm",   "image/x-portable-anymap" },
    { ".png",   "image/png" },
    { ".pot",   "application/vnd.ms-powerpoint" },
    { ".ppm",   "image/x-portable-pixmap" },
    { ".pps",   "application/vnd.ms-powerpoint" },
    { ".ppt",   "application/vnd.ms-powerpoint" },
    { ".ps",    "application/postscript" },
    { ".qt",    "video/quicktime" },
    { ".ra",    "audio/x-realaudio" },
    { ".ram",   "audio/x-pn-realaudio" },
    { ".ras",   "image/x-cmu-raster" },
    { ".rgb",   "image/x-rgb" },
    { ".rm",    "audio/x-pn-realaudio" },
    { ".roff",  "application/x-troff" },
    { ".rpm",   "audio/x-pn-realaudio-plugin" },
    { ".rtf",   "application/rtf" },
    { ".rtx",   "text/richtext" },
    { ".sda",   "application/vnd.stardivision.draw" },
    { ".sdc",   "application/vnd.stardivision.calc" },
    { ".sdd",   "application/vnd.stardivision.impress" },
    { ".sdp",   "application/vnd.stardivision.impress" },
    { ".sdw",   "application/vnd.stardivision.writer" },
    { ".sgl",   "application/vnd.stardivision.writer-global" },
    { ".sgm",   "text/sgml" },
    { ".sgml",  "text/sgml" },
    { ".sh",    "application/x-sh" },
    { ".shar",  "application/x-shar" },
    { ".silo",  "model/mesh" },
    { ".sit",   "application/x-stuffit" },
    { ".skd",   "application/vnd.stardivision.math" },
    { ".skm",   "application/vnd.stardivision.math" },
    { ".skp",   "application/vnd.stardivision.math" },
    { ".skt",   "application/vnd.stardivision.math" },
    { ".smf",   "application/vnd.stardivision.math" },
    { ".smi",   "application/smil" },
    { ".smil",  "application/smil" },
    { ".snd",   "audio/basic" },
    { ".spl",   "application/x-futuresplash" },
    { ".sql",   "application/x-sql" },
    { ".src",   "application/x-wais-source" },
    { ".stc",   "application/vnd.sun.xml.calc.template" },
    { ".std",   "application/vnd.sun.xml.draw.template" },
    { ".sti",   "application/vnd.sun.xml.impress.template" },
    { ".stl",   "application/x-navistyle" },
    { ".stw",   "application/vnd.sun.xml.writer.template" },
    { ".swf",   "application/x-shockwave-flash" },
    { ".sxc",   "application/vnd.sun.xml.calc" },
    { ".sxd",   "application/vnd.sun.xml.draw" },
    { ".sxg",   "application/vnd.sun.xml.writer.global" },
    { ".sxl",   "application/vnd.sun.xml.impress" },
    { ".sxm",   "application/vnd.sun.xml.math" },
    { ".sxw",   "application/vnd.sun.xml.writer" },
    { ".t",     "application/x-troff" },
    { ".tar",   "application/x-tar" },
    { ".tcl",   "x-tcl" },
    { ".tex",   "application/x-tex" },
    { ".texi",  "application/x-texinfo" },
    { ".texinfo", "application/x-texinfo" },
    { ".text",  "text/plain" },
    { ".tgz",   "application/x-gtar" },
    { ".tif",   "image/tiff" },
    { ".tiff",  "image/tiff" },
    { ".tr",    "application/x-troff" },
    { ".tsv",   "text/tab-separated-values" },
    { ".txt",   "text/plain" },
    { ".ustar", "application/x-ustar" },
    { ".vcd",   "application/x-cdlink" },
    { ".vor",   "application/vnd.stardivision.writer" },
    { ".vrml",  "model/vrml" },
    { ".wav",   "audio/x-wav" },
    { ".wbmp",  "image/vnd.wap.wbmp" },
    { ".wkb",   "application/vnd.ms-excel" },
    { ".wks",   "application/vnd.ms-excel" },
    { ".wml",   "text/vnd.wap.wml" },
    { ".wmlc",  "application/vnd.wap.wmlc" },
    { ".wmls",  "text/vnd.wap.wmlscript" },
    { ".wmlsc", "application/vnd.wap.wmlscript" },
    { ".wrl",   "model/vrml" },
    { ".xbm",   "image/x-xbitmap" },
    { ".xls",   "application/vnd.ms-excel" },
    { ".xlw",   "application/vnd.ms-excel" },
    { ".xpm",   "image/x-xpixmap" },
    { ".xht",   "application/xhtml+xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".xml",   "text/xml" },
    { ".xsl",   "text/xml" },
    { ".xyz",   "chemical/x-pdb" },
    { ".xwd",   "image/x-xwindowdump" },
    { ".z",     "application/x-compress" },
    { ".zip",   "application/zip" },
    { NULL,     NULL }
};


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
    char          *start, *ext;
    Ns_DString     ds;
    Tcl_HashEntry *hePtr;

    start = strrchr(file, '/');
    if (start == NULL) {
	start = file;
    }

    ext = strrchr(start, '.');
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
 *----------------------------------------------------------------------
 *
 * NsInitMimeTypes --
 *
 *	Add compiled-in default mime types. 
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
    int     i;

    /*
     * Initialize hash table of file extensions.
     */

    Tcl_InitHashTable(&types, TCL_STRING_KEYS);

    /*
     * Add default system types first from above
     */

    for (i = 0; typetab[i].ext != NULL; ++i) {
        AddType(typetab[i].ext, typetab[i].type);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsUpdateMimeTypes --
 *
 *	Add configured mime types. 
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
NsUpdateMimeTypes(void)
{
    Ns_Set *set;
    int     i;

    set = Ns_ConfigGetSection("ns/mimetypes");
    if (set == NULL) {
	return;
    }

    defaultType = Ns_SetIGet(set, "default");
    if (defaultType == NULL) {
	defaultType = TYPE_DEFAULT;
    }

    noextType = Ns_SetIGet(set, "noextension");
    if (noextType == NULL) {
	noextType = defaultType;
    }

    for (i=0; i < Ns_SetSize(set); i++) {
        AddType(Ns_SetKey(set, i), Ns_SetValue(set, i));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclGuessTypeObjCmd --
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
NsTclGuessTypeObjCmd(ClientData dummy, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    char *type;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "filename");
        return TCL_ERROR;
    }
    type = Ns_GetMimeType(Tcl_GetString(objv[1]));
    Tcl_SetResult(interp, type, TCL_VOLATILE);
    
    return TCL_OK;
}


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

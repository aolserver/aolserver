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
 * encoding.c --
 *
 *	Defines standard default charset to encoding mappings.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/encoding.c,v 1.15 2005/01/17 16:04:04 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


#define DEFAULT_CHARSET_CONFIG    "OutputCharset"
#define HACK_CONTENT_TYPE_CONFIG  "HackContentType"

/*
 * Local functions defined in this file.
 */

static void AddExtension(char *name, char *charset);
static void AddCharset(char *name, char *charset);
static Tcl_Encoding GetCharsetEncoding(char *charset, int len);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable    encodings;
static Tcl_HashTable    charsets;
static Tcl_HashTable    extensions;
static Ns_Mutex		lock;
static Ns_Cond		cond;
#define ENC_LOCKED	((Tcl_Encoding) (-1))

/*
 * The default table maps file extension to Tcl encodings.
 */

static struct {
    char	   *extension;
    char	   *name;
} builtinExt[] = {
    {".txt", "ascii"},
    {".htm", "iso8859-1"},
    {".html", "iso8859-1"},
    {".adp", "iso8859-1"},
    {NULL, NULL}
};

/*
 * The following table provides charset aliases for Tcl encodings.
 */

static struct {
    char           *charset;
    char           *name;
} builtinChar[] = {
    { "iso-2022-jp", "iso2022-jp" },
    { "iso-2022-kr", "iso2022-kr" },
    { "iso-8859-1", "iso8859-1" },
    { "iso-8859-2", "iso8859-2" },
    { "iso-8859-3", "iso8859-3" },
    { "iso-8859-4", "iso8859-4" },
    { "iso-8859-5", "iso8859-5" },
    { "iso-8859-6", "iso8859-6" },
    { "iso-8859-7", "iso8859-7" },
    { "iso-8859-8", "iso8859-8" },
    { "iso-8859-9", "iso8859-9" },
    { "korean", "ksc5601" },
    { "ksc_5601", "ksc5601" },
    { "mac", "macRoman" },
    { "mac-centeuro", "macCentEuro" },
    { "mac-centraleupore", "macCentEuro" },
    { "mac-croatian", "macCroatian" },
    { "mac-cyrillic", "macCyrillic" },
    { "mac-greek", "macGreek" },
    { "mac-iceland", "macIceland" },
    { "mac-japan", "macJapan" },
    { "mac-roman", "macRoman" },
    { "mac-romania", "macRomania" },
    { "mac-thai", "macThai" },
    { "mac-turkish", "macTurkish" },
    { "mac-ukraine", "macUkraine" },
    { "maccenteuro", "macCentEuro" },
    { "maccentraleupore", "macCentEuro" },
    { "maccroatian", "macCroatian" },
    { "maccyrillic", "macCyrillic" },
    { "macgreek", "macGreek" },
    { "maciceland", "macIceland" },
    { "macintosh", "macRoman" },
    { "macjapan", "macJapan" },
    { "macroman", "macRoman" },
    { "macromania", "macRomania" },
    { "macthai", "macThai" },
    { "macturkish", "macTurkish" },
    { "macukraine", "macUkraine" },
    { "shift_jis", "shiftjis" },
    { "us-ascii", "ascii" },
    { "windows-1250", "cp1250" },
    { "windows-1251", "cp1251" },
    { "windows-1252", "cp1252" },
    { "windows-1253", "cp1253" },
    { "windows-1254", "cp1254" },
    { "windows-1255", "cp1255" },
    { "windows-1256", "cp1256" },
    { "windows-1257", "cp1257" },
    { "windows-1258", "cp1258" },
    { "x-mac", "macRoman" },
    { "x-mac-centeuro", "macCentEuro" },
    { "x-mac-centraleupore", "macCentEuro" },
    { "x-mac-croatian", "macCroatian" },
    { "x-mac-cyrillic", "macCyrillic" },
    { "x-mac-greek", "macGreek" },
    { "x-mac-iceland", "macIceland" },
    { "x-mac-japan", "macJapan" },
    { "x-mac-roman", "macRoman" },
    { "x-mac-romania", "macRomania" },
    { "x-mac-thai", "macThai" },
    { "x-mac-turkish", "macTurkish" },
    { "x-mac-ukraine", "macUkraine" },
    { "x-macintosh", "macRoman" },
    { NULL,     NULL }
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetFileEncoding --
 *
 *	Return the Tcl_Encoding for the given file.  Note this may
 *	not be the same as the encoding for the charset of the
 *	file's mimetype.
 *
 * Results:
 *	Tcl_Encoding or NULL if not found.
 *
 * Side effects:
 *	See LoadEncoding().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_GetFileEncoding(char *file)
{
    Tcl_HashEntry *hPtr;
    char *ext, *name;

    ext = strrchr(file, '.');
    if (ext != NULL) {
	hPtr = Tcl_FindHashEntry(&extensions, ext);
	if (hPtr != NULL) {
	    name = Tcl_GetHashValue(hPtr);
	    return Ns_GetEncoding(name);
	}
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetTypeEncoding --
 *
 *	Return the Tcl_Encoding for the given Content-type header,
 *	e.g., "text/html; charset=iso-8859-1" returns Tcl_Encoding
 *	for iso8859-1.
 *      This function will utilize the ns/parameters/OutputCharset
 *      config parameter if given a content-type "text/<anything>" with
 *      no charset.
 *      When no OutputCharset defined, the fall-back behavior is to
 *      return NULL.
 *
 * Results:
 *	Tcl_Encoding or NULL if not found.
 *
 * Side effects:
 *	See LoadEncoding().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_GetTypeEncoding(char *type)
{
    return NsGetTypeEncoding(NULL, &type, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetCharsetEncoding --
 *
 *	Return the Tcl_Encoding for the given charset, e.g.,
 *	"iso-8859-1" returns Tcl_Encoding for iso8859-1.
 *
 * Results:
 *	Tcl_Encoding or NULL if not found.
 *
 * Side effects:
 *	See LoadEncoding().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_GetCharsetEncoding(char *charset)
{
    return GetCharsetEncoding(charset, -1);
}

static Tcl_Encoding
GetCharsetEncoding(char *charset, int len)
{
    Tcl_HashEntry *hPtr;
    Tcl_Encoding encoding;
    Ns_DString ds;

    /*
     * Cleanup the charset name and check for an
     * alias (e.g., iso-8859-1 = iso8859-1) before
     * assuming the charset and Tcl encoding names
     * match (e.g., big5).
     */
    
    Ns_DStringInit(&ds);
    Ns_DStringNAppend(&ds, charset, len);
    charset = Ns_StrTrim(Ns_StrToLower(ds.string));
    hPtr = Tcl_FindHashEntry(&charsets, charset);
    if (hPtr != NULL) {
	charset = Tcl_GetHashValue(hPtr);
    }
    encoding = Ns_GetEncoding(charset);
    Ns_DStringFree(&ds);
    return encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetEncoding --
 *
 *	Return the Tcl_Encoding for the given charset.
 *
 * Results:
 *	Tcl_Encoding or NULL if not found.
 *
 * Side effects:
 *	See GetEncoding().
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
Ns_GetEncoding(char *name)
{
    Tcl_Encoding encoding;
    Tcl_HashEntry *hPtr;
    int new;

    Ns_MutexLock(&lock);
    hPtr = Tcl_CreateHashEntry(&encodings, name, &new);
    if (!new) {
	while ((encoding = Tcl_GetHashValue(hPtr)) == ENC_LOCKED) {
	    Ns_CondWait(&cond, &lock);
	}
    } else {
	Tcl_SetHashValue(hPtr, ENC_LOCKED);
	Ns_MutexUnlock(&lock);
	encoding = Tcl_GetEncoding(NULL, name);
	if (encoding == NULL) {
	    Ns_Log(Warning, "encoding: could not load: %s", name);
	} else {
	    Ns_Log(Notice, "encoding: loaded: %s", name);
	}
	Ns_MutexLock(&lock);
	Tcl_SetHashValue(hPtr, encoding);
	Ns_CondBroadcast(&cond);
    }
    Ns_MutexUnlock(&lock);
    return encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitEncodings --
 *
 *	Add compiled-in default encodings.
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
NsInitEncodings(void)
{
    int     i;

    /*
     * Initialize hash table of encodings and charsets.
     */

    Ns_MutexSetName(&lock, "ns:encodings");
    Tcl_InitHashTable(&encodings, TCL_STRING_KEYS);
    Tcl_InitHashTable(&charsets, TCL_STRING_KEYS);
    Tcl_InitHashTable(&extensions, TCL_STRING_KEYS);

    /*
     * Add default charset and file mappings.
     */

    for (i = 0; builtinChar[i].charset != NULL; ++i) {
        AddCharset(builtinChar[i].charset, builtinChar[i].name);
    }
    for (i = 0; builtinExt[i].extension != NULL; ++i) {
	AddExtension(builtinExt[i].extension, builtinExt[i].name);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsUpdateEncodings --
 *
 *	Add additional configured encodings.
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
NsUpdateEncodings(void)
{
    Ns_Set *set;
    int     i;

    /*
     * Add configured charsets and file mappings.
     */

    set = Ns_ConfigGetSection("ns/charsets");
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	AddCharset(Ns_SetKey(set, i), Ns_SetValue(set, i));
    }
    set = Ns_ConfigGetSection("ns/encodings");
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	AddExtension(Ns_SetKey(set, i), Ns_SetValue(set, i));
    }

    /*
     * Establish default output encoding, if present.  If this
     * configuration specification is not present, the default
     * behavior will be to do not encoding transformation.
     */
    nsconf.encoding.outputCharset = Ns_ConfigGetValue(NS_CONFIG_PARAMETERS,
                                       DEFAULT_CHARSET_CONFIG);
    if (nsconf.encoding.outputCharset != NULL) {
        if (!Ns_ConfigGetBool(NS_CONFIG_PARAMETERS, HACK_CONTENT_TYPE_CONFIG,
                         &nsconf.encoding.hackContentTypeP)) {
            nsconf.encoding.hackContentTypeP = NS_TRUE;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetTypeEncodings --
 *
 *	Get the encoding for type charset, if any.
 *
 * Results:
 *	Pointer to Tcl_Encoding or NULL on no encoding.
 *
 * Side effects:
 *	If servPtr and dsPtr are not NULL, may update use a default
 *	charset for text/ types for the given server, copying
 *	the full type string with charset to given dsPtr and pointing
 *	typePtr to the result.
 *
 *----------------------------------------------------------------------
 */

Tcl_Encoding
NsGetTypeEncoding(NsServer *servPtr, char **typePtr, Ns_DString *dsPtr)
{
    char *start, *end, *type, *charset;
    int len;

    charset = NULL;
    type = *typePtr;

    /*
     * First, look for a charset within the type string.
     */

    start = Ns_StrCaseFind(type, "charset");
    if (start != NULL) {
	start += 7;
	start += strspn(start, " ");
	if (*start++ == '=') {
	    start += strspn(start, " ");
	    end = start;
	    while (*end && !isspace(UCHAR(*end))) {
		++end;
	    }
	    len = end - start;
	    charset = start;
	    len = end - start;
	}
    } else if (servPtr != NULL && dsPtr != NULL
		&& (strncasecmp(type, "text/", 5) == 0)) {
	/*
	 * Use default server-specific default text/ charset,
	 * if any, and update type string if configured.
	 */

	charset = servPtr->encoding.outputCharset;
	len = -1;
	if (charset != NULL && servPtr->encoding.hackContentTypeP) {
	    Ns_DStringVarAppend(dsPtr, type, "; charset=", charset, NULL);
    	    *typePtr = dsPtr->string;
	}
    }
    return (charset ? GetCharsetEncoding(charset, len) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCharsetsCmd --
 *
 *      Tcl command to get the list of charsets for which we have encodings.
 *
 * Results:
 *	TCL_OK
 *
 * Side effects:
 *	Sets Tcl interpreter result.
 *
 *----------------------------------------------------------------------
 */

int
NsTclCharsetsCmd(ClientData dummy, Tcl_Interp *interp, int argc,
    char *argv[])
{
    Tcl_HashEntry  *entry;
    Tcl_HashSearch  search;

    Ns_MutexLock(&lock);
    entry = Tcl_FirstHashEntry(&charsets, &search);
    while (entry != NULL) {
	Tcl_AppendElement(interp, (char *) Tcl_GetHashKey(&charsets, entry));
	entry = Tcl_NextHashEntry(&search);
    }
    Ns_MutexUnlock(&lock);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclEncodingForCharsetCmd --
 *
 *      Return the name of the encoding for the specified charset.
 *
 * Results:
 *	Tcl result contains an encoding name or "".
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclEncodingForCharsetCmd(ClientData dummy, Tcl_Interp *interp, int argc,
                           char **argv)
{
    Tcl_Encoding encoding;

    if (argc != 2) {
        Tcl_AppendResult(interp, "usage: ", argv[0], " charset", NULL);
	return TCL_ERROR;
    }

    encoding = Ns_GetCharsetEncoding(argv[1]);
    if (encoding == NULL) {
	return TCL_OK;
    }

    Tcl_SetResult(interp, (char*)Tcl_GetEncodingName(encoding), TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AddCharset, AddExtension --
 *
 *	Add charsets and extensions to hash tables.
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
AddExtension(char *ext, char *name)
{
    Tcl_HashEntry  *hPtr;
    int             new;

    hPtr = Tcl_CreateHashEntry(&extensions, ext, &new);
    Tcl_SetHashValue(hPtr, name);
}

static void
AddCharset(char *charset, char *name)
{
    Tcl_HashEntry  *hPtr;
    Ns_DString	    ds;
    int             new;

    Ns_DStringInit(&ds);
    charset = Ns_StrToLower(Ns_DStringAppend(&ds, charset));
    hPtr = Tcl_CreateHashEntry(&charsets, charset, &new);
    Tcl_SetHashValue(hPtr, name);
    Ns_DStringFree(&ds);
}

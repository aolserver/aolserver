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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/encoding.c,v 1.5 2001/04/02 19:36:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file.
 */

static void AddEncoding(char *name, char *charset);
static Tcl_Encoding LoadEncoding(Tcl_HashEntry *hPtr);
static Tcl_Encoding GetCharsetEncoding(char *charset, int len);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable    encodings;
static Tcl_HashTable    charsets;
static Ns_Mutex lock;

/*
 * The default encoding matching table.  This should be kept up to date with
 * the Tcl encoding files.
 */

static struct {
    char           *charset;
    char           *name;
} builtins[] = {
    { "ascii", "ascii" },
    { "big5", "big5" },
    { "euc-cn", "euc-cn" },
    { "euc-jp", "euc-jp" },
    { "euc-kr", "euc-kr" },
    { "gb12345", "gb12345" },
    { "gb1988", "gb1988" },
    { "gb2312", "gb2312" },
    { "ibm437", "cp437" },
    { "ibm737", "cp737" },
    { "ibm775", "cp775" },
    { "ibm850", "cp850" },
    { "ibm852", "cp852" },
    { "ibm855", "cp855" },
    { "ibm857", "cp857" },
    { "ibm860", "cp860" },
    { "ibm861", "cp861" },
    { "ibm862", "cp862" },
    { "ibm863", "cp863" },
    { "ibm864", "cp864" },
    { "ibm865", "cp865" },
    { "ibm866", "cp866" },
    { "ibm869", "cp869" },
    { "ibm874", "cp874" },
    { "ibm932", "cp932" },
    { "ibm936", "cp936" },
    { "ibm949", "cp949" },
    { "ibm950", "cp950" },
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
    { "koi8-r", "koi8-r" },
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
    { "unicode", "unicode" },
    { "us-ascii", "ascii" },
    { "utf-8", "utf-8" },
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
 *	Return the Tcl_Encoding for the given file, e.g., if .adp
 *	extension is mapped to "text/html; charset=iso-8859-1",
 *	will return Tcl_Encoding for iso8859-1. 
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
    char *type;

    type = Ns_GetMimeType(file);
    if (type != NULL) {
	return Ns_GetTypeEncoding(type);
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
    char *s, *e;

    s = Ns_StrCaseFind(type, "charset");
    if (s != NULL) {
	s += 7;
	s += strspn(s, " ");
	if (*s++ == '=') {
	    s += strspn(s, " ");
	    e = s;
	    while (*e && !isspace(UCHAR(*e))) {
		++e;
	    }
	    return GetCharsetEncoding(s, e-s);
	}
    }
    return NULL;
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
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&encodings, name);
    if (hPtr != NULL) {
	return LoadEncoding(hPtr);
    }
    return NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * NsInitEncodings --
 *
 *	Add default and configured encodings.
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
    Ns_Set *set;
    int     i;

    /*
     * Initialize hash table of encodings and charsets.
     */

    Ns_MutexSetName(&lock, "ns:encodings");
    Tcl_InitHashTable(&encodings, TCL_STRING_KEYS);
    Tcl_InitHashTable(&charsets, TCL_STRING_KEYS);

    /*
     * Add default encodings.
     */

    for (i = 0; builtins[i].charset != NULL; ++i) {
        AddEncoding(builtins[i].charset, builtins[i].name);
    }

    /*
     * Add configured encodings.
     */

    set = Ns_ConfigGetSection("ns/encodings");
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	AddEncoding(Ns_SetKey(set, i), Ns_SetValue(set, i));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * AddEncoding --
 *
 *	Add an encoding.
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
AddEncoding(char *charset, char *name)
{
    Tcl_HashEntry  *hPtr, *h2Ptr;
    Ns_DString	    ds;
    int             new;

    hPtr = Tcl_CreateHashEntry(&encodings, name, &new);
    if (new) {
	Tcl_SetHashValue(hPtr, (Tcl_Encoding) -1);
    }
    if (charset != NULL) {
	Ns_DStringInit(&ds);
	charset = Ns_StrToLower(Ns_DStringAppend(&ds, charset));
	h2Ptr = Tcl_CreateHashEntry(&charsets, charset, &new);
	if (!new) {
	    Ns_Log(Warning, "duplicate charset: %s", charset);
	}
	Tcl_SetHashValue(h2Ptr, hPtr);
        Ns_DStringFree(&ds);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * GetCharsetEncoding --
 *
 *	Cleanup charset name and return encoding, if any.
 *
 * Results:
 *	Tcl_Encoding or null if not known or not loaded.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static Tcl_Encoding
GetCharsetEncoding(char *charset, int len)
{
    Tcl_HashEntry *hPtr;
    Tcl_Encoding encoding;
    Ns_DString ds;

    encoding = NULL;
    Ns_DStringInit(&ds);
    Ns_DStringNAppend(&ds, charset, len);
    charset = Ns_StrTrim(Ns_StrToLower(ds.string));
    hPtr = Tcl_FindHashEntry(&charsets, charset);
    if (hPtr != NULL) {
	encoding = LoadEncoding(hPtr);
    }
    Ns_DStringFree(&ds);
    return encoding;
}


/*
 *----------------------------------------------------------------------
 *
 * LoadEncoding --
 *
 *	Return the Tcl_Encoding stored in the given hash entry,
 *	loading it the first time if necessary.
 *
 * Results:
 *	Tcl_Encoding or NULL if couldn't be loaded. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static Tcl_Encoding
LoadEncoding(Tcl_HashEntry *hPtr)
{
    Tcl_Encoding  encoding;
    char *name;

    Ns_MutexLock(&lock);
    encoding = Tcl_GetHashValue(hPtr);
    if (encoding == (Tcl_Encoding) -1) {
	name = Tcl_GetHashKey(&encodings, hPtr);
	encoding = Tcl_GetEncoding(NULL, name);
	if (encoding == NULL) {
	    Ns_Log(Warning, "could not load encoding: %s", name);
	} else {
	    Ns_Log(Notice, "loaded encoding: %s", name);
	}
	Tcl_SetHashValue(hPtr, encoding);
    }
    Ns_MutexUnlock(&lock);
    return encoding;
}

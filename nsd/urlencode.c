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
 * urlencode.c --
 *
 *	Encode and decode URLs, as described in RFC 1738.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/urlencode.c,v 1.17 2005/03/25 00:39:47 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static functions defined in this file.
 */

static Tcl_Encoding  GetUrlEncoding(char *charset);

/*
 * The following table is used for URL encoding and decoding
 * all 256 characters.
 */

struct {
    int   hex;	    /* Valid hex value or -1. */
    int   len;	    /* Length required to encode string. */
    char *str;	    /* String for multibyte encoded character. */
} enc[] = {
    {-1, 3, "00"}, {-1, 3, "01"}, {-1, 3, "02"}, {-1, 3, "03"}, 
    {-1, 3, "04"}, {-1, 3, "05"}, {-1, 3, "06"}, {-1, 3, "07"}, 
    {-1, 3, "08"}, {-1, 3, "09"}, {-1, 3, "0a"}, {-1, 3, "0b"}, 
    {-1, 3, "0c"}, {-1, 3, "0d"}, {-1, 3, "0e"}, {-1, 3, "0f"}, 
    {-1, 3, "10"}, {-1, 3, "11"}, {-1, 3, "12"}, {-1, 3, "13"}, 
    {-1, 3, "14"}, {-1, 3, "15"}, {-1, 3, "16"}, {-1, 3, "17"}, 
    {-1, 3, "18"}, {-1, 3, "19"}, {-1, 3, "1a"}, {-1, 3, "1b"}, 
    {-1, 3, "1c"}, {-1, 3, "1d"}, {-1, 3, "1e"}, {-1, 3, "1f"}, 
    {-1, 1, NULL}, {-1, 3, "21"}, {-1, 3, "22"}, {-1, 3, "23"}, 
    {-1, 3, "24"}, {-1, 3, "25"}, {-1, 3, "26"}, {-1, 3, "27"}, 
    {-1, 3, "28"}, {-1, 3, "29"}, {-1, 3, "2a"}, {-1, 3, "2b"}, 
    {-1, 3, "2c"}, {-1, 3, "2d"}, {-1, 3, "2e"}, {-1, 3, "2f"}, 
    { 0, 1, NULL}, { 1, 1, NULL}, { 2, 1, NULL}, { 3, 1, NULL}, 
    { 4, 1, NULL}, { 5, 1, NULL}, { 6, 1, NULL}, { 7, 1, NULL}, 
    { 8, 1, NULL}, { 9, 1, NULL}, {-1, 3, "3a"}, {-1, 3, "3b"}, 
    {-1, 3, "3c"}, {-1, 3, "3d"}, {-1, 3, "3e"}, {-1, 3, "3f"}, 
    {-1, 3, "40"}, {10, 1, NULL}, {11, 1, NULL}, {12, 1, NULL}, 
    {13, 1, NULL}, {14, 1, NULL}, {15, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 3, "5b"}, 
    {-1, 3, "5c"}, {-1, 3, "5d"}, {-1, 3, "5e"}, {-1, 3, "5f"}, 
    {-1, 3, "60"}, {10, 1, NULL}, {11, 1, NULL}, {12, 1, NULL}, 
    {13, 1, NULL}, {14, 1, NULL}, {15, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, 
    {-1, 1, NULL}, {-1, 1, NULL}, {-1, 1, NULL}, {-1, 3, "7b"}, 
    {-1, 3, "7c"}, {-1, 3, "7d"}, {-1, 3, "7e"}, {-1, 3, "7f"}, 
    {-1, 3, "80"}, {-1, 3, "81"}, {-1, 3, "82"}, {-1, 3, "83"}, 
    {-1, 3, "84"}, {-1, 3, "85"}, {-1, 3, "86"}, {-1, 3, "87"}, 
    {-1, 3, "88"}, {-1, 3, "89"}, {-1, 3, "8a"}, {-1, 3, "8b"}, 
    {-1, 3, "8c"}, {-1, 3, "8d"}, {-1, 3, "8e"}, {-1, 3, "8f"}, 
    {-1, 3, "90"}, {-1, 3, "91"}, {-1, 3, "92"}, {-1, 3, "93"}, 
    {-1, 3, "94"}, {-1, 3, "95"}, {-1, 3, "96"}, {-1, 3, "97"}, 
    {-1, 3, "98"}, {-1, 3, "99"}, {-1, 3, "9a"}, {-1, 3, "9b"}, 
    {-1, 3, "9c"}, {-1, 3, "9d"}, {-1, 3, "9e"}, {-1, 3, "9f"}, 
    {-1, 3, "a0"}, {-1, 3, "a1"}, {-1, 3, "a2"}, {-1, 3, "a3"}, 
    {-1, 3, "a4"}, {-1, 3, "a5"}, {-1, 3, "a6"}, {-1, 3, "a7"}, 
    {-1, 3, "a8"}, {-1, 3, "a9"}, {-1, 3, "aa"}, {-1, 3, "ab"}, 
    {-1, 3, "ac"}, {-1, 3, "ad"}, {-1, 3, "ae"}, {-1, 3, "af"}, 
    {-1, 3, "b0"}, {-1, 3, "b1"}, {-1, 3, "b2"}, {-1, 3, "b3"}, 
    {-1, 3, "b4"}, {-1, 3, "b5"}, {-1, 3, "b6"}, {-1, 3, "b7"}, 
    {-1, 3, "b8"}, {-1, 3, "b9"}, {-1, 3, "ba"}, {-1, 3, "bb"}, 
    {-1, 3, "bc"}, {-1, 3, "bd"}, {-1, 3, "be"}, {-1, 3, "bf"}, 
    {-1, 3, "c0"}, {-1, 3, "c1"}, {-1, 3, "c2"}, {-1, 3, "c3"}, 
    {-1, 3, "c4"}, {-1, 3, "c5"}, {-1, 3, "c6"}, {-1, 3, "c7"}, 
    {-1, 3, "c8"}, {-1, 3, "c9"}, {-1, 3, "ca"}, {-1, 3, "cb"}, 
    {-1, 3, "cc"}, {-1, 3, "cd"}, {-1, 3, "ce"}, {-1, 3, "cf"}, 
    {-1, 3, "d0"}, {-1, 3, "d1"}, {-1, 3, "d2"}, {-1, 3, "d3"}, 
    {-1, 3, "d4"}, {-1, 3, "d5"}, {-1, 3, "d6"}, {-1, 3, "d7"}, 
    {-1, 3, "d8"}, {-1, 3, "d9"}, {-1, 3, "da"}, {-1, 3, "db"}, 
    {-1, 3, "dc"}, {-1, 3, "dd"}, {-1, 3, "de"}, {-1, 3, "df"}, 
    {-1, 3, "e0"}, {-1, 3, "e1"}, {-1, 3, "e2"}, {-1, 3, "e3"}, 
    {-1, 3, "e4"}, {-1, 3, "e5"}, {-1, 3, "e6"}, {-1, 3, "e7"}, 
    {-1, 3, "e8"}, {-1, 3, "e9"}, {-1, 3, "ea"}, {-1, 3, "eb"}, 
    {-1, 3, "ec"}, {-1, 3, "ed"}, {-1, 3, "ee"}, {-1, 3, "ef"}, 
    {-1, 3, "f0"}, {-1, 3, "f1"}, {-1, 3, "f2"}, {-1, 3, "f3"}, 
    {-1, 3, "f4"}, {-1, 3, "f5"}, {-1, 3, "f6"}, {-1, 3, "f7"}, 
    {-1, 3, "f8"}, {-1, 3, "f9"}, {-1, 3, "fa"}, {-1, 3, "fb"}, 
    {-1, 3, "fc"}, {-1, 3, "fd"}, {-1, 3, "fe"}, {-1, 3, "ff"}
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_EncodeUrlWithEncoding --
 *
 *	Take a URL and encode any non-alphanumeric characters into 
 *	%hexcode 
 *
 * Results:
 *	A pointer to the encoded string (which is part of the 
 *	passed-in DString's memory) 
 *
 * Side effects:
 *	Encoded URL will be copied to given dstring.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_EncodeUrlWithEncoding(Ns_DString *dsPtr, char *string, Tcl_Encoding encoding)
{
    register int   i, n;
    register char *p, *q;
    Tcl_DString  ds;

    Tcl_DStringInit(&ds);
    if (encoding != NULL) {
        string = Tcl_UtfToExternalDString(encoding, string, -1, &ds);
    }

    /*
     * Determine and set the required dstring length.
     */

    p = string;
    n = 0;
    while ((i = UCHAR(*p)) != 0) {
	n += enc[i].len;
	++p;
    }
    i = dsPtr->length;
    Ns_DStringSetLength(dsPtr, dsPtr->length + n);

    /*
     * Copy the result directly to the pre-sized dstring.
     */

    q = dsPtr->string + i;
    p = string;
    while ((i = UCHAR(*p)) != 0) {
	if (UCHAR(*p) == ' ') {
	    *q++ = '+';
	} else if (enc[i].str == NULL) {
	    *q++ = *p;
	} else {
	    *q++ = '%';
	    *q++ = enc[i].str[0];
	    *q++ = enc[i].str[1];
	}
	++p;
    }
    Tcl_DStringFree(&ds);
    return dsPtr->string;
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_EncodeUrlCharset --
 *
 *	Take a URL and encode any non-alphanumeric characters into 
 *	%hexcode 
 *
 * Results:
 *	A pointer to the encoded string (which is part of the 
 *	passed-in DString's memory) 
 *
 * Side effects:
 *	Encoded URL will be copied to given dstring.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_EncodeUrlCharset(Ns_DString *dsPtr, char *string, char *charset)
{
    Tcl_Encoding encoding = GetUrlEncoding(charset);

    return Ns_EncodeUrlWithEncoding(dsPtr, string, encoding);

}

/*
 *----------------------------------------------------------------------
 *
 * Ns_DecodeUrlCharset --
 *
 *	Decode an encoded URL (with %hexcode, etc.).
 *
 * Results:
 *	A pointer to the dstring's value, containing the decoded 
 *	URL.
 *
 * Side effects:
 *	Decoded URL will be copied to given dstring.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DecodeUrlCharset(Ns_DString *dsPtr, char *string, char *charset)
{
    Tcl_Encoding  encoding = GetUrlEncoding(charset);

    return Ns_DecodeUrlWithEncoding(dsPtr, string, encoding);
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_DecodeUrlWithEncoding --
 *
 *	Decode an encoded URL (with %hexcode, etc.).
 *
 * Results:
 *	A pointer to the dstring's value, containing the decoded 
 *	URL.
 *
 * Side effects:
 *	Decoded URL will be copied to given dstring.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DecodeUrlWithEncoding(Ns_DString *dsPtr, char *string, Tcl_Encoding encoding)
{
    register int i, j, n;
    register char *p, *q;
    char c;
    Ns_DString	 *outPtr, out;
    Tcl_DString   ds;

    /*
     * If using an encoding, copy output to a scratch buffer instead
     * of directly to given dstring.
     */

    if (encoding == NULL) {
	outPtr = dsPtr;
    } else {
    	Ns_DStringInit(&out);
	outPtr = &out;
    }

    /*
     * Expand the output to the length of the input
     * string which will be the largest size required.
     */

    n = strlen(string);
    i = dsPtr->length;
    Ns_DStringSetLength(outPtr, i + n);
    q = outPtr->string + i;
    p = string;
    while ((c = UCHAR(*p)) != '\0') {
	if (c == '%' && n > 2 &&
	    (i = enc[UCHAR(p[1])].hex) >= 0 &&
	    (j = enc[UCHAR(p[2])].hex) >= 0) {
	    *q = (unsigned char) ((i << 4) + j);
	    n -= 2;
	    p += 2;
	} else if (c == '+') {
	    *q = ' ';
	} else {
	    *q = c;
	}
	--n, ++q, ++p;
    }

    /*
     * Terminate the dstring, decoding to utf8 if necessary.
     */

    n = q - outPtr->string;
    if (outPtr == dsPtr) {
        Ns_DStringSetLength(dsPtr, n);
    } else {
        Tcl_ExternalToUtfDString(encoding, outPtr->string, n, &ds);
        Ns_DStringAppend(dsPtr, ds.string);
        Tcl_DStringFree(&ds);
    	Ns_DStringFree(outPtr);
    }
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUrlEncodeObjCmd, NsTclUrlDecodeObjCmd --
 *
 *	Implements ns_urlencode and ns_urldecode.
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
EncodeObjCmd(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[], int encode)
{
    Tcl_DString ds;
    char        *charset;
    char        *data;

    if (objc == 2) {
        charset = NULL;
        data = Tcl_GetString(objv[1]);
    } else if (objc == 4 && STREQ(Tcl_GetString(objv[1]), "-charset")) {
        charset = Tcl_GetString(objv[2]);
        data = Tcl_GetString(objv[3]);
    } else {
	Tcl_WrongNumArgs(interp, 1, objv, "?-charset charset? data");
        return TCL_ERROR;
    }
    Tcl_DStringInit(&ds);
    if (encode) {
	Ns_EncodeUrlCharset(&ds, data, charset);
    } else {
	Ns_DecodeUrlCharset(&ds, data, charset);
    }
    Tcl_DStringResult(interp, &ds);
    Tcl_DStringFree(&ds);
    return TCL_OK;
}

int
NsTclUrlDecodeObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj *CONST objv[])
{
    return EncodeObjCmd(interp, objc, objv, 0);
}

int
NsTclUrlEncodeObjCmd(ClientData arg, Tcl_Interp *interp, int objc,
		     Tcl_Obj *CONST objv[])
{
    return EncodeObjCmd(interp, objc, objv, 1);
}


/*
 *----------------------------------------------------------------------
 *
 * GetUrlEncoding --
 *
 *	Get the encoding to use in Ns_EncodeUrl/Ns_DecodeUrl,
 *	utilizing server defaults for current connection if
 *	necessary.
 *
 * Results:
 *	A Tcl_Encoding.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static Tcl_Encoding
GetUrlEncoding(char *charset)
{
    Tcl_Encoding  encoding = NULL;
    Ns_Conn *conn;

    if (charset != NULL) {
	encoding = Ns_GetCharsetEncoding(charset);
    }

    /*
     * Use server default for current connection, if any.
     */
 
    if (encoding == NULL && (conn = Ns_GetConn()) != NULL) {
	encoding = Ns_ConnGetUrlEncoding(conn);
    }
    return encoding;
}

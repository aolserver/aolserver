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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/urlencode.c,v 1.1.1.1 2000/05/02 13:48:22 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int HexDigit(char c);

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Ns_EncodeUrl --
 *
 *	Take a URL and encode any non-alphanumeric characters into 
 *	%hexcode 
 *
 * Results:
 *	A pointer to the encoded string (which is part of the 
 *	passed-in DString's memory) 
 *
 * Side effects:
 *	Will append to the passed-in dstring 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_EncodeUrl(Ns_DString *pds, char *string)
{
    static char safechars[] =
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789";

    while (*string != '\0') {

        if (strchr(safechars, *string) != NULL) {
            Ns_DStringNAppend(pds, string, 1);
        } else {
            char buf[4];
            sprintf(buf, "%%%02x", (unsigned char) *string);
	    Ns_DStringNAppend(pds, buf, 3);
        }
        ++string;
    }
    return pds->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DecodeUrl --
 *
 *	Take an encoded URL (with %hexcode, etc.) and decode it into 
 *	the DString.
 *
 * Results:
 *	A pointer to the dstring's memory, containing the decoded 
 *	URL, or NULL if incorrectly encoded.
 *
 * Side effects:
 *	Will append stuff to pds.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DecodeUrl(Ns_DString *pds, char *string)
{
    unsigned char decoded;
    char          twobytes[3];

    twobytes[2] = '\0';
    while (*string != '\0') {
        if (*string != '%') {
            Ns_DStringNAppend(pds, string, 1);
            ++string;
        } else {
            if (HexDigit(string[1]) && HexDigit(string[2])) {
                twobytes[0] = string[1];
                twobytes[1] = string[2];
                decoded = (unsigned char) strtol(twobytes, (char **) NULL, 16);
                Ns_DStringNAppend(pds, (char *) &decoded, 1);
                string += 3;
            } else {
                return NULL;
            }
        }
    }
    return pds->string;
}


/* 
 *==========================================================================
 * Exported functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * NsTclUrlEncodeCmd --
 *
 *	Implments ns_urlencode. 
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
NsTclUrlEncodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " data\"", NULL);
        return TCL_ERROR;
    }

    Ns_DStringInit(&ds);

    Ns_UrlEncode(&ds, argv[1]);

    Tcl_SetResult(interp, Ns_DStringExport(&ds), (Tcl_FreeProc *) ns_free);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUrlDecodeCmd --
 *
 *	Implements ns_urldecode. 
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
NsTclUrlDecodeCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DString ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " data\"", NULL);
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    if (Ns_UrlDecode(&ds, argv[1]) == NULL) {
        Ns_DStringFree(&ds);
    }
    Tcl_SetResult(interp, Ns_DStringExport(&ds), (Tcl_FreeProc *) ns_free);
    
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
 * HexDigit --
 *
 *	Determines if a character is a valid hexadecimal digit.
 *
 * Results:
 *	Boolean: true if char is a valid hex digit.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
HexDigit(char c)
{
    static char     hexdigits[] = "0123456789ABCDEFabcdef";

    return (c != '\0' && (strchr(hexdigits, c) != NULL));
}

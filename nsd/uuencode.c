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
 * uuencode.c --
 *
 *      Uuencoding and decoding routines which map 8-bit binary bytes
 *	into 6-bit ascii characters.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/uuencode.c,v 1.2 2001/03/22 21:39:27 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following array specify the output ascii character for each
 * of the 64 6-bit characters.
 */

static char    six2pr[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

/*
 * The following array maps all 256 8-bit ascii characters to
 * either the corresponding 6-bit value or -1 for invalid character.
 */

static int pr2six[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1, 
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, 
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

#define ENC(c) (six2pr[(c)])
#define DEC(c) ((unsigned char) pr2six[(int)(c)])


/*
 *----------------------------------------------------------------------
 *
 * Ns_HtuuEncode --
 *
 *      Encode a string.
 *
 * Results:
 *      Number of bytes placed in output.
 *
 * Side effects:
 *      Encoded characters are placed in output which must be
 *	large enough for the result, i.e., (1 + (len * 4) / 3)
 *	bytes.
 *
 *----------------------------------------------------------------------
 */

int
Ns_HtuuEncode(unsigned char *input, unsigned int len, char *output)
{
    register unsigned char  *p, *q;
    register int n;

    /*
     * Convert every three input bytes into four output
     * characters.
     */

    p = input;
    q = output;
    for (n = len / 3; n > 0; --n) {
	*q++ = ENC(p[0] >> 2);
	*q++ = ENC(((p[0] << 4) & 060) | ((p[1] >> 4) & 017));
	*q++ = ENC(((p[1] << 2) & 074) | ((p[2] >> 6) & 03));
	*q++ = ENC(p[2] & 077);
	p += 3;
    }

    /*
     * Convert and pad any remaining bytes.
     */

    n = len % 3;
    if (n > 0) {
	*q++ = ENC(p[0] >> 2);
	if (n == 1) {
	    *q++ = ENC((p[0] << 4) & 060);
	    *q++ = '=';
	} else {
	    *q++ = ENC(((p[0] << 4) & 060) | ((p[1] >> 4) & 017));
	    *q++ = ENC((p[1] << 2) & 074);
	}
	*q++ = '=';
    }
    *q = '\0';
    return (q - (unsigned char *) output);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_HtuuDecode --
 *
 *      Decode a string.
 *
 * Results:
 *      Number of binary bytes decoded.
 *
 * Side effects:
 *      Decoded characters are placed in output which must be
 *	large enough for the result, i.e., (3 + (len * 3) / 4)
 *	bytes.
 *
 *----------------------------------------------------------------------
 */

int
Ns_HtuuDecode(char *input, unsigned char *output, int outputlen)
{
    register unsigned char *p, *q;
    register int    len, n;


    /*
     * Skip leading space, if any.
     */

    while (*input == ' ' || *input == '\t') {
	++input;
    }

    /*
     * Determine the maximum length of output bytes.
     */

    p = input;
    while (pr2six[(int)(*p)] >= 0) {
	++p;
    }
    len = p - (unsigned char *) input;

    /*
     * Decode every four input bytes.
     */

    p = input;
    q = output;
    for (n = len / 4; n > 0; --n) {
	*q++ = DEC(p[0]) << 2 | DEC(p[1]) >> 4;
	*q++ = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
	*q++ = DEC(p[2]) << 6 | DEC(p[3]);
	p += 4;
    }

    /*
     * Decode remaining 2 or 3 bytes.
     */

    n = len % 4;
    if (n > 1) {
	*q++ = DEC(p[0]) << 2 | DEC(p[1]) >> 4;
    }
    if (n > 2) {
	*q++ = DEC(p[1]) << 4 | DEC(p[2]) >> 2;
    }
    if ((q - output) < outputlen) {
	*q = '\0';
    }
    return (q - output);
}

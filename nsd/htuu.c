/*
 * MODULE                                                       HTUU.c 
 *
 * UENCODE AND UUDECODE 
 * 
 * ACKNOWLEDGEMENT: 
 *   This code is taken from rpem distribution, and was originally written 
 * by Mark Riordan.
 * 
 * AUTHORS:
 *   MR      Mark Riordan    riordanmr@clvax1.cl.msu.edu
 *   AL      Ari Luotonen luotonen@dxcern.cern.ch 
 * 
 * HISTORY:
 *   Added as part of the WWW library and edited to conform with
 * the WWW project coding standards by: AL  5 Aug 1993
 * Originally written
 * by:                          MR 12 Aug 1990
 * Original header text:
 * -------------------------------------------------------------
 * File containing routines to convert a buffer of bytes to/from RFC 1113
 * printable encoding format.
 * 
 * This technique is similar to the familiar Unix uuencode format in that it
 * maps 6 binary bits to one ASCII character (or more aptly, 3 binary
 * bytes to 4 ASCII characters).  However, RFC 1113 does not use the same 
 * mapping to printable characters as uuencode. 
 * 
 * Mark Riordan   12 August 1990 and 17 Feb 1991.
 * This code is hereby placed in the public domain. 
 * ------------------------------------------------------------- 
 * 
 * BUGS: 
 * 
 */


/*
 * htuu.c --
 *
 *      Uuencoding and decoding routines.
 *
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/htuu.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static char    six2pr[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static unsigned char pr2six[256];


/*--- function HTUU_encode -----------------------------------------------
 *
 *   Encode a single line of binary data to a standard format that
 *   uses only printing ASCII characters (but takes up 33% more bytes).
 *
 *    Entry    bufin    points to a buffer of bytes.  If nbytes is not
 *                      a multiple of three, then the byte just beyond
 *                      the last byte in the buffer must be 0.
 *             nbytes   is the number of bytes in that buffer.
 *                      This cannot be more than 48.
 *             bufcoded points to an output buffer.  Be sure that this
 *                      can hold at least 1 + (4*nbytes)/3 characters.
 *
 *    Exit     bufcoded contains the coded line.  The first 4*nbytes/3 bytes
 *                      contain printing ASCII characters representing
 *                      those binary bytes. This may include one or
 *                      two '=' characters used as padding at the end.
 *                      The last byte is a zero byte.
 *             Returns the number of ASCII characters in "bufcoded".
 */
int
Ns_HtuuEncode(unsigned char *bufin, unsigned int nbytes, char * bufcoded)
{
   /* ENC is the basic 1 character encoding function to make a char printing */
#define ENC(c) six2pr[c]

    register char  *outptr = bufcoded;
    unsigned int    i;

    for (i = 0; i < nbytes; i += 3) {
		/* c1 */
        *(outptr++) = ENC(*bufin >> 2);
		/* c2 */
        *(outptr++) = ENC(((*bufin << 4) & 060) | ((bufin[1] >> 4) & 017));
		/* c3 */
        *(outptr++) = ENC(((bufin[1] << 2) & 074) | ((bufin[2] >> 6) & 03));
		/* c4 */
        *(outptr++) = ENC(bufin[2] & 077);      

        bufin += 3;
    }

    /*
     * If nbytes was not a multiple of 3, then we have encoded too many
     * characters.  Adjust appropriately.
     */
    if (i == nbytes + 1) {
        /* There were only 2 bytes in that last group */
        outptr[-1] = '=';
    } else if (i == nbytes + 2) {
        /* There was only 1 byte in that last group */
        outptr[-1] = '=';
        outptr[-2] = '=';
    }
    *outptr = '\0';
    return (outptr - bufcoded);
}


/*--- function HTUU_decode ------------------------------------------------
 *
 *  Decode an ASCII-encoded buffer back to its original binary form.
 *
 *    Entry    bufcoded    points to a uuencoded string.  It is
 *                         terminated by any character not in
 *                         the printable character table six2pr, but
 *                         leading whitespace is stripped.
 *             bufplain    points to the output buffer; must be big
 *                         enough to hold the decoded string (generally
 *                         shorter than the encoded string) plus
 *                         as many as two extra bytes used during
 *                         the decoding process.
 *             outbufsize  is the maximum number of bytes that
 *                         can fit in bufplain.
 *
 *    Exit     Returns the number of binary bytes decoded.
 *             bufplain    contains these bytes.
 */
int
Ns_HtuuDecode(char * bufcoded, unsigned char * bufplain, int outbufsize)
{
    /* single character decode */
#define DEC(c) pr2six[(int)c]
#define MAXVAL 63

    static int      first = 1;

    int             nbytesdecoded, j;
    register char  *bufin = bufcoded;
    register unsigned char *bufout = bufplain;
    register int    nprbytes;

    /*
     * If this is the first call, initialize the mapping table. This code
     * should work even on non-ASCII machines.
     */
    if (first) {
        first = 0;
        for (j = 0; j < 256; j++)
            pr2six[j] = MAXVAL + 1;

        for (j = 0; j < 64; j++)
            pr2six[(int) six2pr[j]] = (unsigned char) j;
    }

    /* Strip leading whitespace. */

    while (*bufcoded == ' ' || *bufcoded == '\t')
        bufcoded++;

    /*
     * Figure out how many characters are in the input buffer. If this would
     * decode into more bytes than would fit into the output buffer, adjust
     * the number of input bytes downwards.
     */
    bufin = bufcoded;
    while (pr2six[(int) *(bufin++)] <= MAXVAL);
    nprbytes = bufin - bufcoded - 1;
    nbytesdecoded = ((nprbytes + 3) / 4) * 3;
    if (nbytesdecoded > outbufsize) {
        nprbytes = (outbufsize * 4) / 3;
    }
    bufin = bufcoded;

    while (nprbytes > 0) {
        *(bufout++) = (unsigned char) (DEC(*bufin) << 2 | DEC(bufin[1]) >> 4);
        *(bufout++) = (unsigned char) (DEC(bufin[1]) << 4 |
				       DEC(bufin[2]) >> 2);
        *(bufout++) = (unsigned char) (DEC(bufin[2]) << 6 | DEC(bufin[3]));
        bufin += 4;
        nprbytes -= 4;
    }

    if (nprbytes & 03) {
        if (pr2six[(int) bufin[-2]] > MAXVAL) {
            nbytesdecoded -= 2;
        } else {
            nbytesdecoded -= 1;
        }
    }
    return (nbytesdecoded);
}

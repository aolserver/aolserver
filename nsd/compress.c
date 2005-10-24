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
 * compress.c --
 *
 * Support for simple gzip compression using Zlib.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/compress.c,v 1.1.2.4 2005/10/24 16:33:36 mooooooo Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

#ifdef HAVE_ZLIB_H
#include <zlib.h>

static char header[] = {
    037, 0213,  /* GZIP magic number. */
    010,        /* Z_DEFLATED */
    0,          /* flags */
    0,0,0,0,    /* timestamp */
    0,          /* xflags */
    03};        /* Unix OS_CODE */


/*
 *----------------------------------------------------------------------
 *
 * Ns_CompressGzip --
 *
 *      Compress a string using gzip with RFC 1952 header/footer.
 *
 * Results:
 *      NS_OK if compression worked, NS_ERROR otherwise.
 *
 * Side effects:
 *      Will write compressed content to given Tcl_DString.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CompressGzip(char *buf, int len, Tcl_DString *outPtr, int level)
{
    uLongf glen;
    char *gbuf;
    uLong crc;
    int skip;
    uint32_t footer[2];

    /*
     * Grow buffer for header, footer, and maximum compressed size.
     */

    glen = compressBound(len) + sizeof(header) + sizeof(footer);
    Tcl_DStringSetLength(outPtr, glen);

    /*
     * Compress output starting 2-bytes from the end of the header. 
     */

    gbuf = outPtr->string;
    skip = sizeof(header) - 2;
    glen -= skip;
    if (compress2(gbuf + skip, &glen, buf, (uLong) len, level) != Z_OK) {
        return NS_ERROR;
    }
    glen -= 4;
    memcpy(gbuf, header, sizeof(header));
    Tcl_DStringSetLength(outPtr, glen + skip);

    /*
     * Append footer of CRC and uncompressed length.
     */

    crc = crc32(0, Z_NULL, 0);
    crc = crc32(crc, buf, len);
    footer[0] = crc;
    footer[1] = len;
    Tcl_DStringAppend(outPtr, (char *) footer, sizeof(footer));

    return NS_OK;
}

#else

int
Ns_CompressGzip(char *buf, int len, Tcl_DString *outPtr, int level)
{
    return NS_ERROR;
}

#endif


/*
 *----------------------------------------------------------------------
 *
 * Ns_Compress --
 *
 *      Deprecated:  Used by nsjk2 module.  Remove this once nsjk2 has
 *      been updated to use Ns_CompressGzip directly.
 *
 * Results:
 *      NS_OK if compression worked, NS_ERROR otherwise.
 *
 * Side effects:
 *      Will write compressed content to given Tcl_DString.
 *
 *----------------------------------------------------------------------
 */
int
Ns_Compress(char *buf, int len, Tcl_DString *outPtr, int level)
{
    return Ns_CompressGzip(buf, len, outPtr, level);
}


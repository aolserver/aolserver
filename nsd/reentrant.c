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
 * reentrant.c --
 *
 *	Reentrant versions of common system utilities using per-thread
 *	data buffers.  See the corresponding manual page for details.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/reentrant.c,v 1.1 2001/11/05 20:26:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure maintains state for the
 * reentrant wrappers.
 */

typedef struct Tls {
    char	    	nabuf[16];
    char	       *stbuf;
    struct tm   	gtbuf;
    struct tm   	ltbuf;
    char		ctbuf[27];
    char		asbuf[27];
    struct {
	struct dirent ent;
	char name[PATH_MAX+1];
    } rdbuf;
} Tls;

static Ns_Tls tls;

void
NsInitReentrant(void)
{
    Ns_TlsAlloc(&tls, ns_free);
}

static Tls *
GetTls(void)
{
    Tls *tlsPtr;

    tlsPtr = Ns_TlsGet(&tls);
    if (tlsPtr == NULL) {
	tlsPtr = ns_calloc(1, sizeof(Tls));
	Ns_TlsSet(&tls, tlsPtr);
    }
    return tlsPtr;
}

struct dirent *
ns_readdir(DIR * dir)
{
    struct dirent *ent;
    Tls *tlsPtr = GetTls();

    ent = &tlsPtr->rdbuf.ent; 
    if (readdir_r(dir, ent, &ent) != 0) {
	ent = NULL;
    }
    return ent;
}

struct tm *
ns_localtime(const time_t * clock)
{
    Tls *tlsPtr = GetTls();

    return localtime_r(clock, &tlsPtr->ltbuf);
}


struct tm *
ns_gmtime(const time_t * clock)
{
    Tls *tlsPtr = GetTls();

    return gmtime_r(clock, &tlsPtr->gtbuf);
}


char *
ns_ctime(const time_t * clock)
{
    Tls *tlsPtr = GetTls();

    return ctime_r(clock, tlsPtr->ctbuf);
}


char *
ns_asctime(const struct tm *tmPtr)
{
    Tls *tlsPtr = GetTls();

    return asctime_r(tmPtr, tlsPtr->asbuf);
}


char *
ns_strtok(char *s1, const char *s2)
{
    Tls *tlsPtr = GetTls();

    return strtok_r(s1, s2, &tlsPtr->stbuf);
}


char *
ns_inet_ntoa(struct in_addr addr)
{
    Tls *tlsPtr = GetTls();
    union {
    	unsigned long l;
    	unsigned char b[4];
    } u;
    
    u.l = (unsigned long) addr.s_addr;
    sprintf(tlsPtr->nabuf, "%u.%u.%u.%u", u.b[0], u.b[1], u.b[2], u.b[3]);
    return tlsPtr->nabuf;
}

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
 * osx.c --
 *
 *	Routines missing from OS/X.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/osx.c,v 1.1 2001/11/05 20:26:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

int
readdir_r(DIR * dir, struct dirent *ent, struct dirent **entPtr)
{
    static Ns_Mutex lock;
    struct dirent *res;

    Ns_MutexLock(&lock);
    res = readdir(dir);
    if (res != NULL) {
	memcpy(ent, res,
	       sizeof(*res) - sizeof(res->d_name) + res->d_namlen + 1);
    }
    Ns_MutexUnlock(&lock);
    *entPtr = res;
    return (res ? 0 : errno);
}

static struct tm *
tmtime_r(const time_t * clock, struct tm *ptmPtr, int gm)
{
    static Ns_Mutex lock;
    struct tm *ptm;

    Ns_MutexLock(&lock);
    if (gm) {
    	ptm = localtime(clock);
    } else {
    	ptm = gmtime(clock);
    }
    if (ptm != NULL) {
	*ptmPtr = *ptm;
	ptm = ptmPtr;
    }
    Ns_MutexUnlock(&lock);
    return ptm;
}

struct tm *
localtime_r(const time_t * clock, struct tm *ptmPtr)
{
    return tmtime_r(clock, ptmPtr, 0);
}

struct tm *
gmtime_r(const time_t * clock, struct tm *ptmPtr)
{
    return tmtime_r(clock, ptmPtr, 1);
}

char *
ctime_r(const time_t * clock, char *buf)
{
    static Ns_Mutex lock;
    char *s;

    Ns_MutexLock(&lock);
    s = ctime(clock);
    if (s != NULL) {
	strcpy(buf, s);
	s = buf;
    }
    Ns_MutexUnlock(&lock);
    return s;
}


char *
asctime_r(const struct tm *tmPtr, char *buf)
{
    static Ns_Mutex lock;
    char *s;

    Ns_MutexLock(&lock);
    s = asctime(tmPtr);
    if (s != NULL) {
	strcpy(buf, s);
	s = buf;
    }
    Ns_MutexUnlock(&lock);
    return s;
}

int
pthread_sigmask(int how, sigset_t *set, sigset_t *oset)
{
    return sigprocmask(how, set, oset);
}

/*
 * Modified from the LinuxThreads source.
 */

static Ns_Tls key;

static void
sigwaithandler(int sig)
{
    Ns_TlsSet(&key, (void *) sig);
}

int
sigwait(sigset_t * set, int *sig)
{
    sigset_t        mask;
    int             s;
    struct sigaction action, saved_signals[NSIG];
    static volatile int initialized = 0;

    if (!initialized) {
	Ns_MasterLock();
	if (!initialized) {
	    Ns_TlsAlloc(&key, NULL);
	}
	Ns_MasterUnlock();
    }
    Ns_TlsSet(&key, (void *) 0);
    sigfillset(&mask);
    for (s = 0; s < NSIG; s++) {
	if (sigismember(set, s)) {
	    sigdelset(&mask, s);
	    action.sa_handler = sigwaithandler;
	    sigfillset(&action.sa_mask);
	    action.sa_flags = 0;
	    sigaction(s, &action, &(saved_signals[s]));
	}
    }
    sigsuspend(&mask);
    for (s = 0; s < NSIG; s++) {
	if (sigismember(set, s)) {
	    sigaction(s, &(saved_signals[s]), NULL);
	}
    }
    *sig = Ns_TlsGet(&key);
    return 0;
}

/*
 * Copyright (c) 1998 Softweyr LLC.  All rights reserved.
 *
 * strtok_r, from Berkeley strtok
 * Oct 13, 1998 by Wes Peters <wes@softweyr.com>
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notices, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *
 *	This product includes software developed by Softweyr LLC, the
 *      University of California, Berkeley, and its contributors.
 *
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY SOFTWEYR LLC, THE REGENTS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL SOFTWEYR LLC, THE
 * REGENTS, OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stddef.h>
#include <string.h>

char *
strtok_r(char *s, const char *delim, char **last)
{
    char *spanp;
    int c, sc;
    char *tok;

    if (s == NULL && (s = *last) == NULL)
    {
	return NULL;
    }

    /*
     * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
     */
cont:
    c = *s++;
    for (spanp = (char *)delim; (sc = *spanp++) != 0; )
    {
	if (c == sc)
	{
	    goto cont;
	}
    }

    if (c == 0)		/* no non-delimiter characters */
    {
	*last = NULL;
	return NULL;
    }
    tok = s - 1;

    /*
     * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
     * Note that delim must have one NUL; we stop if we see that, too.
     */
    for (;;)
    {
	c = *s++;
	spanp = (char *)delim;
	do
	{
	    if ((sc = *spanp++) == c)
	    {
		if (c == 0)
		{
		    s = NULL;
		}
		else
		{
		    char *w = s - 1;
		    *w = '\0';
		}
		*last = s;
		return tok;
	    }
	}
	while (sc != 0);
    }
    /* NOTREACHED */
}

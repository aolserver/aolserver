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
 * osxcompat.cpp --
 *
 *	Routines missing from OS/X required by AOLserver.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/Attic/osxcompat.c,v 1.1 2002/06/10 22:30:23 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/* Copyright (C) 1994, 1996, 1997 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* Poll the file descriptors described by the NFDS structures starting at
   FDS.  If TIMEOUT is nonzero and not -1, allow TIMEOUT milliseconds for
   an event to occur; if TIMEOUT is -1, block until an event occurs.
   Returns the number of file descriptors with events, zero if timed out,
   or -1 for errors.  */

int
poll (fds, nfds, timeout)
     struct pollfd *fds;
     unsigned long int nfds;
     int timeout;
{
 struct timeval tv, *tvp;
 fd_set rset, wset, xset;
 struct pollfd *f;
 int ready;
 int maxfd = 0;
 
 FD_ZERO (&rset);
 FD_ZERO (&wset);
 FD_ZERO (&xset);
 
 for (f = fds; f < &fds[nfds]; ++f)
   if (f->fd != -1)
   {
    if (f->events & POLLIN)
      FD_SET (f->fd, &rset);
    if (f->events & POLLOUT)
      FD_SET (f->fd, &wset);
    if (f->events & POLLPRI)
      FD_SET (f->fd, &xset);
    if (f->fd > maxfd && (f->events & (POLLIN|POLLOUT|POLLPRI)))
      maxfd = f->fd;
   }
 
 if (timeout < 0) {
    tvp = NULL;
 } else {
    tv.tv_sec = timeout / 1000;
    tv.tv_usec = (timeout % 1000) * 1000;
    tvp = &tv;
 }
 
 ready = select (maxfd + 1, &rset, &wset, &xset, tvp);
 if (ready > 0)
   for (f = fds; f < &fds[nfds]; ++f)
   {
    f->revents = 0;
    if (f->fd >= 0)
    {
     if (FD_ISSET (f->fd, &rset))
       f->revents |= POLLIN;
     if (FD_ISSET (f->fd, &wset))
       f->revents |= POLLOUT;
     if (FD_ISSET (f->fd, &xset))
       f->revents |= POLLPRI;
    }
   }
 
 return ready;
}

static pthread_mutex_t rdlock = PTHREAD_MUTEX_INITIALIZER;

int
readdir_r(DIR * dir, struct dirent *ent, struct dirent **entPtr)
{
    struct dirent *res;

    pthread_mutex_lock(&rdlock);
    res = readdir(dir);
    if (res != NULL) {
	memcpy(ent, res,
	       sizeof(*res) - sizeof(res->d_name) + res->d_namlen + 1);
    }
    pthread_mutex_unlock(&rdlock);
    *entPtr = res;
    return (res ? 0 : errno);
}

static pthread_once_t sigwonce = PTHREAD_ONCE_INIT;
static pthread_key_t sigwkey;

int
pthread_sigmask(int how, sigset_t *set, sigset_t *oset)
{
    return sigprocmask(how, set, oset);
}

static void
sigwaithandler(int sig)
{
    pthread_setspecific(sigwkey, (void *) sig);
}

static void
sigwaitinit(void)
{
    pthread_key_create(&sigwkey, NULL);
}

int
sigwait(sigset_t * set, int *sig)
{
    sigset_t        mask;
    int             s;
    struct sigaction action, saved_signals[NSIG];

    pthread_once(&sigwonce, sigwaitinit);
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
    *sig = (int) pthread_getspecific(sigwkey);
    return 0;
}

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

static pthread_mutex_t ctlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t atlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t ltlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t gmlock = PTHREAD_MUTEX_INITIALIZER;

char *
ctime_r(const time_t * clock, char *buf)
{
    char *s;

    pthread_mutex_lock(&ctlock);
    s = ctime(clock);
    if (s != NULL) {
	strcpy(buf, s);
	s = buf;
    }
    pthread_mutex_unlock(&ctlock);
    return s;
}

char *
asctime_r(const struct tm *tmPtr, char *buf)
{
    char *s;

    pthread_mutex_lock(&atlock);
    s = asctime(tmPtr);
    if (s != NULL) {
	strcpy(buf, s);
	s = buf;
    }
    pthread_mutex_unlock(&atlock);
    return s;
}

static struct tm *
tmtime_r(pthread_mutex_t *lockPtr, const time_t * clock, struct tm *ptmPtr, int gm)
{
    struct tm *ptm;

    pthread_mutex_lock(lockPtr);
    if (gm) {
    	ptm = localtime(clock);
    } else {
    	ptm = gmtime(clock);
    }
    if (ptm != NULL) {
	*ptmPtr = *ptm;
	ptm = ptmPtr;
    }
    pthread_mutex_unlock(lockPtr);
    return ptm;
}

struct tm *
localtime_r(const time_t * clock, struct tm *ptmPtr)
{
    return tmtime_r(&ltlock, clock, ptmPtr, 0);
}

struct tm *
gmtime_r(const time_t * clock, struct tm *ptmPtr)
{
    return tmtime_r(&gmlock, clock, ptmPtr, 1);
}

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
 * fd.c --
 *
 *      Manipulate file descriptors of open files.
 */
 
static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/fd.c,v 1.6 2001/04/23 21:27:46 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#ifdef WIN32
#include <share.h>
#endif

/*
 * The following structure maitains and open temp fd.
 */

typedef struct Tmp {
    struct Tmp *nextPtr;
    int fd;
} Tmp;

static Tmp *firstTmpPtr;
static Ns_Mutex lock;

/*
 * The following constants are defined for this file
 */

#ifndef F_CLOEXEC
#define F_CLOEXEC 1
#endif


/*
 *----------------------------------------------------------------------
 * Ns_CloseOnExec --
 *
 *      Set the close-on-exec flag for a file descriptor
 *
 * Results:
 *      Return NS_OK on success or NS_ERROR on failure
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_CloseOnExec(int fd)
{
#ifdef WIN32
    return NS_OK;
#else
    int             i;
    int status = NS_ERROR;

    i = fcntl(fd, F_GETFD);
    if (i != -1) {
        i |= F_CLOEXEC;
        i = fcntl(fd, F_SETFD, i);
	status = NS_OK;
    }
    return status;
#endif
}


/*
 *----------------------------------------------------------------------
 * Ns_NoCloseOnExec --
 *
 *	Clear the close-on-exec flag for a file descriptor
 *
 * Results:
 *	Return NS_OK on success or NS_ERROR on failure
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
ns_closeonexec(int fd)
{
    return Ns_CloseOnExec(fd);
}

int
Ns_NoCloseOnExec(int fd)
{
#ifdef WIN32
    return NS_OK;
#else
    int             i;
    int status = NS_ERROR;

    i = fcntl(fd, F_GETFD);
    if (i != -1) {
        i &= ~F_CLOEXEC;
        i = fcntl(fd, F_SETFD, i);
	status = NS_OK;
    }
    return status;
#endif
}


/*
 *----------------------------------------------------------------------
 * Ns_DupHigh --
 *
 *      Dup a file descriptor to be 256 or higher
 *
 * Results:
 *      Returns new file discriptor.
 *
 * Side effects:
 *      Original file descriptor is closed.
 *
 *----------------------------------------------------------------------
 */

int
ns_duphigh(int *fdPtr)
{
   return Ns_DupHigh(fdPtr);
}

int
Ns_DupHigh(int *fdPtr)
{
#ifdef USE_DUPHIGH
    int             nfd, ofd, flags;

    ofd = *fdPtr;
    if ((flags = fcntl(ofd, F_GETFD)) < 0) {
	Ns_Log(Warning, "fd: duphigh failed: fcntl(%d, F_GETFD): '%s'",
	       ofd, strerror(errno));
    } else if ((nfd = fcntl(ofd, F_DUPFD, 256)) < 0) {
	Ns_Log(Warning, "fd: duphigh failed: fcntl(%d, F_DUPFD, 256): '%s'",
	       ofd, strerror(errno));
    } else if (fcntl(nfd, F_SETFD, flags) < 0) {
	Ns_Log(Warning, "fd: duphigh failed: fcntl(%d, F_SETFD, %d): '%s'",
	       nfd, flags, strerror(errno));
	close(nfd);
    } else {
	close(ofd);
	*fdPtr = nfd;
    }
#endif
    return *fdPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetTemp --
 *
 *	Pop or allocate a temp file.  Temp files are immediately
 *	removed on Unix and marked non-shared and delete on close
 *	on NT to avoid snooping of data being sent to the CGI.
 *
 * Results:
 *	Open file descriptor.
 *
 * Side effects:
 *	File may be opened.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetTemp(void)
{
    Tmp *tmpPtr;
    Ns_Time now;
    Ns_DString ds;
    char *path, buf[64];
    int fd, flags, trys;
    
    Ns_MutexLock(&lock);
    tmpPtr = firstTmpPtr;
    if (tmpPtr != NULL) {
	firstTmpPtr = tmpPtr->nextPtr;
    }
    Ns_MutexUnlock(&lock);
    if (tmpPtr != NULL) {
	fd = tmpPtr->fd;
	ns_free(tmpPtr);
	return fd;
    }
    Ns_DStringInit(&ds);
    flags = O_RDWR|O_CREAT|O_TRUNC|O_EXCL;
#ifdef WIN32
    flags |= _O_SHORT_LIVED|_O_NOINHERIT|_O_TEMPORARY|_O_BINARY;
#endif
    trys = 0;
    do {
	Ns_GetTime(&now);
	sprintf(buf, "nstmp.%d.%d", (int) now.sec, (int) now.usec);
	path = Ns_MakePath(&ds, P_tmpdir, buf, NULL);
#ifdef WIN32
	fd = _sopen(path, flags, _SH_DENYRW, _S_IREAD|_S_IWRITE);
#else
	fd = open(path, flags, 0600);
#endif
    } while (fd < 0 && trys++ < 10 && errno == EEXIST);
    if (fd < 0) {
	Ns_Log(Error, "tmp: could not open temp file %s: %s",
	       path, strerror(errno));
#ifndef WIN32
    } else {
	Ns_DupHigh(&fd);
	Ns_CloseOnExec(fd);
	if (unlink(path) != 0) {
	    Ns_Log(Warning, "tmp: unlink(%s) failed: %s", path, strerror(errno));
	}
#endif
    }
    Ns_DStringFree(&ds);
    return fd;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ReleaseTemp --
 *
 *	Return a temp file to the pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	File may be closed on error.
 *
 *----------------------------------------------------------------------
 */

void
Ns_ReleaseTemp(int fd)
{
    Tmp *tmpPtr;

    if (lseek(fd, 0, SEEK_SET) != 0 || ftruncate(fd, 0) != 0) {
	close(fd);
    } else {
	tmpPtr = ns_malloc(sizeof(Tmp));
	tmpPtr->fd = fd;
	Ns_MutexLock(&lock);
	tmpPtr->nextPtr = firstTmpPtr;
	firstTmpPtr = tmpPtr;
	Ns_MutexUnlock(&lock);
    }
}

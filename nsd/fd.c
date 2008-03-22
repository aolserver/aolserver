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
 
static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/fd.c,v 1.13 2008/03/22 17:43:39 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#ifdef _WIN32
#define DEVNULL "nul:"
#include <share.h>
#else
#define DEVNULL "/dev/null"
static int ClosePipeOnExec(int *fds);
#ifdef USE_DUPHIGH
static int dupHigh = 0;
#endif
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
static int devNull;

/*
 * The following constants are defined for this file
 */

#ifndef F_CLOEXEC
#define F_CLOEXEC 1
#endif


/*
 *----------------------------------------------------------------------
 *
 * NsInitFd --
 *
 *	Initialize the fd API's.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will open a shared fd to /dev/null and ensure stdin, stdout,
 *	and stderr are open on something.
 *
 *----------------------------------------------------------------------
 */

void
NsInitFd(void)
{
#ifndef _WIN32
    struct rlimit  rl;
#endif
    int fd;

    /*
     * Ensure fd 0, 1, and 2 are open on at least /dev/null.
     */
     
    fd = open(DEVNULL, O_RDONLY);
    if (fd > 0) {
	close(fd);
    }
    fd = open(DEVNULL, O_WRONLY);
    if (fd > 0 && fd != 1) {
	close(fd);
    }
    fd = open(DEVNULL, O_WRONLY);
    if (fd > 0 && fd != 2) {
	close(fd);
    }

#ifndef _WIN32
    /*
     * AOLserver now uses poll() but Tcl and other components may
     * still use select() which will likely break when fd's exceed
     * FD_SETSIZE.  We now allow setting the fd limit above FD_SETSIZE,
     * but do so at your own risk.
     */

    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
	Ns_Log(Warning, "fd: getrlimit(RLIMIT_NOFILE) failed: %s",
	       strerror(errno));
    } else {
	if (rl.rlim_cur != rl.rlim_max) {
#if defined(__APPLE__) && defined(__ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__) && __ENVIRONMENT_MAC_OS_X_VERSION_MIN_REQUIRED__ > 1040
            if (rl.rlim_max == RLIM_INFINITY) {
                rl.rlim_cur = OPEN_MAX < rl.rlim_max ? OPEN_MAX : rl.rlim_max;
            } else {
                rl.rlim_cur = rl.rlim_max;
            }
#else
            rl.rlim_cur = rl.rlim_max;
#endif
    	    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
	        Ns_Log(Warning, "fd: setrlimit(RLIMIT_NOFILE, %lld) failed: %s",
		       rl.rlim_max, strerror(errno));
	    } 
	}
#ifdef USE_DUPHIGH
    	if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 256) {
	    dupHigh = 1;
	}
#endif
    }
#endif

    /*
     * Open a fd on /dev/null which can be later re-used.
     */

    devNull = open(DEVNULL, O_RDWR);
    if (devNull < 0) {
	Ns_Fatal("fd: open(%s) failed: %s", DEVNULL, strerror(errno));
    }
    Ns_DupHigh(&devNull);
    Ns_CloseOnExec(devNull);
}


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
#ifdef _WIN32
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
#ifdef _WIN32
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

    if (dupHigh) {
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
#ifdef _WIN32
    flags |= _O_SHORT_LIVED|_O_NOINHERIT|_O_TEMPORARY|_O_BINARY;
#endif
    trys = 0;
    do {
	Ns_GetTime(&now);
	sprintf(buf, "nstmp.%d.%d", (int) now.sec, (int) now.usec);
	path = Ns_MakePath(&ds, P_tmpdir, buf, NULL);
#ifdef _WIN32
	fd = _sopen(path, flags, _SH_DENYRW, _S_IREAD|_S_IWRITE);
#else
	fd = open(path, flags, 0600);
#endif
    } while (fd < 0 && trys++ < 10 && errno == EEXIST);
    if (fd < 0) {
	Ns_Log(Error, "tmp: could not open temp file %s: %s",
	       path, strerror(errno));
#ifndef _WIN32
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
    off_t zero = 0;

    if (lseek(fd, zero, SEEK_SET) != 0 || ftruncate(fd, zero) != 0) {
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


/*
 *----------------------------------------------------------------------
 *
 * Ns_DevNull --
 *
 *	Return an open fd to /dev/null.  This is a read-only, shared
 *	fd which can not be closed.
 *
 * Results:
 *	Open file descriptor.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DevNull(void)
{
    return devNull;
}


/*
 *----------------------------------------------------------------------
 * ns_sockpair, ns_pipe --
 *
 *      Create a pipe/socketpair with fd's set close on exec.
 *
 * Results:
 *      0 if ok, -1 otherwise.
 *
 * Side effects:
 *      Updates given fd array.
 *
 *----------------------------------------------------------------------
 */

int
ns_pipe(int *fds)
{
#ifndef _WIN32
    if (pipe(fds) == 0) {
	return ClosePipeOnExec(fds);
    }
#else
    if (_pipe(fds, 4096, _O_NOINHERIT|_O_BINARY) == 0) {
	return 0;
    }
#endif
    return -1;
}

int
ns_sockpair(SOCKET *socks)
{
#ifndef _WIN32
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, socks) == 0) {
	return ClosePipeOnExec(socks);
    }
    return -1;
#else
    SOCKET          sock;
    struct sockaddr_in ia[2];
    int             size;

    size = sizeof(struct sockaddr_in);
    sock = Ns_SockListen("127.0.0.1", 0);
    if (sock == INVALID_SOCKET ||
    	getsockname(sock, (struct sockaddr *) &ia[0], &size) != 0) {
	return -1;
    }
    size = sizeof(struct sockaddr_in);
    socks[1] = Ns_SockConnect("127.0.0.1", (int) ntohs(ia[0].sin_port));
    if (socks[1] == INVALID_SOCKET ||
    	getsockname(socks[1], (struct sockaddr *) &ia[1], &size) != 0) {
	ns_sockclose(sock);
	return -1;
    }
    size = sizeof(struct sockaddr_in);
    socks[0] = accept(sock, (struct sockaddr *) &ia[0], &size);
    ns_sockclose(sock);
    if (socks[0] == INVALID_SOCKET) {
	ns_sockclose(socks[1]);
	return -1;
    }
    if (ia[0].sin_addr.s_addr != ia[1].sin_addr.s_addr ||
	ia[0].sin_port != ia[1].sin_port) {
	ns_sockclose(socks[0]);
	ns_sockclose(socks[1]);
	return -1;
    }
    return 0;
#endif
}

#ifndef _WIN32
static int
ClosePipeOnExec(int *fds)
{
    if (Ns_CloseOnExec(fds[0]) == NS_OK && Ns_CloseOnExec(fds[1]) == NS_OK) {
	return 0;
    }
    close(fds[0]);
    close(fds[1]);
    return -1;
}
#endif


/*
 *----------------------------------------------------------------------
 *
 * NsMap --
 *
 *	Memory map a region of a file.
 *
 * Results:
 *	Pointer to mapped region or NULL if mapping failed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void *
NsMap(int fd, off_t start, size_t len, int writable, void **argPtr)
{
#ifdef _WIN32
    /* TODO: Make this work on Win32. */
    return NULL;
#else
    int prot;

    prot = PROT_READ;
    if (writable) {
	prot |= PROT_WRITE;
    }
    *argPtr = (void *) len;
    return mmap(NULL, len, prot, MAP_SHARED, fd, start);
#endif
}


/*
 *----------------------------------------------------------------------
 *
 * NsUnMap --
 *
 *	Unmap a previosly mmapped region.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Failure is considered fatal.
 *
 *----------------------------------------------------------------------
 */

void
NsUnMap(void *addr, void *arg)
{
#ifdef _WIN32
    /* TODO: Make this work on Win32. */
    Ns_Fatal("NsUnMap not supported");
#else
    size_t len = (size_t) arg;

    if (munmap(addr, len) != 0) {
	Ns_Fatal("munmap(%p, %u) failed: %s", addr, len, strerror(errno));
    }
#endif
}

/* 
 * tclUnixNotify.c --
 *
 *	This file contains Unix-specific procedures for the notifier,
 *	which is the lowest-level part of the Tcl event loop.  This file
 *	works together with ../generic/tclNotify.c.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclUnixNotfy.c 1.31 96/07/23 16:17:29
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/tcl7.6/unix/Attic/tclUnixNotfy.c,v 1.1.1.1 2000/05/02 13:48:29 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "tclInt.h"
#include "tclPort.h"
#include <signal.h> 

/*
 * The information below is used to provide read, write, and
 * exception masks to select during calls to Tcl_DoOneEvent.
 */

typedef struct {
    fd_mask checkMasks[3*MASK_SIZE];
				/* This array is used to build up the masks
				 * to be used in the next call to select.
				 * Bits are set in response to calls to
				 * Tcl_WatchFile. */
    fd_mask readyMasks[3*MASK_SIZE];
				/* This array reflects the readable/writable
				 * conditions that were found to exist by the
				 * last call to select. */
    int numFdBits;		/* Number of valid bits in checkMasks
				 * (one more than highest fd for which
				 * Tcl_WatchFile has been called). */
} Notifier;

static Notifier *
GetNotifier(void)
{
    Notifier *nfPtr;
    static Ns_Tls tls;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, Ns_Free);
	}
	Ns_MasterUnlock();
    }

    nfPtr = Ns_TlsGet(&tls);
    if (nfPtr == NULL) {
	nfPtr = (Notifier *) ns_calloc(1, sizeof(Notifier));
	Ns_TlsSet(&tls, nfPtr);
    }
    return nfPtr;
}


/*
 * Static routines in this file:
 */

static int	MaskEmpty _ANSI_ARGS_((long *maskPtr));

/*
 *----------------------------------------------------------------------
 *
 * Tcl_WatchFile --
 *
 *	Arrange for Tcl_DoOneEvent to include this file in the masks
 *	for the next call to select.  This procedure is invoked by
 *	event sources, which are in turn invoked by Tcl_DoOneEvent
 *	before it invokes select.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	
 *	The notifier will generate a file event when the I/O channel
 *	given by fd next becomes ready in the way indicated by mask.
 *	If fd is already registered then the old mask will be replaced
 *	with the new one.  Once the event is sent, the notifier will
 *	not send any more events about the fd until the next call to
 *	Tcl_NotifyFile. 
 *
 *----------------------------------------------------------------------
 */

void
Tcl_WatchFile(file, mask)
    Tcl_File file;	/* Generic file handle for a stream. */
    int mask;			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION:
				 * indicates conditions to wait for
				 * in select. */
{
    int fd, type, index;
    fd_mask bit;
    Notifier *nf = GetNotifier();

    fd = (int) Tcl_GetFileInfo(file, &type);

    if (type != TCL_UNIX_FD) {
	panic("Tcl_WatchFile: unexpected file type");
    }

    if (fd >= FD_SETSIZE) {
	panic("Tcl_WatchFile can't handle file id %d", fd);
    }

    index = fd/(NBBY*sizeof(fd_mask));
    bit = 1 << (fd%(NBBY*sizeof(fd_mask)));
    if (mask & TCL_READABLE) {
	nf->checkMasks[index] |= bit;
    }
    if (mask & TCL_WRITABLE) {
	(nf->checkMasks+MASK_SIZE)[index] |= bit;
    }
    if (mask & TCL_EXCEPTION) {
	(nf->checkMasks+2*(MASK_SIZE))[index] |= bit;
    }
    if (nf->numFdBits <= fd) {
	nf->numFdBits = fd+1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_FileReady --
 *
 *	Indicates what conditions (readable, writable, etc.) were
 *	present on a file the last time the notifier invoked select.
 *	This procedure is typically invoked by event sources to see
 *	if they should queue events.
 *
 * Results:
 *	The return value is 0 if none of the conditions specified by mask
 *	was true for fd the last time the system checked.  If any of the
 *	conditions were true, then the return value is a mask of those
 *	that were true.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_FileReady(file, mask)
    Tcl_File file;	/* Generic file handle for a stream. */
    int mask;			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION:
				 * indicates conditions caller cares about. */
{
    int index, result, type, fd;
    fd_mask bit;
    Notifier *nf = GetNotifier();

    fd = (int) Tcl_GetFileInfo(file, &type);
    if (type != TCL_UNIX_FD) {
	panic("Tcl_FileReady: unexpected file type");
    }

    index = fd/(NBBY*sizeof(fd_mask));
    bit = 1 << (fd%(NBBY*sizeof(fd_mask)));
    result = 0;
    if ((mask & TCL_READABLE) && (nf->readyMasks[index] & bit)) {
	result |= TCL_READABLE;
    }
    if ((mask & TCL_WRITABLE) && ((nf->readyMasks+MASK_SIZE)[index] & bit)) {
	result |= TCL_WRITABLE;
    }
    if ((mask & TCL_EXCEPTION) && ((nf->readyMasks+(2*MASK_SIZE))[index] & bit)) {
	result |= TCL_EXCEPTION;
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * MaskEmpty --
 *
 *	Returns nonzero if mask is empty (has no bits set).
 *
 * Results:
 *	Nonzero if the mask is empty, zero otherwise.
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int
MaskEmpty(maskPtr)
    long *maskPtr;
{
    long *runPtr, *tailPtr;
    int found, sz;

    sz = 3 * ((MASK_SIZE) / sizeof(long)) * sizeof(fd_mask);
    for (runPtr = maskPtr, tailPtr = maskPtr + sz, found = 0;
             runPtr < tailPtr;
             runPtr++) {
        if (*runPtr != 0) {
            found = 1;
            break;
        }
    }
    return !found;    
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_WaitForEvent --
 *
 *	This procedure does the lowest level wait for events in a
 *	platform-specific manner.  It uses information provided by
 *	previous calls to Tcl_WatchFile, plus the timePtr argument,
 *	to determine what to wait for and how long to wait.
 *
 * Results:
 *	The return value is normally TCL_OK.  However, if there are
 *	no events to wait for (e.g. no files and no timers) so that
 *	the procedure would block forever, then it returns TCL_ERROR.
 *
 * Side effects:
 *	May put the process to sleep for a while, depending on timePtr.
 *	When this procedure returns, an event of interest to the application
 *	has probably, but not necessarily, occurred.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_WaitForEvent(timePtr)
    Tcl_Time *timePtr;		/* Specifies the maximum amount of time
				 * that this procedure should block before
				 * returning.  The time is given as an
				 * interval, not an absolute wakeup time.
				 * NULL means block forever. */
{
    struct timeval timeout, *timeoutPtr;
    int numFound;
    Notifier *nf = GetNotifier();

    memcpy((VOID *) nf->readyMasks, (VOID *) nf->checkMasks,
	    3*MASK_SIZE*sizeof(fd_mask));
    if (timePtr == NULL) {
	if ((nf->numFdBits == 0) || (MaskEmpty((long *) nf->readyMasks))) {
	    return TCL_ERROR;
	}
	timeoutPtr = NULL;
    } else {
	timeoutPtr = &timeout;
	timeout.tv_sec = timePtr->sec;
	timeout.tv_usec = timePtr->usec;
    }
    numFound = select(nf->numFdBits, (SELECT_MASK *) &nf->readyMasks[0],
	    (SELECT_MASK *) &nf->readyMasks[MASK_SIZE],
	    (SELECT_MASK *) &nf->readyMasks[2*MASK_SIZE], timeoutPtr);

    /*
     * Some systems don't clear the masks after an error, so
     * we have to do it here.
     */

    if (numFound == -1) {
	memset((VOID *) nf->readyMasks, 0, 3*MASK_SIZE*sizeof(fd_mask));
    }

    /*
     * Reset the check masks in preparation for the next call to
     * select.
     */

    nf->numFdBits = 0;
    memset((VOID *) nf->checkMasks, 0, 3*MASK_SIZE*sizeof(fd_mask));
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_Sleep --
 *
 *	Delay execution for the specified number of milliseconds.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Time passes.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_Sleep(ms)
    int ms;			/* Number of milliseconds to sleep. */
{
    static struct timeval delay;
    Tcl_Time before, after;

    /*
     * The only trick here is that select appears to return early
     * under some conditions, so we have to check to make sure that
     * the right amount of time really has elapsed.  If it's too
     * early, go back to sleep again.
     */

    TclpGetTime(&before);
    after = before;
    after.sec += ms/1000;
    after.usec += (ms%1000)*1000;
    if (after.usec > 1000000) {
	after.usec -= 1000000;
	after.sec += 1;
    }
    while (1) {
	delay.tv_sec = after.sec - before.sec;
	delay.tv_usec = after.usec - before.usec;
	if (delay.tv_usec < 0) {
	    delay.tv_usec += 1000000;
	    delay.tv_sec -= 1;
	}

	/*
	 * Special note:  must convert delay.tv_sec to int before comparing
	 * to zero, since delay.tv_usec is unsigned on some platforms.
	 */

	if ((((int) delay.tv_sec) < 0)
		|| ((delay.tv_usec == 0) && (delay.tv_sec == 0))) {
	    break;
	}
	(void) select(0, (SELECT_MASK *) 0, (SELECT_MASK *) 0,
		(SELECT_MASK *) 0, &delay);
	TclpGetTime(&before);
    }
}


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
 * drv.c --
 *
 *	Support for modular socket drivers.
 *
 *      This is where the  queueing and thread support for handling connections
 *      happens. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/drv.c,v 1.4 2000/08/17 06:09:49 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local procedures defined in this file.
 */

static Ns_ThreadProc RunDriver;

/*
 * Static variables defined in this file
 */

static Driver *firstDrvPtr;


/*
 *----------------------------------------------------------------------
 *
 * Ns_RegisterDriver --
 *
 *	Register a set of communications driver procs 
 *
 * Results:
 *	A pointer to the driver structure 
 *
 * Side effects:
 *	A node will be added to the global linked list of drivers 
 *
 *----------------------------------------------------------------------
 */

Ns_Driver
Ns_RegisterDriver(char *ignored, char *label, Ns_DrvProc *procs, void *drvData)
{
    Driver         *drvPtr;

    drvPtr = ns_calloc(1, sizeof(Driver));

    while (procs->proc != NULL) {
	switch (procs->id) {
	    case Ns_DrvIdName:
        	drvPtr->nameProc = (Ns_ConnDriverNameProc *) procs->proc;
        	break;

	    case Ns_DrvIdStart:
        	drvPtr->startProc = (Ns_DriverStartProc *) procs->proc;
        	break;

	    case Ns_DrvIdStop:
        	drvPtr->stopProc = (Ns_DriverStopProc *) procs->proc;
        	break;

	    case Ns_DrvIdAccept:
        	drvPtr->acceptProc = (Ns_DriverAcceptProc *) procs->proc;
        	break;

	    case Ns_DrvIdInit:
        	drvPtr->initProc = (Ns_ConnInitProc *) procs->proc;
        	break;

	    case Ns_DrvIdRead:
        	drvPtr->readProc = (Ns_ConnReadProc *) procs->proc;
        	break;

	    case Ns_DrvIdWrite:
        	drvPtr->writeProc = (Ns_ConnWriteProc *) procs->proc;
        	break;

	    case Ns_DrvIdClose:
        	drvPtr->closeProc = (Ns_ConnCloseProc *) procs->proc;
        	break;

	    case Ns_DrvIdFree:
        	drvPtr->freeProc = (Ns_ConnFreeProc *) procs->proc;
        	break;

	    case Ns_DrvIdPeer:
        	drvPtr->peerProc = (Ns_ConnPeerProc *) procs->proc;
        	break;

	    case Ns_DrvIdPeerPort:
        	drvPtr->peerPortProc = (Ns_ConnPeerPortProc *) procs->proc;
        	break;

	    case Ns_DrvIdLocation:
        	drvPtr->locationProc = (Ns_ConnLocationProc *) procs->proc;
        	break;

	    case Ns_DrvIdHost:
        	drvPtr->hostProc = (Ns_ConnHostProc *) procs->proc;
        	break;

	    case Ns_DrvIdPort:
        	drvPtr->portProc = (Ns_ConnPortProc *) procs->proc;
        	break;

	    case Ns_DrvIdSendFd:
        	drvPtr->sendFdProc = (Ns_ConnSendFdProc *) procs->proc;
        	break;

	    case Ns_DrvIdSendFile:
        	drvPtr->sendFileProc = (Ns_ConnSendFileProc *) procs->proc;
        	break;

	    case Ns_DrvIdDetach:
        	drvPtr->detachProc = (Ns_ConnDetachProc *) procs->proc;
        	break;

	    case Ns_DrvIdConnectionFd:
		drvPtr->sockProc = (Ns_ConnConnectionFdProc *)procs->proc;
		break;

    	    case Ns_DrvIdMoveContext:
	    	/* NB: No longer supported. */
		break;
	}
	procs++;
    }
    if (drvPtr->readProc == NULL
	|| drvPtr->writeProc == NULL || drvPtr->closeProc == NULL) {
	Ns_Log(Error, "drv: driver '%s' missing required procs", label);
	ns_free(drvPtr);
	return NULL;
    }

    drvPtr->label = label;
    drvPtr->drvData = drvData;
    drvPtr->nextPtr = firstDrvPtr;
    firstDrvPtr = drvPtr;

    return (Ns_Driver) drvPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetDriverContext --
 *
 *	Return the driver's context 
 *
 * Results:
 *	A pointer to a driver context 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

void *
Ns_GetDriverContext(Ns_Driver drv)
{
    Driver *drvPtr = (Driver *)drv;

    return drvPtr->drvData;
}


/*
 *----------------------------------------------------------------------
 *
 * NsStartDrivers --
 *
 *	Start all registered drivers. Note that the server will keep
 *	running even if one or more drivers fails to start.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	New thread may be created for external drivers.
 *
 *----------------------------------------------------------------------
 */

void
NsStartDrivers(void)
{
    Driver  *drvPtr;

    drvPtr = firstDrvPtr;
    while (drvPtr != NULL) {
	if (drvPtr->startProc == NULL ||
	    (*drvPtr->startProc)(nsServer, drvPtr->label,
			         &drvPtr->drvData) == NS_OK) {
	    drvPtr->running = 1;
            if (drvPtr->acceptProc != NULL) {
	        Ns_ThreadCreate(RunDriver, (void *) drvPtr, 0, NULL);
            }
	} else {
	    drvPtr->running = 0;
	}
	drvPtr = drvPtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsStopDrivers --
 *
 *	Signal each driver to stop.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on driver stop function.
 *
 *----------------------------------------------------------------------
 */

void
NsStopDrivers(void)
{
    Driver *drvPtr;

    drvPtr = firstDrvPtr;
    while (drvPtr != NULL) {
	if (drvPtr->running && drvPtr->stopProc != NULL) {
	    (*drvPtr->stopProc)(drvPtr->drvData);
	}
	drvPtr = drvPtr->nextPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RunDriver --
 *
 *	Main thread for drivers using an accept proc which is really
 *	sort of silly.  Driver could just as easily create their
 *	own thread (which is what nssock does).
 *
 * Results:
 *	None 
 *
 * Side effects:
 * 	Connections are queued until the accept proc returns something
 * 	other than NS_OK.
 *
 *----------------------------------------------------------------------
 */

static void
RunDriver(void *arg)
{
    Driver         *dPtr = arg;
    int             status;
    void           *dData, *cData;
    char	   *loc;

    Ns_WaitForStartup();

    dPtr = arg;
    dData = dPtr->drvData;
    if (dPtr->locationProc != NULL) {
	loc = (*dPtr->locationProc)(dData);
    } else {
	loc = "<unknown>";
    }
    Ns_Log(Notice, "drv: driver '%s' accepting '%s'", dPtr->label, loc);

    while ((status = ((*dPtr->acceptProc)(dData, &cData))) == NS_OK) {
	if (Ns_QueueConn(dData, cData) != NS_OK) {
	   (*dPtr->closeProc)(dData);
	}
    }
    if (status == NS_SHUTDOWN) {
	Ns_Log(Notice, "drv: driver '%s' stopping '%s'", dPtr->label, loc);
    } else {
	Ns_Log(Error, "drv: driver '%s' failed for '%s': error %d",
	       dPtr->label, loc, status);
    }
}

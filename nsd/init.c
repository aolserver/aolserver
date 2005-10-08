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
 * init.c --
 *
 *	AOLserver libnsd entry.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/init.c,v 1.13 2005/10/08 20:20:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_LibInit --
 *
 *	Initializes core nsd library.  Should be called automatically
 *	during dynamic library initialization, otherwise explicitly for
 *	static builds.
 *
 * Results:
 	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_LibInit(void)
{
    static int once = 0;

    if (!once) {
	once = 1;

	/*
	 * Initialize the threads interface.
	 */

	NsThreads_LibInit();

	/*
	 * Log must be initialized first in case later inits log messages.
	 */

    	NsInitLog();
	NsInitFd();

	/*
	 * Caches and URL space are used by some of the remaining inits.
	 */

    	NsInitCache();
    	NsInitUrlSpace();

	/*
	 * Order of the remaining inits is not significant.
	 */

#ifndef _WIN32
    	NsInitBinder();
#endif
    	NsInitConf();
    	NsInitConfig();
    	NsInitDrivers();
    	NsInitEncodings();
        NsInitLimits();
    	NsInitListen();
    	NsInitMimeTypes();
    	NsInitModLoad();
        NsInitPools();
    	NsInitProcInfo();
    	NsInitQueue();
    	NsInitRequests();
    	NsInitSched();
    	NsInitServers();
    	NsInitTcl();
    }
}

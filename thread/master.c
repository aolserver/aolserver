/*
 * The contents of this file are subject to the AOLserver Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://aolserver.lcs.mit.edu/.
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
 * master.c --
 *
 *	Master lock routines for nsthread.  The master lock is used when
 *	self initializing synchronization objects.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/master.c,v 1.1.1.1 2000/05/02 13:48:40 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

/*
 * The following critical section is used to single thread object
 * initialization.
 */

static Ns_Cs masterLock;


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterLock --
 *
 *	Lock the master lock mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Master lock is initialized if needed.
 *
 *----------------------------------------------------------------------
 */
void
Ns_MasterLock(void)
{
    static int initialized = 0;

    /*
     * Initialize the master lock if needed.  This is safe because a
     * self-initializinng lock is used when creating a thread and its
     * initialization will cause the master lock to be initialized
     * here before the first new thread can be created.
     */

    if (!initialized) {
    	Ns_CsInit(&masterLock);
	initialized = 1;
    }

    Ns_CsEnter(&masterLock);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_MasterUnlock --
 *
 *	Unlock the master lock mutex.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_MasterUnlock(void)
{
    Ns_CsLeave(&masterLock);
}

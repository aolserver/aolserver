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


#include "thread.h"

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/debug.c,v 1.1.1.1 2000/05/02 13:48:40 kriston Exp $, compiled: " __DATE__ " " __TIME__;

static void
Log(char cmd, Ns_Mutex *lockPtr, char *file, int line)
{
    static Ns_Mutex lock;
    static FILE *fp;
    static unsigned int next;

    Ns_MutexLock(&lock);
    if (fp == NULL) {
	char *log = getenv("NS_THREAD_DEBUG_FILE");
	if (log == NULL) {
	    log = "/tmp/nsthread.log";
	}
	fp = fopen(log, "w");
	if (fp == NULL) {
	    NsThreadAbort("fopen(%s) failed: %s", log, strerror(errno));
	}
    }
    fprintf(fp, "%d %d %c %p %-40s:%d\n", (int) time(NULL), next++, cmd,
	lockPtr, file, line);
    fflush(fp);
    Ns_MutexUnlock(&lock);
}

void
Ns_MutexDebugLock(Ns_Mutex *lockPtr, char *file, int line)
{
    Log('L', lockPtr, file, line);
    Ns_MutexLock(lockPtr);
}

void
Ns_MutexDebugUnlock(Ns_Mutex *lockPtr, char *file, int line)
{
    Log('U', lockPtr, file, line);
    Ns_MutexUnlock(lockPtr);
}

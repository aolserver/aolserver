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
 * thread.h --
 *
 *	Private nsthread library include.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/nsthread/thread.h,v 1.8 2005/08/23 21:41:31 jgdavidson Exp $
 */

#ifndef THREAD_H
#define THREAD_H

#define NSTHREAD_EXPORTS
#include "nsthread.h"

#ifdef WIN32
typedef char *caddr_t;
#endif

extern int    NsGetStack(void **addrPtr, size_t *sizePtr);
extern void   NsthreadsInit(void);
extern void   NsInitThreads(void);
extern void   NsInitMaster(void);
extern void   NsInitReentrant(void);
extern void   NsMutexInitNext(Ns_Mutex *mutex, char *prefix, unsigned int *nextPtr);
extern void  *NsGetLock(Ns_Mutex *mutex);
extern void  *NsLockAlloc(void);
extern void   NsLockFree(void *lock);
extern void   NsLockSet(void *lock);
extern int    NsLockTry(void *lock);
extern void   NsLockUnset(void *lock);
extern void   NsCleanupTls(void **slots);
extern void **NsGetTls(void);
extern void   NsThreadMain(void *arg);
extern void   NsCreateThread(void *arg, long stacksize, Ns_Thread *threadPtr);
extern void   NsThreadFatal(char *func, char *osfunc, int err) _nsnoreturn;

#endif /* THREAD_H */

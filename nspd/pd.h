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
 * pd.h --
 *
 *	APIs provided by the proxy daemon library. 
 */

#ifndef PD_H
#define PD_H

#include "nsextmsg.h"
#include "nspd.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include <unistd.h>
#define SOCKET          int
#define INVALID_SOCKET  (-1)
#define socket_errno    errno
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>

#include <syslog.h>

extern void     PdExit(int code);
extern void     PdTraceOn(char *file);
extern void     PdTraceOff(void);
extern void     OpenLog(void);
extern void     CloseLog(void);
extern void     PdMainLoop(void);
extern void     PdListen(int port);
extern char    *pdBin;

extern void     Ns_FatalErrno(char *func);
extern void     Ns_FatalSock(char *func);
extern void     Ns_PdExit(int code);


#endif                                  /* PD_H */

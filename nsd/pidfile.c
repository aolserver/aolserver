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
 * pidfile.c --
 *
 *	Implement the PID file routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/pidfile.c,v 1.1.1.1 2000/05/02 13:48:21 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static char *GetFile(void);


/*
 *----------------------------------------------------------------------
 *
 * NsCreatePidFile --
 *
 *	Create a pid file and optionally kill a running process whose 
 *	pid is in that file. 
 *
 * Results:
 *	Previous pid (if any).
 *
 * Side effects:
 *	May terminate, also registers a realm.
 *
 *----------------------------------------------------------------------
 */

int
NsGetLastPid(void)
{
    char buf[10];
    int fd, pid, n;
    char *file = GetFile();
    
    pid = -1;
    fd = open(file, O_RDONLY|O_TEXT);
    if (fd >= 0) {
	n = read(fd, buf, sizeof(buf)-1);
	if (n < 0) {
	    Ns_Log(Warning, "pid file read() failed: %s", strerror(errno));
	} else {
	    buf[n] = '\0';
	    if (sscanf(buf, "%d", &pid) != 1) {
		Ns_Log(Warning, "invalid pid file: %s", file);
	    	pid = -1;
	    }
	}
	close(fd);
    } else if (errno != ENOENT) {
	Ns_Log(Error, "could not open pid file %s: %s",
		       file, strerror(errno));
    }
    return pid;
}


void
NsCreatePidFile(void)
{
    int	  fd, n;
    char  buf[10];
    char *file = GetFile();

    fd = open(file, O_WRONLY|O_TRUNC|O_CREAT|O_TEXT, 0644);
    if (fd < 0) {
    	Ns_Log(Error, "could not open pid file \"%s\":  %s",
               file, strerror(errno));
    } else {
	sprintf(buf, "%d\n", nsconf.pid);
	n = strlen(buf);
	if (write(fd, buf, n) != n) {
	    Ns_Log(Error, "write() failed: %s", strerror(errno));
	}
        close(fd);
    }
}


void
NsRemovePidFile(void)
{
    char *file = GetFile();
    
    if (unlink(file) != 0) {
    	Ns_Log(Error, "could not remove \"%s\": %s", file, strerror(errno));
    }
}


static char *
GetFile(void)
{
    static char *file;
    
    if (file == NULL) {
    	file = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "pidfile");
	if (file == NULL) {
    	    Ns_DString ds;

	    Ns_DStringInit(&ds);
	    Ns_HomePath(&ds, "log/nspid.", NULL);
	    Ns_DStringAppend(&ds, nsServer);
	    file = Ns_DStringExport(&ds);
	}
    }
    return file;
}

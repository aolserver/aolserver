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
 * pidfile.c --
 *
 *	Implement the PID file routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/pidfile.c,v 1.6 2001/03/12 22:06:14 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static char *GetFile(char *procname);


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
NsGetLastPid(char *procname)
{
    char buf[10];
    int fd, pid, n;
    char *file = GetFile(procname);
    
    pid = -1;
    fd = open(file, O_RDONLY|O_TEXT);
    if (fd >= 0) {
	n = read(fd, buf, sizeof(buf)-1);
	if (n < 0) {
	    Ns_Log(Warning, "pidfile: pid file read() failed: '%s'",
		   strerror(errno));
	} else {
	    buf[n] = '\0';
	    if (sscanf(buf, "%d", &pid) != 1) {
		Ns_Log(Warning, "pidfile: invalid pid file '%s'", file);
	    	pid = -1;
	    }
	}
	close(fd);
    } else if (errno != ENOENT) {
	Ns_Log(Error, "pidfile: failed to open pid file '%s': '%s'",
	       file, strerror(errno));
    }
    return pid;
}


void
NsCreatePidFile(char *procname)
{
    int	  fd, n;
    char  buf[10];
    char *file = GetFile(procname);

    fd = open(file, O_WRONLY|O_TRUNC|O_CREAT|O_TEXT, 0644);
    if (fd < 0) {
    	Ns_Log(Error, "pidfile: failed to open pid file '%s': '%s'",
	       file, strerror(errno));
    } else {
	sprintf(buf, "%d\n", nsconf.pid);
	n = strlen(buf);
	if (write(fd, buf, n) != n) {
	    Ns_Log(Error, "pidfile: write() failed: '%s'", strerror(errno));
	}
        close(fd);
    }
}


void
NsRemovePidFile(char *procname)
{
    char *file = GetFile(procname);
    
    if (unlink(file) != 0) {
    	Ns_Log(Error, "pidfile: failed to remove '%s': '%s'",
	       file, strerror(errno));
    }
}


static char *
GetFile(char *procname)
{
    static char *file;
    
    if (file == NULL) {
    	file = Ns_ConfigGet(NS_CONFIG_PARAMETERS, "pidfile");
	if (file == NULL) {
    	    Ns_DString ds;

	    Ns_DStringInit(&ds);
	    Ns_HomePath(&ds, "log/nspid.", NULL);
	    Ns_DStringAppend(&ds, procname);
	    file = Ns_DStringExport(&ds);
	}
    }
    return file;
}

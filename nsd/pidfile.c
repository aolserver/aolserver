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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/pidfile.c,v 1.9 2005/08/01 20:48:09 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

static char *GetFile(char *procname);


/*
 *----------------------------------------------------------------------
 *
 * NsCreatePidFile, NsRemovePidFile --
 *
 *	Create/remove file with current pid.
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
NsCreatePidFile(char *procname)
{
    int	  fd, n;
    char  buf[10];
    char *file = GetFile(procname);

    fd = open(file, O_WRONLY|O_TRUNC|O_CREAT, 0644);
    if (fd < 0) {
    	Ns_Log(Error, "pidfile: failed to open pid file '%s': '%s'",
	       file, strerror(errno));
    } else {
	sprintf(buf, "%d\n", nsconf.pid);
	n = strlen(buf);
	if (write(fd, buf, (size_t)n) != n) {
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


/*
 *----------------------------------------------------------------------
 *
 * GetFile --
 *
 *	Return the server process id file name.
 *
 * Results:
 *	Pointer to filename in static string 
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
GetFile(char *procname)
{
    static char *file;
    Ns_DString ds;
    
    if (file == NULL) {
    	file = NsParamString("pidfile", NULL);
	if (file == NULL) {
	    Ns_DStringInit(&ds);
	    Ns_HomePath(&ds, "log/nspid.", NULL);
	    Ns_DStringAppend(&ds, procname);
	    file = Ns_DStringExport(&ds);
	}
    }
    return file;
}

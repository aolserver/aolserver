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
 * tclfile.c --
 *
 *	Tcl commands that do stuff to the filesystem. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclfile.c,v 1.12 2002/06/12 23:08:51 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include <utime.h>


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetOpenChannel --
 *
 *	Return an open channel with an interface similar to the
 *	pre-Tcl7.5 Tcl_GetOpenFile, used throughout AOLserver.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	The value at chanPtr is updated with a valid open Tcl_Channel.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetOpenChannel(Tcl_Interp *interp, char *chanId, int write,
	int check, Tcl_Channel *chanPtr)
{
    int mode;

    *chanPtr = Tcl_GetChannel(interp, chanId, &mode);
    if (*chanPtr == NULL) {
	return TCL_ERROR;
    }
    if (check &&
	((write && !(mode & TCL_WRITABLE)) ||
	 (!write && !(mode & TCL_READABLE)))) {
	Tcl_AppendResult(interp, "channel \"", chanId, "\" not open for ",
	    write ? "write" : "read", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_TclGetOpenFd --
 *
 *	Return an open Unix file descriptor for the given channel.
 *	This routine is used by the AOLserver * routines
 *	to provide access to the underlying socket.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	The value at fdPtr is updated with a valid Unix file descriptor.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclGetOpenFd(Tcl_Interp *interp, char *chanId, int write, int *fdPtr)
{
    Tcl_Channel chan;
    ClientData data;

    if (Ns_TclGetOpenChannel(interp, chanId, write, 1, &chan) != TCL_OK) {
	return TCL_ERROR;
    }
    if (Tcl_GetChannelHandle(chan, write ? TCL_WRITABLE : TCL_READABLE,
			     (ClientData*) &data) != TCL_OK) {
	Tcl_AppendResult(interp, "could not get handle for channel: ",
			 chanId, NULL);
	return TCL_ERROR;
    }
    *fdPtr = (int) data;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCpFpCmd --
 *
 *	Implements ns_cpfp. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCpFpCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_Channel  in, out;
    char         buf[2048];
    char        *p;
    int          tocopy, nread, nwrote, toread, ntotal;

    if (argc != 3 && argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " inChan outChan ?ncopy?\"", NULL);
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[1], 0, 1, &in) != TCL_OK ||
	Ns_TclGetOpenChannel(interp, argv[2], 1, 1, &out) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc == 3) {
	tocopy = -1;
    } else {
        if (Tcl_GetInt(interp, argv[3], &tocopy) != TCL_OK) {
            return TCL_ERROR;
        }
        if (tocopy < 0) {
            Tcl_AppendResult(interp, "invalid length \"", argv[3],
		"\": must be >= 0", NULL);
            return TCL_ERROR;
        }
    }
    
    ntotal = 0;
    while (tocopy != 0) {
	toread = sizeof(buf);
	if (tocopy > 0 && toread > tocopy) {
	    toread = tocopy;
	}
	nread = Tcl_Read(in, buf, toread);
	if (nread == 0) {
	    break;
	} else if (nread < 0) {
	    Tcl_AppendResult(interp, "read failed: ",
			     Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}
	if (tocopy > 0) {
	    tocopy -= nread;
	}
	p = buf;
	while (nread > 0) {
	    nwrote = Tcl_Write(out, p, nread);
	    if (nwrote < 0) {
		Tcl_AppendResult(interp, "write failed: ",
				 Tcl_PosixError(interp), NULL);
		return TCL_ERROR;
	    }
	    nread -= nwrote;
	    ntotal += nwrote;
	    p += nwrote;
	}
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ntotal));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCpFpObjCmd --
 *
 *	Implements ns_cpfp as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCpFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Tcl_Channel  in, out;
    char         buf[2048];
    char        *p;
    int          tocopy, nread, nwrote, toread, ntotal;

    if (objc != 3 && objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "inChan outChan ?ncopy?");
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[1]), 0, 1, &in) != TCL_OK ||
	Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[2]), 1, 1, &out) != TCL_OK) {
	return TCL_ERROR;
    }
    if (objc == 3) {
	tocopy = -1;
    } else {
        if (Tcl_GetInt(interp, Tcl_GetString(objv[3]), &tocopy) != TCL_OK) {
            return TCL_ERROR;
        }
        if (tocopy < 0) {
            Tcl_Obj *result = Tcl_NewObj();
            Tcl_AppendStringsToObj(result, "invalid length \"", 
                Tcl_GetString(objv[3]),
		        "\": must be >= 0", NULL);
            Tcl_SetObjResult(interp, result);
            return TCL_ERROR;
        }
    }
    
    ntotal = 0;
    while (tocopy != 0) {
	toread = sizeof(buf);
	if (tocopy > 0 && toread > tocopy) {
	    toread = tocopy;
	}
	nread = Tcl_Read(in, buf, toread);
	if (nread == 0) {
	    break;
	} else if (nread < 0) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "read failed: ",
			     Tcl_PosixError(interp), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
	if (tocopy > 0) {
	    tocopy -= nread;
	}
	p = buf;
	while (nread > 0) {
	    nwrote = Tcl_Write(out, p, nread);
	    if (nwrote < 0) {
		Tcl_Obj *result = Tcl_NewObj();
		Tcl_AppendStringsToObj(result, "write failed: ",
				 Tcl_PosixError(interp), NULL);
		Tcl_SetObjResult(interp, result);
	        return TCL_ERROR;
	    }
	    nread -= nwrote;
	    ntotal += nwrote;
	    p += nwrote;
	}
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj(ntotal));
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCpCmd --
 *
 *	Implements ns_cp. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCpCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int             nread, towrite, nwrote;
    char            buf[4096], *src, *dst, *p, *emsg, *efile;
    int             preserve, result, rfd, wfd;
    struct stat     st;
    struct utimbuf  ut;
    
    if (argc != 3 && argc != 4) {
badargs:
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " ?-preserve? srcfile dstfile\"", NULL);
        return TCL_ERROR;
    }

    wfd = rfd = -1;
    result = TCL_ERROR;

    if (argc == 3) {
	preserve = 0;
	src = argv[1];
	dst = argv[2];
    } else {
	if (!STREQ(argv[1], "-preserve")) {
	    goto badargs;
	}
	preserve = 1;
	src = argv[2];
	dst = argv[3];
	if (stat(src, &st) != 0) {
	    emsg = "stat";
	    efile = src;
	    goto done;
        }
    }

    emsg = "open";
    rfd = open(src, O_RDONLY);
    if (rfd < 0) {
	efile = src;
	goto done;
    }
    wfd = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (wfd < 0) {
	efile = dst;
	goto done;
    }

    while ((nread = read(rfd, buf, sizeof(buf))) > 0) {
	p = buf;
	towrite = nread;
	while (towrite > 0) {
	    nwrote = write(wfd, p, towrite);
	    if (nwrote <= 0) {
		emsg = "write";
		efile = dst;
		goto done;
	    }
	    towrite -= nwrote;
	    p += nwrote;
	}
    }
    if (nread < 0) {
	emsg = "read";
	efile = src;
	goto done;
    }

    if (!preserve) {
	result = TCL_OK;
    } else {
	efile = dst;
	if (chmod(dst, st.st_mode) != 0) {
	    emsg = "chmod";
	    goto done;
	}
        ut.actime  = st.st_atime;
        ut.modtime = st.st_mtime;
        if (utime(dst, &ut) != 0) {
	    emsg = "utime";
	    goto done;
	}
	result = TCL_OK;
    }

done:
    if (result != TCL_OK) {
	Tcl_AppendResult(interp, "could not ", emsg, " \"",
	    efile, "\": ", Tcl_PosixError(interp), NULL);
    }
    if (rfd >= 0) {
	close(rfd);
    }
    if (wfd >= 0) {
	close(wfd);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclCpObjCmd --
 *
 *	Implements ns_cp as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclCpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int             nread, towrite, nwrote;
    char            buf[4096], *src, *dst, *p, *emsg, *efile;
    int             preserve, result, rfd, wfd;
    struct stat     st;
    struct utimbuf  ut;
    
    if (objc != 3 && objc != 4) {
badargs:
        Tcl_WrongNumArgs(interp, 1, objv, "?-preserve? srcfile dstfile");
        return TCL_ERROR;
    }

    wfd = rfd = -1;
    result = TCL_ERROR;

    if (objc == 3) {
	preserve = 0;
	src = Tcl_GetString(objv[1]);
	dst = Tcl_GetString(objv[2]);
    } else {
	if (!STREQ(Tcl_GetString(objv[1]), "-preserve")) {
	    goto badargs;
	}
	preserve = 1;
	src = Tcl_GetString(objv[2]);
	dst = Tcl_GetString(objv[3]);
	if (stat(src, &st) != 0) {
	    emsg = "stat";
	    efile = src;
	    goto done;
        }
    }

    emsg = "open";
    rfd = open(src, O_RDONLY);
    if (rfd < 0) {
	efile = src;
	goto done;
    }
    wfd = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (wfd < 0) {
	efile = dst;
	goto done;
    }

    while ((nread = read(rfd, buf, sizeof(buf))) > 0) {
	p = buf;
	towrite = nread;
	while (towrite > 0) {
	    nwrote = write(wfd, p, towrite);
	    if (nwrote <= 0) {
		emsg = "write";
		efile = dst;
		goto done;
	    }
	    towrite -= nwrote;
	    p += nwrote;
	}
    }
    if (nread < 0) {
	emsg = "read";
	efile = src;
	goto done;
    }

    if (!preserve) {
	result = TCL_OK;
    } else {
	efile = dst;
	if (chmod(dst, st.st_mode) != 0) {
	    emsg = "chmod";
	    goto done;
	}
        ut.actime  = st.st_atime;
        ut.modtime = st.st_mtime;
        if (utime(dst, &ut) != 0) {
	    emsg = "utime";
	    goto done;
	}
	result = TCL_OK;
    }

done:
    if (result != TCL_OK) {
        Tcl_Obj *tclresult = Tcl_NewObj();
        Tcl_AppendStringsToObj(tclresult, "could not ", emsg, " \"",
	            efile, "\": ", Tcl_PosixError(interp), NULL);
    }
    if (rfd >= 0) {
	close(rfd);
    }
    if (wfd >= 0) {
	close(wfd);
    }
    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMkdirCmd --
 *
 *	Implements ns_mkdir 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclMkdirCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " dir\"", (char *) NULL);
        return TCL_ERROR;
    }
    if (mkdir(argv[1], 0755) != 0) {
        Tcl_AppendResult(interp, "mkdir (\"", argv[1],
			 "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMkdirObjCmd --
 *
 *	Implements ns_mkdir as obj command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclMkdirObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "dir");
        return TCL_ERROR;
    }
    if (mkdir(Tcl_GetString(objv[1]), 0755) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "mkdir (\"", 
		Tcl_GetString(objv[1]),
		"\") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRmdirCmd --
 *
 *	Implements ns_rmdir 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRmdirCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " dir\"", (char *) NULL);
        return TCL_ERROR;
    }
    if (rmdir(argv[1]) != 0) {
        Tcl_AppendResult(interp, "rmdir (\"", argv[1],
			 "\") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRmdirObjCmd --
 *
 *	Implements ns_rmdir 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRmdirObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "dir");
        return TCL_ERROR;
    }
    if (rmdir(Tcl_GetString(objv[1])) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "rmdir (\"", 
		Tcl_GetString(objv[1]),
		"\") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRollFileCmd --
 *
 *	Implements ns_rollfile. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
FileCmd(Tcl_Interp *interp, int argc, char **argv, char *cmd)
{
    int max, status;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " file backupMax\"", NULL);
        return TCL_ERROR;
    }
    if (Tcl_GetInt(interp, argv[2], &max) != TCL_OK) {
	return TCL_ERROR;
    }
    if (max <= 0 || max > 1000) {
        Tcl_AppendResult(interp, "invalid max \"", argv[2],
	    "\": should be > 0 and <= 1000.", NULL);
        return TCL_ERROR;
    }
    if (*cmd == 'p') {
	status = Ns_PurgeFiles(argv[1], max);
    } else {
	status = Ns_RollFile(argv[1], max);
    }
    if (status != NS_OK) {
	Tcl_AppendResult(interp, "could not ", cmd, " \"", argv[1],
	    "\": ", Tcl_PosixError(interp), NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRollFileObjCmd --
 *
 *	Implements ns_rollfile obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

static int
FileObjCmd(Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[], char *cmd)
{
    int max, status;

    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "file backupMax");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[2], &max) != TCL_OK) {
	return TCL_ERROR;
    }
    if (max <= 0 || max > 1000) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "invalid max \"", 
		Tcl_GetString(objv[2]),
	        "\": should be > 0 and <= 1000.", NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }
    if (*cmd == 'p') {
	status = Ns_PurgeFiles(Tcl_GetString(objv[1]), max);
    } else {
	status = Ns_RollFile(Tcl_GetString(objv[1]), max);
    }
    if (status != NS_OK) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "could not ", cmd, " \"", 
		Tcl_GetString(objv[1]),
	        "\": ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
	return TCL_ERROR;
    }
    return TCL_OK;
}

int
NsTclRollFileCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return FileCmd(interp, argc, argv, "roll");
}

int
NsTclRollFileObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return FileObjCmd(interp, objc, objv, "roll");
}

int
NsTclPurgeFilesCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    return FileCmd(interp, argc, argv, "purge");
}

int
NsTclPurgeFilesObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    return FileObjCmd(interp, objc, objv, "purge");
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnlinkCmd --
 *
 *	Implement ns_unlink. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclUnlinkCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int fComplain = NS_TRUE;

    if ((argc != 2) && (argc != 3)) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " ?-nocomplain? filename\"", NULL);
        return TCL_ERROR;
    }

    if (argc == 3) {
	if (!STREQ(argv[1], "-nocomplain")) {
	    Tcl_AppendResult(interp, "unknown flag \"",
			     argv[1], "\": should be -nocomplain", NULL);
	    return TCL_ERROR;
	} else {
	    fComplain = NS_FALSE;
	}
    }

    if (unlink(argv[argc-1]) != 0) {
	if (fComplain || errno != ENOENT) {
            Tcl_AppendResult(interp, "unlink (\"", argv[argc-1],
            	"\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnlinkObjCmd --
 *
 *	Implement ns_unlink as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclUnlinkObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int fComplain = NS_TRUE;

    if ((objc != 2) && (objc != 3)) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-nocomplain? filename");
        return TCL_ERROR;
    }

    if (objc == 3) {
	if (!STREQ(Tcl_GetString(objv[1]), "-nocomplain")) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "unknown flag \"",
		    Tcl_GetString(objv[1]), "\": should be -nocomplain", 
		    NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	} else {
	    fComplain = NS_FALSE;
	}
    }

    if (unlink(Tcl_GetString(objv[objc-1])) != 0) {
	if (fComplain || errno != ENOENT) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "unlink (\"", 
		    Tcl_GetString(objv[objc-1]),
		    "\") failed:  ", Tcl_PosixError(interp), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclMkTempCmd --
 *
 *	Implements ns_mktemp. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	Allocates memory for the filename as a TCL_VOLATILE object.
 *
 *----------------------------------------------------------------------
 */

int
NsTclMkTempCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *buffer;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
			 argv[0], " template\"", NULL);
        return TCL_ERROR;
    }

    buffer = ns_strdup(argv[1]);
    Tcl_SetResult(interp, mktemp(buffer), (Tcl_FreeProc *)ns_free);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTmpNamCmd --
 *
 *	Implements ns_tmpnam. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclTmpNamCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char buf[L_tmpnam];

    if (tmpnam(buf) == NULL) {
	Tcl_SetResult(interp, "could not generate temporary filename.", TCL_STATIC);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTmpNamObjCmd --
 *
 *	Implements ns_tmpnam as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclTmpNamObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    char buf[L_tmpnam];

    if (tmpnam(buf) == NULL) {
	Tcl_SetResult(interp, "could not generate temporary filename.", TCL_STATIC);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNormalizePathCmd --
 *
 *	Implements ns_normalizepath. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclNormalizePathCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		       char **argv)
{
    Ns_DString ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " path\"", (char *) NULL);
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_NormalizePath(&ds, argv[1]);
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    Ns_DStringFree(&ds);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclNormalizePathObjCmd --
 *
 *	Implements ns_normalizepath as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclNormalizePathObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    Ns_DString ds;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "path");
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    Ns_NormalizePath(&ds, Tcl_GetString(objv[1]));
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    Ns_DStringFree(&ds);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUrl2FileCmd --
 *
 *	Implements ns_url2file. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclUrl2FileCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    Ns_DString ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " url\"", NULL);
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    NsUrlToFile(&ds, itPtr->servPtr, argv[1]);
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    Ns_DStringFree(&ds);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUrl2FileObjCmd --
 *
 *	Implements ns_url2file as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclUrl2FileObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = arg;
    Ns_DString ds;

    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "url");
        return TCL_ERROR;
    }
    Ns_DStringInit(&ds);
    NsUrlToFile(&ds, itPtr->servPtr, Tcl_GetString(objv[1]));
    Tcl_SetResult(interp, ds.string, TCL_VOLATILE);
    Ns_DStringFree(&ds);
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclKillCmd --
 *
 *	Implements ns_kill. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclKillCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    int pid, signal;

    if ((argc != 3) && (argc != 4)) {
      badargs:
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
                         argv[0], " ?-nocomplain? pid signal", NULL);
        return TCL_ERROR;
    }
    if (argc == 3) {
        if (Tcl_GetInt(interp, argv[1], &pid) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetInt(interp, argv[2], &signal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (kill(pid, signal) != 0) {
	    Tcl_AppendResult(interp, "kill (\"", argv[1], ",", argv[2],
			     "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(argv[1], "-nocomplain") != 0) {
            goto badargs;
        }
        if (Tcl_GetInt(interp, argv[2], &pid) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetInt(interp, argv[3], &signal) != TCL_OK) {
            return TCL_ERROR;
        }
        kill(pid, signal);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclKillObjCmd --
 *
 *	Implements ns_kill as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclKillObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int pid, signal;

    if ((objc != 3) && (objc != 4)) {
      badargs:
        Tcl_WrongNumArgs(interp, 1, objv, "?-nocomplain? pid signal");
        return TCL_ERROR;
    }
    if (objc == 3) {
        if (Tcl_GetIntFromObj(interp, objv[1], &pid) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, objv[2], &signal) != TCL_OK) {
            return TCL_ERROR;
        }
        if (kill(pid, signal) != 0) {
            Tcl_Obj *result = Tcl_NewObj();
            Tcl_AppendStringsToObj(result, "kill (\"", 
                Tcl_GetString(objv[1]), ",", 
                Tcl_GetString(objv[2]),
			    "\") failed:  ", Tcl_PosixError(interp), NULL);
            Tcl_SetObjResult(interp, result);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(Tcl_GetString(objv[1]), "-nocomplain") != 0) {
            goto badargs;
        }
        if (Tcl_GetIntFromObj(interp, objv[2], &pid) != TCL_OK) {
            return TCL_ERROR;
        }
        if (Tcl_GetIntFromObj(interp, objv[3], &signal) != TCL_OK) {
            return TCL_ERROR;
        }
        kill(pid, signal);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLinkCmd --
 *
 *	Implements ns_link. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclLinkCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if ((argc != 3) && (argc != 4)) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " ?-nocomplain? filename1 filename2\"", NULL);
        return TCL_ERROR;
    }
    if (argc == 3) {
        if (link(argv[1], argv[2]) != 0) {
	    Tcl_AppendResult(interp, "link (\"", argv[1], "\", \"", argv[2],
			     "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(argv[1], "-nocomplain") != 0) {
	    Tcl_AppendResult(interp, "wrong # of args:  should be \"",
			     argv[0], " ?-nocomplain? filename1 filename2\"",
			     NULL);
	    return TCL_ERROR;
        }
        link(argv[2], argv[3]);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclLinkObjCmd --
 *
 *	Implements ns_link as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclLinkObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if ((objc != 3) && (objc != 4)) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-nocomplain? filename1 filename2");
        return TCL_ERROR;
    }
    if (objc == 3) {
        if (link(Tcl_GetString(objv[1]), Tcl_GetString(objv[2])) != 0) {
	    Tcl_AppendResult(interp, "link (\"", Tcl_GetString(objv[1]), "\", \"", 
                Tcl_GetString(objv[2]),
			     "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(Tcl_GetString(objv[1]), "-nocomplain") != 0) {
        Tcl_WrongNumArgs(interp, 1, objv, "?-nocomplain? filename1 filename2");
	    return TCL_ERROR;
        }
        link(Tcl_GetString(objv[2]), Tcl_GetString(objv[3]));
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSymlinkCmd --
 *
 *	Implements ns_symlink. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSymlinkCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if ((argc != 3) && (argc != 4)) {
      badargs:
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
            argv[0], " ?-nocomplain? filename1 filename2\"", NULL);
        return TCL_ERROR;
    }

    if (argc == 3) {
        if (symlink(argv[1], argv[2]) != 0) {
	    Tcl_AppendResult(interp, "symlink (\"", argv[1], "\", \"", argv[2],
			     "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(argv[1], "-nocomplain") != 0) {
            goto badargs;
        }
        symlink(argv[2], argv[3]);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclSymlinkObjCmd --
 *
 *	Implements ns_symlink as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclSymlinkObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if ((objc != 3) && (objc != 4)) {
      badargs:
        Tcl_WrongNumArgs(interp, 1, objv, "?-nocomplain? filename1 filename2");
        return TCL_ERROR;
    }

    if (objc == 3) {
        if (symlink(Tcl_GetString(objv[1]), Tcl_GetString(objv[2])) != 0) {
            Tcl_Obj *result = Tcl_NewObj();
            Tcl_AppendStringsToObj(result, "symlink (\"", 
                Tcl_GetString(objv[1]), "\", \"", 
                Tcl_GetString(objv[2]),
			    "\") failed:  ", Tcl_PosixError(interp), NULL);
            Tcl_SetObjResult(interp, result);
            return TCL_ERROR;
        }
    } else {
        if (strcmp(Tcl_GetString(objv[1]), "-nocomplain") != 0) {
            goto badargs;
        }
        symlink(Tcl_GetString(objv[2]), Tcl_GetString(objv[3]));
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRenameCmd --
 *
 *	Implements ns_rename. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRenameCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args:  should be \"",
                         argv[0], " filename1 filename2\"", NULL);
        return TCL_ERROR;
    }
    if (rename(argv[1], argv[2]) != 0) {
        Tcl_AppendResult(interp, "rename (\"", argv[1], "\", \"", argv[2],
    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
            return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclRenameObjCmd --
 *
 *	Implements ns_rename as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclRenameObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "filename1 filename2");
        return TCL_ERROR;
    }
    if (rename(Tcl_GetString(objv[1]), Tcl_GetString(objv[2])) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "rename (\"", 
            Tcl_GetString(objv[1]), "\", \"", 
            Tcl_GetString(objv[2]),
    	    "\") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteFpCmd --
 *
 *	Implements ns_writefp. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclWriteFpCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp   *itPtr = arg;
    Tcl_Channel chan;
    int	        nbytes = INT_MAX;
    int	        result;

    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args:  should be \"",
			 argv[0], " fileid ?nbytes?\"", NULL);
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[1], 0, 1, &chan) != TCL_OK) {
	return TCL_ERROR;
    }
    if (argc == 3 && Tcl_GetInt(interp, argv[2], &nbytes) != TCL_OK) {
	return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
	Tcl_SetResult(interp, "no connection", TCL_STATIC);
	return TCL_ERROR;
    }
    result = Ns_ConnSendChannel(itPtr->conn, chan, nbytes);
    if (result != NS_OK) {
	Tcl_AppendResult(interp, "i/o failed", NULL);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclWriteFpObjCmd --
 *
 *	Implements ns_writefp as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclWriteFpObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp   *itPtr = arg;
    Tcl_Channel chan;
    int	        nbytes = INT_MAX;
    int	        result;

    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "fileid ?nbytes?");
	    return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, Tcl_GetString(objv[1]), 0, 1, &chan) != TCL_OK) {
	    return TCL_ERROR;
    }
    if (objc == 3 && Tcl_GetIntFromObj(interp, objv[2], &nbytes) != TCL_OK) {
	    return TCL_ERROR;
    }
    if (itPtr->conn == NULL) {
	    Tcl_SetResult(interp, "no connection", TCL_STATIC);
	    return TCL_ERROR;
    }
    result = Ns_ConnSendChannel(itPtr->conn, chan, nbytes);
    if (result != NS_OK) {
        Tcl_SetResult(interp, "i/o failed", TCL_STATIC);
	    return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTruncateCmd --
 *
 *	Implements ns_truncate. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclTruncateCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int length;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " file ?length?\"", NULL);
	return TCL_ERROR;
    }

    if (argc == 2) {
    	length = 0;
    } else if (Tcl_GetInt(interp, argv[2], &length) != TCL_OK) {
    	return TCL_ERROR;
    }

    if (truncate(argv[1], length) != 0) {
        Tcl_AppendResult(interp, "truncate (\"", argv[1], "\", ",
            argv[2] ? argv[2] : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclTruncateObjCmd --
 *
 *	Implements ns_truncate as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclTruncateObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int length;
    
    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "file ?length?");
	return TCL_ERROR;
    }

    if (objc == 2) {
    	length = 0;
    } else if (Tcl_GetIntFromObj(interp, objv[2], &length) != TCL_OK) {
    	return TCL_ERROR;
    }

    if (truncate(Tcl_GetString(objv[1]), length) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "truncate (\"", 
            Tcl_GetString(objv[1]), "\", ",
            Tcl_GetString(objv[2]) ? Tcl_GetString(objv[2]) : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclFTruncateCmd --
 *
 *	Implements ns_ftruncate. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclFTruncateCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int length, fd;
    
    if (argc != 2 && argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " fileId ?length?\"", NULL);
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, argv[1], 1, &fd) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (argc == 2) {
    	length = 0;
    } else if (Tcl_GetInt(interp, argv[2], &length) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (ftruncate(fd, length) != 0) {
        Tcl_AppendResult(interp, "ftruncate (\"", argv[1], "\", ",
            argv[2] ? argv[2] : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclFTruncateObjCmd --
 *
 *	Implements ns_ftruncate as obj command. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclFTruncateObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int length, fd;
    
    if (objc != 2 && objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "fileId ?length?");
	return TCL_ERROR;
    }
    if (Ns_TclGetOpenFd(interp, Tcl_GetString(objv[1]), 1, &fd) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (objc == 2) {
    	length = 0;
    } else if (Tcl_GetInt(interp, Tcl_GetString(objv[2]), &length) != TCL_OK) {
    	return TCL_ERROR;
    }
    if (ftruncate(fd, length) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "ftruncate (\"", 
            Tcl_GetString(objv[1]), "\", ",
            Tcl_GetString(objv[2]) ? Tcl_GetString(objv[2]) : "0",
            ") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclChmodCmd --
 *
 *	NsTclChmodCmd 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclChmodCmd(ClientData dummy,Tcl_Interp *interp, int argc, char **argv)
{
    int mode;
    
    if (argc != 3) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " filename mode\"", NULL);
	return TCL_ERROR;
    }

    if (Tcl_GetInt(interp, argv[2], &mode) != TCL_OK) {
    	return TCL_ERROR;
    }

    if (chmod(argv[1], mode) != 0) {
        Tcl_AppendResult(interp, "chmod (\"", argv[1], "\", ", argv[2],
            ") failed:  ", Tcl_PosixError(interp), NULL);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclChmodObjCmd --
 *
 *	NsTclChmodCmd 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclChmodObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    int mode;
    
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "filename mode");
	return TCL_ERROR;
    }

    if (Tcl_GetIntFromObj(interp, objv[2], &mode) != TCL_OK) {
    	return TCL_ERROR;
    }

    if (chmod(Tcl_GetString(objv[1]), mode) != 0) {
        Tcl_Obj *result = Tcl_NewObj();
        Tcl_AppendStringsToObj(result, "chmod (\"", 
            Tcl_GetString(objv[1]), "\", ", 
            Tcl_GetString(objv[2]),
            ") failed:  ", Tcl_PosixError(interp), NULL);
        Tcl_SetObjResult(interp, result);
        return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclChanCmd --
 *
 *	Implement the ns_chan command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclChanCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Tcl_Channel chan;
    int new;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *cmd;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # of args: should be \"",
	    argv[0], " command ?args?\"", NULL);
	return TCL_ERROR;
    }
    cmd = argv[1];

    if (STREQ(cmd, "share")) {
	if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
		argv[0], " share name channel\"", NULL);
	    return TCL_ERROR;
	}
	chan = Tcl_GetChannel(interp, argv[2], NULL);
	if (chan == NULL) {
	    Tcl_AppendResult(interp, "no such channel: ", argv[2], NULL);
	    return TCL_ERROR;
	}
    	Ns_MutexLock(&servPtr->chans.lock);
	hPtr = Tcl_CreateHashEntry(&servPtr->chans.table, argv[3], &new);
    	if (new) {
	    Tcl_RegisterChannel(NULL, chan);
            Tcl_SetHashValue(hPtr, chan);
    	}
    	Ns_MutexUnlock(&servPtr->chans.lock);
	if (!new) {
	    Tcl_AppendResult(interp, "share already in use: ", argv[3], NULL);
	    return TCL_ERROR;
	}
	hPtr = Tcl_CreateHashEntry(&itPtr->chans, argv[3], &new);
	Tcl_SetHashValue(hPtr, chan);

    } else if (STREQ(cmd, "register")) {
	if (argc != 3) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
		argv[0], " register name\"", NULL);
	    return TCL_ERROR;
	}
    	Ns_MutexLock(&servPtr->chans.lock);
    	hPtr = Tcl_FindHashEntry(&servPtr->chans.table, argv[2]);
	if (hPtr != NULL) {
	    chan = Tcl_GetHashValue(hPtr);
	    Tcl_RegisterChannel(interp, chan);
	    Tcl_SetResult(interp, (char *) Tcl_GetChannelName(chan), TCL_VOLATILE);
	}
    	Ns_MutexUnlock(&servPtr->chans.lock);
	if (hPtr == NULL) {
	    Tcl_AppendResult(interp, "no such shared channel: ", argv[2], NULL);
	    return TCL_ERROR;
	}
	hPtr = Tcl_CreateHashEntry(&itPtr->chans, argv[2], &new);
	Tcl_SetHashValue(hPtr, chan);

    } else if (STREQ(cmd, "unregister")) {
	if (argc != 3) {
	    Tcl_AppendResult(interp, "wrong # of args: should be \"",
		argv[0], " unregister name\"", NULL);
	    return TCL_ERROR;
	}
	hPtr = Tcl_FindHashEntry(&itPtr->chans, argv[2]);
	if (hPtr == NULL) {
	    Tcl_AppendResult(interp, "no such registered channel: ", argv[2], NULL);
	    return TCL_ERROR;
	}
	chan = Tcl_GetHashValue(hPtr);
	Tcl_DeleteHashEntry(hPtr);
    	Ns_MutexLock(&servPtr->chans.lock);
	Tcl_UnregisterChannel(interp, chan);
    	Ns_MutexUnlock(&servPtr->chans.lock);

    } else if (STREQ(cmd, "list")) {
	hPtr = Tcl_FirstHashEntry(&itPtr->chans, &search);
	while (hPtr != NULL) {
	    Tcl_AppendElement(interp, Tcl_GetHashKey(&itPtr->chans, hPtr));
	    hPtr = Tcl_NextHashEntry(&search);
	}
    } else if (STREQ(cmd, "cleanup")) {
	if (itPtr->chans.numEntries > 0) {
	    Ns_MutexLock(&servPtr->chans.lock);
	    hPtr = Tcl_FirstHashEntry(&itPtr->chans, &search);
	    while (hPtr != NULL) {
	        chan = Tcl_GetHashValue(hPtr);
		Tcl_UnregisterChannel(interp, chan);
	    	hPtr = Tcl_NextHashEntry(&search);
	    }
	    Ns_MutexUnlock(&servPtr->chans.lock);
	}

    } else {
	Tcl_AppendResult(interp, "no such command \"", cmd,
	   "\": should be share, register, unregister, list, or cleanup", NULL);
	return TCL_ERROR;

    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclChanObjCmd --
 *
 *	Implement the ns_chan command.
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclChanObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
    NsInterp *itPtr = arg;
    NsServer *servPtr = itPtr->servPtr;
    Tcl_Channel chan;
    int new;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    char *cmd;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "command ?args?");
	return TCL_ERROR;
    }
    cmd = Tcl_GetString(objv[1]);

    if (STREQ(cmd, "share")) {
	if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "share name channel");
	    return TCL_ERROR;
	}
	chan = Tcl_GetChannel(interp, Tcl_GetString(objv[2]), NULL);
	if (chan == NULL) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "no such channel: ", 
		    Tcl_GetString(objv[2]), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
    	Ns_MutexLock(&servPtr->chans.lock);
	hPtr = Tcl_CreateHashEntry(&servPtr->chans.table, Tcl_GetString(objv[3]), &new);
    	if (new) {
	    Tcl_RegisterChannel(NULL, chan);
            Tcl_SetHashValue(hPtr, chan);
    	}
    	Ns_MutexUnlock(&servPtr->chans.lock);
	if (!new) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "share already in use: ", 
		    Tcl_GetString(objv[3]), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
	hPtr = Tcl_CreateHashEntry(&itPtr->chans, Tcl_GetString(objv[3]), &new);
	Tcl_SetHashValue(hPtr, chan);

    } else if (STREQ(cmd, "register")) {
	if (objc != 3) {
	    Tcl_WrongNumArgs(interp, 1, objv, "register name");
	    return TCL_ERROR;
	}
    	Ns_MutexLock(&servPtr->chans.lock);
    	hPtr = Tcl_FindHashEntry(&servPtr->chans.table, Tcl_GetString(objv[2]));
	if (hPtr != NULL) {
	    chan = Tcl_GetHashValue(hPtr);
	    Tcl_RegisterChannel(interp, chan);
	    Tcl_SetResult(interp, Tcl_GetChannelName(chan), TCL_VOLATILE);
	}
    	Ns_MutexUnlock(&servPtr->chans.lock);
	if (hPtr == NULL) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "no such shared channel: ",
		    Tcl_GetString(objv[3]), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
	hPtr = Tcl_CreateHashEntry(&itPtr->chans, Tcl_GetString(objv[2]), &new);
	Tcl_SetHashValue(hPtr, chan);

    } else if (STREQ(cmd, "unregister")) {
	if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "unregister name");
	    return TCL_ERROR;
	}
	hPtr = Tcl_FindHashEntry(&itPtr->chans, Tcl_GetString(objv[2]));
	if (hPtr == NULL) {
	    Tcl_Obj *result = Tcl_NewObj();
	    Tcl_AppendStringsToObj(result, "no such registered channel: ",
		    Tcl_GetString(objv[3]), NULL);
	    Tcl_SetObjResult(interp, result);
	    return TCL_ERROR;
	}
	chan = Tcl_GetHashValue(hPtr);
	Tcl_DeleteHashEntry(hPtr);
    	Ns_MutexLock(&servPtr->chans.lock);
	Tcl_UnregisterChannel(interp, chan);
    	Ns_MutexUnlock(&servPtr->chans.lock);

    } else if (STREQ(cmd, "list")) {
	hPtr = Tcl_FirstHashEntry(&itPtr->chans, &search);
	while (hPtr != NULL) {
	    Tcl_AppendElement(interp, Tcl_GetHashKey(&itPtr->chans, hPtr));
	    hPtr = Tcl_NextHashEntry(&search);
	}
    } else if (STREQ(cmd, "cleanup")) {
	if (itPtr->chans.numEntries > 0) {
	    Ns_MutexLock(&servPtr->chans.lock);
	    hPtr = Tcl_FirstHashEntry(&itPtr->chans, &search);
	    while (hPtr != NULL) {
	        chan = Tcl_GetHashValue(hPtr);
		Tcl_UnregisterChannel(interp, chan);
	    	hPtr = Tcl_NextHashEntry(&search);
	    }
	    Ns_MutexUnlock(&servPtr->chans.lock);
	}

    } else {
	Tcl_Obj *result = Tcl_NewObj();
	Tcl_AppendStringsToObj(result, "no such command: ",
		cmd, "should be share, register, unregister, list, or cleanup", NULL);
	Tcl_SetObjResult(interp, result);
	return TCL_ERROR;
    }

    return TCL_OK;
}

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
 * tclenv.c --
 *
 *	Implement the "ns_env" command.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclenv.c,v 1.14 2003/02/04 23:10:49 jrasmuss23 Exp $, compiled: " __DATE__ " " __TIME__;

#include	"nsd.h"

static int PutEnv(Tcl_Interp *interp, char *name, char *value);
static Ns_Mutex lock;
#ifdef HAVE__NSGETENVIRON
#include <crt_externs.h>
#elif !defined(_WIN32)
extern char **environ;
#endif


/*
 *----------------------------------------------------------------------
 *
 * Ns_CopyEnviron --
 *
 *	Copy the environment to the given dstring along with
 *	an argv vector.
 *
 * Results:
 *	Pointer to dsPtr->string.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char **
Ns_CopyEnviron(Ns_DString *dsPtr)
{
    char *s, **envp;
    int i;

    Ns_MutexLock(&lock);
    envp = Ns_GetEnviron();
    for (i = 0; (s = envp[i]) != NULL; ++i) {
	Ns_DStringAppendArg(dsPtr, s);
    }
    Ns_MutexUnlock(&lock);
    return Ns_DStringAppendArgv(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclEnvCmd --
 *
 *	Implement the "env" command.  Read the code to see what it does.
 *	NOTE:  The getenv() and putenv() routines are assumed MT safe
 *	and there's no attempt to avoid the race condition between
 *	finding a variable and using it.  The reason is it's assumed
 *	the environment would only be modified, if ever, at startup.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	Environment variables may be updated.
 *
 *----------------------------------------------------------------------
 */

int
NsTclEnvCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char	*name, *value, **envp;
    int		status, i;
    Tcl_DString	ds;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args:  should be \"",
		argv[0], " command ?args ...?\"", NULL);
	return TCL_ERROR;
    }

    status = TCL_OK;
    Ns_MutexLock(&lock);
    if (STREQ(argv[1], "names")) {
	if (argc != 2) {
	    Tcl_AppendResult(interp, "wrong # args:  should be \"",
		    argv[0], " names\"", NULL);
	    status = TCL_ERROR;
	} else {
	    Tcl_DStringInit(&ds);
    	    envp = Ns_GetEnviron();
	    for (i = 0; envp[i] != NULL; ++i) {
		name = envp[i];
		value = strchr(name, '=');
		Tcl_DStringAppend(&ds, name, value ? value - name : -1);
	    	Tcl_AppendElement(interp, ds.string);
		Tcl_DStringTrunc(&ds, 0);
	    }
	    Tcl_DStringFree(&ds);
	}

    } else if (STREQ(argv[1], "exists")) {
	if (argc != 3) {
	    Tcl_AppendResult(interp, "wrong # args:  should be \"",
		    argv[0], " exists name\"", NULL);
	    status = TCL_ERROR;
	} else {
	    Tcl_SetResult(interp, getenv(argv[2]) ? "1" : "0", TCL_STATIC);
	}

    } else if (STREQ(argv[1], "get")) {
	if ((argc != 3 && argc != 4) ||
		(argc == 4 && !STREQ(argv[2], "-nocomplain"))) {
badargs:
	    Tcl_AppendResult(interp, "wrong # args:  should be \"",
		    argv[0], " ", argv[1], " ?-nocomplain? name\"", NULL);
	    status = TCL_ERROR;
	}
	name = argv[argc-1];
	value = getenv(name);
	if (value != NULL) {
	    Tcl_SetResult(interp, value, TCL_VOLATILE);
	} else if (argc == 4) {
	    Tcl_AppendResult(interp, "no such environment variable: ",
		argv[argc-1], NULL);
	    status = TCL_ERROR;
	}

    } else if (STREQ(argv[1], "set")) {
	if (argc != 4) {
	    Tcl_AppendResult(interp, "wrong # args:  should be \"",
		    argv[0], " set name value\"", NULL);
	    status =  TCL_ERROR;
	} else {
	    status = PutEnv(interp, argv[2], argv[3]);
	}

    } else if (STREQ(argv[1], "unset")) {
	if ((argc != 3 && argc != 4) ||
		(argc == 4 && !STREQ(argv[2], "-nocomplain"))) {
	    goto badargs;
	}
	name = argv[argc-1];
	if (argc == 3 && getenv(name) == NULL) {
	    Tcl_AppendResult(interp, "no such environment variable: ", name,
			     NULL);
	    status = TCL_ERROR;
	} else {
	    status = PutEnv(interp, name, "");
	}

    } else {
	Tcl_AppendResult(interp, "unknown command \"",
		argv[1], "\": should be exists, names, get, set, or unset", NULL);
	status = TCL_ERROR;
    }

    Ns_MutexUnlock(&lock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * PutEnv --
 *
 *	NsTclEnvCmd helper routine to update an environment variable.
 *
 * Results:
 *	TCL_OK or TCL_ERROR.
 *
 * Side effects:
 *	Environment variable is set.
 *
 *----------------------------------------------------------------------
 */

static int
PutEnv(Tcl_Interp *interp, char *name, char *value)
{
    char *s;
    size_t len;

    len = strlen(name);
    if (value != NULL) {
	len += strlen(value) + 1;
    }
    /* NB: Use malloc() directly as putenv() would expect. */
    s = malloc(len + 1);
    if (s == NULL) {
	Tcl_SetResult(interp,
		"could not allocate memory for new env entry", TCL_STATIC);
	return TCL_ERROR;
    }
    strcpy(s, name);
    if (value != NULL) {
	strcat(s, "=");
	strcat(s, value);
    }
    if (putenv(s) != 0) {
	Tcl_AppendResult(interp, "could not put environment entry \"",
		s, "\": ", Tcl_PosixError(interp), NULL);
	free(s);
	return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_GetEnviron --
 *
 *	Return the environment vector.
 *
 * Results:
 *	Pointer to environment.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char **
Ns_GetEnviron(void)
{
   char **envp;

#ifdef HAVE__NSGETENVIRON
    envp = *_NSGetEnviron();
#else
    envp = environ;
#endif
    return envp;
}

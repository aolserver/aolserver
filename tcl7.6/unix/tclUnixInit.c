/* 
 * tclUnixInit.c --
 *
 *	Contains the Unix-specific interpreter initialization functions.
 *
 * Copyright (c) 1995-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclUnixInit.c 1.19 96/10/04 10:28:56
 */

#include "tclInt.h"
#include "tclPort.h"
#if defined(__FreeBSD__)
#   include <floatingpoint.h>
#endif
#if defined(__bsdi__)
#   include <sys/param.h>
#   if _BSDI_VERSION > 199501
#	include <dlfcn.h>
#   endif
#endif

/*
 * Default directory in which to look for Tcl library scripts.  The
 * symbol is defined by Makefile.
 */

static char defaultLibraryDir[200] = TCL_LIBRARY;

/*
 * Directory in which to look for packages (each package is typically
 * installed as a subdirectory of this directory).  The symbol is
 * defined by Makefile.
 */

static char pkgPath[200] = TCL_PACKAGE_PATH;

/*
 * The following string is the startup script executed in new
 * interpreters.  It looks on disk in several different directories
 * for a script "init.tcl" that is compatible with this version
 * of Tcl.  The init.tcl script does all of the real work of
 * initialization.
 */

static char initScript[] =
"proc tclInit {} {\n\
    global tcl_library tcl_version tcl_patchLevel env\n\
    rename tclInit {}\n\
    set dirs {}\n\
    if [info exists env(TCL_LIBRARY)] {\n\
	lappend dirs $env(TCL_LIBRARY)\n\
    }\n\
    lappend dirs [info library]\n\
    set parentDir [file dirname [file dirname [info nameofexecutable]]]\n\
    lappend dirs $parentDir/lib/tcl$tcl_version\n\
    if [string match {*[ab]*} $tcl_patchLevel] {\n\
	set lib tcl$tcl_patchLevel\n\
    } else {\n\
	set lib tcl$tcl_version\n\
    }\n\
    lappend dirs [file dirname $parentDir]/$lib/library\n\
    lappend dirs $parentDir/library\n\
    foreach i $dirs {\n\
	set tcl_library $i\n\
	if ![catch {uplevel #0 source $i/init.tcl}] {\n\
	    return\n\
	}\n\
    }\n\
    set msg \"Can't find a usable init.tcl in the following directories: \n\"\n\
    append msg \"    $dirs\n\"\n\
    append msg \"This probably means that Tcl wasn't installed properly.\n\"\n\
    error $msg\n\
}\n\
tclInit";

/*
 *----------------------------------------------------------------------
 *
 * TclPlatformInit --
 *
 *	Performs Unix-specific interpreter initialization related to the
 *      tcl_library and tcl_platform variables, and other platform-
 *	specific things.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets "tcl_library" and "tcl_platform" Tcl variables.
 *
 *----------------------------------------------------------------------
 */

void
TclPlatformInit(interp)
    Tcl_Interp *interp;
{
#ifndef NO_UNAME
    struct utsname name;
#endif
    int unameOK;
    static int initialized = 0;

    tclPlatform = TCL_PLATFORM_UNIX;
    Tcl_SetVar(interp, "tcl_library", defaultLibraryDir, TCL_GLOBAL_ONLY);
    Tcl_SetVar(interp, "tcl_pkgPath", pkgPath, TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, "tcl_platform", "platform", "unix", TCL_GLOBAL_ONLY);
    unameOK = 0;
#ifndef NO_UNAME
    if (uname(&name) >= 0) {
	unameOK = 1;
	Tcl_SetVar2(interp, "tcl_platform", "os", name.sysname,
		TCL_GLOBAL_ONLY);
	Tcl_SetVar2(interp, "tcl_platform", "osVersion", name.release,
		TCL_GLOBAL_ONLY);
	Tcl_SetVar2(interp, "tcl_platform", "machine", name.machine,
		TCL_GLOBAL_ONLY);
    }
#endif
    if (!unameOK) {
	Tcl_SetVar2(interp, "tcl_platform", "os", "", TCL_GLOBAL_ONLY);
	Tcl_SetVar2(interp, "tcl_platform", "osVersion", "", TCL_GLOBAL_ONLY);
	Tcl_SetVar2(interp, "tcl_platform", "machine", "", TCL_GLOBAL_ONLY);
    }

    if (!initialized) {
	/*
	 * The code below causes SIGPIPE (broken pipe) errors to
	 * be ignored.  This is needed so that Tcl processes don't
	 * die if they create child processes (e.g. using "exec" or
	 * "open") that terminate prematurely.  The signal handler
	 * is only set up when the first interpreter is created;
	 * after this the application can override the handler with
	 * a different one of its own, if it wants.
	 */
    
#ifdef SIGPIPE
	(void) signal(SIGPIPE, SIG_IGN);
#endif /* SIGPIPE */

#ifdef __FreeBSD__
	fpsetround(FP_RN);
	fpsetmask(0L);
#endif

#if defined(__bsdi__) && (_BSDI_VERSION > 199501)
	/*
	 * Find local symbols. Don't report an error if we fail.
	 */
	(void) dlopen (NULL, RTLD_NOW);
#endif
	initialized = 1;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_Init --
 *
 *	This procedure is typically invoked by Tcl_AppInit procedures
 *	to perform additional initialization for a Tcl interpreter,
 *	such as sourcing the "init.tcl" script.
 *
 * Results:
 *	Returns a standard Tcl completion code and sets interp->result
 *	if there is an error.
 *
 * Side effects:
 *	Depends on what's in the init.tcl script.
 *
 *----------------------------------------------------------------------
 */

int
Tcl_Init(interp)
    Tcl_Interp *interp;		/* Interpreter to initialize. */
{
    return Tcl_Eval(interp, initScript);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_SourceRCFile --
 *
 *	This procedure is typically invoked by Tcl_Main of Tk_Main
 *	procedure to source an application specific rc file into the
 *	interpreter at startup time.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Depends on what's in the rc script.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_SourceRCFile(interp)
    Tcl_Interp *interp;		/* Interpreter to source rc file into. */
{
    Tcl_DString temp;
    char *fileName;
    Tcl_Channel errChannel;

    fileName = Tcl_GetVar(interp, "tcl_rcFileName", TCL_GLOBAL_ONLY);

    if (fileName != NULL) {
        Tcl_Channel c;
	char *fullName;

        Tcl_DStringInit(&temp);
	fullName = Tcl_TranslateFileName(interp, fileName, &temp);
	if (fullName == NULL) {
	    errChannel = Tcl_GetStdChannel(TCL_STDERR);
	    if (errChannel) {
		Tcl_Write(errChannel, interp->result, -1);
		Tcl_Write(errChannel, "\n", 1);
	    }
	} else {

	    /*
	     * Test for the existence of the rc file before trying to read it.
	     */

            c = Tcl_OpenFileChannel(NULL, fullName, "r", 0);
            if (c != (Tcl_Channel) NULL) {
                Tcl_Close(NULL, c);
		if (Tcl_EvalFile(interp, fullName) != TCL_OK) {
		    errChannel = Tcl_GetStdChannel(TCL_STDERR);
		    if (errChannel) {
			Tcl_Write(errChannel, interp->result, -1);
			Tcl_Write(errChannel, "\n", 1);
		    }
		}
	    }
	}
        Tcl_DStringFree(&temp);
    }
}

/* 
 * tclUnixPipe.c -- This file implements the UNIX-specific exec pipeline 
 *                  functions.
 *
 * Copyright (c) 1991-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclUnixPipe.c 1.30 96/09/12 14:57:15
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/tcl7.6/unix/Attic/tclUnixPipe.c,v 1.1.1.1 2000/05/02 13:48:29 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "tclInt.h"
#include "tclPort.h"

/*
 * Declarations for local procedures defined in this file:
 */

static void             RestoreSignals _ANSI_ARGS_((void));
static int		SetupStdFile _ANSI_ARGS_((Tcl_File file, int type));

/*
 *----------------------------------------------------------------------
 *
 * TclpCreateProcess --
 *
 *	Create a child process that has the specified files as its 
 *	standard input, output, and error.  The child process runs
 *	asynchronously and runs with the same environment variables
 *	as the creating process.
 *
 *	The path is searched to find the specified executable.  
 *
 * Results:
 *	The return value is TCL_ERROR and an error message is left in
 *	interp->result if there was a problem creating the child 
 *	process.  Otherwise, the return value is TCL_OK and *pidPtr is
 *	filled with the process id of the child process.
 * 
 * Side effects:
 *	A process is created.
 *	
 *----------------------------------------------------------------------
 */

    /* ARGSUSED */
int
TclpCreateProcess(interp, argc, argv, inputFile, outputFile, errorFile, 
	inputFileName, outputFileName, errorFileName, pidPtr)
    Tcl_Interp *interp;		/* Interpreter in which to leave errors that
				 * occurred when creating the child process.
				 * Error messages from the child process
				 * itself are sent to errorFile. */
    int argc;			/* Number of arguments in following array. */
    char **argv;		/* Array of argument strings.  argv[0]
				 * contains the name of the executable
				 * converted to native format (using the
				 * Tcl_TranslateFileName call).  Additional
				 * arguments have not been converted. */
    Tcl_File inputFile;		/* If non-NULL, gives the file to use as
				 * input for the child process.  If inputFile
				 * file is not readable or is NULL, the child
				 * will receive no standard input. */
    Tcl_File outputFile;	/* If non-NULL, gives the file that
				 * receives output from the child process.  If
				 * outputFile file is not writeable or is
				 * NULL, output from the child will be
				 * discarded. */
    Tcl_File errorFile;		/* If non-NULL, gives the file that
				 * receives errors from the child process.  If
				 * errorFile file is not writeable or is NULL,
				 * errors from the child will be discarded.
				 * errorFile may be the same as outputFile. */
    char *inputFileName;	/* If non-NULL, gives the name of the disk
				 * file that corresponds to inputFile
				 * (unused). */
    char *outputFileName;	/* If non-NULL, gives the name of the disk
				 * file that corresponds to outputFile
				 * (unused). */
    char *errorFileName;	/* If non-NULL, gives the name of the disk
				 * file that corresponds to errorFile
				 * (unused). */
    int *pidPtr;		/* If this procedure is successful, pidPtr
				 * is filled with the process id of the child
				 * process. */
{
    Tcl_File errPipeIn, errPipeOut;
    int pid, joinThisError, count, status;
    char errSpace[200];
    
    errPipeIn = NULL;
    errPipeOut = NULL;
    pid = -1;

    /*
     * Create a pipe that the child can use to return error
     * information if anything goes wrong.
     */

    if (TclCreatePipe(&errPipeIn, &errPipeOut) == 0) {
	Tcl_AppendResult(interp, "couldn't create pipe: ",
		Tcl_PosixError(interp), (char *) NULL);
	goto error;
    }

    joinThisError = (errorFile == outputFile);
    /* pid = vfork();	NB: Never use vfork(). */
    pid = fork();
    if (pid == 0) {

	/*
	 * Set up stdio file handles for the child process.
	 */

	if (!SetupStdFile(inputFile, TCL_STDIN)
		|| !SetupStdFile(outputFile, TCL_STDOUT)
		|| (!joinThisError && !SetupStdFile(errorFile, TCL_STDERR))
		|| (joinThisError &&
			((dup2(1,2) == -1) ||
			 (fcntl(2, F_SETFD, 0) != 0)))) {
	    sprintf(errSpace,
		    "%dforked process couldn't set up input/output: ",
		    errno);
	    TclWriteFile(errPipeOut, 1, errSpace, (int) strlen(errSpace));
	    _exit(1);
	}

	/*
	 * Close the input side of the error pipe.
	 */

	RestoreSignals();
	execvp(argv[0], &argv[0]);
	sprintf(errSpace, "%dcouldn't execute \"%.150s\": ", errno,
		argv[0]);
	TclWriteFile(errPipeOut, 1, errSpace, (int) strlen(errSpace));
	_exit(1);
    }
    if (pid == -1) {
	Tcl_AppendResult(interp, "couldn't fork child process: ",
		Tcl_PosixError(interp), (char *) NULL);
	goto error;
    }

    /*
     * Read back from the error pipe to see if the child startup
     * up OK.  The info in the pipe (if any) consists of a decimal
     * errno value followed by an error message.
     */

    TclCloseFile(errPipeOut);
    errPipeOut = NULL;

    count = TclReadFile(errPipeIn, 1, errSpace,
	    (size_t) (sizeof(errSpace) - 1));
    if (count > 0) {
	char *end;
	errSpace[count] = 0;
	Tcl_SetErrno (strtol(errSpace, &end, 10));
	Tcl_AppendResult(interp, end, Tcl_PosixError(interp),
		(char *) NULL);
	goto error;
    }
    
    TclCloseFile(errPipeIn);
    *pidPtr = pid;
    return TCL_OK;

    error:
    if (pid != -1) {
	/*
	 * Reap the child process now if an error occurred during its
	 * startup.
	 */

	Tcl_WaitPid(pid, &status, WNOHANG);
    }
    
    if (errPipeIn) {
	TclCloseFile(errPipeIn);
    }
    if (errPipeOut) {
	TclCloseFile(errPipeOut);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * RestoreSignals --
 *
 *      This procedure is invoked in a forked child process just before
 *      exec-ing a new program to restore all signals to their default
 *      settings.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Signal settings get changed.
 *
 *----------------------------------------------------------------------
 */
 
static void
RestoreSignals()
{
#ifdef SIGABRT
    signal(SIGABRT, SIG_DFL);
#endif
#ifdef SIGALRM
    signal(SIGALRM, SIG_DFL);
#endif
#ifdef SIGFPE
    signal(SIGFPE, SIG_DFL);
#endif
#ifdef SIGHUP
    signal(SIGHUP, SIG_DFL);
#endif
#ifdef SIGILL
    signal(SIGILL, SIG_DFL);
#endif
#ifdef SIGINT
    signal(SIGINT, SIG_DFL);
#endif
#ifdef SIGPIPE
    signal(SIGPIPE, SIG_DFL);
#endif
#ifdef SIGQUIT
    signal(SIGQUIT, SIG_DFL);
#endif
#ifdef SIGSEGV
    signal(SIGSEGV, SIG_DFL);
#endif
#ifdef SIGTERM
    signal(SIGTERM, SIG_DFL);
#endif
#ifdef SIGUSR1
    signal(SIGUSR1, SIG_DFL);
#endif
#ifdef SIGUSR2
    signal(SIGUSR2, SIG_DFL);
#endif
#ifdef SIGCHLD
    signal(SIGCHLD, SIG_DFL);
#endif
#ifdef SIGCONT
    signal(SIGCONT, SIG_DFL);
#endif
#ifdef SIGTSTP
    signal(SIGTSTP, SIG_DFL);
#endif
#ifdef SIGTTIN
    signal(SIGTTIN, SIG_DFL);
#endif
#ifdef SIGTTOU
    signal(SIGTTOU, SIG_DFL);
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * SetupStdFile --
 *
 *	Set up stdio file handles for the child process, using the
 *	current standard channels if no other files are specified.
 *	If no standard channel is defined, or if no file is associated
 *	with the channel, then the corresponding standard fd is closed.
 *
 * Results:
 *	Returns 1 on success, or 0 on failure.
 *
 * Side effects:
 *	Replaces stdio fds.
 *
 *----------------------------------------------------------------------
 */

static int
SetupStdFile(file, type)
    Tcl_File file;		/* File to dup, or NULL. */
    int type;			/* One of TCL_STDIN, TCL_STDOUT, TCL_STDERR */
{
    Tcl_Channel channel;
    int fd;
    int targetFd = 0;		/* Initializations here needed only to */
    int direction = 0;		/* prevent warnings about using uninitialized
				 * variables. */

    switch (type) {
	case TCL_STDIN:
	    targetFd = 0;
	    direction = TCL_READABLE;
	    break;
	case TCL_STDOUT:
	    targetFd = 1;
	    direction = TCL_WRITABLE;
	    break;
	case TCL_STDERR:
	    targetFd = 2;
	    direction = TCL_WRITABLE;
	    break;
    }

    if (!file) {
	channel = Tcl_GetStdChannel(type);
	if (channel) {
	    file = Tcl_GetChannelFile(channel, direction);
	}
    }
    if (file) {
	fd = (int)Tcl_GetFileInfo(file, NULL);
	if (fd != targetFd) {
	    if (dup2(fd, targetFd) == -1) {
		return 0;
	    }

            /*
             * Must clear the close-on-exec flag for the target FD, since
             * some systems (e.g. Ultrix) do not clear the CLOEXEC flag on
             * the target FD.
             */
            
            fcntl(targetFd, F_SETFD, 0);
	} else {
	    int result;

	    /*
	     * Since we aren't dup'ing the file, we need to explicitly clear
	     * the close-on-exec flag.
	     */

	    result = fcntl(fd, F_SETFD, 0);
	}
    } else {
	close(targetFd);
    }
    return 1;
}

/*
 *----------------------------------------------------------------------
 *
 * TclCreatePipe --
 *
 *      Creates a pipe - simply calls the pipe() function.
 *
 * Results:
 *      Returns 1 on success, 0 on failure. 
 *
 * Side effects:
 *      Creates a pipe.
 *
 *----------------------------------------------------------------------
 */
int
TclCreatePipe(readPipe, writePipe)
    Tcl_File *readPipe;	/* Location to store file handle for
				 * read side of pipe. */
    Tcl_File *writePipe;	/* Location to store file handle for
				 * write side of pipe. */
{
    int pipeIds[2];

    if (pipe(pipeIds) != 0) {
	return 0;
    }

    fcntl(pipeIds[0], F_SETFD, FD_CLOEXEC);
    fcntl(pipeIds[1], F_SETFD, FD_CLOEXEC);

    *readPipe = Tcl_GetFile((ClientData)pipeIds[0], TCL_UNIX_FD);
    *writePipe = Tcl_GetFile((ClientData)pipeIds[1], TCL_UNIX_FD);
    return 1;
}

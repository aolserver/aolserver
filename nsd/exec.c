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


static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/exec.c,v 1.13 2001/11/05 20:23:46 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

#define ERR_DUP_0	1
#define ERR_DUP_1	2
#define ERR_DUP_IN	3
#define ERR_DUP_OUT	4
#define ERR_CHDIR	5
#define ERR_EXEC	6

static char   **Args2Argv(Ns_DString *dsPtr, char *args);
static char   **Set2Argv(Ns_DString *dsPtr, Ns_Set *set);
static int  	WaitForProcess(int pid, int *statusPtr);


/*
 *----------------------------------------------------------------------
 * Ns_ExecProcess --
 *
 *	Execute a command in a child process.
 *
 * Results:
 *      Return process id of child process exec'ing the command or
 *	-1 on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ExecProcess(char *exec, char *dir, int fdin, int fdout, char *args,
	       Ns_Set *env)
{
    return Ns_ExecArgblk(exec, dir, fdin, fdout, args, env);
}


/*
 *----------------------------------------------------------------------
 * Ns_ExecProc --
 *
 *      Execute a command in a child process.
 *
 * Results:
 *      Return process id of child process exec'ing the command or
 *	-1 on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ExecProc(char *exec, char **argv)
{
    return Ns_ExecArgv(exec, NULL, 0, 1, argv, NULL);
}


/*
 *----------------------------------------------------------------------
 * Ns_WaitProcess --
 *
 *      Wait for child process
 *
 * Results:
 *      Ruturn NS_OK for success and NS_ERROR for failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_WaitProcess(int pid)
{
    return Ns_WaitForProcess(pid, NULL);
}


/*
 *----------------------------------------------------------------------
 * Ns_WaitForProcess --
 *
 *      Wait for child process.
 *
 * Results:
 *      Ruturn NS_OK for success and NS_ERROR for failure.  *statusPtr
 *	is set to the exit code of the child process.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_WaitForProcess(int pid, int *statusPtr)
{
    int waitstatus;
    int status, exitcode;

    status = NS_ERROR;
waitproc:
    if (waitpid(pid, &waitstatus, 0) != pid) {
        Ns_Log(Error, "exec: waitpid for process %d failed: %s",
	       pid, strerror(errno));
    } else {
        if (WIFEXITED(waitstatus)) {
	    exitcode = WEXITSTATUS(waitstatus);
	    status = NS_OK;
        } else if (WIFSIGNALED(waitstatus)) {
            Ns_Log(Error, "exec: process %d exited from signal: %d",
		   pid, WTERMSIG(waitstatus));
#ifdef WCOREDUMP
            if (WCOREDUMP(waitstatus)) {
                Ns_Log(Error, "exec: process %d dumped core", pid);
            }
#endif /* WCOREDUMP */
        } else if (WIFSTOPPED(waitstatus)) {
            Ns_Log(Notice, "exec: process %d stopped by signal: %d",
		   pid, WSTOPSIG(waitstatus));
            goto waitproc;
#ifdef WIFCONTINUED
        } else if (WIFCONTINUED(waitstatus)) {
            Ns_Log(Notice, "exec: process %d resumed", pid);
            goto waitproc;
#endif /* WIFCONTIUED */
        } else {
            Ns_Log(Bug, "exec: waitpid(%d) returned invalid status: %d",
		   pid, waitstatus);
        }
    }
    if (status == NS_OK) {
    	if (statusPtr != NULL) {
	    *statusPtr = exitcode;
	}
	if (exitcode != 0) {
            Ns_Log(Warning, "exec: process %d exited with non-zero status: %d",
        	   pid, exitcode);
	    status = NS_ERROR;
	}
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 * Ns_ExecArgblk --
 *
 *      Execute a command in a child process.
 *
 * Results:
 *      Return process id of child process exec'ing the command or
 *	-1 on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ExecArgblk(char *exec, char *dir, int fdin, int fdout,
		char *args, Ns_Set *env)
{
    /*
     * Unix ExecArgblk simply calls ExecArgv.
     */
     
    int    pid;
    char **argv;
    Ns_DString vds;

    Ns_DStringInit(&vds);
    if (args != NULL) {
        argv = Args2Argv(&vds, args);
    } else {
        argv = NULL;
    }
    pid = Ns_ExecArgv(exec, dir, fdin, fdout, argv, env);
    Ns_DStringFree(&vds);
    return pid;
}


/*
 *----------------------------------------------------------------------
 * Ns_ExecArgv --
 *
 *	Execute a command in a child process using fork(2) and execve(2)
 *
 *	This function has a bit of a candy machine interface.
 *	The child send an extended error message to the parent.
 *
 * Results:
 *      Return process id of child process exec'ing the command or
 *	-1 on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ExecArgv(char *exec, char *dir, int fdin, int fdout,
	    char **argv, Ns_Set *env)
{
    int             pid;
    struct iovec iov[2];
    int    errpipe[2];
    int    nread, status, err;
    Ns_DString eds, vds;
    char **envp;
    char  *argvSh[4];

    if (exec == NULL) {
        return -1;
    }
    if (argv == NULL) {
        argv = argvSh;
        argv[0] = "/bin/sh";
        argv[1] = "-c";
        argv[2] = exec;
        argv[3] = NULL;
        exec = argv[0];
    }
    if (ns_pipe(errpipe) < 0) {
        Ns_Log(Error, "exec: failed to create pipe for '%s': '%s'",
	       exec, strerror(errno));
        return -1;
    }

    /*
     * Create the envp array, either from the given Ns_Set
     * or from a safe copy of the environment.
     */

    Ns_DStringInit(&eds);
    Ns_DStringInit(&vds);
    if (env == NULL) {
	envp = Ns_GetEnvironment(&eds);
    } else {
	envp = Set2Argv(&eds, env);
    }

    /*
     * By default, input and output are the same as the server.
     */

    if (fdin < 0) {
	fdin = 0;
    }
    if (fdout < 0) {
	fdout = 1;
    }

    /*
     * The following struct iov is used to return two integers
     * from the child process to describe any error setting up
     * or creating the new process.
     */

    err = status = 0;
    iov[0].iov_base = (caddr_t) &err;
    iov[1].iov_base = (caddr_t) &status;
    iov[0].iov_len = iov[1].iov_len = sizeof(int);

    pid = ns_fork();
    if (pid < 0) {
        Ns_Log(Error, "exec: failed to fork '%s': '%s'", exec, strerror(errno));
        close(errpipe[0]);
        close(errpipe[1]);
    } else if (pid == 0) {
	/*
	 * Setup the child and exec the new process.
	 */

        close(errpipe[0]);
        if (dir != NULL && chdir(dir) != 0) {
	    status = ERR_CHDIR;
        } else if (fdin == 1 && (fdin = dup(1)) < 0) {
	    status = ERR_DUP_1;
	} else if (fdout == 0 && (fdout = dup(0)) < 0) {
	    status = ERR_DUP_0;
	} else if (fdin != 0 && dup2(fdin, 0) < 0) {
	    status = ERR_DUP_IN;
	} else if (fdout != 1 && dup2(fdout, 1) < 0) {
	    status = ERR_DUP_OUT;
	} else {
	    if (fdin > 2) {
		close(fdin);
	    }
	    if (fdout > 2) {
            	close(fdout);
	    }
            NsRestoreSignals();
	    Ns_NoCloseOnExec(0);
	    Ns_NoCloseOnExec(1);
	    Ns_NoCloseOnExec(2);
            execve(exec, argv, envp);
	    status = ERR_EXEC;
	}

	/*
	 * Send error status message to parent and exit.
	 */

	err = errno;
	(void) writev(errpipe[1], iov, 2);
	_exit(1);
	
    } else {
	/*
	 * Read error status from the child if any.
	 */

        close(errpipe[1]);
        nread = readv(errpipe[0], iov, 2);
        close(errpipe[0]);
        if (nread != 0) {
            if (nread < 0) {
		Ns_Log(Error, "exec: %s: error reading status from child %d: %s",
		       exec, pid, strerror(errno));
            } else if (nread > 0) {
		switch (status) {
		case ERR_CHDIR:
                    Ns_Log(Error, "exec %s: chdir(%s) failed: %s", exec,
			dir, strerror(err));
		    break;
		case ERR_DUP_1:
		case ERR_DUP_0:
                    Ns_Log(Error, "exec %s: dup(%d) failed: %s", exec,
			(status == ERR_DUP_0 ? 0 : 1), strerror(err));
		    break;
		case ERR_DUP_IN:
		case ERR_DUP_OUT:
                    Ns_Log(Error, "exec %s: dup2(%d, %d) failed: %s", exec,
			(status == ERR_DUP_IN ? fdin : fdout),
			(status == ERR_DUP_IN ? 0 : 1), strerror(err));
		    break;
		case ERR_EXEC:
                    Ns_Log(Error, "exec %s: execve() failed: %s", exec,
			strerror(err));
		    break;
		default: 
                    Ns_Log(Error, "exec %s: invalid status code from child %d: %d",
			exec, pid, status);
		    break;
		}
            }

	    /*
	     * Reap the failed child now to avoid zombies.
	     */

            waitpid(pid, NULL, 0);
            pid = -1;
        }
    }
    Ns_DStringFree(&eds);
    Ns_DStringFree(&vds);
    return pid;
}


/*
 *----------------------------------------------------------------------
 * Args2Argv --
 *
 *      Build an argv vector from a sequence of character strings
 *
 * Results:
 *      Return a pointer to a argv vector stored in given dstring.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char **
Args2Argv(Ns_DString *dsPtr, char *arg)
{
    while (*arg != '\0') {
        Ns_DStringNAppend(dsPtr, (char *) &arg, sizeof(arg));
        arg += strlen(arg) + 1;
    }
    arg = NULL;
    Ns_DStringNAppend(dsPtr, (char *) &arg, sizeof(arg));
    return (char **) dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 * Set2Argv --
 *
 *      Convert an Ns_Set containing key-value pairs into a character
 *	array containing a sequence of name-value pairs with their 
 *	terminating null bytes.
 *
 * Results:
 *      Returns pointer to a character array containing a sequence of
 *	name-value pairs with their terminating null bytes.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char **
Set2Argv(Ns_DString *dsPtr, Ns_Set *env)
{
    int        i;

    for (i = 0; i < Ns_SetSize(env); ++i) {
        Ns_DStringVarAppend(dsPtr,
	    Ns_SetKey(env, i), "=", Ns_SetValue(env, i), NULL);
        Ns_DStringNAppend(dsPtr, "", 1);
    }
    Ns_DStringNAppend(dsPtr, "", 1);
    return Ns_DStringAppendArgv(dsPtr);
}

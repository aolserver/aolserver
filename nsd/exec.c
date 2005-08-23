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
 * exec.c --
 *
 *	Routines for creating and waiting for child processes.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/exec.c,v 1.21 2005/08/23 21:41:36 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

#ifdef _WIN32
#include <process.h>
static char   **Set2Argv(Ns_DString *dsPtr, Ns_Set *set);
static int  	WaitForProcess(int pid, int *statusPtr);
#else
#define ERR_DUP         (-1)
#define ERR_CHDIR	(-2)
#define ERR_EXEC	(-3)

static int ExecProc(char *exec, char *dir, int fdin, int fdout,
		    char **argv, char **envp);
#endif /* _WIN32 */


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
 *      Ruturn NS_OK for success and NS_ERROR for failure.
 *
 * Side effects:
 *      Sets exit code in exitcodePtr if given.
 *
 *----------------------------------------------------------------------
 */

int
Ns_WaitForProcess(int pid, int *exitcodePtr)
{
#ifdef _WIN32
    HANDLE process = (HANDLE) pid;
    int status = NS_OK;
    int exitcode;

    if ((WaitForSingleObject(process, INFINITE) == WAIT_FAILED) ||
        (GetExitCodeProcess(process, &exitcode) != TRUE)) {
        Ns_Log(Error, "exec: failed to get process exit code: %s",
               NsWin32ErrMsg(GetLastError()));
        status = NS_ERROR;
    }
    if (CloseHandle(process) != TRUE) {
        Ns_Log(Warning, "exec: failed to close handle for process %d: %s",
	       pid, NsWin32ErrMsg(GetLastError()));
        status = NS_ERROR;
    }
    if (status == NS_OK) {
        if (exitcodePtr != NULL) {
            *exitcodePtr = exitcode;
        }
        if (nsconf.exec.checkexit && exitcode != 0) {
            Ns_Log(Error, "exec: process %d exited with non-zero status: %d",
                   pid, exitcode);
            status = NS_ERROR;
        }
    }
    return status;

#else
    char *coredump;
    int exitcode, status;
    
    if (waitpid(pid, &status, 0) != pid) {
        Ns_Log(Error, "waitpid(%d) failed: %s", pid, strerror(errno));
	return NS_ERROR;
    }
    if (WIFSIGNALED(status)) {
    	coredump = "";
#ifdef WCOREDUMP
        if (WCOREDUMP(status)) {
	    coredump = " - core dumped";
	}
#endif
        Ns_Log(Error, "process %d killed with signal %d%s", pid,
	    WTERMSIG(status), coredump);
    } else if (!WIFEXITED(status)) {
    	Ns_Log(Error, "waitpid(%d): invalid status: %d", pid, status);
    } else {
    	exitcode = WEXITSTATUS(status);
    	if (exitcode != 0) {
            Ns_Log(Warning, "process %d exited with non-zero exit code: %d",
               pid, exitcode);
    	}
    	if (exitcodePtr != NULL) {
    	    *exitcodePtr = exitcode;
	}
    }
    return NS_OK;
#endif /* _WIN32 */
}


/*
 *----------------------------------------------------------------------
 * Ns_ExecArgblk --
 *
 *      Execute a command in a child process using a null
 *  	byte separated list of args.
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
#ifndef _WIN32
    int    pid;
    char **argv;
    Ns_DString vds;

    Ns_DStringInit(&vds);
    if (args == NULL) {
        argv = NULL;
    } else {
	while (*args != '\0') {
            Ns_DStringNAppend(&vds, (char *) &args, sizeof(args));
            args += strlen(args) + 1;
	}
	args = NULL;
	Ns_DStringNAppend(&vds, (char *) &args, sizeof(args));
	argv = (char **) vds.string;
    }
    pid = Ns_ExecArgv(exec, dir, fdin, fdout, argv, env);
    Ns_DStringFree(&vds);
    return pid;
#else
    STARTUPINFO     si;
    PROCESS_INFORMATION pi;
    HANDLE          hCurrentProcess;
    int             pid;
    Ns_DString      cds, xds, eds;
    char           *envp;
    OSVERSIONINFO   oinfo;
    char           *cmd;

    if (exec == NULL) {
        return -1;
    }
    oinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    if (GetVersionEx(&oinfo) == TRUE && oinfo.dwPlatformId != VER_PLATFORM_WIN32_NT) {
        cmd = "command.com";
    } else {
        cmd = "cmd.exe";
    }

    /*
     * Setup STARTUPINFO with stdin, stdout, and stderr.
     */
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = (HANDLE) _get_osfhandle(_fileno(stderr));
    hCurrentProcess = GetCurrentProcess();
    if (fdout < 0) {
        fdout = 1;
    }
    if (DuplicateHandle(hCurrentProcess, (HANDLE) _get_osfhandle(fdout), hCurrentProcess,
            &si.hStdOutput, 0, TRUE, DUPLICATE_SAME_ACCESS) != TRUE) {
        Ns_Log(Error, "exec: failed to duplicate handle: %s",
        NsWin32ErrMsg(GetLastError()));
        return -1;
    }
    if (fdin < 0) {
        fdin = 0;
    }
    if (DuplicateHandle(hCurrentProcess, (HANDLE) _get_osfhandle(fdin), hCurrentProcess,
            &si.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS) != TRUE) {
        Ns_Log(Error, "exec: failed to duplicate handle: %s",
        NsWin32ErrMsg(GetLastError()));
        (void) CloseHandle(si.hStdOutput);
        return -1;
    }

    /*
     * Setup the command line and environment block and create the new
     * subprocess.
     */

    Ns_DStringInit(&cds);
    Ns_DStringInit(&xds);
    Ns_DStringInit(&eds);
    if (args == NULL) {
        /* NB: exec specifies a complete cmd.exe command string. */
        Ns_DStringVarAppend(&cds, cmd, " /c ", exec, NULL);
        exec = NULL;
    } else {
        char           *s;

        s = args;
        while (*s != '\0') {
            int             len;

            len = strlen(s);
            Ns_DStringNAppend(&cds, s, len);
            s += len + 1;
            if (*s != '\0') {
                Ns_DStringNAppend(&cds, " ", 1);
            }
        }
    Ns_NormalizePath(&xds, exec);
    s = xds.string;
    while (*s != '\0') {
        if (*s == '/') {
            *s = '\\';
        }
        ++s;
    }
    exec = xds.string;
    }
    if (env == NULL) {
        envp = NULL;
    } else {
        Set2Argv(&eds, env);
        envp = eds.string;
    }
    if (CreateProcess(exec, cds.string, NULL, NULL, TRUE, 0, envp, dir, &si, &pi) != TRUE) {
        Ns_Log(Error, "exec: failed to create process: %s: %s",
        exec ? exec : cds.string, NsWin32ErrMsg(GetLastError()));
        pid = -1;
    } else {
        CloseHandle(pi.hThread);
        pid = (int) pi.hProcess;
    }
    Ns_DStringFree(&cds);
    Ns_DStringFree(&xds);
    Ns_DStringFree(&eds);
    CloseHandle(si.hStdInput);
    CloseHandle(si.hStdOutput);
    return pid;
#endif
}


/*
 *----------------------------------------------------------------------
 * Ns_ExecArgv --
 *
 *	Execute a program in a new child process.
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
#ifdef _WIN32
    /*
     * Win32 ExecArgv simply calls ExecArgblk.
     */
    int             pid;     
    Ns_DString      ads;
    char	   *args;
    int		    i;

    Ns_DStringInit(&ads);
    if (argv == NULL) {
        args = NULL;
    } else {
        for (i = 0; argv[i] != NULL; ++i) {
            Ns_DStringNAppend(&ads, argv[i], strlen(argv[i]) + 1);
        }
        args = ads.string;
    }
    pid = Ns_ExecArgblk(exec, dir, fdin, fdout, args, env);
    Ns_DStringFree(&ads);
    return pid;
#else
    Ns_DString eds;
    char *argvSh[4], **envp;
    int i, pid;
    
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
    Ns_DStringInit(&eds);
    if (env == NULL) {
	envp = Ns_CopyEnviron(&eds);
    } else {
	for (i = 0; i < Ns_SetSize(env); ++i) {
            Ns_DStringVarAppend(&eds,
		Ns_SetKey(env, i), "=", Ns_SetValue(env, i), NULL);
            Ns_DStringNAppend(&eds, "", 1);
	}
	Ns_DStringNAppend(&eds, "", 1);
	envp = Ns_DStringAppendArgv(&eds);
    }
    if (fdin < 0) {
	fdin = 0;
    }
    if (fdout < 0) {
	fdout = 1;
    }
    pid = ExecProc(exec, dir, fdin, fdout, argv, envp);
    Ns_DStringFree(&eds);
    return pid;
#endif
}

#ifndef _WIN32

/*
 *----------------------------------------------------------------------
 * ExecProc -- 
 *
 *	Execute a new process.  This code is careful to capture the
 *  	full error status from the child on failure.
 *
 * Results:
 *      Valid new child pid or -1 on error.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ExecProc(char *exec, char *dir, int fdin, int fdout, char **argv,
    	 char **envp)
{
    struct iovec iov[2];
    int    pid, nread, errpipe[2], errnum, result;

    /*
     * Create a pipe for child error message.
     */
     
    if (ns_pipe(errpipe) < 0) {
        Ns_Log(Error, "exec: ns_pipe() failed: %s", strerror(errno));
	return -1;
    }

    /*
     * Fork child and read error message (if any).
     */

    pid = ns_fork();
    if (pid < 0) {
        close(errpipe[0]);
        close(errpipe[1]);
        Ns_Log(Error, "exec: ns_fork() failed: %s", strerror(errno));
	return -1;
    }
    iov[0].iov_base = (caddr_t) &result;
    iov[1].iov_base = (caddr_t) &errnum;
    iov[0].iov_len = iov[1].iov_len = sizeof(int);
    if (pid == 0) {

	/*
	 * Setup child and exec the program, writing any error back
	 * to the parent if necessary.
	 */

        close(errpipe[0]);
        if (dir != NULL && chdir(dir) != 0) {
	    result = ERR_CHDIR;
        } else if ((fdin == 1 && (fdin = dup(1)) < 0) ||
    	    	    (fdout == 0 && (fdout = dup(0)) < 0) ||
	    	    (fdin != 0 && dup2(fdin, 0) < 0) ||
    	    	    (fdout != 1 && dup2(fdout, 1) < 0)) {
	    result = ERR_DUP;
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
	    /* NB: Not reached on successful execve(). */
	    result = ERR_EXEC;
	}
	errnum = errno;
	(void) writev(errpipe[1], iov, 2);
	_exit(1);
	
    } else {
    
	/*
	 * Read result and errno from the child if any.
	 */

        close(errpipe[1]);
	do {
            nread = readv(errpipe[0], iov, 2);
	} while (nread < 0 && errno == EINTR);
        close(errpipe[0]);
        if (nread == 0) {
	    errnum = 0;
	    result = pid;
	} else {
            if (nread != (sizeof(int) * 2)) {
	    	Ns_Log(Error, "exec: %s: error reading status from child: %s",
			   exec, strerror(errno));
            } else {
		switch (result) {
		    case ERR_CHDIR:
            		Ns_Log(Error, "exec %s: chdir(%s) failed: %s",
				exec, dir, strerror(errnum));
	    		break;
		    case ERR_DUP:
	    		Ns_Log(Error, "exec %s: dup failed: %s",
				exec, strerror(errnum));
	    		break;
		    case ERR_EXEC:
	    		Ns_Log(Error, "exec %s: execve() failed: %s",
				exec, strerror(errnum));
	    		break;
		    default:
	    		Ns_Log(Error, "exec %s: unknown result from child: %d",
				exec, result);
	    		break;
		}
	    }
            (void) waitpid(pid, NULL, 0);
	    errno = errnum;
        }
    }
    return result;
}

#else /* _WIN32 */



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

#endif /* _WIN32 */

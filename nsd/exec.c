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


static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/exec.c,v 1.2 2000/05/02 14:39:30 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

#ifdef WIN32
#include <process.h>
#else
static void     ExecFailed(int errPipe, char *errBuf, char *fmt, ...);
#endif
static char   **BuildVector(char *ptr);
static char    *GetEnvBlock(Ns_Set *env);
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
#ifdef WIN32
    HANDLE process;
#else
    int waitstatus;
#endif
    int status, exitcode;

#ifdef WIN32
    status = NS_OK;
    process = (HANDLE) pid;
    if ((WaitForSingleObject(process, INFINITE) == WAIT_FAILED) ||
        (GetExitCodeProcess(process, &exitcode) != TRUE)) {
        Ns_Log(Error, "Could not get process exit code:  %s", NsWin32ErrMsg(GetLastError()));
        status = NS_ERROR;
    }
    if (CloseHandle(process) != TRUE) {
        Ns_Log(Warning, "WaitForProcess: CloseHandle(%d) failed: %s", pid, NsWin32ErrMsg(GetLastError()));
        status = NS_ERROR;
    }
#else
    status = NS_ERROR;
waitproc:
    if (waitpid(pid, &waitstatus, 0) != pid) {
        Ns_Log(Error, "waitpid(%d) failed: %s", pid, strerror(errno));
    } else {
        if (WIFEXITED(waitstatus)) {
	    exitcode = WEXITSTATUS(waitstatus);
	    status = NS_OK;
        } else if (WIFSIGNALED(waitstatus)) {
            Ns_Log(Error, "process %d exited from signal: %d",
		      pid, WTERMSIG(waitstatus));
#ifdef WCOREDUMP
            if (WCOREDUMP(waitstatus)) {
                Ns_Log(Error, "process %d dumped core", pid);
            }
#endif /* WCOREDUMP */
        } else if (WIFSTOPPED(waitstatus)) {
            Ns_Log(Notice, "process %d stopped by signal: %d",
		      pid, WSTOPSIG(waitstatus));
            goto waitproc;
#ifdef WIFCONTINUED
        } else if (WIFCONTINUED(waitstatus)) {
            Ns_Log(Notice, "proces %d resumed", pid);
            goto waitproc;
#endif /* WIFCONTIUED */
        } else {
            Ns_Log(Bug, "waitpid(%d) returned invalid status:  %d",
		pid, waitstatus);
        }
    }
#endif
    if (status == NS_OK) {
    	if (statusPtr != NULL) {
	    *statusPtr = exitcode;
	}
	if (nsconf.exec.checkexit && exitcode != 0) {
            Ns_Log(Error, "process %d exited with non-zero status: %d",
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
Ns_ExecArgblk(char *exec, char *dir, int fdin, int fdout, char *args, Ns_Set *env)
{
#ifndef WIN32
    /*
     * Unix ExecArgblk simply calls ExecArgv.
     */
     
    int    pid;
    char **argv;

    if (args != NULL) {
        argv = BuildVector(args);
    } else {
        argv = NULL;
    }
    pid = Ns_ExecArgv(exec, dir, fdin, fdout, argv, env);
    if (args != NULL) {
        ns_free(argv);
    }
#else
    STARTUPINFO     si;
    PROCESS_INFORMATION pi;
    HANDLE          hCurrentProcess;
    int             pid;
    Ns_DString      dsCmd, dsExec;
    char           *envBlock;
    OSVERSIONINFO   oinfo;
    char           *cmd;

    if (exec == NULL) {
        Ns_Log(Bug, "Ns_ExecProcess() called with NULL command.");
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
        Ns_Log(Error, "DuplicateHande(StdOutput) failed CreateProcess():  %s",
	    NsWin32ErrMsg(GetLastError()));
        return -1;
    }
    if (fdin < 0) {
        fdin = 0;
    }
    if (DuplicateHandle(hCurrentProcess, (HANDLE) _get_osfhandle(fdin), hCurrentProcess,
            &si.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS) != TRUE) {
        Ns_Log(Error, "DuplicateHande(StdInput) failed before CreateProcess():  %s",
	    NsWin32ErrMsg(GetLastError()));
        (void) CloseHandle(si.hStdOutput);
        return -1;
    }

    /*
     * Setup the command line and environment block and create the new
     * subprocess.
     */

    Ns_DStringInit(&dsCmd);
    Ns_DStringInit(&dsExec);
    if (args == NULL) {
        /* NB: exec specifies a complete cmd.exe command string. */
        Ns_DStringVarAppend(&dsCmd, cmd, " /c ", exec, NULL);
        exec = NULL;
    } else {
        char           *s;

        s = args;
        while (*s != '\0') {
            int             len;

            len = strlen(s);
            Ns_DStringNAppend(&dsCmd, s, len);
            s += len + 1;
            if (*s != '\0') {
                Ns_DStringNAppend(&dsCmd, " ", 1);
            }
        }
	Ns_NormalizePath(&dsExec, exec);
	s = dsExec.string;
	while (*s != '\0') {
	    if (*s == '/') {
		*s = '\\';
	    }
	    ++s;
	}
	exec = dsExec.string;
    }
    if (env == NULL) {
        envBlock = NULL;
    } else {
        envBlock = GetEnvBlock(env);
    }
    if (CreateProcess(exec, dsCmd.string, NULL, NULL, TRUE, 0, envBlock, dir, &si, &pi) != TRUE) {
        Ns_Log(Error, "CreateProcess() failed to execute %s: %s",
            exec ? exec : dsCmd.string, NsWin32ErrMsg(GetLastError()));
        pid = -1;
    } else {
        CloseHandle(pi.hThread);
        pid = (int) pi.hProcess;
        Ns_Log(Debug, "Child process %d started", pid);
    }
    Ns_DStringFree(&dsCmd);
    Ns_DStringFree(&dsExec);
    if (envBlock != NULL) {
        ns_free(envBlock);
    }
    CloseHandle(si.hStdInput);
    CloseHandle(si.hStdOutput);
#endif
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
#ifdef WIN32
    /*
     * Win32 ExecArgv simply calls ExecArgblk.
     */
     
    Ns_DString      ds;
    char           *argblk;
    int		    i;

    Ns_DStringInit(&ds);
    if (argv == NULL) {
        argblk = NULL;
    } else {
        for (i = 0; argv[i] != NULL; ++i) {
            Ns_DStringNAppend(&ds, argv[i], strlen(argv[i]) + 1);
        }
        argblk = ds.string;
    }
    pid = Ns_ExecArgblk(exec, dir, fdin, fdout, argblk, env);
    Ns_DStringFree(&ds);
#else
    int    pipeError[2];
    int    nread;
    char   cBuf[200];
    char  *envBlock;
    char **envp;
    char  *argvSh[4];

    if (exec == NULL) {
        Ns_Log(Bug, "Ns_Exec(): NULL command.");
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
    if (pipe(pipeError) < 0) {
        Ns_Log(Error, "Ns_Exec(%s): pipe() failed: %s:  %s",
                  exec, strerror(errno));
        return -1;
    }
    if (env != NULL) {
        envBlock = GetEnvBlock(env);
        envp = BuildVector(envBlock);
    }
    if (fdin < 0) {
	fdin = 0;
    }
    if (fdout < 0) {
	fdout = 1;
    }
    pid = Ns_Fork();
    if (pid < 0) {
        Ns_Log(Error, "Ns_Exec(%s): fork() failed: %s", exec, strerror(errno));
        close(pipeError[0]);
        close(pipeError[1]);
    } else if (pid == 0) {	/* child */
        close(pipeError[0]);
        if (dir != NULL && chdir(dir) != 0) {
            ExecFailed(pipeError[1], cBuf, "%dchdir(\"%.150s\")", errno, dir);
        }
	if (fdin == 1) {
	    fdin = dup(1);
            if (fdin == -1) {
                ExecFailed(pipeError[1], cBuf, "%ddup(1)", errno);
            }
	}
	if (fdout == 0) {
	    fdout = dup(0);
            if (fdout == -1) {
                ExecFailed(pipeError[1], cBuf, "%ddup(0)", errno);
            }
	}
        if (fdin != 0) {
            if (dup2(fdin, 0) == -1) {
                ExecFailed(pipeError[1], cBuf, "%ddup2(%d, 0)", errno, fdin);
            }
	    if (fdin != fdout) {
		close(fdin);
	    }
        }
        if (fdout != 1) {
            if (dup2(fdout, 1) == -1) {
                ExecFailed(pipeError[1], cBuf, "%ddup2(%d, 1)", errno, fdout);
            }
            close(fdout);
        }
        NsRestoreSignals();
	Ns_NoCloseOnExec(0);
	Ns_NoCloseOnExec(1);
	Ns_NoCloseOnExec(2);
	Ns_CloseOnExec(pipeError[1]);
        if (env != NULL) {
            execve(exec, argv, envp);
        } else {
            execv(exec, argv);
        }
        ExecFailed(pipeError[1], cBuf, "%dexecv()", errno);

    } else {	/* parent */

        close(pipeError[1]);
        nread = read(pipeError[0], cBuf, sizeof(cBuf) - 1);
        close(pipeError[0]);
        if (nread != 0) {
            if (nread < 0) {
                Ns_Log(Error, "Ns_Exec(%s): error reading from process %d: %s",
                    exec, pid, strerror(errno));
            } else if (nread > 0) {
                char *msg;
		int   err;

                cBuf[nread] = '\0';
                err = strtol(cBuf, &msg, 10);
                Ns_Log(Error, "%s failed: %s; could not execute %s",
			  msg, strerror(err), exec);
            }
            waitpid(pid, NULL, 0);
            pid = -1;
        }
    }
    if (env != NULL) {
        ns_free(envp);
        ns_free(envBlock);
    }
#endif
    return pid;
}


/*
 *----------------------------------------------------------------------
 * BuildVector --
 *
 *      Build an argv vector from a sequence of character strings
 *
 * Results:
 *      Return a pointer to a argv vector.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static char **
BuildVector(char *ptr)
{
    Ns_DString      dsVec;

    /*
     * ptr points to a char array containing a sequence of character
     * strings with their terminating null bytes.  
     */

    Ns_DStringInit(&dsVec);
    while (*ptr != '\0') {
        Ns_DStringNAppend(&dsVec, (char *) &ptr, sizeof(ptr));
        ptr += strlen(ptr) + 1;
    }
    ptr = NULL;
    Ns_DStringNAppend(&dsVec, (char *) &ptr, sizeof(ptr));

    return (char **) Ns_DStringExport(&dsVec);
}


/*
 *----------------------------------------------------------------------
 * GetEnvBlock --
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

static char *
GetEnvBlock(Ns_Set *env)
{
    Ns_DString dsBuf;
    int        i;

    Ns_DStringInit(&dsBuf);
    for (i = 0; i < Ns_SetSize(env); ++i) {
        Ns_DStringVarAppend(&dsBuf, Ns_SetKey(env, i), "=",
			    Ns_SetValue(env, i), NULL);
        Ns_DStringNAppend(&dsBuf, "", 1);
    }
    Ns_DStringNAppend(&dsBuf, "", 1);

    return Ns_DStringExport(&dsBuf);
}


/*
 *----------------------------------------------------------------------
 * ExecFailed --
 *
 *      Write a formatted message why exec failed and then exit.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

#ifndef WIN32
static void
ExecFailed(int errPipe, char *errBuf, char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsprintf(errBuf, fmt, ap);
    va_end(ap);
    write(errPipe, errBuf, strlen(errBuf));

    _exit(1);
}
#endif

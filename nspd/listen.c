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
 * listen.c --
 *
 *	Listen for the external driver. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nspd/listen.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "pd.h"
#define DEV_NULL        "/dev/null"

#ifndef WCOREDUMP
#define WCOREDUMP(s)    (s & 0200)
#endif

/*
 * Local functions defined in this file
 */

static void     PdNewConn(int sock, int new);
static int      fdNull;

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * PdListen --
 *
 *	Listen on a port and then handshake with the external driver 
 *	when it connects. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	May exit on error; never returns.
 *
 *----------------------------------------------------------------------
 */

void
PdListen(int port)
{
    int                sock, new;
    struct sockaddr_in sa;
    int                n;

    fdNull = open(DEV_NULL, O_WRONLY);
    if (fdNull < 0) {
        Ns_FatalErrno("open");
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        Ns_FatalSock("Socket");
    }
    memset(&sa, 0, sizeof(struct sockaddr_in));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((unsigned short) port);
    n = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &n, sizeof(n));
    if (bind(sock, (struct sockaddr *) &sa, sizeof(struct sockaddr_in)) != 0) {
        Ns_FatalSock("bind");
    }
    if (listen(sock, 5) != 0) {
        Ns_FatalSock("listen");
    }
    if (port == 0) {
        fd_set          set;
        struct timeval  tv;
        char            buf[10];

        n = sizeof(sa);
        getsockname(sock, (struct sockaddr *) &sa, &n);
        sprintf(buf, "%d", ntohs(sa.sin_port));
        write(1, buf, strlen(buf) + 1);
        close(1);

        FD_ZERO(&set);
        FD_SET(sock, &set);
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        if (select(sock + 1, &set, NULL, NULL, &tv) != 1 ||
	    !FD_ISSET(sock, &set)) {
	    
            Ns_FatalSock("select");
        }
    }
    do {
        new = accept(sock, NULL, 0);
        if (new == INVALID_SOCKET) {
            Ns_FatalSock("accept");
        }
        PdNewConn(sock, new);
    } while (port != 0);
}


/*
 *----------------------------------------------------------------------
 *
 * PdNewConn --
 *
 *	Accept a new connection and run PdMainLoop on it. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Forks and exits.
 *
 *----------------------------------------------------------------------
 */

static void
PdNewConn(int sock, int new)
{
    int pid, status;

    pid = fork();
    if (pid < 0) {
        Ns_FatalErrno("fork");
    } else if (pid == 0) {
        pid = fork();
        if (pid < 0) {
            Ns_FatalErrno("fork");
        } else if (pid > 0) {
            exit(0);
        }
        close(sock);
        if (fdNull != 2) {
            if (dup2(fdNull, 2) == -1) {
                Ns_FatalErrno("dup2");
            }
            close(fdNull);
        }
        if (new != 0 && dup2(new, 0) == -1) {
            Ns_FatalErrno("dup2");
        }
        if (new != 1 && dup2(new, 1) == -1) {
            Ns_FatalErrno("dup2");
        }
        if (new > 2) {
            close(new);
        }
        PdMainLoop();
        Ns_PdExit(0);
    } else {
        close(new);
        if (waitpid(pid, &status, 0) != pid) {
            Ns_FatalErrno("waitpid");
        } else if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            Ns_PdLog(Error, "PdNewConn: "
		     "child pid %d returned non-zero status: %d",
		     pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            Ns_PdLog(Error, "PdNewConn: child pid %d exited from signal %d%s",
		     pid, WTERMSIG(status),
		     WCOREDUMP(status) ? " - core dumped" : "");
        }
    }
}

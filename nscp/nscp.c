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
 * nscp.c --
 *
 *	Simple control port module for AOLserver which allows
 *  	one to telnet to a specified port, login, and issue
 *  	Tcl commands.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nscp/nscp.c,v 1.6 2000/08/21 17:58:52 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

/* 
 * The following structure is used to pass arguments to
 * EvalThread from AcceptProc.
 */
 
typedef struct Arg {
    int id;
    SOCKET sock;
    struct sockaddr_in sa;
} Arg;

static Ns_ThreadProc EvalThread;
static Ns_SockProc AcceptProc;
static Tcl_CmdProc ExitCmd;
static int Login(SOCKET sock);
static int GetLine(SOCKET sock, char *prompt, Tcl_DString *dsPtr, int echo);
static char *server;
static Tcl_HashTable users;
static char *addr;
static int port;
static Ns_ArgProc ArgProc;

NS_EXPORT int Ns_ModuleVersion = 1;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Load the config parameters, setup the structures, and
 *	listen on the control port.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Server will listen for control connections on specified
 *  	address and port.
 *
 *----------------------------------------------------------------------
 */
 
NS_EXPORT int
Ns_ModuleInit(char *s, char *module)
{
    char *path, *pass, *user, *key, *end;
    int i, new;
    SOCKET lsock;
    Ns_Set *set;
    Tcl_HashEntry *hPtr;

    /* 
     * Create the listening socket and callback.
     */

    server = s;
    path = Ns_ConfigGetPath(server, module, NULL);
    addr = Ns_ConfigGet(path, "address");
    if (addr == NULL) {
	addr = "127.0.0.1";
    }
    if (!Ns_ConfigGetInt(path, "port", &port)) {
	port = 9999;
    }
    lsock = Ns_SockListen(addr, port);
    if (lsock == INVALID_SOCKET) {
	Ns_Log(Error, "nscp: could not listen on %s:%d", addr, port);
	return NS_ERROR;
    }
    Ns_Log(Notice, "nscp: listening on %s:%d", addr, port);
    Ns_RegisterProcInfo(AcceptProc, "nscp", ArgProc);
    Ns_SockCallback(lsock, AcceptProc, NULL, NS_SOCK_READ|NS_SOCK_EXIT);

    /*
     * Initialize the hash table of authorized users.  Entry values
     * are either NULL indicating authorization should be checked
     * via the Ns_AuthorizeUser() API or contain a Unix crypt(3)
     * sytle encrypted password.  For the later, the entry is
     * compatible with /etc/passwd (i.e., username followed by
     * password separated by colons).
     */

    Tcl_InitHashTable(&users, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "users", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	pass = NULL;
	key = Ns_SetKey(set, i);
	user = Ns_SetValue(set, i);
	if (STRIEQ(key, "user")) {
	    pass = strchr(user, ':');
	    if (pass == NULL) {
	    	Ns_Log(Error, "nscp: invalid user entry: %s", user);
		continue;
	    }
	} else if (!STRIEQ(key, "permuser")) {
	    Ns_Log(Error, "nscp: invalid user key: %s", key);
	    continue;
	}
	if (pass != NULL) {
	    *pass = '\0';
	}
	hPtr = Tcl_CreateHashEntry(&users, user, &new);
	Ns_Log(Notice, "nscp: added user: %s", user);
	if (pass != NULL) {
	    *pass++ = ':';
	    end = strchr(pass, ':');
	    if (end != NULL) {
		*end = '\0';
	    }
	    pass = ns_strdup(pass);
	    if (end != NULL) {
		*end = ':';
	    }
	}
	Tcl_SetHashValue(hPtr, pass);
    }
    if (users.numEntries == 0) {
	Ns_Log(Warning, "nscp: no authorized users");
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ArgProc --
 *
 *	Append listen port info for query callback.
 *
 * Results:
 *	None
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static void
ArgProc(Tcl_DString *dsPtr, void *arg)
{
    char buf[20];

    sprintf(buf, "%d", port);
    Tcl_DStringStartSublist(dsPtr);
    Tcl_DStringAppendElement(dsPtr, addr);
    Tcl_DStringAppendElement(dsPtr, buf);
    Tcl_DStringEndSublist(dsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * AcceptProc --
 *
 *	Socket callback to accept a new connection.
 *
 * Results:
 *	NS_TRUE to keep listening unless shutdown is in progress.
 *
 * Side effects:
 *  	New EvalThread will be created.
 *
 *----------------------------------------------------------------------
 */

static int
AcceptProc(SOCKET lsock, void *ignored, int why)
{
    Arg *aPtr;
    int len;
    static int next;

    if (why == NS_SOCK_EXIT) {
	Ns_Log(Notice, "nscp: shutdown");
	ns_sockclose(lsock);
	return NS_FALSE;
    }
    aPtr = ns_malloc(sizeof(Arg));
    len = sizeof(struct sockaddr_in);
    aPtr->sock = Ns_SockAccept(lsock, (struct sockaddr *) &aPtr->sa, &len);
    if (aPtr->sock == INVALID_SOCKET) {
	Ns_Log(Error, "nscp: accept() failed: %s",
	       ns_sockstrerror(ns_sockerrno));
	ns_free(aPtr);
    } else {
	aPtr->id = ++next;
	Ns_ThreadCreate(EvalThread, (void *) aPtr, 0, NULL);
    }
    return NS_TRUE;
}


/*
 *----------------------------------------------------------------------
 *
 * EvalThread --
 *
 *	Thread to read and evaluate commands from remote.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	Depends on commands.
 *
 *----------------------------------------------------------------------
 */

static void
EvalThread(void *arg)
{
    Tcl_Interp *interp;
    Tcl_DString ds;
    char buf[30], *res;
    int n, len, ncmd, stop;
    Arg *aPtr = (Arg *) arg;

    /*
     * Initialize the thread and login the user.
     */
     
    Tcl_DStringInit(&ds);
    sprintf(buf, "-nscp:%d-", aPtr->id);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "nscp: connect: %s", ns_inet_ntoa(aPtr->sa.sin_addr));
    if (!Login(aPtr->sock)) {
	goto done;
    }

    /*
     * Loop until the remote shuts down, evaluating complete
     * commands.
     */

    interp = Ns_TclAllocateInterp(server);

    /*
     * Create a special exit command for this interp only.
     */

    stop = 0;
    Tcl_CreateCommand(interp, "exit", ExitCmd, (ClientData) &stop, NULL);

    ncmd = 0;
    while (!stop) {
	Tcl_DStringTrunc(&ds, 0);
	++ncmd;
retry:
	sprintf(buf, "nscp %d> ", ncmd);
	while (1) {
	    if (!GetLine(aPtr->sock, buf, &ds, 1)) {
		goto done;
	    }
	    if (Tcl_CommandComplete(ds.string)) {
		break;
	    }
	    sprintf(buf, "nscp %d>>> ", ncmd);
	}
	while (ds.length > 0 && ds.string[ds.length-1] == '\n') {
	    Tcl_DStringTrunc(&ds, ds.length-1);
	}
	if (STREQ(ds.string, "")) {
	    goto retry; /* Empty command - try again. */
	}
	if (Tcl_Eval(interp, ds.string) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
	Tcl_AppendResult(interp, "\r\n", NULL);
	res = interp->result;
	len = strlen(res);
	while (len > 0) {
	    if ((n = send(aPtr->sock, res, len, 0)) <= 0) goto done;
	    len -= n;
	    res += n;
	}
    }
done:
    Tcl_DStringFree(&ds);
    Ns_TclDeAllocateInterp(interp);
    Ns_Log(Notice, "nscp: disconnect: %s", ns_inet_ntoa(aPtr->sa.sin_addr));
    ns_sockclose(aPtr->sock);
    ns_free(aPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * GetLine --
 *
 *	Prompt for a line of input from the remote.  \r\n sequences
 *  	are translated to \n.
 *
 * Results:
 *  	1 if line received, 0 if remote dropped.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetLine(SOCKET sock, char *prompt, Tcl_DString *dsPtr, int echo)
{

#ifndef WIN32

#define TN_IAC  255
#define TN_WILL 251
#define TN_WONT 252
#define TN_DO   253
#define TN_DONT 254

#define TN_ECHO   1

#define TN_EOF  236
#define TN_IP   244

    char do_echo[]    = {TN_IAC, TN_DO,   TN_ECHO};
    char dont_echo[]  = {TN_IAC, TN_DONT, TN_ECHO};
    char will_echo[]  = {TN_IAC, TN_WILL, TN_ECHO};
    char wont_echo[]  = {TN_IAC, TN_WONT, TN_ECHO};
#endif

    unsigned char buf[2048];
    int n;
    int result = 0;

#ifndef WIN32
    /*
     * Suppress output on things like password prompts.
     */
    if (!echo) {
	send(sock, will_echo, 3, 0);
	send(sock, dont_echo, 3, 0);
	recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
#endif

    n = strlen(prompt);
    if (send(sock, prompt, n, 0) != n) {
	result = 0;
	goto bail;
    }

    do {
	if ((n = recv(sock, buf, sizeof(buf), 0)) <= 0) {
	    result = 0;
	    goto bail;
	}
	if (n > 1 && buf[n-1] == '\n' && buf[n-2] == '\r') {
	    buf[n-2] = '\n';
	    --n;
	}

	/*
	 * This EOT checker cannot happen in the context of telnet.
	 */
	if (n == 1 && buf[0] == 4) {
	    result = 0;
	    goto bail;
	}
	
#ifndef WIN32
	/*
	 * Deal with telnet IAC commands in some sane way.
	 */
	if (n > 1 && buf[0] == TN_IAC) {
	    if ( buf[1] == TN_EOF) {
		result = 0;
		goto bail;
	    } else if ( buf[1] == TN_IP) {
		result = 0;
		goto bail;
	    } else {
		Ns_Log(Warning, "nscp: "
		       "unsupported telnet IAC code received from client");
		result = 0;
		goto bail;
	    }
	}
#endif

	
	Tcl_DStringAppend(dsPtr, buf, n);
	result = 1;

    } while (buf[n-1] != '\n');

 bail:

#ifndef WIN32
    if (!echo) {
	send(sock, wont_echo, 3, 0);
	send(sock, do_echo, 3, 0);
	recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
#endif

    return result;
}


/*
 *----------------------------------------------------------------------
 *
 * Login --
 *
 *	Attempt to login the user.
 *
 * Results:
 *  	1 if login ok, 0 otherwise.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
Login(SOCKET sock)
{
    Tcl_HashEntry *hPtr;
    Tcl_DString uds, pds;
    char *encpass, *user, *pass, *msg, buf[30];
    int ok;

    user = NULL;
    ok = 0;
    Tcl_DStringInit(&uds);
    Tcl_DStringInit(&pds);
    if (GetLine(sock, "login: ", &uds, 1) &&
	GetLine(sock, "password: ", &pds, 0)) {
	user = Ns_StrTrim(uds.string);
	pass = Ns_StrTrim(pds.string);
    	hPtr = Tcl_FindHashEntry(&users, user);
	if (hPtr != NULL) {
    	    encpass = Tcl_GetHashValue(hPtr);
    	    if (encpass == NULL) {
	    	if (Ns_AuthorizeUser(user, pass) == NS_OK) {
	    	    ok = 1;
		}
	    } else {
	    	Ns_Encrypt(pass, encpass, buf);
    	    	if (STREQ(buf, encpass)) {
		    ok = 1;
		}
	    }
	}
    }
    if (ok) {
	Ns_Log(Notice, "nscp: logged in: %s", user);
	msg = "\nWelcome to AOLserver\n";
    } else {
	Ns_Log(Warning, "nscp: login failed: %s", user ? user : "?");
	msg = "\nAccess denied\n";
    }
    (void) send(sock, msg, strlen(msg), 0);
    Tcl_DStringFree(&uds);
    Tcl_DStringFree(&pds);
    return ok;
}


/*
 *----------------------------------------------------------------------
 *
 * ExitCmd --
 *
 *	Special exit command for nscp.
 *
 * Results:
 *  	Standard Tcl result.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static int
ExitCmd(ClientData arg, Tcl_Interp *interp, int argc, char **argv)
{
    int *stopPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], "\"", NULL);
	return TCL_ERROR;
    }

    stopPtr = (int *) arg;
    *stopPtr = 1;
    Tcl_SetResult(interp, "\nGoodbye\n", TCL_STATIC);
    return TCL_OK;
}

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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nscp/nscp.c,v 1.25 2008/06/20 08:06:32 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

/*
 * The following structure is allocated each instance of
 * the loaded module.
 */

typedef struct Mod {
    Tcl_HashTable users;
    char *server;
    char *addr;
    int port;
    int echo;
    int commandLogging;
} Mod;

static Ns_ThreadProc EvalThread;

/* 
 * The following structure is allocated for each session.
 */
 
typedef struct Sess {
    Mod *modPtr;
    char *user;
    int id;
    SOCKET sock;
    struct sockaddr_in sa;
} Sess;

static Ns_SockProc AcceptProc;
static Tcl_CmdProc ExitCmd;
static int Login(Sess *sessPtr, Tcl_DString *unameDS);
static int GetLine(SOCKET sock, char *prompt, Tcl_DString *dsPtr, int echo);
static Ns_ArgProc ArgProc;

/*
 * The following values are sent to the telnet client to enable
 * and disable password prompt echo.
 */

#define TN_IAC  255
#define TN_WILL 251
#define TN_WONT 252
#define TN_DO   253
#define TN_DONT 254
#define TN_EOF  236
#define TN_IP   244
#define TN_ECHO   1

static unsigned char do_echo[]    = {TN_IAC, TN_DO,   TN_ECHO};
static unsigned char dont_echo[]  = {TN_IAC, TN_DONT, TN_ECHO};
static unsigned char will_echo[]  = {TN_IAC, TN_WILL, TN_ECHO};
static unsigned char wont_echo[]  = {TN_IAC, TN_WONT, TN_ECHO};


/*
 *----------------------------------------------------------------------
 *
 * NsCp_ModInit --
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
 
int
NsCp_ModInit(char *server, char *module)
{
    Mod *modPtr;
    char *path, *addr, *pass, *user, *key, *end;
    int i, new, port;
    SOCKET lsock;
    Ns_Set *set;
    Tcl_HashEntry *hPtr;

    /* 
     * Create the listening socket and callback.
     */

    path = Ns_ConfigGetPath(server, module, NULL);
    if (((addr = Ns_ConfigGetValue(path, "address")) == NULL)
	 || (!Ns_ConfigGetInt(path, "port", &port)) )  {
	Ns_Log(Error, "nscp: address and port must be specified in config");
	return NS_ERROR;
    }
    lsock = Ns_SockListen(addr, port);
    if (lsock == INVALID_SOCKET) {
	Ns_Log(Error, "nscp: could not listen on %s:%d", addr, port);
	return NS_ERROR;
    }
    Ns_Log(Notice, "nscp: listening on %s:%d", addr, port);

    /*
     * Create a new Mod structure for this instance.
     */

    modPtr = ns_malloc(sizeof(Mod));
    modPtr->server = server;
    modPtr->addr = addr;
    modPtr->port = port;
    if (!Ns_ConfigGetBool(path, "echopassword", &modPtr->echo)) {
    	modPtr->echo = 1;
    }

    if (!Ns_ConfigGetBool(path, "cpcmdlogging", &modPtr->commandLogging)) {
        modPtr->commandLogging = 0; /* Default to off */
    }

    /*
     * Initialize the hash table of authorized users.  Entry values
     * are either NULL indicating authorization should be checked
     * via the Ns_AuthorizeUser() API or contain a Unix crypt(3)
     * sytle encrypted password.  For the later, the entry is
     * compatible with /etc/passwd (i.e., username followed by
     * password separated by colons).
     */

    Tcl_InitHashTable(&modPtr->users, TCL_STRING_KEYS);
    path = Ns_ConfigGetPath(server, module, "users", NULL);
    set = Ns_ConfigGetSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
	key = Ns_SetKey(set, i);
	user = Ns_SetValue(set, i);
	if (!STRIEQ(key, "user") || (pass = strchr(user, ':')) == NULL) {
	    continue;
	}
	*pass = '\0';
	hPtr = Tcl_CreateHashEntry(&modPtr->users, user, &new);
	if (new) {
	    Ns_Log(Notice, "nscp: added user: %s", user);
	} else {
	    Ns_Log(Warning, "nscp: duplicate user: %s", user);
	    ns_free(Tcl_GetHashValue(hPtr));
	}
	*pass++ = ':';
	end = strchr(pass, ':');
	if (end != NULL) {
	    *end = '\0';
	}
	pass = ns_strdup(pass);
	if (end != NULL) {
	    *end = ':';
	}
	Tcl_SetHashValue(hPtr, pass);
    }
    if (modPtr->users.numEntries == 0) {
	Ns_Log(Warning, "nscp: no authorized users");
    }
    Ns_SockCallback(lsock, AcceptProc, modPtr, NS_SOCK_READ|NS_SOCK_EXIT);
    Ns_RegisterProcInfo((void *)AcceptProc, "nscp", ArgProc);

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
    Mod *modPtr = arg;
    char buf[20];

    sprintf(buf, "%d", modPtr->port);
    Tcl_DStringStartSublist(dsPtr);
    Tcl_DStringAppendElement(dsPtr, modPtr->addr);
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
AcceptProc(SOCKET lsock, void *arg, int why)
{
    Mod *modPtr = arg;
    Sess *sessPtr;
    int len;
    static int next;

    if (why == NS_SOCK_EXIT) {
	Ns_Log(Notice, "nscp: shutdown");
	ns_sockclose(lsock);
	return NS_FALSE;
    }
    sessPtr = ns_malloc(sizeof(Sess));
    sessPtr->modPtr = modPtr;
    len = sizeof(struct sockaddr_in);
    sessPtr->sock = Ns_SockAccept(lsock, (struct sockaddr *) &sessPtr->sa, &len);
    if (sessPtr->sock == INVALID_SOCKET) {
	Ns_Log(Error, "nscp: accept() failed: %s",
	       ns_sockstrerror(ns_sockerrno));
	ns_free(sessPtr);
    } else {
	sessPtr->id = ++next;
	Ns_ThreadCreate(EvalThread, sessPtr, 0, NULL);
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
    Tcl_DString unameDS;
    char buf[64], *res;
    int n, len, ncmd, stop;
    Sess *sessPtr = arg;
    char *server = sessPtr->modPtr->server;

    /*
     * Initialize the thread and login the user.
     */
     
    interp = NULL;
    Tcl_DStringInit(&ds);
    Tcl_DStringInit(&unameDS);
    sprintf(buf, "-nscp:%d-", sessPtr->id);
    Ns_ThreadSetName(buf);
    Ns_Log(Notice, "nscp: %s connected", ns_inet_ntoa(sessPtr->sa.sin_addr));
    if (!Login(sessPtr, &unameDS)) {
	goto done;
    }

    sessPtr->user = Tcl_DStringValue(&unameDS);

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
	sprintf(buf, "%s:nscp %d> ", server, ncmd);
	while (1) {
	    if (!GetLine(sessPtr->sock, buf, &ds, 1)) {
		goto done;
	    }
	    if (Tcl_CommandComplete(ds.string)) {
		break;
	    }
	    sprintf(buf, "%s:nscp %d>>> ", server, ncmd);
	}
	while (ds.length > 0 && ds.string[ds.length-1] == '\n') {
	    Tcl_DStringTrunc(&ds, ds.length-1);
	}
	if (STREQ(ds.string, "")) {
	    goto retry; /* Empty command - try again. */
	}

        if (sessPtr->modPtr->commandLogging) {
            Ns_Log(Notice, "nscp: %s %d: %s", sessPtr->user, ncmd, ds.string);
        }

	if (Tcl_RecordAndEval(interp, ds.string, 0) != TCL_OK) {
	    Ns_TclLogError(interp);
	}
	Tcl_AppendResult(interp, "\r\n", NULL);
        res = Tcl_GetStringResult(interp); 
	len = strlen(res);
	while (len > 0) {
	    if ((n = send(sessPtr->sock, res, len, 0)) <= 0) goto done;
	    len -= n;
	    res += n;
	}

        if (sessPtr->modPtr->commandLogging) {
            Ns_Log(Notice, "nscp: %s %d: done", sessPtr->user, ncmd);
        }
    }
done:
    Tcl_DStringFree(&ds);
    Tcl_DStringFree(&unameDS);
    if (interp != NULL) {
    	Ns_TclDeAllocateInterp(interp);
    }
    Ns_Log(Notice, "nscp: %s disconnected", ns_inet_ntoa(sessPtr->sa.sin_addr));
    ns_sockclose(sessPtr->sock);
    ns_free(sessPtr);
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
    unsigned char buf[2048];
    int n;
    int result = 0;
    int retry = 0;

    /*
     * Suppress output on things like password prompts.
     */

    if (!echo) {
	send(sock, will_echo, 3, 0);
	send(sock, dont_echo, 3, 0);
	recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
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
	
	/*
	 * Deal with telnet IAC commands in some sane way.
	 */

	if (n > 1 && buf[0] == TN_IAC) {
	    if ( buf[1] == TN_EOF) {
		result = 0;
		goto bail;
	    } else if (buf[1] == TN_IP) {
		result = 0;
		goto bail;
            } else if ((buf[1] == TN_WONT) && (retry < 2)) {
                /*
                 * It seems like the flush at the bottom of this func
                 * does not always get all the acks, thus an echo ack
                 * showing up here. Not clear why this would be.  Need
                 * to investigate further. For now, breeze past these
                 * (within limits).
                 */
                retry++;
                continue;
	    } else {
		Ns_Log(Warning, "nscp: "
		       "unsupported telnet IAC code received from client");
		result = 0;
		goto bail;
	    }
	}

	Tcl_DStringAppend(dsPtr, buf, n);
	result = 1;

    } while (buf[n-1] != '\n');

 bail:
    if (!echo) {
	send(sock, wont_echo, 3, 0);
	send(sock, do_echo, 3, 0);
	recv(sock, buf, sizeof(buf), 0); /* flush client ack thingies */
    }
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
 *  	Stores user's login name into unameDSPtr.
 *
 *----------------------------------------------------------------------
 */

static int
Login(Sess *sessPtr, Tcl_DString *unameDSPtr)
{
    Tcl_HashEntry *hPtr;
    Tcl_DString uds, pds;
    char *encpass, *user, *pass, msg[255], buf[30];
    int ok;

    user = NULL;
    ok = 0;
    Tcl_DStringInit(&uds);
    Tcl_DStringInit(&pds);
    if (GetLine(sessPtr->sock, "login: ", &uds, 1) &&
	GetLine(sessPtr->sock, "Password: ", &pds, sessPtr->modPtr->echo)) {
	user = Ns_StrTrim(uds.string);
	pass = Ns_StrTrim(pds.string);
    	hPtr = Tcl_FindHashEntry(&sessPtr->modPtr->users, user);
	if (hPtr != NULL) {
    	    encpass = Tcl_GetHashValue(hPtr);
	    Ns_Encrypt(pass, encpass, buf);
    	    if (STREQ(buf, encpass)) {
		ok = 1;
	    }
	}
    }
    if (ok) {
	Ns_Log(Notice, "nscp: %s logged in", user);
        Tcl_DStringAppend(unameDSPtr, user, -1);
	sprintf(msg, "\nWelcome to %s running at %s (pid %d)\n"
		"%s/%s (%s) for %s built on %s\nCVS Tag: %s\n",
		sessPtr->modPtr->server,
		Ns_InfoNameOfExecutable(), Ns_InfoPid(),
		Ns_InfoServerName(), Ns_InfoServerVersion(), Ns_InfoLabel(),
		Ns_InfoPlatform(), Ns_InfoBuildDate(), Ns_InfoTag());
    } else {
	Ns_Log(Warning, "nscp: login failed: '%s'", user ? user : "?");
	sprintf(msg, "Access denied!\n");
    }
    (void) send(sessPtr->sock, msg, (int)strlen(msg), 0);
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
ExitCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    int *stopPtr;

    if (argc != 1) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    (char*)argv[0], "\"", NULL);
	return TCL_ERROR;
    }

    stopPtr = (int *) arg;
    *stopPtr = 1;
    Tcl_SetResult(interp, "\nGoodbye!", TCL_STATIC);
    return TCL_OK;
}

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
 * nsperm --
 *
 *	Permissions
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsperm/nsperm.c,v 1.11 2008/05/10 19:31:32 mooooooo Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

#ifndef INADDR_NONE
#define INADDR_NONE (-1)
#endif

/*
 * The following structure is allocated for each instance of the module.
 */

typedef struct Server {
    char	  *server;
    Tcl_HashTable  users;
    Tcl_HashTable  groups;
    Ns_RWLock	   lock;
} Server;

/*
 * The "users" hash table points to this kind of data:
 */

typedef struct {
    char    pass[16];
    Tcl_HashTable groups;
    Tcl_HashTable nets;
    Tcl_HashTable masks;
    Tcl_HashTable hosts;
    int   filterallow;
} User;

/*
 * The "groups" hash table points to this kind of data:
 */

typedef struct {
    Tcl_HashTable  users;
} Group;

/*
 * The urlspecific data referenced by uskey hold pointers to these:
 */

typedef struct {
    char         *baseurl;
    Tcl_HashTable allowuser;
    Tcl_HashTable denyuser;
    Tcl_HashTable allowgroup;
    Tcl_HashTable denygroup;
    int           implicit_allow;
} Perm;

/*
 * Local functions defined in this file
 */

static Tcl_CmdProc PermCmd;
static int AddCmds(Tcl_Interp *interp, void *arg);
static int AddUserCmd(Server *servPtr, Tcl_Interp *interp,
    int argc, char **argv);
static int AddGroupCmd(Server *servPtr, Tcl_Interp *interp,
    int argc, char **argv);
static int AllowDenyCmd(Server *servPtr, Tcl_Interp *interp,
    int argc, char **argv, int allow, int user);

static int ValidateUserAddr(User *userPtr, char *peer);
static int AuthProc(char *server, char *method, char *url, char *user,
		    char *pass, char *peer);

/*
 * Static variables defined in this file.
 */

static int             uskey = -1;
static Tcl_HashTable   serversTable;


/*
 *----------------------------------------------------------------------
 *
 * NsPerm_ModInit --
 *
 *	Initialize the perms module
 *
 * Results:
 *	NS_OK/NS_ERROR
 *
 * Side effects:
 *	Init hash table, add tcl commands.
 *
 *----------------------------------------------------------------------
 */

int
NsPerm_ModInit(char *server, char *module)
{
    Server *servPtr;
    char *path;
    Tcl_HashEntry *hPtr;
    int new;

    if (uskey < 0) {
    	uskey = Ns_UrlSpecificAlloc();
	Tcl_InitHashTable(&serversTable, TCL_STRING_KEYS);
    }
    servPtr = ns_malloc(sizeof(Server));
    servPtr->server = server;
    path = Ns_ConfigGetPath(server, module, NULL);
    Tcl_InitHashTable(&servPtr->users, TCL_STRING_KEYS);
    Tcl_InitHashTable(&servPtr->groups, TCL_STRING_KEYS);
    Ns_RWLockInit(&servPtr->lock);
    Ns_SetRequestAuthorizeProc(server, AuthProc);
    Ns_TclInitInterps(server, AddCmds, servPtr);
    hPtr = Tcl_CreateHashEntry(&serversTable, server, &new);
    Tcl_SetHashValue(hPtr, servPtr);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AddCmds --
 *
 *	Add tcl commands for perms
 *
 * Results:
 *	NS_OK
 *
 * Side effects:
 *	Adds tcl commands
 *
 *----------------------------------------------------------------------
 */

static int
AddCmds(Tcl_Interp *interpermPtr, void *arg)
{
    Tcl_CreateCommand(interpermPtr, "ns_perm", PermCmd, arg, NULL);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * PermCmd --
 *
 *	The ns_perm tcl command
 *
 * Results:
 *	Std tcl ret val
 *
 * Side effects:
 *	Yes.
 *
 *----------------------------------------------------------------------
 */

static int
PermCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    Server *servPtr = arg;
    int status;

    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " command ?args ...?\"", NULL);
	return TCL_ERROR;
    }
    Ns_RWLockWrLock(&servPtr->lock);
    if (STREQ(argv[1], "adduser")) {
	status = AddUserCmd(servPtr, interp, argc, (char**)argv);
    } else if (STREQ(argv[1], "addgroup")) {
	status = AddGroupCmd(servPtr, interp, argc, (char**)argv);
    } else if (STREQ(argv[1], "allowuser")) {
	status = AllowDenyCmd(servPtr, interp, argc, (char**)argv, 1, 1);
    } else if (STREQ(argv[1], "denyuser")) {
	status = AllowDenyCmd(servPtr, interp, argc, (char**)argv, 0, 1);
    } else if (STREQ(argv[1], "allowgroup")) {
	status = AllowDenyCmd(servPtr, interp, argc, (char**)argv, 1, 0);
    } else if (STREQ(argv[1], "denygroup")) {
	status = AllowDenyCmd(servPtr, interp, argc, (char**)argv, 0, 0);
    } else {
	Tcl_AppendResult(interp, "unknown command \"",
			 argv[1],
			 "\": should be adduser, addgroup, ",
			 "allowuser, denyuser, "
			 "allowgroup, or denygroup", NULL);
	status = TCL_ERROR;
    }
    Ns_RWLockUnlock(&servPtr->lock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * AuthProc --
 *
 *	Authorize a URL--this callback is called when a new
 *	connection is recieved
 *
 * Results:
 *	NS_OK: accept;
 *	NS_FORBIDDEN or NS_UNAUTHORIZED: go away;
 *	NS_ERROR: oops
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int
AuthProc(char *server, char *method, char *url, char *user, char *pass,
	 char *peer)
{
    Server	  *servPtr;
    Perm          *permPtr;
    User          *userPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    int status;
    char *group, buf[16];

    if (user == NULL) {
	user = "";
    }
    if (pass == NULL) {
	pass = "";
    }
    hPtr = Tcl_FindHashEntry(&serversTable, server);
    if (hPtr == NULL) {
	return NS_FORBIDDEN;
    }
    servPtr = Tcl_GetHashValue(hPtr);

    Ns_RWLockRdLock(&servPtr->lock);
    permPtr = Ns_UrlSpecificGet(server, method, url, uskey);
    if (permPtr == NULL) {
    	status = NS_OK;
	goto done;
    }

    /*
     * The first checks below deny access.
     */

    status = NS_UNAUTHORIZED;

    /*
     * Verify user password (if any).
     */

    hPtr = Tcl_FindHashEntry(&servPtr->users, user);
    if (hPtr == NULL) {
    	goto done;
    }
    userPtr = Tcl_GetHashValue(hPtr);
    if (userPtr->pass[0] != 0) {
    	if (pass[0] == 0) {
	    goto done;
	}
	Ns_Encrypt(pass, userPtr->pass, buf);
	if (!STREQ(userPtr->pass, buf)) {
    	    goto done;
	}
    }

    /*
     * Check for a vaild user address.
     */

    if (!ValidateUserAddr(userPtr, peer)) {
	/*
	 * Null user never gets forbidden--give a chance to enter password.
	 */
deny:
	if (*user != '\0') {
	    status = NS_FORBIDDEN;
	}
	goto done;
    }

    /*
     * Check user deny list.
     */

    if (Tcl_FindHashEntry(&permPtr->denyuser, user) != NULL) {
	goto deny;
    }

    /*
     * Loop over all groups in this perm record, and then
     * see if the user is in any of those groups.
     */

    hPtr = Tcl_FirstHashEntry(&permPtr->denygroup, &search);
    while (hPtr != NULL) {
	group = Tcl_GetHashKey(&permPtr->denygroup, hPtr);
	if (Tcl_FindHashEntry(&userPtr->groups, group) != NULL) {
	    goto deny;
	}
	hPtr = Tcl_NextHashEntry(&search);
    }

    /*
     * Valid checks below allow access.
     */

    status = NS_OK;

    /*
     * Check the allow lists, starting with users
     */

    if (Tcl_FindHashEntry(&permPtr->allowuser, user) != NULL) {
	goto done;
    }

    /*
     * Loop over all groups in this perm record, and then
     * see if the user is in any of those groups.
     */

    hPtr = Tcl_FirstHashEntry(&permPtr->allowgroup, &search);
    while (hPtr != NULL) {
	group = Tcl_GetHashKey(&permPtr->allowgroup, hPtr);
	if (Tcl_FindHashEntry(&userPtr->groups, group) != NULL) {
	    goto done;
	}
	hPtr = Tcl_NextHashEntry(&search);
    }

    /*
     * Checks above failed.  If implicit allow is not set,
     * change the status back to unauthorized.
     */

    if (!permPtr->implicit_allow) {
	status = NS_UNAUTHORIZED;
    }

done:
    Ns_RWLockUnlock(&servPtr->lock);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ValidateUserAddr --
 *
 *	Validate that the peer address is valid for this user
 *
 * Results:
 *	NS_TRUE if allowed, NS_FALSE if not
 *
 * Side effects:
 *	None
 *
 *----------------------------------------------------------------------
 */

static int
ValidateUserAddr(User *userPtr, char *peer)
{
    struct in_addr  peerip, ip, mask;
    int             retval;
    Tcl_HashSearch  search;
    Tcl_HashEntry  *hPtr, *entryPtr;

    if (peer == NULL) {
	return NS_TRUE;
    }

    peerip.s_addr = inet_addr(peer);
    if (peerip.s_addr == INADDR_NONE) {
	return NS_FALSE;
    }

    /*
     * Loop over each netmask, AND the peer address with it,
     * then see if that address is in the list.
     */

    hPtr = Tcl_FirstHashEntry(&userPtr->masks, &search);
    while (hPtr != NULL) {
	mask.s_addr = (unsigned long) Tcl_GetHashKey(&userPtr->masks, hPtr);
	ip.s_addr = peerip.s_addr & mask.s_addr;

	/*
	 * There is a potential match. Now make sure it works with the
	 * right address's mask.
	 */

	entryPtr = Tcl_FindHashEntry(&userPtr->nets, (char *) ip.s_addr);
	if (entryPtr != NULL && mask.s_addr == (unsigned long) Tcl_GetHashValue(entryPtr)) {
	    if (userPtr->filterallow) {
		return NS_TRUE;
	    } else {
		return NS_FALSE;
	    }
	}
	hPtr = Tcl_NextHashEntry(&search);
    }

    if (userPtr->filterallow) {
	retval = NS_FALSE;
    } else {
	retval = NS_TRUE;
    }
    if (userPtr->hosts.numEntries > 0) {
	Ns_DString addr;

	/*
	 * If we have gotten this far, it's necessary to do a
	 * reverse dns lookup and try to make a decision
	 * based on that, if possible.
	 */

	Ns_DStringInit(&addr);
	if (Ns_GetHostByAddr(&addr, peer) == NS_TRUE) {
	    char *start = addr.string;

	    /*
	     * If the hostname is blah.aol.com, check the hash table
	     * for:
	     *
	     * blah.aol.com
	     * .aol.com
	     * .com
	     *
	     * Break out of the loop as soon as a match is found or
	     * all possibilities are exhausted.
	     */

	    while (start != NULL && start[0] != '\0') {
		char *last;

		last = start;
		hPtr = Tcl_FindHashEntry(&userPtr->hosts, start);
		if (hPtr != NULL) {
		    if (userPtr->filterallow) {
			retval = NS_TRUE;
		    } else {
			retval = NS_FALSE;
		    }
		    break;
		}
		start = strchr(start+1, '.');
		if (start == NULL) {
		    break;
		}
		if (last == start) {
		    Ns_Log(Warning, "nsperm: "
			   "invalid hostname '%s'", addr.string);
		    break;
		}
	    }
	}
    }

    return retval;
}


/*
 *----------------------------------------------------------------------
 *
 * AddUserCmd --
 *
 *	Implements the Tcl command ns_perm adduser
 *
 * Results:
 *	Tcl resut
 *
 * Side effects:
 *	A user may be added to the global user hash table
 *
 *----------------------------------------------------------------------
 */

static int
AddUserCmd(Server *servPtr, Tcl_Interp *interp, int argc, char **argv)
{
    User *userPtr;
    Group *groupPtr;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int   new, i, allow;
    char    *name, *slash, *net;
    struct in_addr ip, mask;

    if (argc < 5 || argc == 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " name encpass userfield ?-allow|-deny host ...?\"",
			 NULL);
	return TCL_ERROR;
    }

    allow = 0;
    if (argc > 6) {
	if (STREQ(argv[5], "-allow")) {
	    allow = 1;
	} else if (!STREQ(argv[5], "-deny")) {
	    Tcl_AppendResult(interp, "invalid switch \"", argv[5], "\". ",
			     "Should be -allow or -deny",
			     NULL);
    	    return TCL_ERROR;
	}
    }

    name = argv[2];
    userPtr = ns_malloc(sizeof(User));
    strncpy(userPtr->pass, argv[3], sizeof(userPtr->pass) - 1);
    Tcl_InitHashTable(&userPtr->nets, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&userPtr->masks, TCL_ONE_WORD_KEYS);
    Tcl_InitHashTable(&userPtr->hosts, TCL_STRING_KEYS);
    Tcl_InitHashTable(&userPtr->groups, TCL_STRING_KEYS);
    userPtr->filterallow = allow;

    /*
     * Loop over each parameter and figure out what it is. The
     * possiblities are ipaddr/netmask, hostname, or partial hostname:
     * 192.168.2.3/255.255.255.0, foo.bar.com, or .bar.com
     */

    for (i = 6; i < argc; ++i) {
	mask.s_addr = INADDR_NONE;
	net = argv[i];
	slash = strchr(net, '/');
	if (slash == NULL) {
	    hPtr = Tcl_CreateHashEntry(&userPtr->hosts, net, &new);
	} else {

	    /*
	     * Try to conver the IP address/netmask into binary
	     * values.
	     */

	    *slash = '\0';
	    if (inet_aton(net, &ip) == 0 || inet_aton(slash+1, &mask) == 0) {
		Tcl_AppendResult(interp, "invalid address or hostname \"",
				 net, "\". "
				 "should be ipaddr/netmask or hostname",
				 NULL);
		goto fail;
	    }

	    /*
	     * Do a bitwise AND of the ip address with the netmask
	     * to make sure that all non-network bits are 0. That
	     * saves us from doing this operation every time a
	     * connection comes in.
	     */

	    ip.s_addr &= mask.s_addr;

	    /*
	     * Is this a new netmask? If so, add it to the list.
	     * A list of netmasks is maintained and every time a
	     * new connection comes in, the peer address is ANDed with
	     * each of them and a lookup on that address is done
	     * on the hash table of networks.
	     */

	    (void) Tcl_CreateHashEntry(&userPtr->masks,
					(char *) mask.s_addr, &new);

	    hPtr = Tcl_CreateHashEntry(&userPtr->nets, (char *) ip.s_addr, &new);
	    Tcl_SetHashValue(hPtr, mask.s_addr);
	}
	if (!new) {
	    Tcl_AppendResult(interp, "duplicate entry: ", net, NULL);
	    goto fail;
	}
    }

    /*
     * Add the user.
     */

    hPtr = Tcl_CreateHashEntry(&servPtr->users, name, &new);
    if (!new) {
    	Tcl_AppendResult(interp, "duplicate user: ", name, NULL);
	goto fail;
    }
    Tcl_SetHashValue(hPtr, userPtr);
    return TCL_OK;

fail:
    hPtr = Tcl_FirstHashEntry(&userPtr->groups, &search);
    while (hPtr != NULL) {
	groupPtr = Tcl_GetHashValue(hPtr);
	hPtr = Tcl_FindHashEntry(&groupPtr->users, name);
	if (hPtr != NULL) {
	    Tcl_DeleteHashEntry(hPtr);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&userPtr->groups);
    Tcl_DeleteHashTable(&userPtr->masks);
    Tcl_DeleteHashTable(&userPtr->nets);
    Tcl_DeleteHashTable(&userPtr->hosts);
    ns_free(userPtr);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * AddGroupCmd --
 *
 *	Add a group to the global groups list
 *
 * Results:
 *	Standard tcl
 *
 * Side effects:
 *	A group will be created
 *
 *----------------------------------------------------------------------
 */

static int
AddGroupCmd(Server *servPtr, Tcl_Interp *interp, int argc, char *argv[])
{
    char *name, *user;
    User *userPtr;
    Group *groupPtr;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    int   new, param;

    if (argc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1], " name user ?user ...?",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Create & populate the structure for a new group.
     */

    name = argv[2];
    groupPtr = ns_malloc(sizeof(Group));
    Tcl_InitHashTable(&groupPtr->users, TCL_STRING_KEYS);

    /*
     * Loop over each of the users who is to be in the group, make sure
     * it's ok, and add him. Also put the group into the user's list
     * of groups he's in.
     */

    for (param = 3; param < argc; param++) {
    	user = argv[param];
    	hPtr = Tcl_FindHashEntry(&servPtr->users, user);
	if (hPtr == NULL) {
	    Tcl_AppendResult(interp, "no such user: ", user, NULL);
	    goto fail;
	}
	userPtr = Tcl_GetHashValue(hPtr);

	/*
	 * Add the user to the group's list of users
	 */

	hPtr = Tcl_CreateHashEntry(&groupPtr->users, user, &new);
	if (!new) {
dupuser:
	    Tcl_AppendResult(interp,
	    	"user \"", user, "\" already in group \"", name, "\"", NULL);
	    goto fail;
	}
	Tcl_SetHashValue(hPtr, userPtr);

	/*
	 * Add the group to the user's list of groups
	 */

	hPtr = Tcl_CreateHashEntry(&userPtr->groups, name, &new);
	if (!new) {
	    goto dupuser;
	}
	Tcl_SetHashValue(hPtr, groupPtr);
    }

    /*
     * Add the group to the global list of groups
     */

    hPtr = Tcl_CreateHashEntry(&servPtr->groups, name, &new);
    if (!new) {
	Tcl_AppendResult(interp, "duplicate group: ", name, NULL);
	goto fail;
    }
    Tcl_SetHashValue(hPtr, groupPtr);
    return TCL_OK;

fail:
    hPtr = Tcl_FirstHashEntry(&groupPtr->users, &search);
    while (hPtr != NULL) {
	userPtr = Tcl_GetHashValue(hPtr);
	hPtr = Tcl_FindHashEntry(&userPtr->groups, name);
	if (hPtr != NULL) {
	    Tcl_DeleteHashEntry(hPtr);
	}
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&groupPtr->users);
    ns_free(groupPtr);
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * GroupCmd --
 *
 *	Add a group to allow or deny access.
 *
 * Results:
 *	Std tcl
 *
 * Side effects:
 *	A perm record may be created, a group will be added to its
 *	deny list
 *
 *----------------------------------------------------------------------
 */

static int
AllowDenyCmd(Server *servPtr, Tcl_Interp *interp, int argc, char **argv, int allow, int user)
{
    Perm *permPtr;
    Ns_DString base;
    char *method, *url, *key;
    int flags, new;

    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"", argv[0],
	    " cmd ?-noinherit? method url key", NULL);
	return TCL_ERROR;
    }
    if (argc != 6) {
	flags = 0;
    } else {
	if (!STREQ(argv[2], "-noinherit")) {
	    Tcl_AppendResult(interp, "invalid option \"", argv[2],
	    	"\": should be -noinherit", NULL);
	    return TCL_ERROR;
	}
	flags = NS_OP_NOINHERIT;
    }
    key = argv[argc-1];
    url = argv[argc-2];
    method = argv[argc-3];

    /*
     * Construct the base url.
     */

    Ns_DStringInit(&base);
    Ns_NormalizePath(&base, url);

    /*
     * Locate and verify the exact record.
     */

    permPtr = Ns_UrlSpecificGet(servPtr->server, method, url, uskey);
    if (permPtr != NULL && !STREQ(base.string, permPtr->baseurl)) {
    	permPtr = NULL;
    }
    if (permPtr == NULL) {
	permPtr = ns_malloc(sizeof(Perm));
	permPtr->baseurl = Ns_DStringExport(&base);
	Tcl_InitHashTable(&permPtr->allowuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&permPtr->denyuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&permPtr->allowgroup, TCL_STRING_KEYS);
	Tcl_InitHashTable(&permPtr->denygroup, TCL_STRING_KEYS);
	Ns_UrlSpecificSet(servPtr->server, method, url, uskey, permPtr, flags, NULL);
    }
    permPtr->implicit_allow = !allow;
    if (user) {
	if (allow) {
            (void) Tcl_CreateHashEntry(&permPtr->allowuser, key, &new);
	} else {
            (void) Tcl_CreateHashEntry(&permPtr->denyuser, key, &new);
	}
    } else {
	if (allow) {
            (void) Tcl_CreateHashEntry(&permPtr->allowgroup, key, &new);
	} else {
            (void) Tcl_CreateHashEntry(&permPtr->denygroup, key, &new);
	}
    }
    Ns_DStringFree(&base);
    return TCL_OK;
}

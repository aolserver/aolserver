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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsperm/nsperm.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"

/*
 * Possible values for User.filterstatus
 */

#define USEALLOWLIST 0
#define USEDENYLIST  1

/*
 * Configuration parameters/defaults
 */

#define CONFIG_SKIPLOCKS  "SkipLocks"
#define DEFAULT_SKIPLOCKS NS_TRUE

/*
 * For AOLserver
 */

NS_EXPORT int Ns_ModuleVersion = 1;

/*
 * The "users" hash table points to this kind of data:
 */

typedef struct {
    char *name;
    char *encpass;
    char *uf1;
    Tcl_HashTable groups;
    Tcl_HashTable nets;
    Tcl_HashTable masks;
    int   filterstatus;
    int   do_dns;
    Ns_Mutex lock;
} User;

/*
 * The "groups" hash table points to this kind of data:
 */

typedef struct {
    char          *name;
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
 * The nets table in User points to these:
 */

typedef struct {
    struct in_addr  ip;
    struct in_addr  mask;
    char          *hostname;
} Network;

/*
 * Local functions defined in this file
 */

static int CheckPassCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int SetPassCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int DenyGroupCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int AllowGroupCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int DenyUserCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int AllowUserCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int AddGroupCmd(Tcl_Interp *interp, int argc, char *argv[]);
static int ValidateUserAddr(User *userPtr, char *peer);
static Group *GetGroup(char *group);
static User *GetUser(char *user);
static User *GetUser2(char *user);
static int CheckPass(User *userPtr, char *pass);
static int UserAuthProc(char *user, char *pass);
static int AuthProc(char *server, char *method, char *url, char *user,
		    char *pass, char *peer);
static int PermCmd(ClientData parse, Tcl_Interp *interp, int argc,
		   char **argv);
static int AddCmds(Tcl_Interp *interpPtr, void *ctx);
static int AddUserCmd(Tcl_Interp *interp, int argc, char **argv);

/*
 * Static variables defined in this file.
 */

static Tcl_HashTable   users;
static Tcl_HashTable   groups;
static Tcl_HashTable   netmasks;
static Ns_Mutex        userlock;
static Ns_Mutex        grouplock;
static Ns_Mutex        uslock;
static Ns_Mutex        permlock;
static int             uskey;

/*
 * As an optimization, all locking can be turned off. This prevents
 * you from being able to add users/groups/perms at runtime but
 * really speeds things up. It's on by default.
 */

static int             skiplocks;


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
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

NS_EXPORT int
Ns_ModuleInit(char *hServer, char *hModule)
{
    char *path;

    path = Ns_ConfigGetPath(hServer, hModule, NULL);
    if (Ns_ConfigGetBool(path, CONFIG_SKIPLOCKS, &skiplocks) == NS_FALSE) {
	skiplocks = DEFAULT_SKIPLOCKS;
    }
    Tcl_InitHashTable(&users, TCL_STRING_KEYS);
    Tcl_InitHashTable(&groups, TCL_STRING_KEYS);
    uskey = Ns_UrlSpecificAlloc();
    Ns_SetRequestAuthorizeProc(nsServer, AuthProc);
    Ns_SetUserAuthorizeProc(UserAuthProc);
    Ns_TclInitInterps(hServer, AddCmds, NULL);
    return NS_OK;
}

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */

/*
 *----------------------------------------------------------------------
 *
 * Ns_PermPasswordCheck --
 *
 *  Validate a user's (encrypted) password.  A wrapper for CheckPass().
 *
 * Results:
 *  NS_TRUE if OK 
 *
 * Side effects:
 *  None 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PermPasswordCheck(char *user, char *password) 
{

    User *userPtr;
    int status = NS_FALSE;
    char temp[32];

    userPtr = GetUser(user);

    if (!skiplocks) {
       Ns_MutexLock(&permlock);
    }

    userPtr = GetUser(user);

    if (userPtr == NULL) {
       if (!skiplocks) {
          Ns_MutexUnlock(&permlock);
       }
       goto done;
    }

    strncpy(temp, userPtr->encpass, 31);    

    if (CheckPass(userPtr, password) == NS_FALSE) {

       if (!skiplocks) {
          Ns_MutexUnlock(&permlock);
       }
       goto done;
    } 

    status = NS_TRUE;

 done:
    return status;
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
AddCmds(Tcl_Interp *interpPtr, void *ctx)
{
    Tcl_CreateCommand(interpPtr, "ns_perm", PermCmd, NULL, NULL);

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
PermCmd(ClientData parse, Tcl_Interp *interp, int argc, char **argv)
{
    if (argc < 2) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " command ?args ...?\"", NULL);
	return TCL_ERROR;
    }
    if (!strcasecmp(argv[1], "adduser")) {
	return AddUserCmd(interp, argc, argv);    
    } else if (!strcasecmp(argv[1], "addgroup")) {
	return AddGroupCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "allowuser")) {
	return AllowUserCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "denyuser")) {
	return DenyUserCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "allowgroup")) {
	return AllowGroupCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "denygroup")) {
	return DenyGroupCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "checkpass")) {
	return CheckPassCmd(interp, argc, argv);
    } else if (!strcasecmp(argv[1], "setpass")) {
	return SetPassCmd(interp, argc, argv);
    } else {
	Tcl_AppendResult(interp, "unknown command \"",
			 argv[1],
			 "\": should be adduser, addgroup, ",
			 "allowuser, denyuser, "
			 "allowgroup, denygroup ",
			 "or checkpass",
			 NULL);
	return TCL_ERROR;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * UserAuthProc --
 *
 *	Authorize a user
 *
 * Results:
 *	NS_OK: allowed
 *	NS_ERROR: not allowed
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
UserAuthProc(char *user, char *pass)
{
    User *userPtr;
    char  buf[32];
    char  salt[3];
    
    userPtr = GetUser(user);
    if (userPtr == NULL) {
	return NS_ERROR;
    }
    Ns_MutexLock(&userPtr->lock);
    salt[0] = userPtr->encpass[0];
    salt[1] = userPtr->encpass[1];
    salt[2] = '\0';
    
    Ns_Encrypt(pass, salt, buf);
    if (!strncmp(buf, userPtr->encpass, strlen(userPtr->encpass))) {
	Ns_MutexUnlock(&userPtr->lock);
	return NS_OK;
    }
    Ns_MutexUnlock(&userPtr->lock);
    return NS_ERROR;
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
    Perm          *permPtr;
    User          *userPtr;
    Tcl_HashEntry *hePtr;
    Tcl_HashSearch search;
    char           temp[32];

    if (user == NULL) {
	user = "";
    }
    if (pass == NULL) {
	pass = "";
    }
    if (!skiplocks) {
	Ns_MutexLock(&uslock);
    }
    permPtr = Ns_UrlSpecificGet(server, method, url, uskey);
    if (!skiplocks) {
	Ns_MutexUnlock(&uslock);
    }
    if (permPtr == NULL) {
	return NS_OK;
    }
    if (!skiplocks) {
	Ns_MutexLock(&permlock);
    }

    userPtr = GetUser(user);
    if (userPtr == NULL) {
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_UNAUTHORIZED;
    }
    strncpy(temp, userPtr->encpass, 31);    
    if (CheckPass(userPtr, pass) == NS_FALSE) {
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_UNAUTHORIZED;
    }
    if (ValidateUserAddr(userPtr, peer) == NS_FALSE) {
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	/*
	 * Null user never gets forbidden--give a chance to enter password.
	 */
	if (!strcmp(user, "") && !strcmp(pass, "")) {
	    return NS_UNAUTHORIZED;
	} else {
	    return NS_FORBIDDEN;
	}
    }
    /*
     * Check the deny lists
     */
    
    hePtr = Tcl_FindHashEntry(&permPtr->denyuser, user);
    if (hePtr != NULL) {
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}

	/*
	 * Null user never gets forbidden--give a chance to enter password.
	 */

	if (!strcmp(user, "") && !strcmp(pass, "")) {
	    return NS_UNAUTHORIZED;
	} else {
	    return NS_FORBIDDEN;
	}
    }

    /*
     * Loop over all groups in this perm record, and then
     * see if the user is in any of those groups.
     */
    
    hePtr = Tcl_FirstHashEntry(&permPtr->denygroup, &search);
    while (hePtr != NULL) {
	char          *gname;
	Tcl_HashEntry *uePtr;
	
	gname = Tcl_GetHashKey(&permPtr->denygroup, hePtr);
	uePtr = Tcl_FindHashEntry(&userPtr->groups, gname);
	if (uePtr != NULL) {
	    /*
	     * The user is in one of the groups
	     */
	    if (!skiplocks) {
		Ns_MutexUnlock(&permlock);
	    }

	    /*
	     * Null user never gets forbidden--give a chance to enter password.
	     */
	    
	    if (!strcmp(user, "") && !strcmp(pass, "")) {
		return NS_UNAUTHORIZED;
	    } else {
		return NS_FORBIDDEN;
	    }
	}
	hePtr = Tcl_NextHashEntry(&search);
    }

    /*
     * Check the allow lists, starting with users
     */
    
    hePtr = Tcl_FindHashEntry(&permPtr->allowuser, user);
    if (hePtr != NULL) {
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_OK;
    }

    /*
     * Loop over all groups in this perm record, and then
     * see if the user is in any of those groups.
     */
    
    hePtr = Tcl_FirstHashEntry(&permPtr->allowgroup, &search);
    while (hePtr != NULL) {
	char          *gname;
	Tcl_HashEntry *uePtr;
	
	gname = Tcl_GetHashKey(&permPtr->allowgroup, hePtr);
	uePtr = Tcl_FindHashEntry(&userPtr->groups, gname);
	if (uePtr != NULL) {
	    /*
	     * The user is in one of the groups
	     */
	    if (!skiplocks) {
		Ns_MutexUnlock(&permlock);
	    }
	    return NS_OK;
	}
	hePtr = Tcl_NextHashEntry(&search);
    }

    /*
     * The user is on no lists.
     */

    if (permPtr->implicit_allow) {
	/*
	 * Only deny lists exist for this perm record, so
	 * allow users who are unlisted.
	 */
	
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_OK;
    }
    
    if (pass[0] == '\0' && user[0] == '\0') {
	/*
	 * While this is a vaild user/pass combo, which would
	 * normally result in a forbidden, treat it as a
	 * special case because this is how people would normally
	 * first attempt a page w/ a password.
	 */
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_UNAUTHORIZED;
    } else {
	/*
	 * User is denied because there is a
	 * perm record but he's not listed as an allow,
	 * and implicit allow is turned off (meaning that
	 * there IS an allow list--he's just not on it).
	 */
	if (!skiplocks) {
	    Ns_MutexUnlock(&permlock);
	}
	return NS_UNAUTHORIZED;
    }

    /*
     * This is unreachable.
     */
}


/*
 *----------------------------------------------------------------------
 *
 * CheckPass --
 *
 *	Validate a user's (encrypted) password 
 *
 * Results:
 *	NS_TRUE if OK 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static int
CheckPass(User *userPtr, char *pass)
{
    char buf[32];
    if (pass[0] == 0 && userPtr->encpass[0] == 0) {
	return NS_TRUE;
    }
    if (pass[0] != 0 && userPtr->encpass[0] == 0) {
	/*
	 * This is here because Ns_Encrypt doesn't deal well with
	 * null salt.
	 */
	return NS_FALSE;
    }
    Ns_Encrypt(pass, userPtr->encpass, buf);
    if (!strcasecmp(userPtr->encpass, buf)) {
	return NS_TRUE;
    }

    return NS_FALSE;
}


/*
 *----------------------------------------------------------------------
 *
 * GetUser --
 *
 *	Retrieve a User structure given a string username 
 *
 * Results:
 *	A pointer to a User structure or null 
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static User *
GetUser2(char *user)
{
    Tcl_HashEntry *hePtr;
    User          *ret;
    
    hePtr = Tcl_FindHashEntry(&users, user);
    if (hePtr != NULL) {
	ret = Tcl_GetHashValue(hePtr);
    } else {
	ret = NULL;
    }
    return ret;
}

static User *
GetUser(char *user)
{
    User *ret;
    
    if (!skiplocks) {
	Ns_MutexLock(&userlock);
    }
    
    ret = GetUser2(user);
    
    if (!skiplocks) {
	Ns_MutexUnlock(&userlock);
    }
    return ret;
}


/*
 *----------------------------------------------------------------------
 *
 * GetGroup --
 *
 *	A pointer to a Group structure or null 
 *
 * Results:
 *	Retreive a Group struct given a string group name
 *
 * Side effects:
 *	None 
 *
 *----------------------------------------------------------------------
 */

static Group *
GetGroup(char *group)
{
    Tcl_HashEntry *hePtr;
    Group         *ret;
    
    if (!skiplocks) {
	Ns_MutexLock(&grouplock);
    }
    hePtr = Tcl_FindHashEntry(&groups, group);
    if (hePtr != NULL) {
	ret = Tcl_GetHashValue(hePtr);
    } else {
	ret = NULL;
    }
    if (!skiplocks) {
	Ns_MutexUnlock(&grouplock);
    }
    return ret;
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

#ifdef WIN32
static int
inet_aton(char *peer, struct in_addr *inPtr)
{
    unsigned long l;

    l = inet_addr(peer);
    if (l == INADDR_NONE) {
	return 0;
    }
    inPtr->s_addr = l;
    return 1;
}
#endif

static int
ValidateUserAddr(User *userPtr, char *peer)
{
    struct in_addr  peerip;
    int             do_dns;
    int             retval;
    Tcl_HashSearch  search;
    Tcl_HashEntry  *hePtr, *entryPtr;

    if (peer == NULL) {
	return NS_TRUE;
    }
    if (inet_aton(peer, &peerip) == 0) {
	Ns_Log(Bug, "ValidateUserAddr: "
	       "bogus peer address of '%s'", peer);
	return NS_FALSE;
    }
    if (!skiplocks) {
	Ns_LockMutex(&userPtr->lock);
    }
    /*
     * Loop over each netmask, AND the peer address with it,
     * then see if that address is in the list.
     */
    hePtr = Tcl_FirstHashEntry(&userPtr->masks, &search);
    while (hePtr != NULL) {
	struct in_addr  tempaddr, tempaddr2, tempmask;
	char           *straddr;
	
	tempmask.s_addr = (unsigned long) Tcl_GetHashKey(&userPtr->masks,
							 hePtr);
	tempaddr.s_addr = peerip.s_addr & tempmask.s_addr;
	straddr = ns_inet_ntoa(tempaddr);
	entryPtr = Tcl_FindHashEntry(&userPtr->nets, straddr);
	/*
	 * There is a potential match. Now make sure it works with the
	 * right address's mask.
	 */
	if (entryPtr != NULL) {
	    Network *nPtr = Tcl_GetHashValue(entryPtr);
	    
	    tempmask.s_addr = nPtr->mask.s_addr;
	    tempaddr2.s_addr = peerip.s_addr & tempmask.s_addr;
	    if (tempaddr2.s_addr == nPtr->ip.s_addr) {
		if (userPtr->filterstatus == USEALLOWLIST) {
		    if (!skiplocks) {
			Ns_MutexUnlock(&userPtr->lock);
		    }
		    return NS_TRUE;
		} else {
		    if (!skiplocks) {
			Ns_MutexUnlock(&userPtr->lock);
		    }
		    return NS_FALSE;
		}
	    }
	}
	hePtr = Tcl_NextHashEntry(&search);
    }
    do_dns = userPtr->do_dns;

    if (userPtr->filterstatus == USEALLOWLIST) {
	retval = NS_FALSE;
    } else {
	retval = NS_TRUE;
    }
    
    if (!skiplocks) {
	Ns_MutexUnlock(&userPtr->lock);
    }

    if (do_dns) {
	/*
	 * If we have gotten this far, it's necessary to do a
	 * reverse dns lookup and try to make a decision
	 * based on that, if possible.
	 */
	Ns_DString addr;
	Ns_DStringInit(&addr);
	if (Ns_GetHostByAddr(&addr, peer) == NS_TRUE) {
	    char *start = addr.string;
	    if (!skiplocks) {
		Ns_MutexLock(&userPtr->lock);
	    }

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
		hePtr = Tcl_FindHashEntry(&userPtr->nets, start);
		if (hePtr != NULL) {
		    if (userPtr->filterstatus == USEALLOWLIST) {
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
		    Ns_Log(Warning, "ValidateUserAddr: "
			   "invalid hostname: %s", addr.string);
		    break;
		}
	    }

	    if (!skiplocks) {
		Ns_MutexUnlock(&userPtr->lock);
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
AddUserCmd(Tcl_Interp *interp, int argc, char **argv)
{
    char *name, *encpass, *uf1;
    User *uPtr;
    Tcl_HashEntry *hePtr;
    int   new;

    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add users after server startup",
			 NULL);
	return TCL_ERROR;
    }

    if (argc < 5) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " name encpass userfield ?-allow|-deny host ...?\"",
			 NULL);
	return TCL_ERROR;
    }
    name = argv[2] ? ns_strdup(argv[2]) : NULL;
    encpass = ns_strdup(argv[3]);
    uf1 = ns_strdup(argv[4]);

    uPtr = ns_malloc(sizeof(User));
    uPtr->name = name;
    uPtr->encpass = encpass;
    uPtr->uf1 = uf1;
    uPtr->do_dns = NS_FALSE;
    if (!skiplocks) {
	Ns_MutexInit(&uPtr->lock);
    }
    Tcl_InitHashTable(&uPtr->nets, TCL_STRING_KEYS);
    Tcl_InitHashTable(&uPtr->masks, TCL_ONE_WORD_KEYS);
    uPtr->filterstatus = USEDENYLIST;
    if (argc > 5) {
	int param = 5;

	/*
	 * There is an accept/deny list which needs to be parsed.
	 * Only one of accept or deny may be set to keep things
	 * simple.
	 */
	
	if (!strcasecmp(argv[param], "-allow")) {
	    uPtr->filterstatus = USEALLOWLIST;
	} else if (!strcasecmp(argv[param], "-deny")) {
	    uPtr->filterstatus = USEDENYLIST;
	} else {
	    Tcl_AppendResult(interp, "invalid switch \"", argv[param], "\". ",
			     "Should be -allow or -deny",
			     NULL);
	    return TCL_ERROR;
	}
	param++;
	
	/*
	 * Loop over each parameter and figure out what it is. The
	 * possiblities are ipaddr/netmask, hostname, or partial hostname:
	 * 192.168.2.3/255.255.255.0, foo.bar.com, or .bar.com
	 */
	
	for (; param < argc; param++) {
	    Network *nPtr;
	    char    *slash;
	    char    *net;
	    Tcl_HashEntry *hePtr;
	    int      new;
	    char    *hashkey;
	    char     buf[32];
		
	    net = argv[param];
	    hashkey = net;
	    nPtr = ns_malloc(sizeof(Network));
	    
	    /*
	     * If a slash appears that means that it's an IP address
	     * and netmask seperated by a slash.
	     */
	    
	    slash = strchr(net, '/');
	    if (slash == NULL) {
		nPtr->hostname = ns_strdup(net);
	    } else {

		/*
		 * Try to conver the IP address/netmask into binary
		 * values.
		 */
		
		*slash = '\0';
		if (inet_aton(net, &nPtr->ip) == 0 ||
		    inet_aton(slash+1, &nPtr->mask) == 0) {
		    *slash = '/';
		    Tcl_AppendResult(interp, "invalid address or hostname \"",
				     net, "\". "
				     "Must be ipaddr/netmask or hostname",
				     NULL);
		    return TCL_ERROR;
		}

		/*
		 * Do a bitwise AND of the ip address with the netmask
		 * to make sure that all non-network bits are 0. That
		 * saves us from doing this operation every time a
		 * connection comes in.
		 */
		
		nPtr->ip.s_addr &= nPtr->mask.s_addr;
		strncpy(buf, ns_inet_ntoa(nPtr->ip), 31);
		hashkey = buf;

		/*
		 * Is this a new netmask? If so, add it to the list.
		 * A list of netmasks is maintained and every time a
		 * new connection comes in, the peer address is ANDed with
		 * each of them and a lookup on that address is done
		 * on the hash table of networks.
		 */
		
		hePtr = Tcl_CreateHashEntry(&uPtr->masks,
					    (char*)nPtr->mask.s_addr, &new);
		if (new) {
		    /*
		     * The value doesn't matter--it's really a cheap
		     * linked list.
		     */
		    Tcl_SetHashValue(hePtr, NULL);
		}
	    }
	    hePtr = Tcl_CreateHashEntry(&uPtr->nets, hashkey, &new);
	    
	    /*
	     * Put back the slash character that was removed earlier for
	     * the benefit of inet_aton
	     */
	    
	    if (slash != NULL) {
		*slash = '/';
	    }
	    if (!new) {
		Tcl_AppendResult(interp, "entry \"", net, "\" already in list",
				 NULL);
		return TCL_ERROR;
	    }

	    /*
	     * If this entry was not an IP address then reverse lookups
	     * on this perm record may be necessary. Set the flag so
	     * that it'll try that as a last resort.
	     */
	    
	    if (slash == NULL) {
		uPtr->do_dns = NS_TRUE;
	    }		    
	    Tcl_SetHashValue(hePtr, nPtr);
	}
    }
    Tcl_InitHashTable(&uPtr->groups, TCL_STRING_KEYS);
    if (!skiplocks) {
	Ns_LockMutex(&userlock);
    }

    /*
     * Finally put an entry into the global hash table of users.
     */
    
    hePtr = Tcl_CreateHashEntry(&users, name, &new);
    if (!new) {
	if (!skiplocks) {
	    Ns_UnlockMutex(&userlock);
	}
	Tcl_AppendResult(interp, "user \"", name, "\" already exists",
			 NULL);
	ns_free(name);
	ns_free(encpass);
	if (uf1 != NULL) {
	    ns_free(uf1);
	}
	ns_free(uPtr);
	return TCL_ERROR;
    }

    Tcl_SetHashValue(hePtr, uPtr);
    if (!skiplocks) {
	Ns_UnlockMutex(&userlock);
    }

    return TCL_OK;
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
AddGroupCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    char *name;
    Group *gPtr;
    Tcl_HashEntry *hePtr;
    int   new, param;

    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add groups after server startup",
			 NULL);
	return TCL_ERROR;
    }

    if (argc < 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1], " name user ?user ...?",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Create & populate the structure for a new group.
     */
    
    gPtr = ns_malloc(sizeof(Group));
    name = ns_strdup(argv[2]);
    gPtr->name = name;
    Tcl_InitHashTable(&gPtr->users, TCL_STRING_KEYS);
    if (!skiplocks) {
	Ns_LockMutex(&grouplock);
	Ns_LockMutex(&userlock);
    }

    /*
     * Loop over each of the users who is to be in the group, make sure
     * it's ok, and add him. Also put the group into the user's list
     * of groups he's in.
     */
     
    for (param = 3; param < argc; param++) {
	User *uPtr;

	/*
	 * GetUser2 is like GetUser but it doesn't touch the mutex which
	 * has already been locked by this function.
	 */
	
	uPtr = GetUser2(argv[param]);
	if (uPtr == NULL) {
	    Tcl_AppendResult(interp, "no such user \"", argv[param], "\"",
			     NULL);
	    if (!skiplocks) {
		Ns_UnlockMutex(&userlock);
		Ns_UnlockMutex(&grouplock);
	    }
	    return TCL_ERROR;
	}

	/*
	 * Add the user to the group's list of users
	 */

	hePtr = Tcl_CreateHashEntry(&gPtr->users, argv[param], &new);
	if (!new) {
	    Tcl_AppendResult(interp, "user \"", argv[param], "\" "
			     "already in group "
			     "\"", name, "\"",
			     NULL);
	    if (!skiplocks) {
		Ns_UnlockMutex(&userlock);
		Ns_UnlockMutex(&grouplock);
	    }
	    return TCL_ERROR;
	}
	Tcl_SetHashValue(hePtr, uPtr);

	/*
	 * Add the group to the user's list of groups
	 */
	
	hePtr = Tcl_CreateHashEntry(&uPtr->groups, name, &new);
	if (!new) {
	    Tcl_AppendResult(interp, "user \"", argv[param], "\" already "
			     "in Group "
			     "\"", name, "\"",
			     NULL);
	    if (!skiplocks) {
		Ns_UnlockMutex(&userlock);
		Ns_UnlockMutex(&grouplock);
	    }
	    return TCL_ERROR;
	}
	Tcl_SetHashValue(hePtr, gPtr);
    }

    /*
     * Add the group to the global list of groups
     */
    
    hePtr = Tcl_CreateHashEntry(&groups, name, &new);
    if (!new) {
	if (!skiplocks) {
	    Ns_UnlockMutex(&userlock);
	    Ns_UnlockMutex(&grouplock);
	}
	Tcl_AppendResult(interp, "group \"", name, "\" already exists",
			 NULL);
	return TCL_ERROR;
    }

    Tcl_SetHashValue(hePtr, gPtr);
    if (!skiplocks) {
	Ns_UnlockMutex(&userlock);
	Ns_UnlockMutex(&grouplock);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AllowUserCmd --
 *
 *	Add a perm record allowing a user 
 *
 * Results:
 *	Std tcl 
 *
 * Side effects:
 *	A perm record may be added 
 *
 *----------------------------------------------------------------------
 */

static int
AllowUserCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    char *method, *url, *user;
    User *userPtr;
    Perm *pPtr;
    int   inherit = 0;
    int   param = 2;
    Tcl_HashEntry *hePtr;
    int   new;
    Ns_DString usg;
    
    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add permissions after server startup",
			 NULL);
	return TCL_ERROR;
    }
	
    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " ?-noinherit? method url user",
			 NULL);
	return TCL_ERROR;
    }

    if (argc == 6) {
	if (!strcasecmp(argv[param++], "-noinherit")) {
	    inherit |= NS_OP_NOINHERIT;
	} else {
	    Tcl_AppendResult(interp, "unknown switch: ", argv[2],
			     ": should be \"-noinherit\"", NULL);
	    return TCL_ERROR;
	}
    }

    method = argv[param++];
    url    = argv[param++];
    user   = argv[param++];

    userPtr = GetUser(user);
    if (userPtr == NULL) {
	Tcl_AppendResult(interp, "unkown user \"", user, "\"",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Get the existing perm record, or allocate a new one if needed.
     */
	
    if (!skiplocks) {
	Ns_MutexLock(&uslock);
    }
    pPtr = Ns_UrlSpecificGet(nsServer, method, url, uskey);
    if (!skiplocks) {
	Ns_MutexUnlock(&uslock);
    }
    if (pPtr != NULL) {

	/*
	 * Check the baseurl because it may have gotten a parent
	 * url. For example, if /NS were already registered and
	 * we want to add /NS/Admin, urlspecificget would happily
	 * return /NS when asked for /NS/Admin. That's expected
	 * behavior. We couldn't do a Ns_UrlSpecificGetExact call
	 * because that pays too much attention to inheritance.
	 */
	
	Ns_DStringInit(&usg);
	Ns_DStringPrintf(&usg, "%s/%s/%s", nsServer, method, url);
	if (strcmp(usg.string, pPtr->baseurl)) {
	    pPtr = NULL;
	}
	Ns_DStringFree(&usg);
    }
    if (pPtr == NULL) {
	Ns_DString base;

	/*
	 * If we got here it's because we need to create a new perm
	 * record. Allocate it, populate it, and urlspecificset it.
	 */
	
	pPtr = ns_malloc(sizeof(Perm));
	Ns_DStringInit(&base);
	Ns_DStringPrintf(&base, "%s/%s/%s", nsServer, method, url);
	pPtr->baseurl = Ns_DStringExport(&base);
	pPtr->implicit_allow = 0;
	Tcl_InitHashTable(&pPtr->allowuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denyuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->allowgroup, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denygroup, TCL_STRING_KEYS);
	if (!skiplocks) {
	    Ns_MutexInit(&permlock);
	    Ns_MutexLock(&permlock);
	}
	if (!skiplocks) {
	    Ns_MutexLock(&uslock);
	}
	Ns_UrlSpecificSet(nsServer, method, url, uskey, pPtr, inherit, NULL);
	if (!skiplocks) {
	    Ns_MutexUnlock(&uslock);
	}
    } else {
	if (!skiplocks) {
	    Ns_MutexLock(&permlock);
	}
    }

    /*
     * Finally, put the user into the perm record.
     */
    
    hePtr = Tcl_CreateHashEntry(&pPtr->allowuser, user, &new);
    Tcl_SetHashValue(hePtr, userPtr);
    if (!skiplocks) {
	Ns_MutexUnlock(&permlock);
    }
    
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DenyUserCmd --
 *
 *	Deny a specific user access to a (set of) url(s) 
 *
 * Results:
 *	Tcl result 
 *
 * Side effects:
 *	Adds a user to the deny list 
 *
 *----------------------------------------------------------------------
 */

static int
DenyUserCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    char *method, *url, *user;
    User *userPtr;
    Perm *pPtr;
    int   inherit = 0;
    int   param = 2;
    Tcl_HashEntry *hePtr;
    int   new;
    Ns_DString usg;
    
    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add permissions after server startup",
			 NULL);
	return TCL_ERROR;
    }

    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " ?-noinherit? method url user",
			 NULL);
	return TCL_ERROR;
    }

    if (argc == 6) {
	if (!strcasecmp(argv[param++], "-noinherit")) {
	    inherit |= NS_OP_NOINHERIT;
	} else {
	    Tcl_AppendResult(interp, "unknown switch: ", argv[2],
			     ": should be \"-noinherit\"", NULL);
	    return TCL_ERROR;
	}
    }

    method = argv[param++];
    url    = argv[param++];
    user   = argv[param++];

    userPtr = GetUser(user);
    if (userPtr == NULL) {
	Tcl_AppendResult(interp, "unkown user \"", user, "\"",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Get the existing perm record, or allocate a new one if needed.
     */
	
    if (!skiplocks) {
	Ns_MutexLock(&uslock);
    }
    pPtr = Ns_UrlSpecificGet(nsServer, method, url, uskey);
    if (!skiplocks) {
	Ns_MutexUnlock(&uslock);
    }
    if (pPtr != NULL) {

	/*
	 * Check the baseurl because it may have gotten a parent
	 * url. For example, if /NS were already registered and
	 * we want to add /NS/Admin, urlspecificget would happily
	 * return /NS when asked for /NS/Admin. That's expected
	 * behavior. We couldn't do a Ns_UrlSpecificGetExact call
	 * because that pays too much attention to inheritance.
	 */
	
	Ns_DStringInit(&usg);
	Ns_DStringPrintf(&usg, "%s/%s/%s", nsServer, method, url);
	if (strcmp(usg.string, pPtr->baseurl)) {
	    pPtr = NULL;
	}
	Ns_DStringFree(&usg);
    }
    if (pPtr == NULL) {
	Ns_DString base;

	/*
	 * If we got here it's because we need to create a new perm
	 * record. Allocate it, populate it, and urlspecificset it.
	 */
	
	pPtr = ns_malloc(sizeof(Perm));
	Ns_DStringInit(&base);
	Ns_DStringPrintf(&base, "%s/%s/%s", nsServer, method, url);
	pPtr->baseurl = Ns_DStringExport(&base);
	pPtr->implicit_allow = 1;
	Tcl_InitHashTable(&pPtr->allowuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denyuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->allowgroup, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denygroup, TCL_STRING_KEYS);
	if (!skiplocks) {
	    Ns_MutexInit(&permlock);
	    Ns_MutexLock(&permlock);
	}
	if (!skiplocks) {
	    Ns_MutexLock(&uslock);
	}
	Ns_UrlSpecificSet(nsServer, method, url, uskey, pPtr, inherit, NULL);
	if (!skiplocks) {
	    Ns_MutexUnlock(&uslock);
	}
    } else {
	if (!skiplocks) {
	    Ns_MutexLock(&permlock);
	}
    }

    /*
     * Finally, put the user into the perm record.
     */

    hePtr = Tcl_CreateHashEntry(&pPtr->denyuser, user, &new);
    Tcl_SetHashValue(hePtr, userPtr);
    if (!skiplocks) {
	Ns_MutexUnlock(&permlock);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * AllowGroupCmd --
 *
 *	Allow a group access to url(s) 
 *
 * Results:
 *	Std tcl 
 *
 * Side effects:
 *	A group will be added to a perm rec which may be created 
 *
 *----------------------------------------------------------------------
 */

static int
AllowGroupCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    char *method, *url, *group;
    Group *groupPtr;
    Perm *pPtr;
    int   inherit = 0;
    int   param = 2;
    Tcl_HashEntry *hePtr;
    int   new;
    Ns_DString usg;
	
    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add permissions after server startup",
			 NULL);
	return TCL_ERROR;
    }
	
    /*
	 * ns_perm allowgroup ?-noinherit? method url group
	 */
	
    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " ?-noinherit? method url group",
			 NULL);
	return TCL_ERROR;
    }

    if (argc == 6) {
	if (!strcasecmp(argv[param++], "-noinherit")) {
	    inherit |= NS_OP_NOINHERIT;
	} else {
	    Tcl_AppendResult(interp, "unknown switch: ", argv[2],
			     ": should be \"-noinherit\"", NULL);
	    return TCL_ERROR;
	}
    }

    method = argv[param++];
    url    = argv[param++];
    group  = argv[param++];

    groupPtr = GetGroup(group);
    if (groupPtr == NULL) {
	Tcl_AppendResult(interp, "unkown group \"", group, "\"",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Get the existing perm record, or allocate a new one if needed.
     */
	
    if (!skiplocks) {
	Ns_MutexLock(&uslock);
    }
    pPtr = Ns_UrlSpecificGet(nsServer, method, url, uskey);
    if (!skiplocks) {
	Ns_MutexUnlock(&uslock);
    }
    if (pPtr != NULL) {
	Ns_DStringInit(&usg);
	Ns_DStringPrintf(&usg, "%s/%s/%s", nsServer, method, url);
	if (strcmp(usg.string, pPtr->baseurl)) {
	    pPtr = NULL;
	}
	Ns_DStringFree(&usg);
    }
	
    if (pPtr == NULL) {
	Ns_DString base;
	    
	pPtr = ns_malloc(sizeof(Perm));
	Ns_DStringInit(&base);
	Ns_DStringPrintf(&base, "%s/%s/%s", nsServer, method, url);
	pPtr->baseurl = Ns_DStringExport(&base);
	pPtr->implicit_allow = 0;
	Tcl_InitHashTable(&pPtr->allowuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denyuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->allowgroup, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denygroup, TCL_STRING_KEYS);
	if (!skiplocks) {
	    Ns_MutexInit(&permlock);
	    Ns_MutexLock(&permlock);
	}
	if (!skiplocks) {
	    Ns_MutexLock(&uslock);
	}
	Ns_UrlSpecificSet(nsServer, method, url, uskey, pPtr, inherit, NULL);
	if (!skiplocks) {
	    Ns_MutexUnlock(&uslock);
	}
    } else {
	if (!skiplocks) {
	    Ns_MutexLock(&permlock);
	}
    }
    pPtr->implicit_allow = 0;
    hePtr = Tcl_CreateHashEntry(&pPtr->allowgroup, group, &new);
    Tcl_SetHashValue(hePtr, groupPtr);
    if (!skiplocks) {
	Ns_MutexUnlock(&permlock);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DenyGroupCmd --
 *
 *	Add a group to deny access to 
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
DenyGroupCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    char *method, *url, *group;
    Group *groupPtr;
    Perm *pPtr;
    int   inherit = 0;
    int   param = 2;
    Tcl_HashEntry *hePtr;
    int   new;
    Ns_DString usg;
	
    if (Ns_InfoStarted() && skiplocks) {
	Tcl_AppendResult(interp, "skiplocks config parameter must be off "
			 "to add permissions after server startup",
			 NULL);
	return TCL_ERROR;
    }

    if (argc != 5 && argc != 6) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " ?-noinherit? method url group",
			 NULL);
	return TCL_ERROR;
    }

    if (argc == 6) {
	if (!strcasecmp(argv[param++], "-noinherit")) {
	    inherit |= NS_OP_NOINHERIT;
	} else {
	    Tcl_AppendResult(interp, "unknown switch: ", argv[2],
			     ": should be \"-noinherit\"", NULL);
	    return TCL_ERROR;
	}
    }

    method = argv[param++];
    url    = argv[param++];
    group  = argv[param++];

    groupPtr = GetGroup(group);
    if (groupPtr == NULL) {
	Tcl_AppendResult(interp, "unkown group \"", group, "\"",
			 NULL);
	return TCL_ERROR;
    }

    /*
     * Get the existing perm record, or allocate a new one if needed.
     */
	
    if (!skiplocks) {
	Ns_MutexLock(&uslock);
    }
    pPtr = Ns_UrlSpecificGet(nsServer, method, url, uskey);
    if (!skiplocks) {
	Ns_MutexUnlock(&uslock);
    }
    if (pPtr != NULL) {
	Ns_DStringInit(&usg);
	Ns_DStringPrintf(&usg, "%s/%s/%s", nsServer, method, url);
	if (strcmp(usg.string, pPtr->baseurl)) {
	    pPtr = NULL;
	}
	Ns_DStringFree(&usg);
    }

    if (pPtr == NULL) {
	Ns_DString base;

	pPtr = ns_malloc(sizeof(Perm));
	Ns_DStringInit(&base);
	Ns_DStringPrintf(&base, "%s/%s/%s", nsServer, method, url);
	pPtr->baseurl = Ns_DStringExport(&base);
	pPtr->implicit_allow = 1;
	Tcl_InitHashTable(&pPtr->allowuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denyuser, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->allowgroup, TCL_STRING_KEYS);
	Tcl_InitHashTable(&pPtr->denygroup, TCL_STRING_KEYS);
	if (!skiplocks) {
	    Ns_MutexInit(&permlock);
	    Ns_MutexLock(&permlock);
	}
	if (!skiplocks) {
	    Ns_MutexLock(&uslock);
	}
	Ns_UrlSpecificSet(nsServer, method, url, uskey, pPtr, inherit, NULL);
	if (!skiplocks) {
	    Ns_MutexUnlock(&uslock);
	}
    } else {
	if (!skiplocks) {
	    Ns_MutexLock(&permlock);
	}
    }

    hePtr = Tcl_CreateHashEntry(&pPtr->denygroup, group, &new);
    Tcl_SetHashValue(hePtr, groupPtr);
    if (!skiplocks) {
	Ns_MutexUnlock(&permlock);
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CheckPassCmd --
 *
 *	Verify a user password 
 *
 * Results:
 *	TCL result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
CheckPassCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " user pass",
			 NULL);
	return TCL_ERROR;
    }

    if (Ns_AuthorizeUser(argv[2], argv[3]) != NS_OK) {
	Tcl_AppendResult(interp, "Access denied", NULL);
	return TCL_ERROR;
    } else {
	return TCL_OK;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SetPassCmd --
 *
 *	Update a user password (expects an already-encrypted passwd)
 *
 * Results:
 *	TCL result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
SetPassCmd(Tcl_Interp *interp, int argc, char *argv[])
{
    User *userPtr;
    
    if (argc != 4) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
			 argv[0], " ", argv[1],
			 " user pass",
			 NULL);
	return TCL_ERROR;
    }

    userPtr = GetUser(argv[2]);
    if (userPtr == NULL) {
	Tcl_AppendResult(interp, "no such user", NULL);
	return TCL_ERROR;
    }
    Ns_MutexLock(&userPtr->lock);
    /*
     * There is a very small memory leak here.
     */
    userPtr->encpass = ns_strdup(argv[3]);
    Ns_MutexUnlock(&userPtr->lock);

    return TCL_OK;
}

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
 * id.c --
 *
 *      Miscellaneous IF functions.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/id.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#include <pwd.h>
#include <grp.h>

static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 * Ns_GetUserHome --
 *
 *      Get the home directory name for a user name
 *
 * Results:
 *      Return NS_TRUE if user name is found in /etc/passwd file and 
 * 	NS_FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUserHome(Ns_DString *pds, char *user)
{
    struct passwd  *pw;
    int             retcode;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw == NULL) {
        retcode = NS_FALSE;
    } else {
        Ns_DStringAppend(pds, pw->pw_dir);
        retcode = NS_TRUE;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetGid --
 *
 *      Get the group id from a group name.
 *
 * Results:
 * 	Group id or -1 if not found.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetGid(char *group)
{
    int             retcode;
    struct group   *grent;

    Ns_MutexLock(&lock);
    grent = getgrnam(group);
    if (grent == NULL) {
        retcode = -1;
    } else {
        retcode = grent->gr_gid;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUserGid --
 *
 *      Get the group id for a user name
 *
 * Results:
 *      Returns group id of the user name found in /etc/passwd or -1
 *	otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUserGid(char *user)
{
    struct passwd  *pw;
    int             retcode;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw == NULL) {
        retcode = -1;
    } else {
        retcode = pw->pw_gid;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}


/*
 *----------------------------------------------------------------------
 * Ns_GetUid --
 *
 *      Get user id for a user name.
 *
 * Results:
 *      Return NS_TRUE if user name is found in /etc/passwd file and 
 * 	NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_GetUid(char *user)
{
    struct passwd  *pw;
    int retcode;

    Ns_MutexLock(&lock);
    pw = getpwnam(user);
    if (pw == NULL) {
        retcode = -1;
    } else {
        retcode = pw->pw_uid;
    }
    Ns_MutexUnlock(&lock);

    return retcode;
}

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

/* 
 * windir.c --
 *
 *	Simple opendir/readdir/closedir implementation for Win32.
 *
 */


#include "thread.h"
#include <io.h>

/*
 * The following structure is used to maintain state during
 * an open directory search.
 */

typedef struct Search {
    long handle;
    struct _finddata_t fdata;
    struct dirent ent;
} Search;


/*
 *----------------------------------------------------------------------
 *
 * opendir --
 *
 *	Start a directory search.
 *
 * Results:
 *	Pointer to DIR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

DIR *
opendir(char *pathname)
{
    Search *sPtr;
    char pattern[PATH_MAX];

    if (strlen(pathname) > PATH_MAX - 3) {
	errno = EINVAL;
	return NULL;
    }
    sprintf(pattern, "%s/*", pathname);
    sPtr = ns_malloc(sizeof(Search));
    sPtr->handle = _findfirst(pattern, &sPtr->fdata);
    if (sPtr->handle == -1) {
	ns_free(sPtr);
	return NULL;
    }
    sPtr->ent.d_name = NULL;
    return (DIR *) sPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * closedir --
 *
 *	Closes and active directory search.
 *
 * Results:
 *	0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
closedir(DIR *dp)
{
    Search *sPtr = (Search *) dp;

    _findclose(sPtr->handle);
    ns_free(sPtr);
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * readdir --
 *
 *	Returns the next file in an active directory search.
 *
 * Results:
 *	Pointer to thread-local struct dirent.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

struct dirent  *
readdir(DIR * dp)
{
    Search *sPtr = (Search *) dp;

    if (sPtr->ent.d_name != NULL && _findnext(sPtr->handle, &sPtr->fdata) != 0) {
	return NULL;
    }
    sPtr->ent.d_name = sPtr->fdata.name;
    return &sPtr->ent;
}

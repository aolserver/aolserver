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
 * rollfile.c --
 *
 *	Routines to roll files.
 */
 
static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/rollfile.c,v 1.7 2005/07/18 23:32:12 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

typedef struct FInfo {
    time_t  	mtime;
    char        name[4];
} FInfo;

static int AppendFInfo(Ns_DString *dsPtr, char *dir, char *tail);
static int CmpFInfo(const void *p1, const void *p2);
static int Rename(char *from, char *to);
static int Exists(char *file);
static int Unlink(char *file);
 

/*
 *----------------------------------------------------------------------
 *
 * Ns_RollFile --
 *
 *	Roll the log file. When the log is rolled, it gets renamed to 
 *	filename.xyz, where 000 <= xyz <= 999. Older files have 
 *	higher numbers. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *  	If there were files: filename.000, filename.001, filename.002,
 *  	the names would end up thusly:
 *  	    filename.002 => filename.003
 *  	    filename.001 => filename.002
 *  	    filename.000 => filename.001
 *  	with nothing left named filename.000.
 *
 *----------------------------------------------------------------------
 */

int
Ns_RollFile(char *file, int max)
{
    char *first, *next, *dot;
    int   num;
    int   err;
    
    if (max < 0 || max > 999) {
        Ns_Log(Error, "rollfile: invalid max parameter '%d'; "
	       "must be > 0 and < 999", max);
	return NS_ERROR;
    }
    
    first = ns_malloc(strlen(file) + 5);
    sprintf(first, "%s.000", file);
    err = Exists(first);
    if (err > 0) {
	next = ns_strdup(first);
	num = 0;
	do {
            dot = strrchr(next, '.') + 1;
            sprintf(dot, "%03d", num++);
	} while ((err = Exists(next)) == 1 && num < max);
	num--;
	if (err == 1) {
    	    err = Unlink(next);
	}
	while (err == 0 && num-- > 0) {
            dot = strrchr(first, '.') + 1;
            sprintf(dot, "%03d", num);
            dot = strrchr(next, '.') + 1;
            sprintf(dot, "%03d", num + 1);
    	    err = Rename(first, next);
	}
	ns_free(next);
    }
    if (err == 0) {
    	err = Exists(file);
	if (err > 0) {
	    err = Rename(file, first);
	}
    }
    ns_free(first);
    
    if (err != 0) {
    	return NS_ERROR;
    }
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PurgeFiles, Ns_RollFileByDate --
 *
 *	Purge files by date, keeping max files.  The file parameter is
 *	used a basename to select files to purge.  Ns_RollFileByDate
 *	is a poorly named wrapper for historical reasons (rolling
 *	implies rotating filenames).
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	May remove (many) files.
 *
 *----------------------------------------------------------------------
 */

int
Ns_RollFileByDate(char *file, int max)
{
    return Ns_PurgeFiles(file, max);
}

int
Ns_PurgeFiles(char *file, int max)
{
    char *slash, *tail;
    DIR *dp;
    struct dirent *ent;
    FInfo **files;
    int tlen, i, nfiles, status;
    Ns_DString dir, list;
    
    status = NS_ERROR;
    Ns_DStringInit(&dir);
    Ns_DStringInit(&list);
    
    /*
     * Determine the directory component. 
     */

    Ns_NormalizePath(&dir, file);
    slash = strrchr (dir.string, '/');
    if (slash == NULL || slash[1] == '\0') {
	Ns_Log (Error, "rollfile: failed to purge files: invalid path '%s'",
		file);
    	goto err;
    }
    *slash = '\0';
    tail = slash + 1;
    tlen = strlen(tail);
    
    dp = opendir(dir.string);
    if (dp == NULL) {
    	Ns_Log(Error, "rollfile: failed to purge files:opendir(%s) failed: '%s'",
	       dir.string, strerror(errno));
	goto err;
    }
    while ((ent = ns_readdir(dp)) != NULL) {
	if (strncmp(tail, ent->d_name, (size_t)tlen) != 0) {
	    continue;
	}
    	if (!AppendFInfo(&list, dir.string, ent->d_name)) {
	    closedir(dp);
	    goto err;
	}
    }
    closedir(dp);

    nfiles = list.length / sizeof(FInfo *);
    if (nfiles >= max) {
	files = (FInfo **) list.string;
	qsort(files, (size_t)nfiles, sizeof(FInfo *), CmpFInfo);
	for (i = max; i < nfiles; ++i) {
	    if (Unlink(files[i]->name) != 0) {
	    	goto err;
	    }
	}
    }
    status = NS_OK;

err:
    nfiles = list.length / sizeof(FInfo *);
    if (nfiles > 0) {
	files = (FInfo **) list.string;
	for (i = 0; i < nfiles; ++i) {
    	    ns_free(files[i]);
	}
    }
    Ns_DStringFree(&list);
    Ns_DStringFree(&dir);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * AppendFInfo --
 *
 *	Append a file entry with mtime to the list kept in the dstring.
 *
 * Results:
 *	1 if file added, 0 otherwise.
 *
 * Side effects:
 *	Allocates memory for entry.
 *
 *----------------------------------------------------------------------
 */

static int
AppendFInfo(Ns_DString *dsPtr, char *dir, char *tail)
{
    FInfo *fPtr;
    struct stat st;
    
    fPtr = ns_malloc(sizeof(FInfo) + strlen(dir) + strlen(tail));
    sprintf(fPtr->name, "%s/%s", dir, tail);
    if (stat(fPtr->name, &st) != 0) {
    	Ns_Log(Error, "rollfile: failed to append to file '%s': '%s'",
	       fPtr->name, strerror(errno));
    	ns_free(fPtr);
	return 0;
    }
    fPtr->mtime = st.st_mtime;
    Ns_DStringNAppend(dsPtr, (char *) &fPtr, sizeof(FInfo *));
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * CmpFInfo --
 *
 *	qsort() callback to select oldest file.
 *
 * Results:
 *	Stadard qsort() result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int 
CmpFInfo(const void *arg1, const void *arg2)
{
    FInfo *f1Ptr = *((FInfo **) arg1);
    FInfo *f2Ptr = *((FInfo **) arg2);
    
    if (f1Ptr->mtime < f2Ptr->mtime) {
	return 1;
    } else if (f1Ptr->mtime > f2Ptr->mtime) {
	return -1;
    } 
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Unlink, Rename, Exists --
 *
 *	Simple wrappers used by Ns_RollFile and Ns_PurgeFiles.
 *
 * Results:
 *	System call result (except Exists).
 *
 * Side effects:
 *	May modify filesystem.
 *
 *----------------------------------------------------------------------
 */
 
static int
Unlink(char *file)
{
    int err;
    
    err = unlink(file);
    if (err != 0) {
        Ns_Log(Error, "rollfile: failed to delete file '%s': '%s'",
	       file, strerror(errno));
    }
    return err;
}

static int
Rename(char *from, char *to)
{
    int err;
    
    err = rename(from, to);
    if (err != 0) {
    	Ns_Log(Error, "rollfile: failed to rename file '%s' to '%s': '%s'",
	       from, to, strerror(errno));
    }
    return err;
}

static int
Exists(char *file)
{
    int exists;
    
    if (access(file, F_OK) == 0) {
    	exists = 1;
    } else if (errno == ENOENT) {
    	exists = 0;
    } else {
	Ns_Log(Error, "rollfile: failed to determine if file '%s' exists: '%s'",
	       file, strerror(errno));
    	exists = -1;
    }
    return exists;
}

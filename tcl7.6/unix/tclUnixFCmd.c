/*
 * tclUnixFCmd.c
 *
 *      This file implements the unix specific portion of file manipulation 
 *      subcommands of the "file" command.  All filename arguments should
 *	already be translated to native format.
 *
 * Copyright (c) 1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclUnixFCmd.c 1.16 96/12/16 13:36:01
 *
 * Portions of this code were derived from NetBSD source code which has
 * the following copyright notice:
 *
 * Copyright (c) 1988, 1993, 1994
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/tcl7.6/unix/Attic/tclUnixFCmd.c,v 1.2 2000/05/02 14:39:31 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "tclInt.h"
#include "tclPort.h"
#include <utime.h>

/*
 * The following constants specify the type of callback when
 * TraverseUnixTree() calls the traverseProc()
 */

#define DOTREE_PRED   1     /* pre-order directory  */
#define DOTREE_POSTD  2     /* post-order directory */
#define DOTREE_F      3     /* regular file */

/*
 * Prototype for the TraverseUnixTree callback function.
 */

typedef int (TraversalProc) _ANSI_ARGS_((char *src, char *dst, 
        struct stat *sb, int type, Tcl_DString *errorPtr));

/*
 * Declarations for local procedures defined in this file:
 */

static int		CopyFile _ANSI_ARGS_((char *src, char *dst, 
			    struct stat *srcStatBufPtr));
static int		CopyFileAtts _ANSI_ARGS_((char *src, char *dst, 
			    struct stat *srcStatBufPtr));
static int		TraversalCopy _ANSI_ARGS_((char *src, char *dst, 
			    struct stat *sbPtr, int type,
			    Tcl_DString *errorPtr));
static int		TraversalDelete _ANSI_ARGS_((char *src, char *dst, 
			    struct stat *sbPtr, int type,
			    Tcl_DString *errorPtr));
static int		TraverseUnixTree _ANSI_ARGS_((
			    TraversalProc *traversalProc,
			    Tcl_DString *sourcePath, Tcl_DString *destPath,
			    Tcl_DString *errorPtr));

/*
 *---------------------------------------------------------------------------
 *
 * TclpRenameFile --
 *
 *      Changes the name of an existing file or directory, from src to dst.
 *	If src and dst refer to the same file or directory, does nothing
 *	and returns success.  Otherwise if dst already exists, it will be
 *	deleted and replaced by src subject to the following conditions:
 *	    If src is a directory, dst may be an empty directory.
 *	    If src is a file, dst may be a file.
 *	In any other situation where dst already exists, the rename will
 *	fail.  
 *
 * Results:
 *	If the directory was successfully created, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR and errno is set to
 *	indicate the error.  Some possible values for errno are:
 *
 *	EACCES:     src or dst parent directory can't be read and/or written.
 *	EEXIST:	    dst is a non-empty directory.
 *	EINVAL:	    src is a root directory or dst is a subdirectory of src.
 *	EISDIR:	    dst is a directory, but src is not.
 *	ENOENT:	    src doesn't exist, or src or dst is "".
 *	ENOTDIR:    src is a directory, but dst is not.  
 *	EXDEV:	    src and dst are on different filesystems.
 *	
 * Side effects:
 *	The implementation of rename may allow cross-filesystem renames,
 *	but the caller should be prepared to emulate it with copy and
 *	delete if errno is EXDEV.
 *
 *---------------------------------------------------------------------------
 */

int
TclpRenameFile(src, dst)
    char *src;			/* Pathname of file or dir to be renamed. */
    char *dst;			/* New pathname of file or directory. */
{
    if (rename(src, dst) == 0) {
	return TCL_OK;
    }
    if (errno == ENOTEMPTY) {
	Tcl_SetErrno (EEXIST);
    }

#ifdef sparc
    /*
     * SunOS 4.1.4 reports overwriting a non-empty directory with a
     * directory as EINVAL instead of EEXIST (first rule out the correct
     * EINVAL result code for moving a directory into itself).  Must be
     * conditionally compiled because realpath() is only defined on SunOS.
     */

    if (errno == EINVAL) {
	char srcPath[MAXPATHLEN], dstPath[MAXPATHLEN];
	DIR *dirPtr;
	struct dirent *dirEntPtr;

	if ((realpath(src, srcPath) != NULL)
		&& (realpath(dst, dstPath) != NULL)
		&& (strncmp(srcPath, dstPath, strlen(srcPath)) != 0)) {
	    dirPtr = opendir(dst);
	    if (dirPtr != NULL) {
		while ((dirEntPtr = readdir(dirPtr)) != NULL) {
		    if ((strcmp(dirEntPtr->d_name, ".") != 0) &&
			    (strcmp(dirEntPtr->d_name, "..") != 0)) {
			Tcl_SetErrno (EEXIST);
			closedir(dirPtr);
			return TCL_ERROR;
		    }
		}
		closedir(dirPtr);
	    }
	}
	Tcl_SetErrno (EINVAL);
    }
#endif	/* sparc */

    if (strcmp(src, "/") == 0) {
	/*
	 * Alpha reports renaming / as EBUSY and Linux reports it as EACCES,
	 * instead of EINVAL.
	 */
	 
	Tcl_SetErrno (EINVAL);
    }

    /*
     * DEC Alpha OSF1 V3.0 returns EACCES when attempting to move a
     * file across filesystems and the parent directory of that file is
     * not writable.  Most other systems return EXDEV.  Does nothing to
     * correct this behavior.
     */

    return TCL_ERROR;
}


/*
 *---------------------------------------------------------------------------
 *
 * TclpCopyFile --
 *
 *      Copy a single file (not a directory).  If dst already exists and
 *	is not a directory, it is removed.
 *
 * Results:
 *	If the file was successfully copied, returns TCL_OK.  Otherwise
 *	the return value is TCL_ERROR and errno is set to indicate the
 *	error.  Some possible values for errno are:
 *
 *	EACCES:     src or dst parent directory can't be read and/or written.
 *	EISDIR:	    src or dst is a directory.
 *	ENOENT:	    src doesn't exist.  src or dst is "".
 *
 * Side effects:
 *      This procedure will also copy symbolic links, block, and
 *      character devices, and fifos.  For symbolic links, the links 
 *      themselves will be copied and not what they point to.  For the
 *	other special file types, the directory entry will be copied and
 *	not the contents of the device that it refers to.
 *
 *---------------------------------------------------------------------------
 */

int 
TclpCopyFile(src, dst)
    char *src;			/* Pathname of file to be copied. */
    char *dst;			/* Pathname of file to copy to. */
{
    struct stat srcStatBuf, dstStatBuf;
    char link[MAXPATHLEN];
    int length;

    /*
     * Have to do a stat() to determine the filetype.
     */
    
    if (lstat(src, &srcStatBuf) != 0) {
	return TCL_ERROR;
    }
    if (S_ISDIR(srcStatBuf.st_mode)) {
	Tcl_SetErrno (EISDIR);
	return TCL_ERROR;
    }

    /*
     * symlink, and some of the other calls will fail if the target 
     * exists, so we remove it first
     */
    
    if (lstat(dst, &dstStatBuf) == 0) {
	if (S_ISDIR(dstStatBuf.st_mode)) {
	    Tcl_SetErrno (EISDIR);
	    return TCL_ERROR;
	}
    }
    if (unlink(dst) != 0) {
	if (errno != ENOENT) {
	    return TCL_ERROR;
	} 
    }

    switch ((int) (srcStatBuf.st_mode & S_IFMT)) {
        case S_IFLNK:
	    length = readlink(src, link, sizeof(link)); 
	    if (length == -1) {
		return TCL_ERROR;
	    }
	    link[length] = '\0';
	    if (symlink(link, dst) < 0) {
		return TCL_ERROR;
	    }
	    return TCL_OK;

        case S_IFBLK:
        case S_IFCHR:
	    if (mknod(dst, srcStatBuf.st_mode, srcStatBuf.st_rdev) < 0) {
		return TCL_ERROR;
	    }
	    return CopyFileAtts(src, dst, &srcStatBuf);

        case S_IFIFO:
	    if (mkfifo(dst, srcStatBuf.st_mode) < 0) {
		return TCL_ERROR;
	    }
	    return CopyFileAtts(src, dst, &srcStatBuf);

        default:
	    return CopyFile(src, dst, &srcStatBuf);
    }
    
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CopyFile - 
 *
 *      Helper function for TclpCopyFile.  Copies one regular file,
 *	using read() and write().
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *      A file is copied.  Dst will be overwritten if it exists.
 *
 *----------------------------------------------------------------------
 */

static int 
CopyFile(src, dst, srcStatBufPtr) 
    char *src;                   /* Pathname of file to copy. */
    char *dst;                   /* Pathname of file to create/overwrite. */
    struct stat *srcStatBufPtr;  /* Used to determine mode and blocksize */
{
    int srcFd;
    int dstFd;
    u_int blockSize;   /* Optimal I/O blocksize for filesystem */
    char *buffer;      /* Data buffer for copy */
    size_t nread;

    if ((srcFd = open(src, O_RDONLY, 0)) < 0) { 
	return TCL_ERROR;
    }

    dstFd = open(dst, O_CREAT | O_TRUNC | O_WRONLY, srcStatBufPtr->st_mode);
    if (dstFd < 0) {
	close(srcFd); 
	return TCL_ERROR;
    }

    blockSize = srcStatBufPtr->st_blksize;
    buffer = ckalloc(blockSize);
    while (1) {
	nread = read(srcFd, buffer, blockSize);
	if ((nread == -1) || (nread == 0)) {
	    break;
	}
	if (write(dstFd, buffer, nread) != nread) {
	    nread = (size_t) -1;
	    break;
	}
    }
	
    ckfree(buffer);
    close(srcFd);
    if ((close(dstFd) != 0) || (nread == -1)) {
	unlink(dst);
	return TCL_ERROR;
    }
    if (CopyFileAtts(src, dst, srcStatBufPtr) == TCL_ERROR) {
	/*
	 * The copy succeeded, but setting the permissions failed, so be in
	 * a consistent state, we remove the file that was created by the
	 * copy.
	 */

	unlink(dst);
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpDeleteFile --
 *
 *      Removes a single file (not a directory).
 *
 * Results:
 *	If the file was successfully deleted, returns TCL_OK.  Otherwise
 *	the return value is TCL_ERROR and errno is set to indicate the
 *	error.  Some possible values for errno are:
 *
 *	EACCES:     a parent directory can't be read and/or written.
 *	EISDIR:	    path is a directory.
 *	ENOENT:	    path doesn't exist or is "".
 *
 * Side effects:
 *      The file is deleted, even if it is read-only.
 *
 *---------------------------------------------------------------------------
 */

int
TclpDeleteFile(path) 
    char *path;			/* Pathname of file to be removed. */
{
    if (unlink(path) != 0) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpCreateDirectory --
 *
 *      Creates the specified directory.  All parent directories of the
 *	specified directory must already exist.  The directory is
 *	automatically created with permissions so that user can access
 *	the new directory and create new files or subdirectories in it.
 *
 * Results:
 *	If the directory was successfully created, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR and errno is set to
 *	indicate the error.  Some possible values for errno are:
 *
 *	EACCES:     a parent directory can't be read and/or written.
 *	EEXIST:	    path already exists.
 *	ENOENT:	    a parent directory doesn't exist.
 *
 * Side effects:
 *      A directory is created with the current umask, except that
 *	permission for u+rwx will always be added.
 *
 *---------------------------------------------------------------------------
 */

int
TclpCreateDirectory(path)
    char *path;			/* Pathname of directory to create. */
{
    mode_t mode;

    mode = umask(0);
    umask(mode);

    /*
     * umask return value is actually the inverse of the permissions.
     */
    
    mode = (0777 & ~mode);

    if (mkdir(path, mode | S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpCopyDirectory --
 *
 *      Recursively copies a directory.  The target directory dst must
 *	not already exist.  Note that this function does not merge two
 *	directory hierarchies, even if the target directory is an an
 *	empty directory.
 *
 * Results:
 *	If the directory was successfully copied, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR, errno is set to indicate
 *	the error, and the pathname of the file that caused the error
 *	is stored in errorPtr.  See TclpCreateDirectory and TclpCopyFile
 *	for a description of possible values for errno.
 *
 * Side effects:
 *      An exact copy of the directory hierarchy src will be created
 *	with the name dst.  If an error occurs, the error will
 *      be returned immediately, and remaining files will not be
 *	processed.
 *
 *---------------------------------------------------------------------------
 */

int
TclpCopyDirectory(src, dst, errorPtr)
    char *src;			/* Pathname of directory to be copied.  */
    char *dst;			/* Pathname of target directory. */
    Tcl_DString *errorPtr;	/* If non-NULL, initialized DString for
				 * error reporting. */
{
    int result;
    Tcl_DString srcBuffer;
    Tcl_DString dstBuffer;

    Tcl_DStringInit(&srcBuffer);
    Tcl_DStringInit(&dstBuffer);
    Tcl_DStringAppend(&srcBuffer, src, -1);
    Tcl_DStringAppend(&dstBuffer, dst, -1);
    result = TraverseUnixTree(TraversalCopy, &srcBuffer, &dstBuffer,
	    errorPtr);
    Tcl_DStringFree(&srcBuffer);
    Tcl_DStringFree(&dstBuffer);
    return result;
}

/*
 *---------------------------------------------------------------------------
 *
 * TclpRemoveDirectory --
 *
 *	Removes directory (and its contents, if the recursive flag is set).
 *
 * Results:
 *	If the directory was successfully removed, returns TCL_OK.
 *	Otherwise the return value is TCL_ERROR, errno is set to indicate
 *	the error, and the pathname of the file that caused the error
 *	is stored in errorPtr.  Some possible values for errno are:
 *
 *	EACCES:     path directory can't be read and/or written.
 *	EEXIST:	    path is a non-empty directory.
 *	EINVAL:	    path is a root directory.
 *	ENOENT:	    path doesn't exist or is "".
 * 	ENOTDIR:    path is not a directory.
 *
 * Side effects:
 *	Directory removed.  If an error occurs, the error will be returned
 *	immediately, and remaining files will not be deleted.
 *
 *---------------------------------------------------------------------------
 */
 
int
TclpRemoveDirectory(path, recursive, errorPtr) 
    char *path;			/* Pathname of directory to be removed. */
    int recursive;		/* If non-zero, removes directories that
				 * are nonempty.  Otherwise, will only remove
				 * empty directories. */
    Tcl_DString *errorPtr;	/* If non-NULL, initialized DString for
				 * error reporting. */
{
    int result;
    Tcl_DString buffer;

    if (rmdir(path) == 0) {
	return TCL_OK;
    }
    if (((errno != EEXIST) && (errno != ENOTEMPTY)) || (recursive == 0)) {
	if (errorPtr != NULL) {
	    Tcl_DStringAppend(errorPtr, path, -1);
	}
	return TCL_ERROR;
    }
    
    /*
     * The directory is nonempty, but the recursive flag has been
     * specified, so we recursively remove all the files in the directory.
     */

    Tcl_DStringInit(&buffer);
    Tcl_DStringAppend(&buffer, path, -1);
    result = TraverseUnixTree(TraversalDelete, &buffer, NULL, errorPtr);
    Tcl_DStringFree(&buffer);
    return result;
}
	
/*
 *---------------------------------------------------------------------------
 *
 * TraverseUnixTree --
 *
 *      Traverse directory tree specified by sourcePtr, calling the function 
 *	traverseProc for each file and directory encountered.  If destPtr 
 *	is non-null, each of name in the sourcePtr directory is appended to 
 *	the directory specified by destPtr and passed as the second argument 
 *	to traverseProc() .
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      None caused by TraverseUnixTree, however the user specified 
 *	traverseProc() may change state.  If an error occurs, the error will
 *      be returned immediately, and remaining files will not be processed.
 *
 *---------------------------------------------------------------------------
 */

static int 
TraverseUnixTree(traverseProc, sourcePtr, targetPtr, errorPtr)
    TraversalProc *traverseProc;/* Function to call for every file and
				 * directory in source hierarchy. */
    Tcl_DString *sourcePtr;	/* Pathname of source directory to be
				 * traversed. */
    Tcl_DString *targetPtr;	/* Pathname of directory to traverse in
				 * parallel with source directory. */
    Tcl_DString *errorPtr;	/* If non-NULL, an initialized DString for
				 * error reporting. */
{
    struct stat statbuf;
    char *source, *target, *errfile;
    int result, sourceLen;
    int targetLen = 0;		/* Initialization needed only to prevent
				 * warning in gcc. */
    struct dirent *dirp;
    DIR *dp;

    if (Ns_CheckStack() != NS_OK) {
	/* NB: errorPtr not designed for this but it's better than nothing. */
	if (errorPtr != NULL) {
	    Tcl_DStringAppend(errorPtr, "(stack overflow)", -1);
	}
	return TCL_ERROR;
    }

    result = TCL_OK;
    source = Tcl_DStringValue(sourcePtr);
    if (targetPtr != NULL) {
	target = Tcl_DStringValue(targetPtr);
    } else {
	target = NULL;
    }

    errfile = NULL;
    if (lstat(source, &statbuf) != 0) {
	errfile = source;
	goto end;
    }
    if (!S_ISDIR(statbuf.st_mode)) {
	/*
	 * Process the regular file
	 */

	return (*traverseProc)(source, target, &statbuf, DOTREE_F, errorPtr);
    }

    dp = opendir(source);
    if (dp == NULL) {
	/* 
	 * Can't read directory
	 */

	errfile = source;
	goto end;
    }
    result = (*traverseProc)(source, target, &statbuf, DOTREE_PRED, errorPtr);
    if (result != TCL_OK) {
	closedir(dp);
	return result;
    }
    
    Tcl_DStringAppend(sourcePtr, "/", 1);
    source = Tcl_DStringValue(sourcePtr);
    sourceLen = Tcl_DStringLength(sourcePtr);	

    if (targetPtr != NULL) {
	Tcl_DStringAppend(targetPtr, "/", 1);
	target = Tcl_DStringValue(targetPtr);
	targetLen = Tcl_DStringLength(targetPtr);
    }
				  
    while ((dirp = readdir(dp)) != NULL) {
	if ((strcmp(dirp->d_name, ".") == 0)
	        || (strcmp(dirp->d_name, "..") == 0)) {
	    continue;
	}

	/* 
	 * Append name after slash, and recurse on the file.
	 */

	Tcl_DStringAppend(sourcePtr, dirp->d_name, -1);
	if (targetPtr != NULL) {
	    Tcl_DStringAppend(targetPtr, dirp->d_name, -1);
	}
	result = TraverseUnixTree(traverseProc, sourcePtr, targetPtr,
		errorPtr);
	if (result != TCL_OK) {
	    break;
	}
	
	/*
	 * Remove name after slash.
	 */

	Tcl_DStringSetLength(sourcePtr, sourceLen);
	if (targetPtr != NULL) {
	    Tcl_DStringSetLength(targetPtr, targetLen);
	}
    }
    closedir(dp);
    
    /*
     * Strip off the trailing slash we added
     */

    Tcl_DStringSetLength(sourcePtr, sourceLen - 1);
    source = Tcl_DStringValue(sourcePtr);
    if (targetPtr != NULL) {
	Tcl_DStringSetLength(targetPtr, targetLen - 1);
	target = Tcl_DStringValue(targetPtr);
    }

    if (result == TCL_OK) {
	/*
	 * Call traverseProc() on a directory after visiting all the
	 * files in that directory.
	 */

	result = (*traverseProc)(source, target, &statbuf, DOTREE_POSTD,
		errorPtr);
    }
    end:
    if (errfile != NULL) {
	if (errorPtr != NULL) {
	    Tcl_DStringAppend(errorPtr, errfile, -1);
	}
	result = TCL_ERROR;
    }
	    
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * TraversalCopy
 *
 *      Called from TraverseUnixTree in order to execute a recursive copy of a 
 *      directory. 
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      The file or directory src may be copied to dst, depending on 
 *      the value of type.
 *      
 *----------------------------------------------------------------------
 */

static int 
TraversalCopy(src, dst, sbPtr, type, errorPtr) 
    char *src;			/* Source pathname to copy. */
    char *dst;                  /* Destination pathname of copy. */
    struct stat *sbPtr;		/* Stat info for file specified by src. */
    int type;                   /* Reason for call - see TraverseUnixTree(). */
    Tcl_DString *errorPtr;	/* If non-NULL, initialized DString for
				 * error return. */
{
    switch (type) {
	case DOTREE_F:
	    if (TclpCopyFile(src, dst) == TCL_OK) {
		return TCL_OK;
	    }
	    break;

	case DOTREE_PRED:
	    if (TclpCreateDirectory(dst) == TCL_OK) {
		return TCL_OK;
	    }
	    break;

	case DOTREE_POSTD:
	    if (CopyFileAtts(src, dst, sbPtr) == TCL_OK) {
		return TCL_OK;
	    }
	    break;

    }

    /*
     * There shouldn't be a problem with src, because we already
     * checked it to get here.
     */

    if (errorPtr != NULL) {
	Tcl_DStringAppend(errorPtr, dst, -1);
    }
    return TCL_ERROR;
}

/*
 *---------------------------------------------------------------------------
 *
 * TraversalDelete --
 *
 *      Called by procedure TraverseUnixTree for every file and directory
 *	that it encounters in a directory hierarchy. This procedure unlinks
 *      files, and removes directories after all the containing files 
 *      have been processed.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      Files or directory specified by src will be deleted.
 *
 *----------------------------------------------------------------------
 */

static int
TraversalDelete(src, ignore, sbPtr, type, errorPtr) 
    char *src;			/* Source pathname. */
    char *ignore;		/* Destination pathname (not used). */
    struct stat *sbPtr;		/* Stat info for file specified by src. */
    int type;                   /* Reason for call - see TraverseUnixTree(). */
    Tcl_DString *errorPtr;	/* If non-NULL, initialized DString for
				 * error return. */
{
    switch (type) {
        case DOTREE_F:
	    if (unlink(src) == 0) {
		return TCL_OK;
	    }
	    break;

        case DOTREE_PRED:
	    return TCL_OK;

        case DOTREE_POSTD:
	    if (rmdir(src) == 0) {
		return TCL_OK;
	    }
	    break;
	    
    }

    if (errorPtr != NULL) {
	Tcl_DStringAppend(errorPtr, src, -1);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * CopyFileAtts
 *
 *      Copy the file attributes such as owner, group, permissions, and
 *      modification date from one file to another.
 *
 * Results:
 *      Standard Tcl result.
 *
 * Side effects:
 *      user id, group id, permission bits, last modification time, and 
 *      last access time are updated in the new file to reflect the old
 *      file.
 *      
 *----------------------------------------------------------------------
 */
static int
CopyFileAtts(src, dst, statBufPtr) 
    char *src;                 /* Path name of source file */
    char *dst;                 /* Path name of target file */
    struct stat *statBufPtr;   /* ptr to stat info for source file */
{
    struct utimbuf tval;
    mode_t newMode;
    
    newMode = statBufPtr->st_mode & (S_ISUID | S_ISGID | S_IRWXU | S_IRWXG | S_IRWXO);
	
    /*
     * On some systems chown will always fail for a non-root user unless
     * POSIX_CHOWN_RESTRICTED is not set.  Others will succeed as long as 
     * you don't try to chown a file to someone besides youself.
     */
    
    if (chown(dst, statBufPtr->st_uid, statBufPtr->st_gid)) {
	if (errno != EPERM) {
	    return TCL_ERROR;
	}
    }

    /* 
     * Note that if you copy a setuid file that is owned by someone
     * else, and you are not root, then the copy will be setuid to you.
     * The most correct implementation would probably be to have the
     * copy not setuid to anyone if the original file was owned by 
     * someone else, but this corner case isn't currently handled.
     * It would require another lstat(), or getuid().
     */
    
    if (chmod(dst, newMode)) {
	newMode &= ~(S_ISUID | S_ISGID);
	if (chmod(dst, newMode)) {
	    return TCL_ERROR;
	}
    }

    tval.actime = statBufPtr->st_atime; 
    tval.modtime = statBufPtr->st_mtime; 

    if (utime(dst, &tval)) {
	return TCL_ERROR;
    }
    return TCL_OK;
}

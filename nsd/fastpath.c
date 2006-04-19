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
 * fastpath.c --
 *
 *      Get page possibly from a file cache.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/fastpath.c,v 1.26 2006/04/19 17:48:47 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following constants are defined for this file
 */

#ifdef MAP_FAILED
#undef MAP_FAILED 
#endif
#define MAP_FAILED ((void *) (-1))

/*
 * The following structure defines the contents of a file
 * stored in the file cache.
 */

typedef struct {
    time_t mtime;
    int size;
    int refcnt;
    char bytes[1];	/* Grown to actual file size. */
} File;

/*
 * Local functions defined in this file
 */

static Ns_Callback FreeEntry;
static void DecrEntry(File *);
static int UrlIs(char *server, char *url, int dir);
static int FastStat(char *file, struct stat *stPtr);
static int FastReturn(NsServer *servPtr, Ns_Conn *conn, int status,
    char *type, char *file, struct stat *stPtr);


/*
 *----------------------------------------------------------------------
 * NsFastpathCache --
 *
 *	Initialize the fastpath cache.
 *
 * Results:
 *	Pointer to Ns_Cache.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Ns_Cache *
NsFastpathCache(char *server, int size)
{
    Ns_DString ds;
    Ns_Cache *fpCache;
    int keys;

#ifdef _WIN32
    keys = TCL_STRING_KEYS;
#else
    keys = FILE_KEYS;
#endif
    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "nsfp:", server, NULL);
    fpCache = Ns_CacheCreateSz(ds.string, keys, (size_t) size, FreeEntry);
    Ns_DStringFree(&ds);
    return fpCache;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ConnReturnFile --
 *
 *	Send the contents of a file out the conn. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	See FastReturn.
 *
 *----------------------------------------------------------------------
 */

int
Ns_ConnReturnFile(Ns_Conn *conn, int status, char *type, char *file)
{
    struct stat st;
    char *server;
    NsServer *servPtr;
    
    if (!FastStat(file, &st)) {
    	return Ns_ConnReturnNotFound(conn);
    }
    server = Ns_ConnServer(conn);
    servPtr = NsGetServer(server);
    return FastReturn(servPtr, conn, status, type, file, &st);
}


/*
 *----------------------------------------------------------------------
 * Ns_PageRoot --
 *
 *      Return path name of the server pages directory.
 *
 * Results:
 *      Server pageroot or NULL on invalid server.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_PageRoot(char *server)
{
    NsServer *servPtr = NsGetServer(server);
    
    return servPtr->fastpath.pageroot;
}


/*
 *----------------------------------------------------------------------
 * Ns_SetUrlToFileProc --
 *
 *      Set pointer to custom routine that acts like Ns_UrlToFile();
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_SetUrlToFileProc(char *server, Ns_UrlToFileProc *procPtr)
{
    NsServer *servPtr = NsGetServer(server);

    servPtr->fastpath.url2file = procPtr;
}


/*
 *----------------------------------------------------------------------
 * Ns_UrlToFile --
 *
 *      Construct the filename that corresponds to a URL.
 *
 * Results:
 *      Return NS_OK on success or NS_ERROR on failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */


int
Ns_UrlToFile(Ns_DString *dsPtr, char *server, char *url)
{
    NsServer *servPtr = NsGetServer(server);
    
    return NsUrlToFile(dsPtr, servPtr, url);
}


/*
 *----------------------------------------------------------------------
 * Ns_UrlIsFile, Ns_UrlIsDir --
 *
 *      Check if a file/directory that corresponds to a URL exists.
 *
 * Results:
 *      Return NS_TRUE if the file exists and NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_UrlIsFile(char *server, char *url)
{
    return UrlIs(server, url, 0);
}

int
Ns_UrlIsDir(char *server, char *url)
{
    return UrlIs(server, url, 1);
}

static int
UrlIs(char *server, char *url, int dir)
{
    Ns_DString ds;
    int is = NS_FALSE;
    struct stat st;

    Ns_DStringInit(&ds);
    if (Ns_UrlToFile(&ds, server, url) == NS_OK &&
	(stat(ds.string, &st) == 0) &&
	((dir && S_ISDIR(st.st_mode)) ||
	    (dir == NS_FALSE && S_ISREG(st.st_mode)))) {
	is = NS_TRUE;
    }
    Ns_DStringFree(&ds);
    return is;
}


/*
 *----------------------------------------------------------------------
 * FastGetRestart --
 *
 *	Construct the full URL and redirect internally.
 *
 * Results:
 *      See Ns_ConnRedirect().
 *
 * Side effects:
 *      See Ns_ConnRedirect().
 *
 *----------------------------------------------------------------------
 */

static int
FastGetRestart(Ns_Conn *conn, char *page)
{
    int		    status;
    Ns_DString      ds;

    Ns_DStringInit(&ds);
    Ns_MakePath(&ds, conn->request->url, page, NULL);
    status = Ns_ConnRedirect(conn, ds.string);
    Ns_DStringFree(&ds);

    return status;
}


/*
 *----------------------------------------------------------------------
 * Ns_FastPathOp --
 *
 *      Return the contents of a URL.
 *
 * Results:
 *      Return NS_OK for success or NS_ERROR for failure.
 *
 * Side effects:
 *      Contents of file may be cached in file cache.
 *
 *----------------------------------------------------------------------
 */

int
Ns_FastPathOp(void *arg, Ns_Conn *conn)
{
    Conn	   *connPtr = (Conn *) conn;
    NsServer  	   *servPtr = connPtr->servPtr;
    char	   *url = conn->request->url;
    Ns_DString      ds;
    struct stat	    st;
    int             result, i;

    Ns_DStringInit(&ds);
    if (NsUrlToFile(&ds, servPtr, url) != NS_OK
    	    || !FastStat(ds.string, &st)) {
	goto notfound;
    }
    if (S_ISREG(st.st_mode)) {
	/*
	 * Return ordinary files as with Ns_ConnReturnFile.
	 */

	result = FastReturn(servPtr, conn, 200, NULL, ds.string, &st);

    } else if (S_ISDIR(st.st_mode)) {
	/*
	 * For directories, search for a matching directory file and 
	 * restart the connection if found.
	 */

    	for (i = 0; i < servPtr->fastpath.dirc; ++i) {
	    Ns_DStringTrunc(&ds, 0);
	    if (NsUrlToFile(&ds, servPtr, url) != NS_OK) {
		goto notfound;
	    }
	    Ns_DStringVarAppend(&ds, "/", servPtr->fastpath.dirv[i], NULL);
            if ((stat(ds.string, &st) == 0) && S_ISREG(st.st_mode)) {
                if (url[strlen(url) - 1] != '/') {
                    Ns_DStringTrunc(&ds, 0);
                    Ns_DStringVarAppend(&ds, url, "/", NULL);
                    result = Ns_ConnReturnRedirect(conn, ds.string);
                } else {
                    result = FastGetRestart(conn, servPtr->fastpath.dirv[i]);
                }
                goto done;
            }
        }

    	/*
	 * If no index file was found, invoke a directory listing
	 * ADP or Tcl proc if configured.
	 */
	 
	if (servPtr->fastpath.diradp != NULL) {
	    result = Ns_AdpRequest(conn, servPtr->fastpath.diradp);
	} else if (servPtr->fastpath.dirproc != NULL) {
	    result = Ns_TclRequest(conn, servPtr->fastpath.dirproc);
	} else {
	    goto notfound;
	}
    } else {
 notfound:
	result = Ns_ConnReturnNotFound(conn);
    }

 done:
    Ns_DStringFree(&ds);
    return result;
}


/*
 *----------------------------------------------------------------------
 * FreeEntry --
 *
 *      Logically remove a cached file from file cache.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
FreeEntry(void *arg)
{
    File *filePtr = (File *) arg;

    DecrEntry(filePtr);
}


/*
 *----------------------------------------------------------------------
 * DecrEntry --
 *
 *      Decrement reference count of cached file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
DecrEntry(File *filePtr)
{
    if (--filePtr->refcnt == 0) {
	ns_free(filePtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FastStat --
 *
 *      Stat a file, logging an error on unexpected results.
 *
 * Results:
 *      1 if stat ok, 0 otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
FastStat(char *file, struct stat *stPtr)
{
    if (stat(file, stPtr) != 0) {
	if (errno != ENOENT && errno != EACCES) {
	    Ns_Log(Error, "fastpath: stat(%s) failed: %s",
		   file, strerror(errno));
	}
	return 0;
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * FastReturn --
 *
 *      Return an open file, possibly from cache.
 *
 * Results:
 *      Standard Ns_Request result.
 *
 * Side effects:
 *      May map, cache, open, and/or send file out connection.
 *
 *----------------------------------------------------------------------
 */

static int
FastReturn(NsServer *servPtr, Ns_Conn *conn, int status,
    char *type, char *file, struct stat *stPtr)
{
    int             result = NS_ERROR, fd, new, nread;
    File	   *filePtr;
    char	   *key;
    Ns_Entry	   *entPtr;
    void           *map, *arg;
#ifndef _WIN32
    FileKey	    ukey;
#endif

    /*
     * Determine the mime type if not given.
     */
     
    if (type == NULL) {
    	type = Ns_GetMimeType(file);
    }
    
    /*
     * Set the last modified header if not set yet and, if not
     * modified since last request, return now.
     */
     
    Ns_ConnSetLastModifiedHeader(conn, &stPtr->st_mtime);
    if (Ns_ConnModifiedSince(conn, stPtr->st_mtime) == NS_FALSE) {
	return Ns_ConnReturnNotModified(conn);
    }
    
    /*
     * For no output (i.e., HEAD request), just send required
     * headers.
     */
     
    if (conn->flags & NS_CONN_SKIPBODY) {
	Ns_ConnSetRequiredHeaders(conn, type, (int) stPtr->st_size);
	return Ns_ConnFlushHeaders(conn, status);
    }

    if (servPtr->fastpath.cache == NULL
    	    || stPtr->st_size > servPtr->fastpath.cachemaxentry) {
	/*
	 * Caching is disabled or the entry is too large for the cache
	 * so just open, mmap, and send the content directly.
	 */

    	fd = open(file, O_RDONLY|O_BINARY);
    	if (fd < 0) {
	    Ns_Log(Warning, "fastpath: open(%s) failed: %s",
		   file, strerror(errno));
	    goto notfound;
	}
	if (servPtr->fastpath.mmap) {
	    map = NsMap(fd, 0, stPtr->st_size, 0, &arg);
	    if (map != NULL) {
	    	close(fd);
	    	fd = -1;
            	result = Ns_ConnReturnData(conn, status, map,
					   (int) stPtr->st_size, type);
		NsUnMap(map, arg);
	    }
	}
	if (fd != -1) {
            result = Ns_ConnReturnOpenFd(conn, status, type, fd,
					 (int) stPtr->st_size);
	    close(fd);
	}

    } else {
	/*
	 * Search for an existing cache entry for this file, validating 
	 * the contents against the current file mtime and size.
	 */

#ifdef _WIN32
	key = file;
#else
	ukey.dev = stPtr->st_dev;
	ukey.ino = stPtr->st_ino;
	key = (char *) &ukey;
#endif
	filePtr = NULL;
	Ns_CacheLock(servPtr->fastpath.cache);
	entPtr = Ns_CacheCreateEntry(servPtr->fastpath.cache, key, &new);
	if (!new) {
	    while (entPtr != NULL &&
		   (filePtr = Ns_CacheGetValue(entPtr)) == NULL) {
		Ns_CacheWait(servPtr->fastpath.cache);
		entPtr = Ns_CacheFindEntry(servPtr->fastpath.cache, key);
	    }
	    if (filePtr != NULL &&
		    (filePtr->mtime != stPtr->st_mtime ||
		     filePtr->size != stPtr->st_size)) {
		Ns_CacheUnsetValue(entPtr);
		new = 1;
	    }
	}
	if (new) {
	    /*
	     * Read and cache new or invalidated entries in one big chunk.
	     */

	    Ns_CacheUnlock(servPtr->fastpath.cache);
	    fd = open(file, O_RDONLY|O_BINARY);
	    if (fd < 0) {
	    	filePtr = NULL;
	        Ns_Log(Warning, "fastpath: failed to open '%s': '%s'",
		       file, strerror(errno));
	    } else {
		filePtr = ns_malloc(sizeof(File) + (size_t) stPtr->st_size);
		filePtr->refcnt = 1;
		filePtr->size = stPtr->st_size;
		filePtr->mtime = stPtr->st_mtime;
		nread = read(fd, filePtr->bytes, (size_t)filePtr->size);
		close(fd);
		if (nread != filePtr->size) {
	    	    Ns_Log(Warning, "fastpath: failed to read '%s': '%s'",
			   file, strerror(errno));
		    ns_free(filePtr);
		    filePtr = NULL;
		}
	    }
	    Ns_CacheLock(servPtr->fastpath.cache);
	    entPtr = Ns_CacheCreateEntry(servPtr->fastpath.cache, key, &new);
	    if (filePtr != NULL) {
		Ns_CacheSetValueSz(entPtr, filePtr, (size_t)filePtr->size);
	    } else {
		Ns_CacheFlushEntry(entPtr);
	    }
	    Ns_CacheBroadcast(servPtr->fastpath.cache);
	}
	if (filePtr != NULL) {
	    ++filePtr->refcnt;
	    Ns_CacheUnlock(servPtr->fastpath.cache);
            result = Ns_ConnReturnData(conn, status, filePtr->bytes,
			    filePtr->size, type);
	    Ns_CacheLock(servPtr->fastpath.cache);
	    DecrEntry(filePtr);
	}
	Ns_CacheUnlock(servPtr->fastpath.cache);
	if (filePtr == NULL) {
	    goto notfound;
	}
    }
    return result;

notfound:
    return Ns_ConnReturnNotFound(conn);
}


int
NsUrlToFile(Ns_DString *dsPtr, NsServer *servPtr, char *url)
{
    int status;

    if (servPtr->fastpath.url2file != NULL) {
	status = (*servPtr->fastpath.url2file)(dsPtr, servPtr->server, url);
    } else {
	Ns_MakePath(dsPtr, servPtr->fastpath.pageroot, url, NULL);
	status = NS_OK;
    }
    if (status == NS_OK) {
	while (dsPtr->length > 0 && dsPtr->string[dsPtr->length-1] == '/') {
	    Ns_DStringTrunc(dsPtr, dsPtr->length-1);
	}
    }
    return status;
}

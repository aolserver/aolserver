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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/fastpath.c,v 1.3 2000/08/02 23:38:25 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"
#ifndef WIN32
#include <sys/mman.h>
#endif

/*
 * The following constants are defined for this file
 */

#ifdef MAP_FAILED
#undef MAP_FAILED 
#endif
#define MAP_FAILED ((void *) (-1))

#define CONFIG_INDEX    "DirectoryFile"
#define CONFIG_PAGES    "PageRoot"
#define DEFAULT_PAGES   "pages"

/*
 * The following structure defines the key to indentify a 
 * in the file cache.
 */

typedef struct {
    dev_t dev;
    ino_t ino;
} Key;

/*
 * The following structure defines the contents of a file
 * stored in the file cache.
 */

typedef struct {
    time_t mtime;
    int size;
    int refcnt;
    char bytes[1];	/* really char bytes[size]; file contents is copied
			 * here */
} File;

/*
 * Local functions defined in this file
 */

static int FastGet(void *ignored, Ns_Conn *conn);
static Ns_Callback FreeEntry;
static void DecrEntry(File *);
static int UrlIs(char *server, char *url, int dir);

/*
 * Static variables defined in this file
 */

static Ns_UrlToFileProc *url2filePtr;
static Ns_Cache 	*cachePtr;	/* the file cache */


/*
 *----------------------------------------------------------------------
 * Ns_PageRoot --
 *
 *      Return path name of the AOLserver pages directory.
 *
 * Results:
 *      Return path name of the AOLserver pages directory.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_PageRoot(char *ignored)
{
    return nsconf.fastpath.pageroot;
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
    url2filePtr = procPtr;
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
    int status;

    if (url2filePtr != NULL) {
	status = (*url2filePtr)(dsPtr, server, url);
    } else {
	Ns_MakePath(dsPtr, nsconf.fastpath.pageroot, url, NULL);
	status = NS_OK;
    }
    if (status == NS_OK) {
	while (dsPtr->length > 0 && dsPtr->string[dsPtr->length-1] == '/') {
	    Ns_DStringTrunc(dsPtr, dsPtr->length-1);
	}
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 * Ns_UrlIsFile --
 *
 *      Check if a file that corresponds to a URL exists.
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


/*
 *----------------------------------------------------------------------
 * Ns_UrlIsDir --
 *
 *      Check if a directory that corresponds to a URL exists
 *
 * Results:
 *      Return NS_TRUE if the directory exists and NS_FALSE otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_UrlIsDir(char *server, char *url)
{
    return UrlIs(server, url, 1);
}


/*
 *----------------------------------------------------------------------
 * NsInitFastpath --
 *
 *      Initialization function called at server start up.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      File cache may be created.
 *
 *----------------------------------------------------------------------
 */

void
NsInitFastpath(void)
{
    if (nsconf.fastpath.cache) {
	cachePtr = Ns_CacheCreateSz("ns_fastpath", sizeof(Key) / sizeof(int),
				    nsconf.fastpath.cachesize, FreeEntry);
    }
    Ns_RegisterRequest(NULL, "GET", "/", FastGet, NULL, NULL, 0);
    Ns_RegisterRequest(NULL, "HEAD", "/", FastGet, NULL, NULL, 0);
    Ns_RegisterRequest(NULL, "POST", "/", FastGet, NULL, NULL, 0);
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
 * FastGet --
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

static int
FastGet(void *ignored, Ns_Conn *conn)
{
    Ns_DString      ds;
    char           *type;
    char	   *url = conn->request->url;
    int             result, fd, new, nread, i;
    struct stat	    st;
    File	   *filePtr;
    Key		    key;
    Ns_Entry	   *entPtr;
#ifndef WIN32
    char *map;
#endif

    Ns_DStringInit(&ds);

    if (Ns_UrlToFile(&ds, nsServer, url) != NS_OK) {
	goto notfound;
    }

    if (stat(ds.string, &st) != 0) {
	if (errno != ENOENT) {
	    Ns_Log(Error, "FastGet: stat(%s) failed: %s",
		   ds.string, strerror(errno));
	}
	goto notfound;
    }

    if (S_ISREG(st.st_mode)) {
	Ns_ConnSetLastModifiedHeader(conn, &st.st_mtime);
    	if (Ns_ConnModifiedSince(conn, st.st_mtime) == NS_FALSE) {
	    result = Ns_ReturnNotModified(conn);
	    goto done;
	}
    	type = Ns_GetMimeType(ds.string);
	if (conn->flags & NS_CONN_SKIPBODY) {
	    Ns_HeadersRequired(conn, type, st.st_size);
	    result = Ns_ConnFlushHeaders(conn, 200);
	    goto done;
	}


	if (nsconf.fastpath.cache == NS_FALSE || st.st_size > nsconf.fastpath.cachemaxentry) {

	    /*
	     * Caching is disabled or the entry is too large for the cache
	     * so just open, mmap, and send the content directly.
	     */

    	    fd = open(ds.string, O_RDONLY|O_BINARY);
    	    if (fd < 0) {
	        Ns_Log(Warning, "FastGet: open(%s) failed: %s",
		       ds.string, strerror(errno));
	     	goto notfound;
	    }

#ifdef WIN32
            result = Ns_ConnReturnOpenFd(conn, 200, type, fd, st.st_size);
	    close(fd);
#else
	    if (nsconf.fastpath.mmap && (map = mmap(0, st.st_size, PROT_READ, MAP_SHARED,
			  fd, 0)) != MAP_FAILED) {
		close(fd);
            	result = Ns_ConnReturnData(conn, 200, map, st.st_size, type);
		munmap(map, st.st_size);
	    } else {
            	result = Ns_ConnReturnOpenFd(conn, 200, type, fd, st.st_size);
	    	close(fd);
	    }
#endif

	} else {

	    /*
	     * Search for an existing cache entry for this file, validating 
	     * the contents against the current file mtime and size.
	     */

	    filePtr = NULL;
	    key.dev = st.st_dev;
	    key.ino = st.st_ino;
	    Ns_CacheLock(cachePtr);
	    entPtr = Ns_CacheCreateEntry(cachePtr, (char *) &key, &new);
	    if (!new) {
		while (entPtr != NULL &&
		       (filePtr = Ns_CacheGetValue(entPtr)) == NULL) {
		    Ns_CacheWait(cachePtr);
		    entPtr = Ns_CacheFindEntry(cachePtr, (char *) &key);
		}
		if (filePtr != NULL &&
			(filePtr->mtime != st.st_mtime ||
			 filePtr->size != st.st_size)) {
		    Ns_CacheUnsetValue(entPtr);
		    new = 1;
		}
	    }
	    if (new) {

		/*
		 * Read and cache new or invalidated entries in one big chunk.
		 */
		
		Ns_CacheUnlock(cachePtr);
		fd = open(ds.string, O_RDONLY|O_BINARY);
		if (fd < 0) {
	    	    filePtr = NULL;
	            Ns_Log(Warning, "FastGet: open(%s) failed: %s",
			   ds.string, strerror(errno));
		} else {
		    filePtr = ns_malloc(sizeof(File) + st.st_size);
		    filePtr->refcnt = 1;
		    filePtr->size = st.st_size;
		    filePtr->mtime = st.st_mtime;
		    nread = read(fd, filePtr->bytes, filePtr->size);
		    close(fd);
		    if (nread != filePtr->size) {
	    	    	Ns_Log(Warning, "FastGet: read(%s) failed: %s",
			       ds.string, strerror(errno));
			ns_free(filePtr);
			filePtr = NULL;
		    }
		}
		Ns_CacheLock(cachePtr);
		if (filePtr != NULL) {
		    Ns_CacheSetValueSz(entPtr, filePtr, filePtr->size);
		} else {
		    Ns_CacheDeleteEntry(entPtr);
		}
		Ns_CacheBroadcast(cachePtr);
	    }
	    if (filePtr != NULL) {
		++filePtr->refcnt;
		Ns_CacheUnlock(cachePtr);
            	result = Ns_ConnReturnData(conn, 200, filePtr->bytes,
				filePtr->size, type);
		Ns_CacheLock(cachePtr);
		DecrEntry(filePtr);
	    }
	    Ns_CacheUnlock(cachePtr);
	    if (filePtr == NULL) {
		goto notfound;
	    }
	}

    } else if (S_ISDIR(st.st_mode)) {
    
	/*
	 * For directories, search for a matching directory file and restart
	 * the connection if found.
	 */

    	for (i = 0; i < nsconf.fastpath.dirc; ++i) {
	    Ns_DStringTrunc(&ds, 0);
	    if (Ns_UrlToFile(&ds, nsServer, url) != NS_OK) {
		goto notfound;
	    }
	    Ns_DStringVarAppend(&ds, "/", nsconf.fastpath.dirv[i], NULL);
            if ((stat(ds.string, &st) == 0) && S_ISREG(st.st_mode)) {
                if (url[strlen(url) - 1] != '/') {
                    Ns_DStringTrunc(&ds, 0);
                    Ns_DStringVarAppend(&ds, url, "/", NULL);
                    result = Ns_ReturnRedirect(conn, ds.string);
                } else {
                    result = FastGetRestart(conn, nsconf.fastpath.dirv[i]);
                }
                goto done;
            }
        }

    	/*
	 * If no index file was found, invoke a directory listing
	 * ADP or Tcl proc if configured.
	 */
	 
	if (nsconf.fastpath.diradp != NULL) {
	    result = Ns_AdpRequest(conn, nsconf.fastpath.diradp);
	} else if (nsconf.fastpath.dirproc != NULL) {
	    result = Ns_TclRequest(conn, nsconf.fastpath.dirproc);
	} else {
	    goto notfound;
	}

    } else {
 notfound:
	result = Ns_ReturnNotFound(conn);
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
 * UrlIs --
 *
 *      Check if a file or directory that corresponds to a URL exists.
 *
 * Results:
 *      Return NS_TRUE if the file or directory exists and NS_FALSE
 *	otherwise.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

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

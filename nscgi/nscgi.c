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


static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nscgi/nscgi.c,v 1.6.4.3 2003/03/11 06:01:10 scottg Exp $, compiled: " __DATE__ " " __TIME__;

#include "ns.h"
#include <sys/stat.h>
#include <ctype.h>
#include <stdlib.h>	/* environ */

#define BUFSIZE	    4096
#define UCHAR(c)	((unsigned char) (c))
#define DEFAULT_MAXINPUT    1024000
#define CGI_NPH	    1
#define CGI_GETHOST 2
#define CGI_ECONTENT   4

#ifdef WIN32
#include <share.h>
#define S_ISREG(m)	((m)&_S_IFREG)
#define S_ISDIR(m)	((m)&_S_IFDIR)
#define DEVNULL	    "nul:"
#else
#define DEVNULL	    "/dev/null"
extern char **Ns_GetEnviron(void);
#endif

/*
 * The following structure is allocated for each instance the module is
 * loaded (normally just once).
 */
 
struct Cgi;

typedef struct Mod {
    char	   *server;
    char	   *module;
    char	   *tmpdir;
    Ns_Set         *interps;
    Ns_Set         *mergeEnv;
    Ns_Set	   *sysEnv;
    struct Cgi     *firstCgiPtr;
    int		    flags;
    int             maxInput;
    int             maxCgi;
    int     	    maxWait;
    int             activeCgi;
    Ns_Mutex 	    lock;
    Ns_Cond 	    cond;
} Mod;

/*
 * The following structure is used to maintain a list of open temp files
 * used to spool CGI input.  A file is used to avoid tricky asynchronous I/O
 * between two open pipes for input and output and to ensure the client gets
 * a proper EOF.  You may think this would be slower however it's probably
 * fine because the majority of the work for a temp file is the multiple,
 * synchronous I/O's for creating and deleting the file.  By creating the file
 * once and quickly truncating the content after the CGI consumes the data
 * it's likely the content simply dies in the kernel buffer cache anyway so
 * that the overall performance should approach (or even surpass) that of
 * a direct pipe.  Note that a pipe is used for output for proper streaming.
 */
 
typedef struct Tmp {
    struct Tmp *nextPtr;
    int fd;
} Tmp;

static Ns_Mutex tmpLock;
static Tmp *firstTmpPtr;

/*
 * The following structure, allocated on the stack of CgiRequest, is used
 * to accumulate all the resources of a CGI.  CGI is a very messy interface
 * which requires varying degrees of resources.  Packing everything into
 * this structure allows building up the state in multiple places and
 * tearing it all down in FreeCgi, thus simplifying the CgiRequest procedure.
 */
 
typedef struct Cgi {
    Mod     	   *modPtr;
    int		    flags;
    int     	    pid;
    Ns_Set	   *env;
    char    	   *name;
    char    	   *path;
    char           *pathinfo;
    char    	   *dir;
    char           *exec;
    char           *interp;
    Ns_Set         *interpEnv;
    Ns_DString	   *firstPtr;
    Tmp		   *tmpPtr;
    int		    ofd;
    int		    cnt;
    char	   *ptr;
    char	    buf[BUFSIZE];
} Cgi;

/*
 * The following structure defines the context of a single CGI config
 * mapping, supporting both directory-style and pageroot-style CGI locations.
 */
 
typedef struct Map {
    Mod	     *modPtr;
    char     *url;
    char     *path;
} Map;

/*
 * The following file descriptor is opened once on the first load and used
 * simply for duping as stdin in the child process.  This ensures the child
 * will get a proper EOF without having to allocate an empty temp file.
 */
 
static int devNull;

static Ns_OpProc CgiRequest;
static void     CgiRegister(Mod *modPtr, char *map);
static Ns_Callback CgiFreeMap;
static Ns_DString *CgiDs(Cgi *cgiPtr);
static int	CgiInit(Cgi *cgiPtr, Map *mapPtr, Ns_Conn *conn);
static void	CgiFree(Cgi *cgiPtr);
static int  	CgiExec(Cgi *cgiPtr, Ns_Conn *conn);
static int	CgiSpool(Cgi *cgiPtr, Ns_Conn *conn);
static int	CgiCopy(Cgi *cgiPtr, Ns_Conn *conn);
static int	CgiRead(Cgi *cgiPtr);
static int	CgiReadLine(Cgi *cgiPtr, Ns_DString *dsPtr);
static int	CgiOpenTmp(char *tmp);
static Tmp *	CgiGetTmp(Mod *modPtr);
static void 	CgiFreeTmp(Tmp *tmpPtr);
static void	CgiCloseTmp(Tmp *tmpPtr, char *err);
static char    *NextWord(char *s);
static void	SetAppend(Ns_Set *set, int index, char *sep, char *value);
static void	SetUpdate(Ns_Set *set, char *key, char *value);

NS_EXPORT int Ns_ModuleVersion = 1;	


/*
 *----------------------------------------------------------------------
 *
 * Ns_ModuleInit --
 *
 *	Create a new CGI module instance.  Note: This module can
 *	be loaded multiple times.
 *
 * Results:
 *	NS_OK/NS_ERROR.
 *
 * Side effects:
 *	URL's may be registered for CGI.
 *
 *----------------------------------------------------------------------
 */
 
NS_EXPORT int
Ns_ModuleInit(char *server, char *module)
{
    char           *path, *key, *value, *section;
    int             i;
    Ns_Set         *set;
    Ns_DString      ds;
    Mod	   *modPtr;
    static int	    initialized;

    /*
     * On the first (and likely only) load, register
     * the temp file cleanup routine and open devNull
     * for requests without content data.
     */

    if (!initialized) {
	devNull = open(DEVNULL, O_RDONLY);
	if (devNull < 0) {
	    Ns_Log(Error, "nscgi: open(%s) failed: %s",
		   DEVNULL, strerror(errno));
	    return NS_ERROR;
	}
	Ns_DupHigh(&devNull);
	Ns_CloseOnExec(devNull);
	Ns_MutexSetName2(&tmpLock, "nscgi", "tmpfd");
	initialized = 1;
    }

    /*
     * Config basic options.
     */

    path = Ns_ConfigPath(server, module, NULL);
    modPtr = ns_calloc(1, sizeof(Mod));
    modPtr->module = module;
    modPtr->server = server;
    modPtr->tmpdir = Ns_ConfigGet(path, "tmpdir");
    if (modPtr->tmpdir == NULL) {
	modPtr->tmpdir = P_tmpdir;
    }
    if (!Ns_ConfigGetInt(path, "maxinput", &modPtr->maxInput)) {
        modPtr->maxInput = DEFAULT_MAXINPUT;
    }
    if (!Ns_ConfigGetInt(path, "limit", &modPtr->maxCgi)) {
        modPtr->maxCgi = 0;
    }
    if (!Ns_ConfigGetInt(path, "maxwait", &modPtr->maxWait)) {
        modPtr->maxWait = 30;
    }
    if (!Ns_ConfigGetBool(path, "gethostbyaddr", &i)) {
        i = 0;
    }
    if (i) {
	modPtr->flags |= CGI_GETHOST;
    }

    /*
     * Configure the various interp and env options.
     */

    Ns_DStringInit(&ds);
    section = Ns_ConfigGet(path, "interps");
    if (section != NULL) {
        Ns_DStringVarAppend(&ds, "ns/interps/", section, NULL);
        modPtr->interps = Ns_ConfigSection(ds.string);
        if (modPtr->interps == NULL) {
            Ns_Log(Warning, "nscgi: no such interps section: %s",
		   ds.string);
        }
    	Ns_DStringTrunc(&ds, 0);
    }
    section = Ns_ConfigGet(path, "environment");
    if (section != NULL) {
        Ns_DStringVarAppend(&ds, "ns/environment/", section, NULL);
        modPtr->mergeEnv = Ns_ConfigSection(ds.string);
        if (modPtr->mergeEnv == NULL) {
            Ns_Log(Warning, "nscgi: no such environment section: %s",
		   ds.string);
        }
    	Ns_DStringTrunc(&ds, 0);
    }
    if (!Ns_ConfigGetBool(path, "systemenvironment", &i)) {
        i = 0;
    }
    if (i) {
	extern char **Ns_GetEnviron(void);
	char **envp = Ns_GetEnviron();
        modPtr->sysEnv = Ns_SetCreate(NULL);
        for (i = 0; envp[i] != NULL; ++i) {
            Ns_DStringAppend(&ds, envp[i]);
            key = ds.string;
            value = strchr(key, '=');
            if (value != NULL) {
                *value++ = '\0';
            }
            Ns_SetPut(modPtr->sysEnv, key, value);
            Ns_DStringTrunc(&ds, 0);
        }
    }

    /*
     * Register all requested mappings.
     */

    set = Ns_ConfigSection(path);
    for (i = 0; set != NULL && i < Ns_SetSize(set); ++i) {
        key = Ns_SetKey(set, i);
        value = Ns_SetValue(set, i);
        if (STRIEQ(key, "map")) {
            CgiRegister(modPtr, value);
        }
    }
    Ns_DStringFree(&ds);

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiRequest -
 *
 *	Process a CGI request.
 *
 * Results:
 *	Standard AOLserver request result.
 *
 * Side effects:
 *	Program may be executed.
 *
 *----------------------------------------------------------------------
 */

static int
CgiRequest(void *arg, Ns_Conn *conn)
{
    Map		   *mapPtr;
    Mod		   *modPtr;
    Cgi		    cgi;
    int             status;

    mapPtr = arg;
    modPtr = mapPtr->modPtr;

    /*
     * Check for input overflow and initialize the CGI context.
     */

    if (modPtr->maxInput > 0 && conn->contentLength > modPtr->maxInput) {
        return Ns_ReturnBadRequest(conn, "Exceeded maximum CGI input size");
    }
    if (CgiInit(&cgi, mapPtr, conn) != NS_OK) {
	return Ns_ReturnNotFound(conn);
    } else if (cgi.interp == NULL && access(cgi.exec, X_OK) != 0) {
        if (STREQ(conn->request->method, "GET") ||
	    STREQ(conn->request->method, "HEAD")) {

	    /*
	     * Evidently people are storing images and such in
	     * their cgi bin directory and they expect us to
	     * return these files directly.
	     */

            status = Ns_ConnReturnFile(conn, 200, NULL, cgi.exec);
        } else {
	    status = Ns_ReturnNotFound(conn);
	}
	goto done;
    }

    /*
     * Spool input to temp file if necessary.
     */

    if (conn->contentLength > 0 && CgiSpool(&cgi, conn) != NS_OK) {
	if (cgi.flags & CGI_ECONTENT) {
	    status = Ns_ConnReturnBadRequest(conn, "Insufficient Content");
	} else {
	    status = Ns_ConnReturnInternalError(conn);
	}
	goto done;
    }

    /*
     * Wait for CGI access if necessary.
     */

    if (modPtr->maxCgi > 0) {
	Ns_Time timeout;
	int wait = NS_OK;

	Ns_GetTime(&timeout);
	Ns_IncrTime(&timeout, modPtr->maxWait, 0);
        Ns_MutexLock(&modPtr->lock);
	while (wait == NS_OK && modPtr->activeCgi >= modPtr->maxCgi) {
	    wait = Ns_CondTimedWait(&modPtr->cond, &modPtr->lock, &timeout);
	}
	if (wait == NS_OK) {
	    ++modPtr->activeCgi;
	}
	Ns_MutexUnlock(&modPtr->lock);
	if (wait != NS_OK) {
	    status = Ns_ConnReturnStatus(conn, 503);
	    goto done;
	}
    }

    /*
     * Execute the CGI and copy output.
     */
    
    if (CgiExec(&cgi, conn) != NS_OK) {
	status = Ns_ConnReturnInternalError(conn);
    } else {
	status = CgiCopy(&cgi, conn);
    }
    
    /*
     * Release CGI access.
     */
     
    if (modPtr->maxCgi > 0) {
	Ns_MutexLock(&modPtr->lock);
	--modPtr->activeCgi;
	Ns_CondSignal(&modPtr->cond);
	Ns_MutexUnlock(&modPtr->lock);
    }

done:
    CgiFree(&cgi);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiInit -
 *
 *	Setup a CGI context structure.  This function
 *	encapsulates the majority of the CGI semantics.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CgiInit(Cgi *cgiPtr, Map *mapPtr, Ns_Conn *conn)
{
    Mod		   *modPtr;
    Ns_DString     *dsPtr;
    int             ulen, plen;
    struct stat     st;
    char           *s, *e;
    char    	   *url = conn->request->url;
    
    modPtr = mapPtr->modPtr;
    memset(cgiPtr, 0, sizeof(Cgi));
    cgiPtr->modPtr = modPtr;
    cgiPtr->pid = -1;
    cgiPtr->ofd = -1;
    cgiPtr->ptr = cgiPtr->buf;

    /*
     * Determine the executable or script to run.
     */

    ulen = strlen(url);
    plen = strlen(mapPtr->url);
    if ((strncmp(mapPtr->url, url, plen) == 0) &&
    	(ulen == plen || url[plen] == '/')) {
	
        if (mapPtr->path == NULL) {

            /*
             * No path mapping, script in pages directory:
             * 
             * 1. Path is Url2File up to the URL prefix.
	     * 2. SCRIPT_NAME is the URL prefix.
	     * 3. PATH_INFO is everything past SCRIPT_NAME in the URL.
             */
	     
            cgiPtr->name = Ns_DStringNAppend(CgiDs(cgiPtr), url, plen);
	    dsPtr = CgiDs(cgiPtr);
            Ns_UrlToFile(dsPtr, nsServer, cgiPtr->name);
	    cgiPtr->path = dsPtr->string;
            cgiPtr->pathinfo = url + plen;
	    
        } else if (stat(mapPtr->path, &st) != 0) {
	    goto err;
	
    	} else if (S_ISDIR(st.st_mode)) {

            /*
             * Path mapping is a directory:
             * 
             * 1. The script file is the first path element in the URL past
             * the mapping prefix.
	     * 2. SCRIPT_NAME is the URL up to and including the
	     * script file.
	     * 3. PATH_INFO is everything in the URL past SCRIPT_NAME.
	     * 4. The script pathname is the script prefix plus the
	     * script file.
             */

            if (plen == ulen) {
		goto err;
            }

            s = url + plen + 1;
            e = strchr(s, '/');
	    if (e != NULL) {
		*e = '\0';
	    }
	    cgiPtr->name = url;
            cgiPtr->path = Ns_DStringVarAppend(CgiDs(cgiPtr),
	    	    	    	    	      mapPtr->path, "/", s, NULL);
    	    if (e == NULL) {
		cgiPtr->pathinfo = "";
	    } else {
		*e = '/';
		cgiPtr->pathinfo = e;
	    }

        } else if (S_ISREG(st.st_mode)) {

            /*
             * When the path mapping is (or at least could be) a file:
             * 
             * 1. The script pathname is the mapping.
	     * 2. SCRIPT_NAME is the url prefix.
	     * 3. PATH_INFO is everything in the URL past SCRIPT_NAME.
             */

	    cgiPtr->path = Ns_DStringAppend(CgiDs(cgiPtr), mapPtr->path);
	    cgiPtr->name = Ns_DStringAppend(CgiDs(cgiPtr), mapPtr->url);
            cgiPtr->pathinfo = url + plen;

    	} else {
	    goto err;
        }

    } else {

        /*
         * The prefix didn't match.  Assume the mapping was a wildcard
         * mapping like *.cgi which was fetched by UrlSpecificGet() but
         * skipped by strncmp() above. In this case:
         * 
         * 1. The script pathname is the URL file in the pages directory.
	 * 2. SCRIPT_NAME is the URL.
	 * 3. PATH_INFO is "".
         */
	 
	dsPtr = CgiDs(cgiPtr);   
	Ns_UrlToFile(dsPtr, nsServer, url);
	cgiPtr->path = dsPtr->string;
	cgiPtr->name = url;
        cgiPtr->pathinfo = url + ulen;
    }

    /*
     * Copy the script directory and see if the script is NPH.
     */
     
    s = strrchr(cgiPtr->path, '/');
    if (s == NULL || access(cgiPtr->path, R_OK) != 0) {
	goto err;
    }
    *s = '\0';
    cgiPtr->dir = Ns_DStringAppend(CgiDs(cgiPtr), cgiPtr->path);
    *s++ = '/';
    if (strncmp(s, "nph-", 4) == 0) {
        cgiPtr->flags |= CGI_NPH;
    }

    /*
     * Look for a script interpreter.
     */

    if (modPtr->interps != NULL
    	&&(s = strrchr(cgiPtr->path, '.')) != NULL
        && (cgiPtr->interp = Ns_SetIGet(modPtr->interps, s)) != NULL) {
    	cgiPtr->interp = Ns_DStringAppend(CgiDs(cgiPtr), cgiPtr->interp);
        s = strchr(cgiPtr->interp, '(');
        if (s != NULL) {
            *s++ = '\0';
            e = strchr(s, ')');
            if (e != NULL) {
                *e = '\0';
            }
            cgiPtr->interpEnv = Ns_ConfigSection(s);
        }
    }
    if (cgiPtr->interp != NULL) {
        cgiPtr->exec = cgiPtr->interp;
    } else {
        cgiPtr->exec = cgiPtr->path;
    }
    return NS_OK;

err:
    CgiFree(cgiPtr);
    return NS_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiSpool --
 *
 *	Spool content to a temp file.
 *
 * Results:
 *	File descriptor of temp file or -1 on error.
 *
 * Side effects:
 *	May open a new temp file.
 *
 *----------------------------------------------------------------------
 */

static int
CgiSpool(Cgi *cgiPtr, Ns_Conn *conn)
{
    int     tocopy, toread, nread;
    Tmp	   *tmpPtr;
    char   *err;
    Mod *modPtr = cgiPtr->modPtr;

    /*
     * Pop a temp file.
     */

    tmpPtr = CgiGetTmp(modPtr);
    if (tmpPtr == NULL) {
	return NS_ERROR;
    }

    /*
     * Copy content to the file.
     */

    err = NULL;
    tocopy = conn->contentLength;
    while (tocopy > 0) {
	toread = tocopy;
	if (toread > sizeof(cgiPtr->buf)) {
	    toread = sizeof(cgiPtr->buf);
	}
	nread = Ns_ConnRead(conn, cgiPtr->buf, toread);
	if (nread <= 0) {
	    cgiPtr->flags |= CGI_ECONTENT;
	    CgiFreeTmp(tmpPtr);
	    return NS_ERROR;
	}
	if (write(tmpPtr->fd, cgiPtr->buf, nread) != nread) {
	    err = "write";
	    break;
	}
	tocopy -= nread;
    }
    if (tocopy == 0 && lseek(tmpPtr->fd, 0L, SEEK_SET) != 0) {
	err = "lseek";
    }
    if (err != NULL) {
	CgiCloseTmp(tmpPtr, err);
	return NS_ERROR;
    }
    cgiPtr->tmpPtr = tmpPtr;
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiDs -
 *
 *	Pop another dstring for a CGI context.
 *
 * Results:
 *	Pointer to DString.
 *
 * Side effects:
 *	DString is pushed on stack to be freed with CgiFree.
 *
 *----------------------------------------------------------------------
 */

static Ns_DString *
CgiDs(Cgi *cgiPtr)
{
    Ns_DString *dsPtr;
    
    dsPtr = Ns_DStringPop();
    dsPtr->addr = cgiPtr->firstPtr;
    cgiPtr->firstPtr = dsPtr;
    return dsPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiFree -
 *
 *	Free temp buffers used in CGI context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CgiFree(Cgi *cgiPtr)
{
    Ns_DString *dsPtr;

    /*
     * Close the pipe.
     */
    
    if (cgiPtr->ofd >= 0) {
    	close(cgiPtr->ofd);
    }
        
    /*
     * Truncate and release the temp file.
     */

    if (cgiPtr->tmpPtr != NULL) {
    	CgiFreeTmp(cgiPtr->tmpPtr);
    }
     
    /*
     * Free the environment.
     */
     
    if (cgiPtr->env != NULL) {
	Ns_SetFree(cgiPtr->env);
    }

    /*
     * Push back all dstrings.
     */

    while ((dsPtr = cgiPtr->firstPtr) != NULL) {
	cgiPtr->firstPtr = dsPtr->addr;
	Ns_DStringPush(dsPtr);
    }
    
    /*
     * Reap the process.
     */
     
    if (cgiPtr->pid != -1 && Ns_WaitProcess(cgiPtr->pid) != NS_OK) {
	Ns_Log(Error, "nscgi: wait for %s failed: %s",
	       cgiPtr->exec, strerror(errno));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * CgiExec -
 *
 *	Construct the command args and environment and execute
 *  	the CGI.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CgiExec(Cgi *cgiPtr, Ns_Conn *conn)
{
    int i, index, opipe[2];
    char *s, *p, *e;
    Ns_DString *dsPtr;
    Mod *modPtr = cgiPtr->modPtr;

    /*
     * Get a dstring which will be used to setup env variables
     * and the arg list.
     */
     
    dsPtr = CgiDs(cgiPtr);

    /*
     * Setup and merge the environment set.
     */

    cgiPtr->env = Ns_SetCreate(NULL);
    if (cgiPtr->interpEnv != NULL) {
        cgiPtr->env = Ns_SetCopy(cgiPtr->interpEnv);
    } else {
        cgiPtr->env = Ns_SetCreate(NULL);
    }
    if (modPtr->mergeEnv != NULL) {
        Ns_SetMerge(cgiPtr->env, modPtr->mergeEnv);
    }
    if (modPtr->sysEnv != NULL) {
        Ns_SetMerge(cgiPtr->env, modPtr->sysEnv);
    }

    /*
     * PATH is the only variable copied from the running environment if it
     * isn't already in the server default environment.
     */

    if (Ns_SetFind(cgiPtr->env, "PATH") < 0) {
        s = getenv("PATH");
        if (s != NULL) {
            SetUpdate(cgiPtr->env, "PATH", s);
        }
    }

    /*
     * Set all the CGI specified variables.
     */

    Ns_DStringInit(dsPtr);
    SetUpdate(cgiPtr->env, "SCRIPT_NAME", cgiPtr->name);
    if (cgiPtr->pathinfo != NULL && *cgiPtr->pathinfo != '\0') {
    	Ns_DString tmp;
	
        if (Ns_UrlDecode(dsPtr, cgiPtr->pathinfo) != NULL) {
            SetUpdate(cgiPtr->env, "PATH_INFO", dsPtr->string);
        } else {
            SetUpdate(cgiPtr->env, "PATH_INFO", cgiPtr->pathinfo);
        }
	Ns_DStringTrunc(dsPtr, 0);
	Ns_DStringInit(&tmp);
        Ns_UrlToFile(dsPtr, modPtr->server, cgiPtr->pathinfo);
        if (Ns_UrlDecode(&tmp, dsPtr->string) != NULL) {
            SetUpdate(cgiPtr->env, "PATH_TRANSLATED", tmp.string);
        } else {
            SetUpdate(cgiPtr->env, "PATH_TRANSLATED", dsPtr->string);
        }
	Ns_DStringFree(&tmp);
	Ns_DStringTrunc(dsPtr, 0);
    } else {
        SetUpdate(cgiPtr->env, "PATH_INFO", "");
    }
    SetUpdate(cgiPtr->env, "GATEWAY_INTERFACE", "CGI/1.1");
    Ns_DStringVarAppend(dsPtr, Ns_InfoServer(), "/", Ns_InfoVersion(), NULL);
    SetUpdate(cgiPtr->env, "SERVER_SOFTWARE", dsPtr->string);
    Ns_DStringTrunc(dsPtr, 0);
    Ns_DStringPrintf(dsPtr, "HTTP/%2.1f", conn->request->version);
    SetUpdate(cgiPtr->env, "SERVER_PROTOCOL", dsPtr->string);
    Ns_DStringTrunc(dsPtr, 0);

    /*
     * Determine SERVER_NAME from the conn location.
     */

    s = Ns_ConnLocation(conn);
    p = NULL;
    if (s != NULL) {
        if (strstr(s, "://") == NULL) {
            Ns_Log(Warning, "nscgi: location does not contain '://'");
            s = NULL;
        } else {
            s = strchr(s, ':');         /* Get past the http */
            if (s != NULL) {
                s += 3;                 /* Get past the // */
                p = strchr(s, ':');     /* Get to the port number */ 
            }
        }
    }
    if (s == NULL) {
        s = Ns_ConnHost(conn);
        SetUpdate(cgiPtr->env, "SERVER_NAME", s);
    } else {
        if (p == NULL) {
            Ns_DStringAppend(dsPtr, s);           /* No port number */
        } else {
            Ns_DStringNAppend(dsPtr, s, (p - s)); /* Port number exists */
        }
        s = Ns_DStringExport(dsPtr);
        SetUpdate(cgiPtr->env, "SERVER_NAME", s);
        ns_free(s);
    }

    /*
     * Determine SERVER_PORT from the conn location.
     */

    s = Ns_ConnLocation(conn);
    if (s != NULL) {
        s = strchr(s, ':');             /* Skip past http. */
        if (s != NULL) {
            ++s;
            s = strchr(s, ':');         /* Skip past hostname. */
            if (s != NULL) {
                ++s;
            }
        }
    }
    if (s == NULL) {
        s = "80";
    }
    SetUpdate(cgiPtr->env, "SERVER_PORT", s);
    SetUpdate(cgiPtr->env, "AUTH_TYPE", "Basic");
    SetUpdate(cgiPtr->env, "REMOTE_USER", conn->authUser);
    s = Ns_ConnPeer(conn);
    if (s != NULL) {
        SetUpdate(cgiPtr->env, "REMOTE_ADDR", s);
        if ((modPtr->flags & CGI_GETHOST)) {
            if (Ns_GetHostByAddr(dsPtr, s)) {
                SetUpdate(cgiPtr->env, "REMOTE_HOST", dsPtr->string);
            }
            Ns_DStringTrunc(dsPtr, 0);
        } else {
            SetUpdate(cgiPtr->env, "REMOTE_HOST", s);
        }
    }
    SetUpdate(cgiPtr->env, "REQUEST_METHOD", conn->request->method);
    SetUpdate(cgiPtr->env, "QUERY_STRING", conn->request->query);

    s = Ns_SetIGet(conn->headers, "Content-Type");
    if (s == NULL) {
        if (STREQ("POST", conn->request->method)) {
            s = "application/x-www-form-urlencoded";
        } else {
            s = "";
        }
    }
    SetUpdate(cgiPtr->env, "CONTENT_TYPE", s);

    if (conn->contentLength <= 0) {
        SetUpdate(cgiPtr->env, "CONTENT_LENGTH", "");
    } else {
        Ns_DStringPrintf(dsPtr, "%u", (unsigned) conn->contentLength);
        SetUpdate(cgiPtr->env, "CONTENT_LENGTH", dsPtr->string);
        Ns_DStringTrunc(dsPtr, 0);
    }

    /*
     * Set the HTTP_ header variables.
     */

    Ns_DStringAppend(dsPtr, "HTTP_");
    for (i = 0; i < Ns_SetSize(conn->headers); ++i) {
        s = Ns_SetKey(conn->headers, i);
        e = Ns_SetValue(conn->headers, i);
        Ns_DStringAppend(dsPtr, s);
        s = dsPtr->string + 5;
        while (*s != '\0') {
            if (*s == '-') {
                *s = '_';
            } else if (islower(UCHAR(*s))) {
                *s = toupper(UCHAR(*s));
            }
            ++s;
        }
        index = Ns_SetFind(cgiPtr->env, dsPtr->string);
        if (index < 0) {
            Ns_SetPut(cgiPtr->env, dsPtr->string, e);
        } else {
	    SetAppend(cgiPtr->env, index, ", ", e);
        }
        Ns_DStringTrunc(dsPtr, 5);
    }

    /*
     * Build up the argument block.
     */

    Ns_DStringTrunc(dsPtr, 0);
    if (cgiPtr->interp != NULL) {
        Ns_DStringAppendArg(dsPtr, cgiPtr->interp);
    }
    if (cgiPtr->path != NULL) {
        Ns_DStringAppendArg(dsPtr, cgiPtr->path);
    }
    s = conn->request->query;
    if (s != NULL) {
	if (strchr(s, '=') == NULL) {
    	    do {
	    	e = strchr(s, '+');
		if (e != NULL) {
		    *e = '\0';
		}
		Ns_UrlDecode(dsPtr, s);
		Ns_DStringNAppend(dsPtr, "", 1);
		if (e != NULL) {
		    *e++ = '+';
		}
		s = e;
	    } while (s != NULL);
	}
	Ns_DStringNAppend(dsPtr, "", 1);
    }

    /*
     * Create the output pipe.
     */
     
    if (ns_pipe(opipe) != 0) {
	Ns_Log(Error, "nscgi: pipe() failed: %s", strerror(errno));
	return NS_ERROR;
    }

    /*
     * Execute the CGI.
     */
     
    cgiPtr->pid = Ns_ExecProcess(cgiPtr->exec, cgiPtr->dir,
    	cgiPtr->tmpPtr ? cgiPtr->tmpPtr->fd : devNull,
	opipe[1], dsPtr->string, cgiPtr->env);
    close(opipe[1]);
    if (cgiPtr->pid < 0) {
    	close(opipe[0]);
	return NS_ERROR;
    }

    cgiPtr->ofd = opipe[0];
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiRead -
 *
 *	Read content from pipe into the CGI buffer.
 *
 * Results:
 *	Number of bytes read or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CgiRead(Cgi *cgiPtr)
{
    int n;
    
    cgiPtr->ptr = cgiPtr->buf;
    n = read(cgiPtr->ofd, cgiPtr->buf, sizeof(cgiPtr->buf));
    if (n > 0) {
	cgiPtr->cnt = n;
    } else if (n < 0) {
	Ns_Log(Error, "nscgi: pipe read() from %s failed: %s",
	       cgiPtr->exec, strerror(errno));
    }
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiReadLine -
 *
 *	Read and right trim a line from the pipe.
 *
 * Results:
 *	Length of header read or -1 on error.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CgiReadLine(Cgi *cgiPtr, Ns_DString *dsPtr)
{
    char c;
    int n;

    do {
	while (cgiPtr->cnt > 0) { 
	    c = *cgiPtr->ptr;
	    ++cgiPtr->ptr;
	    --cgiPtr->cnt;
	    if (c == '\n') {
		while (dsPtr->length > 0
		    && isspace(UCHAR(dsPtr->string[dsPtr->length - 1]))) {
		    Ns_DStringTrunc(dsPtr, dsPtr->length-1);
		}
		return dsPtr->length;
	    }
	    Ns_DStringNAppend(dsPtr, &c, 1);
	}
    } while ((n = CgiRead(cgiPtr)) > 0);
    return n;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiCopy
 *
 *	Read and parse headers and then copy output.
 *
 * Results:
 *	AOLserver request result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
CgiCopy(Cgi *cgiPtr, Ns_Conn *conn)
{
    Ns_DString      ds, redir;
    int             status, last, n, httpstatus;
    char           *value;
    Ns_Set         *hdrs;
    
    /*
     * Skip to copy for nph CGI's.
     */

    if (cgiPtr->flags & CGI_NPH) {
    	goto copy;
    }

    /*
     * Read and parse headers up to the blank line or end of file.
     */
     
    Ns_DStringInit(&ds);
    last = -1;
    httpstatus = 200;
    hdrs = conn->outputheaders;
    while ((n = CgiReadLine(cgiPtr, &ds)) > 0) {
        if (isspace(UCHAR(*ds.string))) {   /* NB: Continued header. */
            if (last == -1) {
		continue;	/* NB: Silently ignore bad header. */
            }
	    SetAppend(hdrs, last, "\n", ds.string);
        } else {
            value = strchr(ds.string, ':');
            if (value == NULL) {
		continue;	/* NB: Silently ignore bad header. */
            }
            *value++ = '\0';
            while (isspace(UCHAR(*value))) {
                ++value;
            }
            if (STRIEQ(ds.string, "status")) {
                httpstatus = atoi(value);
            } else if (STRIEQ(ds.string, "location")) {
                httpstatus = 302;
                if (*value == '/') {
                    Ns_DStringInit(&redir);
                    Ns_DStringVarAppend(&redir, Ns_ConnLocation(conn), value, NULL);
                    last = Ns_SetPut(hdrs, ds.string, redir.string);
                    Ns_DStringFree(&redir);
                } else {
                    last = Ns_SetPut(hdrs, ds.string, value);
                }
            } else {
                last = Ns_SetPut(hdrs, ds.string, value);
            }
        }
        Ns_DStringTrunc(&ds, 0);
    }
    Ns_DStringFree(&ds);
    if (n < 0) {
	return Ns_ConnReturnInternalError(conn);
    }
    
    /*
     * Output the parsed and copied headers.
     */
     
    Ns_ConnSetRequiredHeaders(conn, NULL, 0);
    status = Ns_ConnFlushHeaders(conn, httpstatus);
    if (status != NS_OK) {
    	return status;
    }

    /*
     * Copy remaining content up to end of file.
     */

copy:
    do {
    	status = Ns_WriteConn(conn, cgiPtr->ptr, cgiPtr->cnt);
    } while (status == NS_OK && CgiRead(cgiPtr) > 0);

    /*
     * Close connection now so it will not linger on
     * waiting for process exit.
     */
     
    if (status == NS_OK) {
    	status = Ns_ConnClose(conn);
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NextWord -
 *
 *	Locate next word in CGI mapping.
 *
 * Results:
 *	Pointer to next word.
 *
 * Side effects:
 *	String is modified in place.
 *
 *----------------------------------------------------------------------
 */

static char    *
NextWord(char *s)
{
    while (*s != '\0' && !isspace(UCHAR(*s))) {
        ++s;
    }
    if (*s != '\0') {
        *s++ = '\0';
        while (isspace(UCHAR(*s))) {
            ++s;
        }
    }
    return s;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiRegister -
 *
 *	Register a CGI request mapping.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	May register or re-register a mapping.
 *
 *----------------------------------------------------------------------
 */

static void
CgiRegister(Mod *modPtr, char *map)
{
    char           *method;
    char           *url;
    char           *path;
    Ns_DString      ds1, ds2;
    Map     	   *mapPtr;

    Ns_DStringInit(&ds1);
    Ns_DStringInit(&ds2);
    
    Ns_DStringAppend(&ds1, map);
    method = ds1.string;
    url = NextWord(method);
    if (*method == '\0' || *url == '\0') {
        Ns_Log(Error, "nscgi: invalid mapping: %s", map);
	goto done;
    }
    
    path = NextWord(url);
    if (*path == '\0') {
        path = NULL;
    } else {
    	Ns_NormalizePath(&ds2, path);
    	path = ds2.string;
    	if (!Ns_PathIsAbsolute(path) || access(path, R_OK) != 0) {
            Ns_Log(Error, "nscgi: invalid directory: %s", path);
	    goto done;
	}
    }

    mapPtr = ns_malloc(sizeof(Map));
    mapPtr->modPtr = modPtr;
    mapPtr->url = ns_strdup(url);
    mapPtr->path = ns_strcopy(path);
    Ns_Log(Notice, "nscgi: %s %s%s%s",
	   method, url, path ? " -> " : "", path ? path : "");
    Ns_RegisterRequest(modPtr->server, method, url, 
		       CgiRequest, CgiFreeMap, mapPtr, 0);

done:
    Ns_DStringFree(&ds1);
    Ns_DStringFree(&ds2);
}


/*
 *----------------------------------------------------------------------
 *
 * CgiFreeMap -
 *
 *	Free a request mapping context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
CgiFreeMap(void *arg)
{
    Map  *mapPtr = (Map *) arg;

    ns_free(mapPtr->url);
    ns_free(mapPtr->path);
    ns_free(mapPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * SetAppend -
 *
 *	Append data to an existing Ns_Set value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SetAppend(Ns_Set *set, int index, char *sep, char *value)
{
    Ns_DString ds;

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, Ns_SetValue(set, index),
	sep, value, NULL);
    Ns_SetPutValue(set, index, ds.string);
    Ns_DStringFree(&ds);
}


/*
 *----------------------------------------------------------------------
 *
 * SetUpdate -
 *
 *	Update value in an Ns_Set, translating NULL to "".
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
SetUpdate(Ns_Set *set, char *key, char *value)
{
    Ns_SetUpdate(set, key, value ? value : "");
}


/*
 *----------------------------------------------------------------------
 *
 * CgiCloseTmp -
 *
 *	Close temp file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	On NT, file is removed.
 *
 *----------------------------------------------------------------------
 */

static void
CgiCloseTmp(Tmp *tmpPtr, char *err)
{
    if (err != NULL) {
	Ns_Log(Error, "nscgi: temp file %s(%d) failed: %s",
	       err, tmpPtr->fd, strerror(errno));
    }
    close(tmpPtr->fd);
    ns_free(tmpPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * CgiGetTmp -
 *
 *	Pop or allocate a temp file.  Temp files are immediately
 *	removed on Unix and marked non-shared and delete on close
 *	on NT to avoid snooping of data being sent to the CGI.
 *
 * Results:
 *	Pointer to Tmp.
 *
 * Side effects:
 *	File may be opened.
 *
 *----------------------------------------------------------------------
 */

static Tmp *
CgiGetTmp(Mod *modPtr)
{
    Tmp *tmpPtr;
    
    Ns_MutexLock(&tmpLock);
    tmpPtr = firstTmpPtr;
    if (tmpPtr != NULL) {
	firstTmpPtr = tmpPtr->nextPtr;
    }
    Ns_MutexUnlock(&tmpLock);
    if (tmpPtr == NULL) {
	Ns_DString ds;
	char *tmp;
	int fd;
	Ns_DStringInit(&ds);
	tmp = Ns_MakePath(&ds, modPtr->tmpdir, "cgi.XXXXXX", NULL);
	if (mktemp(tmp) == NULL || tmp[0] == '\0') {
	    Ns_Log(Error, "nscgi: %s: mktemp(%s) failed: %s",
		   modPtr->server, tmp, strerror(errno));
	} else {
	    int flags = O_RDWR|O_CREAT|O_TRUNC;

#ifdef WIN32
	    flags |= _O_SHORT_LIVED|_O_NOINHERIT|_O_TEMPORARY;
	    fd = _sopen(tmp, flags, _SH_DENYRW, _S_IREAD|_S_IWRITE);
#else
	    fd = open(tmp, flags, 0600);
	    if (fd >= 0 && unlink(tmp) != 0) {
		Ns_Log(Error, "nscgi: unlink(%s) failed: %s",
		       tmp, strerror(errno));
		close(fd);
		fd = -1;
	    }
	    if (fd >= 0) {
		Ns_DupHigh(&fd);
		Ns_CloseOnExec(fd);
	    }
#endif
	    if (fd < 0) {
		Ns_Log(Error, "nscgi: could not open temp file %s: %s",
		       tmp, strerror(errno));
	    } else {
		tmpPtr = ns_malloc(sizeof(Tmp));
		tmpPtr->nextPtr = NULL;
		tmpPtr->fd = fd;
	    }
	}
	Ns_DStringFree(&ds);
    }
    return tmpPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * CgiFreeTmp -
 *
 *	Return a temp file to the pool.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	File may be closed on error.
 *
 *----------------------------------------------------------------------
 */

static void
CgiFreeTmp(Tmp *tmpPtr)
{
    if (lseek(tmpPtr->fd, 0, SEEK_SET) != 0) {
	CgiCloseTmp(tmpPtr, "lseek");
    } else if (ftruncate(tmpPtr->fd, 0) != 0) {
	CgiCloseTmp(tmpPtr, "ftruncate");
    } else {
	Ns_MutexLock(&tmpLock);
	tmpPtr->nextPtr = firstTmpPtr;
	firstTmpPtr = tmpPtr;
	Ns_MutexUnlock(&tmpLock);
    }
}

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
 * nsext.c --
 *
 *	External Database Driver
 *
 *
 * External Database Driver
 * ========================
 *
 * Description:
 *
 * This driver interfaces with an external database proxy daemon instead of
 * calling database client libraries directly.  The impetus for this ostensibly
 * unnecessary complication/indirection is that some database client
 * libraries have turned out to be undesirable partners in the AOLserver
 * process space.  E.g., they may make assumptions regarding per-process
 * resources such as signals, or they may not be thread-safe.  Further,
 * platforms without support for a particular database client library
 * can still interface with a database via a remote database proxy daemon.
 *
 * This approach is not intended to replace the existing practice of
 * linking database client libraries into the server via a driver.
 * It merely provides an alternative interface mechanism--augmenting
 * the choices available to developers extending the AOLserver database
 * interface capabilities.
 *
 * Configuration:
 *
 * The database proxy daemon to which this module talks is specified in
 * the external driver section as follows:
 * [ns/db/Driver/external]
 * LocalDaemon=/usr/local/bin/nsillpd
 * ; OR
 * RemoteHost=dbhost.xyzzy.com
 * RemotePort=8199
 * ;In both cases above, the database proxy daemon process will be spawned
 * ;for each
 * ;connection in the pool at server startup time.  The external driver then
 * ;communicates with this daemon via pipes or sockets as database requests
 * ;are received.
 * Param=/usr/local/miadmin/MiParams
 * ;Param is a generic argument passed to the proxy daemon on startup
 * ;with the datasource, username, and password from the database pool.
 * ;In the above example, we are using to specifiy Illustra's MiParams file.
 *
 * If you're using a remote daemon (e.g. the Host/Port combination
 * above), you can configure inetd on dbhost.xyzzy.com as shown in the
 * following example:
 * 1) Add to /etc/inet/services:
 *    nsillpd      8199/tcp        # AOLserver Illustra proxy daemon
 * 2) Add to /etc/inet/inetd.conf:
 *    nsillpd stream tcp nowait miadmin /srvrhome/bin/nsillpd nsillpd
 * 3) Send a SIGHUP signal to your inetd process.
 *
 * See the man page for inetd.conf for further details.
 *
 * Protocol Description:
 *
 * See the file 'Protocol'.
 *
 * TCL Commands:
 *     ns_ext <cmd> <dbhandle>
 *
 *     where <cmd> is one of:
 *        ping:       verify that the proxy daemon process is running
 *        identify:   return the database type and version of the daemon
 *        gettypes:   return a string (to be 'split') containing the data types
 *        traceon:    turn message tracing on in the proxy daemon
 *        traceoff:   turn message tracing off in the proxy daemon
 *        number:     returns the connection number
 *        resultid:   returns id of the last object affected by an exec command
 *        resultrows: returns the number of rows affected by an exec command
 *        setmaxrows: set the limit on the number of rows to be returned
 *        mktemp:     create unique temp file in proxy daemon (remote) file
 *                    space
 *        rm:         remove file in proxy daemon file space
 *        cpto:       copy from local file to remote proxy daemon file space
 *        cpfrom:     copy from remote proxy daemon file to local file
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsext/nsext.c,v 1.13 2008/12/05 08:51:44 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsdb.h"
#include "nsextmsg.h"
#include <ctype.h>

#ifdef USE_FIONREAD
#include <sys/ioctl.h>
#endif

#define CONFIG_LOCALDAEMON      "LocalDaemon"
#define CONFIG_REMOTEHOST       "RemoteHost"
#define CONFIG_REMOTEPORT       "RemotePort"
#define CONFIG_PARAM            "Param"
#define CONFIG_TIMEOUT          "Timeout"
#define CONFIG_IOTRACE          "IOTrace"
#define CONFIG_TRIMDATA         "TrimData"
#define CONFIG_MAX_ELEMENT_SIZE "maxElementSize"
#define DEFAULT_TIMEOUT         60
#define DEFAULT_MAX_ELEMENT_SIZE 32768
#define STATUS_BUFSIZE          4096
#define RSP_BUFSIZE             32768
#define MSG_BUFSIZE             4096
#define FILE_IOSIZE             1024
#define END_LIST_VAL            -1
#define DRIVER_NAME             "External"
#define MAX_DBTYPE              64
#define MAX_SIZEDIGITS          32
#define RESPBUFMAX              256
#define ROWIDMAX                512
#define DELIMITERS              " \t"
#define ErrnoPeerClose(e)       (((e) == ECONNABORTED) || ((e) == ETIMEDOUT) || ((e) == ECONNRESET) || ((e) == EPIPE) || ((e) == EINVAL))

/*
 * This is the callback context.
 */

typedef struct {
    char         *path;
    char         *host;
    int           timeout;
    int           port;
    char         *param;
    unsigned int  connNum;
    short         initOK;
    int           ioTrace;
    char          ident[RSP_BUFSIZE];
    char          dbtype[MAX_DBTYPE];
    int           trimdata;
    int		  maxElementSize;
    Ns_Mutex      muIdent;
} NsExtCtx;

/*
 * This represents a connection to the external driver.
 */

typedef struct NsExtConn {
    int       socks[2];
    int       connNum;
    NsExtCtx *ctx;
} NsExtConn;

/*
 * One element of input from the db proxy held here.
 */

typedef struct DbProxyInputElement {
    int   size;
    char *data;
} DbProxyInputElement;

/*
 * Types of IO
 */

typedef enum {
    Read, Write
} SockIOType;

/*
 * Local functions defined in this file
 */

static int      NetProxy(NsExtConn *nsConn, char *host, int port);
static int      LocalProxy(NsExtConn * nsConn);
static int ExtCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv);
static char    *ExtName(Ns_DbHandle *handle);
static char    *ExtDbType(Ns_DbHandle *handle);
static int      ExtOpenDb(Ns_DbHandle *dbhandle);
static int      ExtCloseDb(Ns_DbHandle *dbhandle);
static int      ExtExec(Ns_DbHandle *handle, char *sql);
static int      ExtGetRow(Ns_DbHandle *handle, Ns_Set *row);
static int      ExtFlush(Ns_DbHandle *handle);
static int      Ns_ExtCancel(Ns_DbHandle *handle);
static Ns_Set  *ExtBindRow(Ns_DbHandle *handle);
static int      ExtServerInit(char *hServer, char *hModule, char *hDriver);
static int      ExtResetHandle(Ns_DbHandle *handle);
static int      ExtSpStart(Ns_DbHandle *handle, char *procname);
static int      ExtSpSetParam(Ns_DbHandle *handle, char *args);
static int      ExtSpExec(Ns_DbHandle *handle);
static int      ExtSpReturnCode(Ns_DbHandle *handle, char *returnCode,
				   int bufsize);
static Ns_Set  *ExtSpGetParams(Ns_DbHandle *handle);
static void     ExtFree(void *ptr);

static int      DbProxyCheckStatus(NsExtConn * nsConn, Ns_DbHandle *handle);
static int      DbProxyStart(NsExtConn * nsConn);
static void     DbProxyStop(NsExtConn * nsConn);
static int      DbProxyGetString(Ns_DbHandle *dbhandle, char *buf, int maxbuf);
static int      DbProxyGetPingReply(Ns_DbHandle *dbhandle);
static int      DbProxyIsAlive(Ns_DbHandle *dbhandle);
static int      DbProxySend(Ns_DbHandle *dbhandle, Ns_ExtDbCommandCode msgType,
			    char *arg, size_t argSize);
static int      DbProxyGetTypes(Ns_DbHandle *dbhandle, char *typesbuf);
static int      DbProxyTraceOn(Ns_DbHandle *dbhandle, char *filepath);
static int      DbProxyTraceOff(Ns_DbHandle *dbhandle);
static int      DbProxyResultId(Ns_DbHandle *dbhandle, char *idbuf);
static int      DbProxyResultRows(Ns_DbHandle *dbhandle, char *rowCountStr);
static int      DbProxySetMaxRows(Ns_DbHandle *dbhandle, char *maxRowsStr);

static int      DbProxyCreateRemoteTmpFile(Ns_DbHandle *dbhandle,
					   char *remoteFileName,
					   char *errbuf);
static int      DbProxyCopyToRemoteFile(Ns_DbHandle *dbhandle, char *srcFile,
					char *remoteFileName, char *errbuf);
static int      DbProxyCopyFromRemoteFile(Ns_DbHandle *dbhandle,
					  char *destFile,
					  char *remoteFileName, char *errbuf);
static int      DbProxyDeleteRemoteFile(Ns_DbHandle *dbhandle,
					char *remoteFileName,
					char *errbuf);
static int      DbProxyTimedIO(int sock, char *buf, int nbytes, int flags,
			       SockIOType iotype, int timeout,
			       Ns_DbHandle *dbhandle, int readExact);
static void     DbProxyCleanup(Ns_DbHandle *dbhandle);
static int      DbProxyIdentify(Ns_DbHandle *dbhandle, char *identbuf);
static Ns_List *DbProxyGetList(Ns_DbHandle *dbhandle);
static int      AllDigits(char *str);

/*
 * Static variables defined in this file
 */

static char    *extName = DRIVER_NAME;
static Tcl_HashTable htCtx;
static Ns_Mutex muCtx;
static Ns_Callback ExtCleanup;

/*
 * This lists all the callback functions and connects them to
 * IDs.
 */

static Ns_DbProc ExtProcs[] = {
    {DbFn_Name, (void *) ExtName},
    {DbFn_DbType, (void *) ExtDbType},
    {DbFn_ServerInit, (void *) ExtServerInit},
    {DbFn_OpenDb, (void *) ExtOpenDb},
    {DbFn_CloseDb, (void *) ExtCloseDb},
    {DbFn_Flush, (void *) ExtFlush},
    {DbFn_Cancel, (void *) Ns_ExtCancel},
    {DbFn_Exec, (void *) ExtExec},
    {DbFn_BindRow, (void *) ExtBindRow},
    {DbFn_GetRow, (void *) ExtGetRow},
    {DbFn_ResetHandle, (void *) ExtResetHandle},
    {DbFn_SpStart, (void *) ExtSpStart},
    {DbFn_SpSetParam, (void *) ExtSpSetParam},
    {DbFn_SpExec, (void *) ExtSpExec},
    {DbFn_SpReturnCode, (void *) ExtSpReturnCode},
    {DbFn_SpGetParams, (void *) ExtSpGetParams},
    {0, NULL}
};

static    int   verbose = NS_FALSE;

int   Ns_ModuleVersion = 1;

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDriverInit --
 *
 *	Initialize this database driver. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Allocates memory, initializes hash tables, etc. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbDriverInit(char *hDriver, char *configPath)
{
    int            status = NS_ERROR;
    static int     initialized = NS_FALSE;
    NsExtCtx      *ctx;
    Tcl_HashEntry *he;
    int            new;

    if (initialized == NS_FALSE) {
        Ns_MutexInit(&muCtx);
        Ns_MutexSetName(&muCtx, "nsext");
        Tcl_InitHashTable(&htCtx, TCL_STRING_KEYS);
        Ns_RegisterShutdown(ExtCleanup, NULL);
        initialized = NS_TRUE;
    }

    /*
     * Register the external driver functions with nsdb. Nsdb will later call
     * the ExtServerInit() function for each virtual server which uses
     * nsdb.
     */
    
    if (Ns_DbRegisterDriver(hDriver, &(ExtProcs[0])) != NS_OK) {
        Ns_Log(Error, "nsext: "
	       "failed to register driver: %s", extName);
    } else {
        ctx = ns_malloc(sizeof(NsExtCtx));
        ctx->connNum = 0;
        ctx->ident[0] = '\0';
        Ns_MutexInit(&ctx->muIdent);
        Ns_MutexSetName(&ctx->muIdent, "nsext:ident");
        ctx->param = Ns_ConfigGetValue(configPath, CONFIG_PARAM);
        ctx->path = Ns_ConfigGetValue(configPath, CONFIG_LOCALDAEMON);
        ctx->host = Ns_ConfigGetValue(configPath, CONFIG_REMOTEHOST);
        if (Ns_ConfigGetInt(configPath, CONFIG_REMOTEPORT,
                &ctx->port) != NS_TRUE) {
            ctx->port = 0;
        }
        if (ctx->path == NULL && ctx->host == NULL) {
            Ns_Log(Error, "nsext: "
		   "bad config: localdaemon or remotehost required");
        } else if (ctx->path == NULL && ctx->port == 0) {
            Ns_Log(Error, "nsext: "
		   "bad config: proxyhost requires proxyport");
        } else {
            if (!Ns_ConfigGetInt(configPath, CONFIG_TIMEOUT, &ctx->timeout)) {
                ctx->timeout = DEFAULT_TIMEOUT;
            }
            if (!Ns_ConfigGetInt(configPath, 
				 CONFIG_MAX_ELEMENT_SIZE,
				 &ctx->maxElementSize)) {
		
                ctx->maxElementSize = DEFAULT_MAX_ELEMENT_SIZE;
            }
            if (!Ns_ConfigGetBool(configPath, CONFIG_IOTRACE,
                    &ctx->ioTrace)) {
                ctx->ioTrace = 0;
            }
            if (ctx->path != NULL) {
                if (Ns_PathIsAbsolute(ctx->path)) {
                    ctx->path = ns_strdup(ctx->path);
                } else {
                    Ns_DString ds;

                    Ns_DStringInit(&ds);
                    Ns_HomePath(&ds, "bin", ctx->path, NULL);
                    ctx->path = Ns_DStringExport(&ds);
                }
            }

            /*
	     * Trim data
	     */
	    
            if (!Ns_ConfigGetBool(configPath, CONFIG_TRIMDATA,
				  &ctx->trimdata)) {
                ctx->trimdata = NS_FALSE;
            }
            
            ctx->initOK = 1;
            status = NS_OK;
        }
        if (status != NS_OK) {
            ns_free(ctx);
        } else {
            Ns_MutexLock(&muCtx);
            he = Tcl_CreateHashEntry(&htCtx, hDriver, &new);
            Tcl_SetHashValue(he, ctx);
            Ns_MutexUnlock(&muCtx);
        }
    }
    Ns_Log (Notice, "nsext: module started; built on %s/%s)",
	    __DATE__, __TIME__);

    return status;
}



/*
 *----------------------------------------------------------------------
 *
 * ExtName --
 *
 *	Return the driver name 
 *
 * Results:
 *	Driver name. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
ExtName(Ns_DbHandle *handle)
{
    assert(handle != NULL);
    return extName;
}



/*
 *----------------------------------------------------------------------
 *
 * ExtDbType --
 *
 *	Return the database type of the proxy daemon, parsed from the 
 *	identity string of the form: <dbtype> Proxy Daemon vX.X. 
 *
 * Results:
 *	String database type. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
ExtDbType(Ns_DbHandle *handle)
{
    NsExtConn *nsConn;
    char      *identstr, *firstDelim;
    int        typelen;

    assert(handle != NULL);
    nsConn = handle->connection;
    Ns_MutexLock(&nsConn->ctx->muIdent);
    identstr = nsConn->ctx->ident;
    firstDelim = strchr(identstr, ' ');
    typelen = firstDelim == NULL ? strlen(identstr) : firstDelim - identstr;
    if (typelen >= MAX_DBTYPE) {
        typelen = MAX_DBTYPE - 1;
    }
    strncpy(nsConn->ctx->dbtype, identstr, (size_t)typelen);
    Ns_MutexUnlock(&nsConn->ctx->muIdent);
    nsConn->ctx->dbtype[typelen] = '\0';

    return nsConn->ctx->dbtype;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtOpenDb --
 *
 *	Open an external connection on an nsdb handle. The datasource 
 *	for the external driver is interpreted by the remote database 
 *	proxy daemon; it usually takes the form "host:port:database". 
 *
 * Results:
 *	NS_OK/NSERROR. 
 *
 * Side effects:
 *	May spawn the external driver. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtOpenDb(Ns_DbHandle *handle)
{
    NsExtConn     *nsConn;
    NsExtCtx      *ctx;
    int            status = NS_ERROR;
    Ns_DString     dsOpenData;
    Tcl_HashEntry *he;
    int            ok = 0;

    Ns_MutexLock(&muCtx);
    he = Tcl_FindHashEntry(&htCtx, handle->driver);

    if (he != NULL) {
        ctx = Tcl_GetHashValue(he);
        ok = ctx->initOK;
    }

    Ns_MutexUnlock(&muCtx);

    if (ok) {
        Ns_DStringInit(&dsOpenData);
        Ns_DStringPrintf(&dsOpenData, "%s%c%s%c%s%c%s",
	    handle->datasource == NULL ? "" : handle->datasource,
            ARG_TOKEN_DELIMITER, handle->user == NULL ? "" : handle->user,
	    ARG_TOKEN_DELIMITER,
	    handle->password == NULL ? "" : handle->password,
            ARG_TOKEN_DELIMITER, ctx->param == NULL ? "" : ctx->param);

        handle->connection = NULL;
        handle->connected = 0;
        nsConn = ns_malloc(sizeof(NsExtConn));
        nsConn->ctx = ctx;
        nsConn->connNum = ctx->connNum++;
        handle->connection = nsConn;

        if (DbProxyStart(nsConn) == NS_OK) {

	    /*
	     * Ping daemon
	     */
	    
            if (DbProxyIsAlive(handle)) {
                /*
		 * daemon is alive, proceed...
		 */

		/*
		 * consider this 'connected' so that we can shut down the
		 * daemon when/if there's a problem opening.  otherwise
		 * a 'zombie' nssybpd will be left lying around.
		 */
		
		handle->connected = 1;

                if (DbProxySend(handle, Open, dsOpenData.string,
                        strlen(dsOpenData.string)) == NS_OK &&
                    DbProxyCheckStatus(nsConn, handle) == NS_OK) {

		    char *temp = ns_malloc(RSP_BUFSIZE);
		    
		    if (DbProxyIdentify(handle, temp) == NS_OK) {
			Ns_Log(Notice, "nsext: "
			       "datasource opened: %s with %s",
			       handle->datasource, ctx->ident); 
			status = NS_OK;
                        Ns_MutexLock(&ctx->muIdent);
			strcpy(ctx->ident, temp);
			Ns_MutexUnlock(&ctx->muIdent);
		    }

		    ns_free(temp);
                }
            }
        } 
        Ns_DStringFree(&dsOpenData);
    }
    if (status == NS_ERROR) {
	 DbProxyCleanup(handle);
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtCloseDb --
 *
 *	Close an external connection on an nsdb handle. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Will free the NsExtConn. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtCloseDb(Ns_DbHandle *handle)
{
    NsExtConn   *nsConn;
    int          status = NS_OK;

    if (handle == NULL || handle->connection == NULL) {
	Ns_Log(Bug, "nsext: connection handle is null");
	status = NS_ERROR;
	goto done;
    }
    
    nsConn = handle->connection;
    DbProxySend(handle, Close, NULL, 0);

    if ((status = DbProxyCheckStatus(nsConn, handle)) != NS_OK) {
        Ns_Log(Error, "nsext: error closing connection: %d",
	       nsConn->connNum);
    } 
    if (handle->connected) {            
	 /*
	  * shutdown the proxy daemon in any case
	  */
	
	 DbProxyStop(nsConn);
    }
    ns_free(nsConn);
    handle->connection = NULL;
    handle->connected = 0;
    
done:
    return (status);
}


/*
 *----------------------------------------------------------------------
 *
 * ExtExec --
 *
 *	Send an external query. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtExec(Ns_DbHandle *handle, char *sql)
{
    NsExtConn *nsConn;
    char       respBuf[RESPBUFMAX];
    int        retcode = NS_ERROR;

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;

    if (DbProxySend(handle, Exec, sql, strlen(sql)) == NS_OK &&
        DbProxyCheckStatus(nsConn, handle) == NS_OK) {
        if (DbProxyGetString(handle, respBuf, RESPBUFMAX) == NS_OK) {
            if (strcmp(respBuf, EXEC_RET_ROWS) == 0) {
                retcode = NS_ROWS;
            } else if (strcmp(respBuf, EXEC_RET_DML) == 0) {
                retcode = NS_DML;
            } else {
                retcode = NS_ERROR;
            }
        }
    }
    return retcode;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtBindRow --
 *
 *	Put a row from a select into an Ns_Set. 
 *
 * Results:
 *	An Ns_Set contianing key/value pairs. 
 *
 * Side effects:
 *	Creates the Ns_Set returned. 
 *
 *----------------------------------------------------------------------
 */

static Ns_Set  *
ExtBindRow(Ns_DbHandle *handle)
{
    Ns_Set              *rows = NULL;
    int                  status = NS_ERROR;
    NsExtConn           *nsConn;
    Ns_List             *colList, *currCol;
    DbProxyInputElement *colEl;

    assert(handle != NULL);
    nsConn = handle->connection;
    
    if (DbProxySend(handle, BindRow, NULL, 0) == NS_OK &&
        DbProxyCheckStatus(nsConn, handle) == NS_OK) {
        if ((colList = DbProxyGetList(handle)) != NULL) {
	    rows = handle->row;
	    Ns_SetTrunc(rows, 0);

            for (currCol = colList; currCol != NULL;
		 currCol = Ns_ListRest(currCol)) {
		
                colEl = Ns_ListFirst(currCol);

		/*
		 * colEl->data is NULL-terminated
		 * (colEl->size included NULL)
		 */
		
                Ns_SetPut(rows, colEl->data, NULL);
                ns_free(colEl->data);
                colEl->data = NULL;
            }

            Ns_ListFree(colList, (Ns_ElemVoidProc *) ExtFree);
            status = NS_OK;
        }
    }
    return rows;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtGetRow --
 *
 *	Fetch rows after an ExtBindRow. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Puts a row into the passed-in set. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtGetRow(Ns_DbHandle *handle, Ns_Set *row)
{
    int                  status = NS_ERROR;
    int                  i;
    char                 colCountStr[32];
    NsExtConn           *nsConn;
    Ns_List             *rowList, *currRow;
    DbProxyInputElement *rowEl;
    int                  trimcnt;

    assert(handle != NULL);
    nsConn = handle->connection;
    sprintf(colCountStr, "%d", Ns_SetSize(row));

    if (DbProxySend(handle, GetRow, colCountStr,
		    strlen(colCountStr)) == NS_OK &&
        DbProxyCheckStatus(nsConn, handle) == NS_OK) {
        if ((rowList = DbProxyGetList(handle)) != NULL) {

            for (currRow = rowList, i = 0; currRow != NULL;
                currRow = Ns_ListRest(currRow), i++) {
                rowEl = Ns_ListFirst(currRow);
		
                /*
		 * rowEl->data is NULL-terminated (rowEl->size included NULL)
		 * String trim the value if called for
		 */

                if (nsConn->ctx->trimdata == NS_TRUE) {
                    for (trimcnt = rowEl->size - 1;
                         trimcnt >= 0;
                         trimcnt--) {
                        if (rowEl->data[trimcnt] == 0x20) {
                            rowEl->data[trimcnt] = '\0';
                            rowEl->size--;
                            if (rowEl->data[trimcnt-1] != 0x20) {
                                goto trimdone;
                            }
                        } else {
                            if (rowEl->data[trimcnt-1] != 0x20) {
                                goto trimdone;
                            }
                        }
                    }
                }
		trimdone:
                Ns_SetPutValue(row, i, rowEl->size == 0 ? NULL : rowEl->data);
            }
            Ns_ListFree(rowList, (Ns_ElemVoidProc *) ExtFree);
            status = NS_OK;

	    /*
	     * got 0-length list, end of data
	     */
        } else {                        
            status = NS_END_DATA;
        }
    }
    return status;
}



/*
 *----------------------------------------------------------------------
 *
 * ExtFlush --
 *
 *	Flush any waiting rows not needed after an Ns_DbSelect(). 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtFlush(Ns_DbHandle *handle)
{
    NsExtConn *nsConn;
    int        status;

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;
    DbProxySend(handle, Flush, NULL, 0);
    status = DbProxyCheckStatus(nsConn, handle);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ExtCancel --
 *
 *	Cancel any pending request. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
Ns_ExtCancel(Ns_DbHandle *handle)
{
    NsExtConn *nsConn;
    int        status;

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;
    DbProxySend(handle, Cancel, NULL, 0);
    status = DbProxyCheckStatus(nsConn, handle);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtInterpInit --
 *
 *	Add the "ns_ext" command to a single Tcl interpreter. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtInterpInit(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateCommand(interp, "ns_ext", ExtCmd, NULL, NULL);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtServerInit --
 *
 *	Have ExtInterpInit called for each interpreter in the virtual 
 *	server which is being intialized. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtServerInit(char *hServer, char *hModule, char *hDriver)
{
    if (!Ns_ConfigGetBool ("ns/parameters", "verbose", &verbose)) {
	verbose = NS_FALSE;
    }

    return Ns_TclInitInterps(hServer, ExtInterpInit, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * AllDigits --
 *
 *	Determines if a string contains only digits. 
 *
 * Results:
 *	1 if all digits, 0 if not. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
AllDigits(char *str)
{
    char *p = str;

    if (*p == '-') {
        p++;
    }
    while (*p) {
        if (!isdigit( (unsigned char)*p++ ) ) {
            return 0;
        }
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtResetHandle --
 *
 *	The function is only here for the Oracle external driver. 
 *	Oracle has autocommit off by default which means major damage 
 *	can occur if the transaction is munged. So before a handle is 
 *	put back into a pool (Ns_DbPoolPutHandle called from dbtcl.c 
 *	with `ns_db close` or `ns_db releasehandle`, this guy can 
 *	register a function to reset the handle (i.e., check its 
 *	status and perform whatever cleanup measures that are 
 *	necessary with a rollback, etc.). We may or may not have a 
 *	good use for this in the future. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int 
ExtResetHandle(Ns_DbHandle *handle)
{
    char         statusBuf[STATUS_BUFSIZE];
    int          status = NS_ERROR;
    
    if (handle == NULL || handle->connection == NULL) {
	
	Ns_Log(Error, "nsext: %s is null", 
	       ((handle == NULL)?("handle"):("connection")));
    } else {
	DbProxySend(handle, ResetHandle, NULL, 0);
	if ((status = DbProxyGetString(handle, statusBuf,
				       STATUS_BUFSIZE)) == NS_OK) {
	    if (strncasecmp(statusBuf, OK_STATUS, 2) == 0) {
		status = NS_OK;
	    } else {
		Ns_Log(Error,"nsext: "
		       "protocol error: received|%s| expected|%s|",
		       statusBuf, OK_STATUS);
		
		DbProxyCleanup(handle);
		status = NS_ERROR;
	    }
	}
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtSpStart --
 *
 *	Begin an SP. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int 
ExtSpStart(Ns_DbHandle *handle, char *procname)
{
    NsExtConn   *nsConn;
    int          status;

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;
    DbProxySend(handle, SpStart, procname, strlen(procname));
    status = DbProxyCheckStatus(nsConn, handle);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtSpSetParam --
 *
 *	Set a param for an SP. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int 
ExtSpSetParam(Ns_DbHandle *handle, char *args)
{
    NsExtConn   *nsConn;
    int          status;

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;
    DbProxySend(handle, SpSetParam, args, strlen(args));
    status = DbProxyCheckStatus(nsConn, handle);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtSpExec --
 *
 *	Run an SP. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtSpExec(Ns_DbHandle *handle)
{
    NsExtConn   *nsConn;
    int          retcode = NS_ERROR;
    char         respBuf[RESPBUFMAX];

    assert(handle != NULL);
    assert(handle->connection != NULL);
    nsConn = handle->connection;
    DbProxySend(handle, SpExec, NULL, 0);
    retcode = DbProxyCheckStatus(nsConn, handle);
    if (retcode == NS_OK) {
        if (DbProxyGetString(handle, respBuf, RESPBUFMAX) == NS_OK) {
            if (strcmp(respBuf, EXEC_RET_ROWS) == 0) {
                retcode = NS_ROWS;
            } else if (strcmp(respBuf, EXEC_RET_DML) == 0) {
                retcode = NS_DML;
            } else {
                retcode = NS_ERROR;
            }
        }
    }

    return retcode;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtSpReturnCode --
 *
 *	Returns the return code from the just-run SP. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtSpReturnCode(Ns_DbHandle *dbhandle, char *returnCode, int bufsize)
{
    int status = NS_ERROR;

    if (DbProxySend(dbhandle, SpReturnCode, NULL, 0) == NS_OK &&
        DbProxyGetString(dbhandle, returnCode, bufsize) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'returncode' command to proxy daemon failed");
        returnCode[0] = '\0';
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ExtGetParams --
 *
 *	Fetch rows after an Ns_ExtExProc. 
 *
 * Results:
 *	A set of rows. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static Ns_Set *
ExtSpGetParams(Ns_DbHandle *handle)
{
    int                  status = NS_ERROR;
    int                  i;
    NsExtConn           *nsConn;
    Ns_Set              *paramSet = NULL;
    Ns_List             *paramList, *rowList, *currParam, *currRow;
    DbProxyInputElement *rowEl, *paramEl;
    int                  trimcnt;

    assert(handle != NULL);
    nsConn = handle->connection;

    if (DbProxySend(handle, SpGetParams, NULL, 0) == NS_OK &&
        DbProxyCheckStatus(nsConn, handle) == NS_OK) {
        if ((paramList = DbProxyGetList(handle)) != NULL) {
            if ((rowList = DbProxyGetList(handle)) == NULL) {
                Ns_Log(Error, "nsext: rowlist did not arrive");
                Ns_ListFree(paramList, (Ns_ElemVoidProc *) ExtFree);
                return NULL;
            }
            paramSet = Ns_SetCreate(NULL);
            for (currParam = paramList, currRow = rowList, i = 0;
                 currRow != NULL && currParam != NULL;
                 currParam = Ns_ListRest(currParam),
		     currRow = Ns_ListRest(currRow), i++) {
		
                paramEl = Ns_ListFirst(currParam);
                rowEl   = Ns_ListFirst(currRow);
                /*
		 * rowEl->data is NULL-terminated (rowEl->size included NULL)
		 * String trim the value if called for
		 */

                if (nsConn->ctx->trimdata == NS_TRUE) {
                    for (trimcnt = rowEl->size - 1;
                         trimcnt >= 0;
                         trimcnt--) {
                        if (rowEl->data[trimcnt] == 0x20) {
                            rowEl->data[trimcnt] = '\0';
                            rowEl->size--;
                            if (rowEl->data[trimcnt-1] != 0x20) {
                                goto trimdone;
                            }
                        } else {
                            if (rowEl->data[trimcnt-1] != 0x20) {
                                goto trimdone;
                            }
                        }
                    }
                }
		
	     trimdone:
                Ns_SetPut(paramSet, paramEl->data, NULL);
                Ns_SetPutValue(paramSet, i,
                               rowEl->size == 0 ? NULL : rowEl->data);
            }
            Ns_ListFree(paramList, (Ns_ElemVoidProc *) ExtFree);
            Ns_ListFree(rowList,   (Ns_ElemVoidProc *) ExtFree);
            status = NS_OK;

        } else {
	    /*
	     * got 0-length list, end of data
	     */
            status = NS_NO_DATA;
        }
    }

    if (status == NS_OK) {
        return paramSet;
    } 
    else {
        return NULL;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ExtFree --
 *
 *	Free a dbproxyinputelement. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Frees both the data and the struct. 
 *
 *----------------------------------------------------------------------
 */

static void
ExtFree(void *ptr)
{
    DbProxyInputElement *element;
    char                *data;

    if (ptr != NULL) {
        element = (DbProxyInputElement *) ptr;
        data    = element->data;
        ns_free(data);
        ns_free(element);
    }
}

/*
 *==========================================================================
 * Callback functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * DbProxyStart --
 *
 *	Starts a new connection. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	May spawn a new proxy daemon. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyStart(NsExtConn * nsConn)
{
    if (nsConn->ctx->path != NULL) {
        return LocalProxy(nsConn);
    } else {
        return NetProxy(nsConn, nsConn->ctx->host, nsConn->ctx->port);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyStop --
 *
 *	Stops an external proxy daemon. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Closes daemon sockets. 
 *
 *----------------------------------------------------------------------
 */

static void
DbProxyStop(NsExtConn * nsConn)
{
    Ns_Log(Debug, "nsext: stopping db proxy daemon connection %d",
	   nsConn->connNum);
    close(nsConn->socks[0]);
    close(nsConn->socks[1]);
    nsConn->socks[0] = nsConn->socks[1] = -1;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxySend --
 *
 *	Write to the proxy. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxySend(Ns_DbHandle *dbhandle, Ns_ExtDbCommandCode msgType, char *arg,
	    size_t argSize)
{
    int             status = NS_ERROR;
    int             arglen;
    char           *msg;
    int             msglen = 0;
    NsExtConn      *nsConn = (NsExtConn *) dbhandle->connection;  
    Ns_DString      ds;

    Ns_DStringInit(&ds);

    if ((msg = Ns_ExtDbMsgCodeToName(msgType)) == NULL) {
        Ns_Log(Bug, "nsext: "
               "unknown message type received for connection %d",
               nsConn->connNum);
    } else if (arg == NULL && Ns_ExtDbMsgRequiresArg(msgType)) {
        Ns_Log(Bug, "nsext: "
	       "'%s' message requires argument (connection %d)",
               Ns_ExtDbMsgCodeToName(msgType), nsConn->connNum);
    } else {

        if (arg == NULL) {
            arglen = 0;
        } else {
            arglen = argSize;
        }

        Ns_DStringPrintf(&ds, "%s\n%d\n", msg, arglen);
        msglen = ds.length;

        if (arg != NULL) {
            Ns_DStringAppend(&ds, arg);
            msglen += arglen;
        }
        if (DbProxyTimedIO(nsConn->socks[1], ds.string, msglen, 0,
                           Write, nsConn->ctx->timeout, dbhandle,
			   0) == NS_ERROR) {
	    
            Ns_Log(Error, "nsext: error sending buffer(%s): %s",
                   ds.string, strerror(errno));
        } else {
            if (nsConn->ctx->ioTrace) {
                Ns_Log(Notice, "SENT |%s|", ds.string);
            }
            status = NS_OK;
        }
    }
    Ns_DStringFree(&ds);
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyGetPingReply --
 *
 *	Recieves the ping reply from the proxy. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyGetPingReply(Ns_DbHandle *dbhandle)
{
    int  status;
    char statusBuf[STATUS_BUFSIZE];

    if ((status = DbProxyGetString(dbhandle, statusBuf,
				   STATUS_BUFSIZE)) == NS_OK) {
	
        if (strcasecmp(statusBuf, OK_STATUS) == 0) {
            status = NS_OK;
        } else {
            Ns_Log(Error, "nsext: protocol error on ping: "
		   "received|%s| expected|%s|", statusBuf, OK_STATUS);
	    
            DbProxyCleanup(dbhandle);
            status = NS_ERROR;
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyCheckStatus --
 *
 *	Checks return status. On error, copies exception code/message 
 *	to handle 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyCheckStatus(NsExtConn * nsConn, Ns_DbHandle *handle)
{
    int   status = NS_ERROR;
    char  statusBuf[STATUS_BUFSIZE];
    char *exceptionCode, *exceptionMsg, *p;

    if (DbProxyGetString(handle, statusBuf, STATUS_BUFSIZE) == NS_OK) {
        if (strcasecmp(statusBuf, OK_STATUS) == 0) {
            status = NS_OK;
	    
	    /*
	     * Clear out any old messages.  This was added because of
	     * a problem encountered when processing multiple result
	     * sets [e.g. multiple selects] from a Sybase stored
	     * procedure.  You'd Ns_DbBindRow until you couldn't bind
	     * no more.  The last bind would result in an error, which
	     * would put a message into dsExceptionMsg.  After that
	     * binding work is done, that message is still in there,
	     * so on the next Exec, an Ns_DbLogSql would see there's
	     * content there, and would output a message, leading to
	     * end-user confusion due to an inexplicable error message
	     * happening.  Hopefully this won't silence Real messages...
	     */
	    
	    Ns_DStringTrunc(&handle->dsExceptionMsg, 0);
	} else if (strstr(statusBuf, SILENT_ERROR_STATUS) != NULL) {
	    if (verbose) {
		Ns_Log(Debug, "nsext: "
		       "silent error string '%s'", statusBuf);
	    }
        } else {
	    /*
	     * db error: exception form is
	     * exceptionCode#exceptionMessage
	     */
	    
            exceptionCode = statusBuf;
            if ((p = strchr(statusBuf, (int) ARG_TOKEN_DELIMITER)) == NULL) {
                /*
		 * generic error message, not a db exception, just log msg
		 */
		
                Ns_Log(Error, "nsext: "
		       "database error message: '%s'", statusBuf);
            } else {
                *p++ = '\0';
                if (*p == '\0') {
                    Ns_Log(Error, "nsext: "
			   "invalid exception status string: '%s'", statusBuf);
                } else {
                    exceptionMsg = p;
                    Ns_Log(Debug, "nsext: received exception code=%s msg=%s",
			   exceptionCode, exceptionMsg);
                    strcpy(handle->cExceptionCode, exceptionCode);
                    Ns_DStringFree(&handle->dsExceptionMsg);
                    Ns_DStringAppend(&handle->dsExceptionMsg, exceptionMsg);
                }
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyTimedIO --
 *
 *	Perform IO with the proxy; may read or write depending on 
 *	iotype flg. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyTimedIO(int sock, char *buf, int nbytes, int flags,
	       SockIOType iotype, int timeout, Ns_DbHandle *dbhandle,
	       int readExact)
{
    int status = NS_OK;
    int ioreturn;

    if (timeout > 0) {
        fd_set         set;
        int            nsel;
        struct timeval tv;
        int         max = sock;

        tv.tv_sec = timeout;
        tv.tv_usec = 0;
        FD_ZERO(&set);
        FD_SET(sock, &set);
        if (iotype == Write) {
            nsel = Ns_Select(max + 1, NULL, &set, NULL, &tv);
        } else {
            nsel = Ns_Select(max + 1, &set, NULL, NULL, &tv);
        }
        if (nsel <= 0 || !FD_ISSET(sock, &set)) {
            if (nsel == 0) {
                Ns_Log(Warning, "nsext: "
		       "exceeded proxy i/o timeout (%d seconds)", timeout);
                DbProxyCleanup(dbhandle);
            } else {
                Ns_Log(Error, "nsext: "
		       "select() of %d failed: %s (code %d)",
		       sock, strerror(errno), errno);
            }
            status = NS_ERROR;
        }
    }
    if (status == NS_OK) {
        if (iotype == Read) {
            int bytesRead = 0;

#ifdef USE_FIONREAD	    
	    int bytesAvail;
	    if (flags & MSG_PEEK) {
		ioctl(sock, FIONREAD, &bytesAvail);
		if (bytesAvail < nbytes) {
		    nbytes = bytesAvail;
		}
	    }
#endif
	    
            while (bytesRead < nbytes) {
                ioreturn = recv(sock, buf + bytesRead, (size_t)(nbytes - bytesRead),
                   flags);
                if (ioreturn < 0) {
                    break;
		}
		bytesRead += ioreturn;
		if (flags & MSG_PEEK) {
		    break;
		}
            }
            ioreturn = bytesRead;

        } else {
            ioreturn = send(sock, buf, (size_t)nbytes, flags);
        }

        if (ioreturn != nbytes) {
            if (iotype == Write || ioreturn <= 0 || readExact) {
                status = NS_ERROR;
                if (ErrnoPeerClose(errno)) {
                    Ns_Log(Warning, "nsext: "
			   "connection dropped by external proxy daemon");
                } else {
                    Ns_Log(Error, "nsext: socket %s %d failed: "
			   "%s (code %d), ioreturn=%d, nbytes=%d",
			   iotype == Write ? "write to" : "read from", sock,
			   strerror(errno), errno,
			   ioreturn, nbytes);
                }
                DbProxyCleanup(dbhandle);
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyCleanup --
 *
 *	Zero out an Ns_DbHandle. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	If connected, will stop the connection. 
 *
 *----------------------------------------------------------------------
 */

static void
DbProxyCleanup(Ns_DbHandle *handle)
{
    if (handle != NULL) {
	if (handle->connected) {
	    DbProxyStop(handle->connection);
	}
	ns_free(handle->connection);
	handle->connection = NULL;
	handle->connected = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyGetString --
 *
 *	Read a single string from the proxy. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Will read from proxy. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyGetString(Ns_DbHandle *dbhandle, char *buf, int maxbuf)
{
    int        status = NS_ERROR;
    char      *nlTailPos;
    int        strSize;
    NsExtConn *nsConn = dbhandle->connection;

    if (DbProxyTimedIO(nsConn->socks[0], buf, maxbuf, MSG_PEEK,
            Read, nsConn->ctx->timeout, dbhandle, 0) >= 0) {

        if ((nlTailPos = strchr(buf, (int) '\n')) == NULL) {
            Ns_Log(Error, "nsext: "
		   "protocol error: no record separator in '%s'", buf);
            DbProxyCleanup(dbhandle);
        } else {
            strSize = nlTailPos - buf;
            strSize++;
            if (DbProxyTimedIO(nsConn->socks[0], buf, strSize, 0,
                    Read, nsConn->ctx->timeout, dbhandle, 0) == NS_OK) {
		
                *nlTailPos = '\0';
                if (nsConn->ctx->ioTrace) {
                    Ns_Log(Notice, "REC |%s|", buf);
                }
                status = NS_OK;
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyGetList --
 *
 *	Read a list from the proxy. 
 *
 * Results:
 *	A list or NULL if error. 
 *
 * Side effects:
 *	Allocates new list. 
 *
 *----------------------------------------------------------------------
 */

static Ns_List *
DbProxyGetList(Ns_DbHandle *dbhandle)
{
    Ns_List             *destList;
    int                  status = NS_OK;
    int                  done = 0;
    char                *datum;
    DbProxyInputElement *el;
    NsExtConn           *nsConn = dbhandle->connection;
    int                  size;
    char                 sizebuf[MAX_SIZEDIGITS]; /* holds ASCII representation
                                                   * of datasize */
    destList = NULL;
    while (status == NS_OK && !done) {
        if ((status = DbProxyGetString(dbhandle, sizebuf,
				       MAX_SIZEDIGITS)) == NS_OK) {
            if (!AllDigits(sizebuf)) {
                Ns_Log(Error, "nsext: "
		       "protocol error: number expected, got '%s'", sizebuf);
                status = NS_ERROR;
            } else if ((size = atoi(sizebuf)) > nsConn->ctx->maxElementSize) {
                Ns_Log(Error, "nsext: "
		       "exceeded element size limit of %d", size);
                status = NS_ERROR;
            } else if (size == END_LIST_VAL) {
                done = 1;
            } else {
                datum = ns_malloc((size_t)(size + 1));
		/*
		 * add NULL until nsdb does
		 * binary data
		 */
		
                if (size && DbProxyTimedIO(nsConn->socks[0], datum, size, 0,
                        Read, nsConn->ctx->timeout, dbhandle, 1) != NS_OK) {
		    
                    Ns_Log(Error, "nsext: "
			   "read error: %s", strerror(errno));
                    status = NS_ERROR;
                } else {
                    datum[size] = '\0';
                    if (nsConn->ctx->ioTrace) {
                        Ns_Log(Notice, "REC |%s|", datum);
                    }
                    el = ns_malloc(sizeof(DbProxyInputElement));
                    el->size = size == 0 ? 0 : size + 1;
                    el->data = datum;
                    Ns_ListPush(el, destList);
                }
            }
        }
    }
    if (status == NS_ERROR) {
        Ns_ListFree(destList, (Ns_ElemVoidProc *) ExtFree);
        destList = NULL;
        DbProxyCleanup(dbhandle);
    } else {
        destList = Ns_ListNreverse(destList);
    }
    
    return destList;
}



/*
 *----------------------------------------------------------------------
 *
 * DbProxyIsAlive --
 *
 *	Determines if the proxy is still running. 
 *
 * Results:
 *	Boolean: false=no, true=yes 
 *
 * Side effects:
 *	Pings the proxy. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyIsAlive(Ns_DbHandle *dbhandle)
{
    return (DbProxySend(dbhandle, Ping, NULL, 0) == NS_OK &&
            DbProxyGetPingReply(dbhandle) == NS_OK);
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyIdentify --
 *
 *	Get the proxy's name. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Talks to the proxy. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyIdentify(Ns_DbHandle *dbhandle, char *identbuf)
{
    int status = NS_OK;

    if (DbProxySend(dbhandle, Identify, NULL, 0) == NS_OK &&
        DbProxyGetString(dbhandle, identbuf, RSP_BUFSIZE) == NS_OK) {
        /*
	 * result is now in identbuf
	 */
    } else {
        Ns_Log(Error, "nsext: "
	       "'identify' command to proxy daemon failed");
        sprintf(identbuf, "Error: Identify command to Proxy Daemon failed\n");
	status = NS_ERROR;
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyGetTypes --
 *
 *	Issues the GetTypes command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR. Puts the results in typebuf. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyGetTypes(Ns_DbHandle *dbhandle, char *typesbuf)
{
    int status = NS_ERROR;

    if (DbProxySend(dbhandle, GetTypes, NULL, 0) == NS_OK &&
        DbProxyGetString(dbhandle, typesbuf, RSP_BUFSIZE) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'gettypes' command to proxy daemon failed");
        typesbuf[0] = '\0';
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyResultId --
 *
 *	Issues the ResultId command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR; fills in idbuf with result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyResultId(Ns_DbHandle *dbhandle, char *idbuf)
{
    int             status = NS_ERROR;

    if (DbProxySend(dbhandle, ResultId, NULL, 0) == NS_OK &&
        DbProxyGetString(dbhandle, idbuf, RSP_BUFSIZE) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'resultid' command to proxy daemon failed");
        idbuf[0] = '\0';
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyResultRows --
 *
 *	Issues the ResultRows command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR; fills in rowCountStr with result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyResultRows(Ns_DbHandle *dbhandle, char *rowCountStr)
{
    int status = NS_ERROR;

    if (DbProxySend(dbhandle, ResultRows, NULL, 0) == NS_OK &&
        DbProxyGetString(dbhandle, rowCountStr, RSP_BUFSIZE) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'resultrows' command to proxy daemon failed");
        rowCountStr[0] = '\0';
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxySetMaxRows --
 *
 *	Issues the SetMaxRows command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR; fills in maxRowsStr with result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxySetMaxRows(Ns_DbHandle *dbhandle, char *maxRowsStr)
{
    int  status = NS_ERROR;

    if (DbProxySend(dbhandle, SetMaxRows, maxRowsStr,
		    strlen(maxRowsStr)) == NS_OK &&
        DbProxyGetPingReply(dbhandle) == NS_OK) {
	
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'setmaxrows' command to proxy daemon failed");
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyTraceOn --
 *
 *	Issues the TraceOn command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR; fills in filepath with result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyTraceOn(Ns_DbHandle *dbhandle, char *filepath)
{
    int status = NS_ERROR;

    if (DbProxySend(dbhandle, TraceOn, filepath, strlen(filepath)) == NS_OK &&
        DbProxyGetPingReply(dbhandle) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'traceon' command to proxy daemon failed");
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyTraceOff --
 *
 *	Issues the TraceOff command to the proxy daemon. 
 *
 * Results:
 *	NS_OK/NS_ERROR;
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyTraceOff(Ns_DbHandle *dbhandle)
{
    int status = NS_ERROR;

    if (DbProxySend(dbhandle, TraceOff, NULL, 0) == NS_OK &&
        DbProxyGetPingReply(dbhandle) == NS_OK) {
        status = NS_OK;
    } else {
        Ns_Log(Error, "nsext: "
	       "'traceoff' command to proxy daemon failed");
    }
    
    return status;
}

/*
 *----------------------------------------------------------------------
 *
 * DbProxyCreateRemoteTmpFile --
 *
 *	Ask the proxy daemon to make a remote temp file. 
 *
 * Results:
 *	NS_OK/NS_ERROR; fills in remoteFileName with the remote 
 *	side's filename. 
 *
 * Side effects:
 *	Creates a remote temp file, or fills in an error in errbuf. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyCreateRemoteTmpFile(Ns_DbHandle *dbhandle, char *remoteFileName, char *errbuf)
{
    int  status = NS_ERROR;
    char respbuf[RSP_BUFSIZE];

    if (DbProxySend(dbhandle, CreateTmpF, NULL, 0) != NS_OK) {
        sprintf(errbuf, "Can't send CreateTmpF command to Proxy Daemon");
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get CreateTmpF status response "
		"from Proxy Daemon");
    } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
        strcpy(errbuf, respbuf);
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get CreateTmpF name response "
		"from Proxy Daemon");
    } else {
        strcpy(remoteFileName, respbuf);
        status = NS_OK;
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyCopyToRemoteFile --
 *
 *	Copy a local file to a remote file. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Fills in errbuf with an error on failure. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyCopyToRemoteFile(Ns_DbHandle *dbhandle, char *srcFile, char *remoteFileName,
    char *errbuf)
{
    int  status = NS_ERROR;
    char remoteFdStr[4];
    char readbuf[FILE_IOSIZE];
    char outbuf[FILE_IOSIZE + 128];
    char respbuf[RSP_BUFSIZE];
    char remoteOpenParams[512];
    int  dataOffset;
    int  bytesRead;
    int  localSrcFd = -1;
    int  fileLoc;
    int  done = 0;

    remoteFdStr[0] = '\0';
    sprintf(remoteOpenParams, "%d%c%s", O_RDWR, ARG_TOKEN_DELIMITER,
        remoteFileName);
    if ((localSrcFd = open(srcFile, O_RDONLY, 0)) < 0) {
        sprintf(errbuf, "open on %s failed: %s", srcFile, strerror(errno));
    } else if (DbProxySend(dbhandle, OpenF, remoteOpenParams,
            strlen(remoteOpenParams)) != NS_OK) {
        sprintf(errbuf, "Can't send OpenFcommand to Proxy Daemon");
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get OpenF status response from Proxy Daemon");
    } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
        strcpy(errbuf, respbuf);
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get OpenF fd response from Proxy Daemon");
    } else {
        strcpy(remoteFdStr, respbuf);

        /*
         * we now have a local (source) and remote (destination) file
         * descriptor
         */
        fileLoc = 0;
        status = NS_OK;
        while (!done && status == NS_OK) {
            status = NS_ERROR;
            if ((bytesRead = read(localSrcFd, readbuf, FILE_IOSIZE)) > 0) {
                /*
		 * package data for transmission
		 */
                dataOffset = sprintf(outbuf, "%s%c%d%c%d%c",
				     remoteFdStr, ARG_TOKEN_DELIMITER,
				     fileLoc, ARG_TOKEN_DELIMITER,
				     bytesRead, ARG_TOKEN_DELIMITER);
                memcpy(&outbuf[dataOffset], readbuf, (size_t)bytesRead);
                if (DbProxySend(dbhandle, WriteF, outbuf,
                        (size_t)(dataOffset + bytesRead)) != NS_OK) {
                    sprintf(errbuf, "Remote write failure, file offset=%d",
			    fileLoc);
                } else if (DbProxyGetString(dbhandle, respbuf,
					    RSP_BUFSIZE) != NS_OK) {
		    
                    sprintf(errbuf, "Can't get WriteF status response "
			    "from Proxy Daemon");
                } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
                    strcpy(errbuf, respbuf);
                } else {
                    status = NS_OK;
                    fileLoc += bytesRead;
                }
            } else if (bytesRead == 0) {
                status = NS_OK;
                done = 1;
            } else {
                sprintf(errbuf, "read on %s failed: %s",
			srcFile, strerror(errno));
            }
        }
    }
    if (localSrcFd != -1) {
        close(localSrcFd);
    }
    if (remoteFdStr[0] != '\0') {
	/*
	 * close remote fd
	 */
	
        status = NS_ERROR;
        if (DbProxySend(dbhandle, CloseF, remoteFdStr,
                strlen(remoteFdStr)) != NS_OK) {
            sprintf(errbuf, "Can't send CloseF command to Proxy Daemon");
        } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
            sprintf(errbuf,
		    "Can't get CloseF status response from Proxy Daemon");
        } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
            strcpy(errbuf, respbuf);
        } else {
            status = NS_OK;
        }
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyCopyFromRemoteFile --
 *
 *	Copy a remote file from the proxy to a local file. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Writes to a local file, or fills in errbuf on error. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyCopyFromRemoteFile(Ns_DbHandle *dbhandle, char *destFile,
			  char *remoteFileName, char *errbuf)
{
    int                  status = NS_ERROR;
    char                 remoteFdStr[4];
    char                 readRequest[128];
    char                 respbuf[RSP_BUFSIZE];
    char                 remoteOpenParams[512];
    int                  localDestFd = -1;
    int                  fileLoc;
    int                  done = 0;
    Ns_List             *readInfo;
    DbProxyInputElement *readEl;

    remoteFdStr[0] = '\0';
    sprintf(remoteOpenParams, "%d%c%s", O_RDONLY, ARG_TOKEN_DELIMITER,
	    remoteFileName);
    if ((localDestFd = open(destFile, O_CREAT | O_TRUNC | O_WRONLY,
			    0644)) < 0) {
	
        sprintf(errbuf, "open/create on %s failed: %s", destFile,
		strerror(errno));
    } else if (DbProxySend(dbhandle, OpenF, remoteOpenParams,
			   strlen(remoteOpenParams)) != NS_OK) {
        sprintf(errbuf, "Can't send OpenF command to Proxy Daemon");
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get OpenF status response from Proxy Daemon");
    } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
        strcpy(errbuf, respbuf);
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf, "Can't get OpenF fd response from Proxy Daemon");
    } else {
        strcpy(remoteFdStr, respbuf);

        /*
         * we now have a local (destination) and remote (source) file
         * descriptor
         */
	
        fileLoc = 0;
        status = NS_OK;
        while (!done && status == NS_OK) {
            status = NS_ERROR;
            sprintf(readRequest, "%s%c%d%c%d", remoteFdStr,
		    ARG_TOKEN_DELIMITER,
		    fileLoc, ARG_TOKEN_DELIMITER, FILE_IOSIZE);
            if (DbProxySend(dbhandle, ReadF, readRequest,
			        strlen(readRequest)) != NS_OK) {
                sprintf(errbuf, "Can't send ReadF command to Proxy Daemon");
            } else if (DbProxyGetString(dbhandle, respbuf,
					RSP_BUFSIZE) != NS_OK) {
		
                sprintf(errbuf, "Can't get ReadF status response "
			"from Proxy Daemon");
            } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
                strcpy(errbuf, respbuf);
            } else if ((readInfo = DbProxyGetList(dbhandle)) == NULL) {
                sprintf(errbuf, "Can't read data from Proxy Daemon");
            } else {
                readEl = Ns_ListFirst(readInfo);
                if (readEl->size == 0) {
                    status = NS_OK;
                    done = 1;
                } else if (readEl->size > 0) {
                    readEl->size--;
		    /*
		     * ignore NULL added by DbProxyGetList
		     */
		    
                    if (write(localDestFd, readEl->data,
			      (size_t)readEl->size) != readEl->size) {
			
                        sprintf(errbuf, "Local write to %s failed: %s",
				destFile, strerror(errno));
                        status = NS_ERROR;
                    } else {
                        status = NS_OK;
                        fileLoc += readEl->size;
                    }
                } else {
		    /*
		     * readEl->size < 0
		     */
		    
                    Ns_Log(Bug, "nsext: "
			   "negative size from remote read");
                    sprintf(errbuf,
			    "Read error from Proxy Daemon (negative size)");
                }
                ns_free(readEl->data);
                readEl->data = NULL;
                Ns_ListFree(readInfo, (Ns_ElemVoidProc *) ExtFree);
            }
        }
    }
    if (localDestFd != -1) {
        close(localDestFd);
    }
    if (remoteFdStr[0] != '\0') {
	/*
	 * close remote fd
	 */
	
        status = NS_ERROR;
        if (DbProxySend(dbhandle, CloseF, remoteFdStr,
                strlen(remoteFdStr)) != NS_OK) {
            sprintf(errbuf, "Can't send CloseF command to Proxy Daemon");
        } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
            sprintf(errbuf,
		    "Can't get CloseF status response from Proxy Daemon");
        } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
            strcpy(errbuf, respbuf);
        } else {
            status = NS_OK;
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DbProxyDeleteRemoteFile --
 *
 *	Deletes a file on the remote system 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Fills in errbuf on failure. 
 *
 *----------------------------------------------------------------------
 */

static int
DbProxyDeleteRemoteFile(Ns_DbHandle *dbhandle, char *remoteFileName, char *errbuf)
{
    int  status = NS_ERROR;
    char respbuf[RSP_BUFSIZE];

    if (DbProxySend(dbhandle, DeleteF, remoteFileName,
            strlen(remoteFileName)) != NS_OK) {
        sprintf(errbuf, "Unable to send DeleteF command Proxy Daemon");
    } else if (DbProxyGetString(dbhandle, respbuf, RSP_BUFSIZE) != NS_OK) {
        sprintf(errbuf,
		"Unable to get DeleteF status response from Proxy Daemon");
    } else if (strcasecmp(respbuf, OK_STATUS) != 0) {
        strcpy(errbuf, respbuf);
    } else {
        status = NS_OK;
    }
    
    return status;
}


/*
 *==========================================================================
 * Static functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * NetProxy --
 *
 *	Connect this NsExtConn to a host/port. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Sets the socket in nsConn 
 *
 *----------------------------------------------------------------------
 */

static int
NetProxy(NsExtConn *nsConn, char *host, int port)
{
    nsConn->socks[0] = nsConn->socks[1] = Ns_SockConnect(host, port);
    if (nsConn->socks[0] == -1) {
        Ns_Log(Error, "nsext: connect failure: %s:%d", host, port);
        return NS_ERROR;
    }
    Ns_Log(Notice, "nsext: connect success: %s:%d", host, port);
    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LocalProxy --
 *
 *	Connect to a proxy running on this very machine. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	Forks and execs a proxy. 
 *
 *----------------------------------------------------------------------
 */

static int
LocalProxy(NsExtConn * nsConn)
{
    int  in[2], out[2];
    char   *argv[3];
    int     status, pid, code;

    status = NS_ERROR;


    if (ns_sockpair(in) < 0) {
        Ns_Log(Error, "nsext: failed to create input socket pipes");
    } else {
        if (ns_sockpair(out) < 0) {
	    ns_sockclose(in[0]);
	    ns_sockclose(in[1]);
            Ns_Log(Error, "nsext: failed to create output socket pipes");
        } else {
            /*
             * Set CloseOnExec to avoid stuck-open connections.
             */
            Ns_CloseOnExec(in[0]);
            Ns_CloseOnExec(in[1]);
            Ns_CloseOnExec(out[0]);
            Ns_CloseOnExec(out[1]);
            argv[0] = nsConn->ctx->path;
            argv[1] = NULL;
            pid = Ns_ExecArgv(nsConn->ctx->path, NULL, out[0], in[1], argv,
			      NULL);
            ns_sockclose(out[0]);
            ns_sockclose(in[1]);
            if (pid == -1) {
                Ns_Log(Error, "nsext: spawn failed for '%s'",
		       nsConn->ctx->path);
            } else {
                if (Ns_WaitForProcess(pid, &code) == NS_OK) {
                    if (code != 0) {
                        Ns_Log(Error, "nsext: "
			       "proxy returned non-zero exit code: %d", code);
                    } else {
                        nsConn->socks[0] = in[0];
                        nsConn->socks[1] = out[1];
                        status = NS_OK;
                    }
                }
            }
            if (status != NS_OK) {
            	ns_sockclose(in[0]);
            	ns_sockclose(out[1]);
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtCmd --
 *
 *	This function implements the "ns_ext" Tcl command installed 
 *	into each interpreter of each virtual server. It provides 
 *	access to features specific to the external driver. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
ExtCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DbHandle *handle;
    char         rspbuf[RSP_BUFSIZE];
    char         errbuf[512];
    NsExtConn   *extconn;

    errbuf[0] = '\0';
    if (argc < 3) {
        Tcl_AppendResult(interp, "insufficient args: should be \"",
            argv[0], " command dbId (optional cmd arg)\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclDbGetHandle(interp, argv[2], &handle) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Make sure this is a external handle before accessing
     * handle->connection as an NsExtConn.
     */
    if (strstr(Ns_DbDriverName(handle), extName) == NULL) {
        Tcl_AppendResult(interp, "handle \"", argv[2], "\" is not of type \"",
            extName, "\"", NULL);
        return TCL_ERROR;
    }
    extconn = (NsExtConn *) handle->connection;
    if (!strcmp(argv[1], "ping")) {
        sprintf(interp->result, "%s", DbProxyIsAlive(handle) ? "ok" : "error");
    } else if (!strcmp(argv[1], "isremote")) {
        Tcl_SetResult(interp, extconn->ctx->path ? "0" : "1", TCL_STATIC);
    } else if (!strcmp(argv[1], "number")) {
        sprintf(interp->result, "%d", extconn->connNum);
    } else if (!strcmp(argv[1], "identify")) {
        Ns_MutexLock(&extconn->ctx->muIdent);
	sprintf(interp->result, "%s", extconn->ctx->ident);
	Ns_MutexUnlock(&extconn->ctx->muIdent);
    } else if (!strcmp(argv[1], "dbtype")) {
        sprintf(interp->result, "%s", ExtDbType(handle));
    } else if (!strcmp(argv[1], "resethandle")) {
      sprintf(interp->result, "%d", ExtResetHandle(handle));
    } else if (!strcmp(argv[1], "traceon")) {
        if (argc != 4) {
            Tcl_AppendResult(interp, "insufficient args: should be \"",
                argv[0], " traceon dbId filepath\"", NULL);
            return TCL_ERROR;
        }
        if (!Ns_PathIsAbsolute(argv[3])) {
            Tcl_AppendResult(interp, "filepath argument: \"",
                argv[3], "\" must be absolute.", NULL);
            return TCL_ERROR;
        }
        if (DbProxyTraceOn(handle, argv[3]) == NS_OK) {
            sprintf(interp->result, "Trace is on, using file: %s", argv[3]);
        } else {
            sprintf(interp->result,
                "Error: traceon command to file: %s failed", argv[3]);
        }
    } else if (!strcmp(argv[1], "traceoff")) {
        if (DbProxyTraceOff(handle) == NS_OK) {
            sprintf(interp->result, "Trace is off.");
        } else {
            sprintf(interp->result, "Error: traceoff command failed");
        }
    } else if (!strcmp(argv[1], "gettypes")) {
        if (DbProxyGetTypes(handle, rspbuf) == NS_OK) {
            char *p;

            if ((p = ns_strtok(rspbuf, DELIMITERS)) != NULL) {
                do {
                    Tcl_AppendElement(interp, p);
                } while ((p = ns_strtok(NULL, DELIMITERS)) != NULL);
            }
        } else {
            sprintf(interp->result, "Error: gettypes command failed");
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "resultid")) {
        if (DbProxyResultId(handle, rspbuf) == NS_OK) {
            sprintf(interp->result, rspbuf);
        } else {
            sprintf(interp->result, "Error: resultid command failed");
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "resultrows")) {
        if (DbProxyResultRows(handle, rspbuf) == NS_OK) {
            sprintf(interp->result, rspbuf);
        } else {
            sprintf(interp->result, "Error: resultrows command failed");
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "setmaxrows")) {
        if (argc != 4) {
            Tcl_AppendResult(interp, "insufficient args: should be \"",
                argv[0], " setmaxrows dbId rowcount\"", NULL);
            return TCL_ERROR;
        }
        if (!AllDigits(argv[3])) {
            Tcl_AppendResult(interp, "rowcount argument: \"",
                argv[3], "\" must be numeric.", NULL);
            return TCL_ERROR;
        }
        if (DbProxySetMaxRows(handle, argv[3]) == NS_OK) {
            sprintf(interp->result, "Max rows set to %s", argv[3]);
        } else {
            sprintf(interp->result, "Error: setmaxrows command failed");
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "mktemp")) {
        char            tmpName[512];

        if (DbProxyCreateRemoteTmpFile(handle, tmpName, errbuf) == NS_OK) {
            sprintf(interp->result, tmpName);
        } else {
            sprintf(interp->result, "mktemp error: %s", errbuf);
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "cpto")) {
        if (argc != 5) {
            Tcl_AppendResult(interp, "insufficient args: should be \"",
                argv[0], " cpto dbId localSrcFile remoteDestFile\"", NULL);
            return TCL_ERROR;
        }
        if (DbProxyCopyToRemoteFile(handle, argv[3], argv[4],
				    errbuf) != NS_OK) {
	    
            sprintf(interp->result, "cpto error: %s", errbuf);
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "cpfrom")) {
        if (argc != 5) {
            Tcl_AppendResult(interp, "insufficient args: should be \"",
                argv[0], " cpfrom dbId localDestFile remoteSrcFile\"", NULL);
            return TCL_ERROR;
        }
        if (DbProxyCopyFromRemoteFile(handle, argv[3], argv[4],
				      errbuf) != NS_OK) {
	    
            sprintf(interp->result, "cpfrom error: %s", errbuf);
            return TCL_ERROR;
        }
    } else if (!strcmp(argv[1], "rm")) {
        if (argc != 4) {
            Tcl_AppendResult(interp, "insufficient args: should be \"",
                argv[0], " rm dbId remoteFile\"", NULL);
            return TCL_ERROR;
        }
        if (DbProxyDeleteRemoteFile(handle, argv[3], errbuf) != NS_OK) {
            sprintf(interp->result, "rm error: %s", errbuf);
        }
    } else {
        Tcl_AppendResult(interp, "unknown command \"", argv[1],
            "\": should be ping, gettypes, number, traceon, traceoff, "
            "identify, resultid, resultrows, setmaxrows, "
            "mktemp, rm, cpto, cpfrom.",
            NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * ExtCleanup --
 *
 *	Clean up all data structures 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Frees everything. 
 *
 *----------------------------------------------------------------------
 */

static void
ExtCleanup(void *ignored)
{
    Tcl_HashEntry  *he;
    Tcl_HashSearch  hs;

    he = Tcl_FirstHashEntry(&htCtx, &hs);
    while (he != NULL) {
        ns_free(Tcl_GetHashValue(he));
        he = Tcl_NextHashEntry(&hs);
    }
    Tcl_DeleteHashTable(&htCtx);
    Ns_MutexDestroy(&muCtx);
}



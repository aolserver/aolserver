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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsingres/Attic/nsingres.ec,v 1.1.1.1 2004/09/13 15:21:51 dossy Exp $, compiled: " __DATE__ " " __TIME__;

#include <stdio.h>
#include <assert.h>

#define USE_TCL8X
#include "ns.h"

#if NS_MAJOR_VERSION >= 4
#include "nsdb.h"
#endif

EXEC SQL INCLUDE SQLCA;
EXEC SQL INCLUDE SQLDA;

EXEC SQL DECLARE stmt STATEMENT;
EXEC SQL DECLARE curs CURSOR FOR stmt;

#define DRIVER_VERSION "Panoptic Ingres Driver v0.1"

/* Size of a DATE string variable */
#define DATE_SIZE 25

/* The SQL code for the NOT FOUND condition */
#define SQL_NOTFOUND 100

/* Result storage buffer for dynamic SELECT statements */
typedef struct ResultBuffer {
    int             length;
    char           *data;
} ResultBuffer;

typedef struct DbContext {
    int             session;
    ResultBuffer   *resBufPtr;
} DbContext;

static int next_session_id;

static int DbServerInit(char *server, char *module, char *driver);
static const char *DbName(void);
static const char *DbType(Ns_DbHandle *handle);
static int DbOpen(Ns_DbHandle *handle);
static int DbClose(Ns_DbHandle *handle);
static int DbGetRow(Ns_DbHandle *handle, Ns_Set *row);
static int DbFlush(Ns_DbHandle *handle);
static int DbCancel(Ns_DbHandle *handle);
static int DbExec(Ns_DbHandle *handle, char *sql);
static Ns_Set *DbBindRow(Ns_DbHandle *handle);
static int DbSpStart(Ns_DbHandle *handle, char *procname);
static int DbSpExec(Ns_DbHandle *handle);

static Tcl_ObjCmdProc DbCmd;
static Ns_TclInterpInitProc AddCmds;

static void LogError(Ns_DbHandle *handle, const char *func, const char *msg);
static char *ColumnToString(IISQLDA *sqlda, int i, char *buf, unsigned int len);

static Ns_DbProc dbProcs[] = {
    { DbFn_ServerInit,   DbServerInit },
    { DbFn_Name,         DbName },
    { DbFn_DbType,       DbType },
    { DbFn_OpenDb,       DbOpen },
    { DbFn_CloseDb,      DbClose },
    { DbFn_GetRow,       DbGetRow },
    { DbFn_Flush,        DbFlush },
    { DbFn_Cancel,       DbCancel },
    { DbFn_Exec,         DbExec },
    { DbFn_BindRow,      DbBindRow },
    { DbFn_SpStart,      DbSpStart },
    { DbFn_SpExec,       DbSpExec },
    { 0, NULL }
};


DllExport int   Ns_ModuleVersion = 1;
DllExport int   Ns_ModuleFlags = 0;
DllExport int
Ns_DbDriverInit(char *driver, char *path)
{
    if (driver == NULL) {
        Ns_Log(Bug,
                "nsingres: Ns_DbDriverInit() called with NULL driver name.");
        return NS_ERROR;
    }
    if (getenv("II_SYSTEM") == NULL) {
        Ns_Log(Error, "nsingres: II_SYSTEM variable not set.");
        return NS_ERROR;
    }
    if (Ns_DbRegisterDriver(driver, dbProcs) != NS_OK) {
        Ns_Log(Error, "nsingres: could not register the '%s' driver.",
                driver);
        return NS_ERROR;
    }
    Ns_Log(Notice, "nsingres: (%s) Loaded %s, built on %s at %s.",
        driver, DRIVER_VERSION, __DATE__, __TIME__);

    next_session_id = 0;

    /*
     * Calling IIsqlca() here is necessary for ESQL/C to initialize the
     * per-thread SQLCA diagnostic area for multi-threaded use.
     */

    IIsqlca();

    return NS_OK;
}

static int
DbServerInit(char *server, char *module, char *driver)
{
    Ns_TclInitInterps(server, AddCmds, NULL);
    return NS_OK;
}

static const char *
DbName(void)
{
    return "nsingres";
}


static const char *
DbType(Ns_DbHandle *handle)
{
    return "ingres";
}

static int
DbOpen(Ns_DbHandle *handle)
{
    DbContext *dbCtxPtr;
EXEC SQL BEGIN DECLARE SECTION;
    char *db = NULL;
    char *opts = NULL;
    char *user = NULL;
    char *pass = NULL;
    int sid;
EXEC SQL END DECLARE SECTION;

    assert(handle != NULL);
    assert(handle->datasource != NULL);

    if (handle->verbose) {
        Ns_Log(Notice, "nsingres: DbOpen(%s) called.",
                handle->datasource);
    }

    sid = ++next_session_id;
    db = handle->datasource;
    user = handle->user;
    pass = handle->password;

    /*
     * TODO: set opts= from handle->datasource as a Tcl list, idx 1
     */

    if (opts) {
        EXEC SQL CONNECT :db SESSION :sid IDENTIFIED BY :user DBMS_PASSWORD = :pass OPTIONS = :opts;
    } else {
        EXEC SQL CONNECT :db SESSION :sid IDENTIFIED BY :user DBMS_PASSWORD = :pass;
    }
    if (sqlca.sqlcode < 0) {
        LogError(handle, "DbOpen", "SQL CONNECT");
        Ns_DbSetException(handle, "NSDB", "couldn't open connection");
        return NS_ERROR;
    }

    handle->connection = (void *) IIsqlca();

    if (handle->statement != NULL) {
        Ns_Log(Bug, "nsingres: DbOpen(%s): handle->statement != NULL.  Leaking.",
                handle->datasource);
    }

    handle->statement = NULL;

    if (handle->context != NULL) {
        Ns_Log(Bug, "nsingres: DbOpen(%s): handle->context != NULL.  Leaking.",
                handle->datasource);
    }

    dbCtxPtr = (DbContext *) ns_malloc(sizeof(DbContext));
    handle->context = (void *) dbCtxPtr;
    dbCtxPtr->session = sid;
    dbCtxPtr->resBufPtr = (ResultBuffer *) ns_malloc(sizeof(ResultBuffer));
    dbCtxPtr->resBufPtr->length = 0;
    dbCtxPtr->resBufPtr->data = NULL;

    handle->connected = NS_TRUE;

    return NS_OK;
}

static int
DbClose(Ns_DbHandle *handle)
{
    DbContext *dbCtxPtr = (DbContext *) handle->context;
    ResultBuffer *resBufPtr = dbCtxPtr->resBufPtr;
    int status = NS_OK;

    if (handle->verbose) {
        Ns_Log(Notice, "nsingres: DbClose(%s) called.", handle->datasource);
    }

    DbCancel(handle);

    EXEC SQL DISCONNECT;

    if (sqlca.sqlcode < 0) {
        LogError(handle, "DbClose", "SQL DISCONNECT");
        Ns_DbSetException(handle, "NSDB", "couldn't close connection");
        status = NS_ERROR;
    }

    if (resBufPtr->data) {
        ns_free(resBufPtr->data);
    }
    ns_free(resBufPtr);
    ns_free(dbCtxPtr);
    handle->context = NULL;

    handle->connected = NS_FALSE;

    return status;
}

static int
DbGetRow(Ns_DbHandle *handle, Ns_Set *row)
{
    IISQLDA *sqlda = (IISQLDA *) handle->statement;
    char buf[32 * 1024], *bufPtr;
    int i;
    int status = NS_OK;

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbGetRow(%s) called.", handle->datasource);

    if (! handle->fetchingRows) {
        Ns_Log(Error, "nsingres: DbGetRow(%s):  No rows waiting to fetch.",
            handle->datasource);
        Ns_DbSetException(handle, "NSDB", "no rows waiting to fetch");
        return NS_ERROR;
    }

    EXEC SQL FETCH curs USING DESCRIPTOR :sqlda;

    if (sqlca.sqlcode != 0) {
        status = NS_END_DATA;
        if (sqlca.sqlcode < 0) {
            LogError(handle, "DbGetRow", "SQL FETCH");
            Ns_DbSetException(handle, "NSDB", "error fetching next row");
            status = NS_ERROR;
        }
        DbFlush(handle);
        return status;
    }

    for (i = 0; i < sqlda->sqld; i++) {
        bufPtr = ColumnToString(sqlda, i, buf, 32 * 1024);
        if (bufPtr == NULL) {
            /* error */
        }
        Ns_SetPutValue(row, i, bufPtr);
    }

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbGetRow(%s) completed.", handle->datasource);

    return NS_OK;
}


static int
DbFlush(Ns_DbHandle *handle)
{
    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbFlush(%s) called.", handle->datasource);

    EXEC SQL CLOSE curs;
    EXEC SQL SET_SQL(SESSION = NONE);

    /*
     * Clear out any previous SQL error.
     */

    sqlca.sqlcode = 0;

    if (handle->statement) {
        ns_free(handle->statement);
    }

    handle->statement = NULL;
    handle->fetchingRows = 0;

    return NS_OK;
}

static int
DbCancel(Ns_DbHandle *handle)
{
EXEC SQL BEGIN DECLARE SECTION;
    int transaction;
EXEC SQL END DECLARE SECTION;
    int status = NS_OK;

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbCancel(%s) called.", handle->datasource);

    EXEC SQL INQUIRE_SQL(:transaction = TRANSACTION);
    if (transaction) {
        EXEC SQL ROLLBACK;
        if (sqlca.sqlcode < 0) {
            LogError(handle, "DbCancel", "SQL ROLLBACK");
            Ns_DbSetException(handle, "NSDB", "couldn't cancel transaction");
            status = NS_ERROR;
        }
    }

    EXEC SQL SET_SQL(SESSION = NONE);

    /*
     * Clear out any previous SQL error.
     */

    sqlca.sqlcode = 0;

    if (handle->statement) {
        ns_free(handle->statement);
    }

    handle->statement = NULL;
    handle->fetchingRows = 0;

    return status;
}


static int
DbExec(Ns_DbHandle *handle, char *sql)
{
    DbContext *dbCtxPtr = (DbContext *) handle->context;
    IISQLDA *sqlda;
EXEC SQL BEGIN DECLARE SECTION;
    char *query = sql;
    int sid;
EXEC SQL END DECLARE SECTION;
    int status = NS_ERROR;

    assert(handle != NULL);
    assert(sql != NULL);

    if (handle->verbose) {
        Ns_Log(Notice, "nsingres: DbExec(%s) called.", handle->datasource);
        Ns_Log(Notice, "nsingres: DbExec(%s, sql) = '%s'",
            handle->datasource, sql);
    }

    sid = dbCtxPtr->session;
    EXEC SQL SET_SQL(SESSION = :sid);
    if (sqlca.sqlcode < 0) {
        Ns_DbSetException(handle, "NSDB", "couldn't set session");
        LogError(handle, "DbExec", "SQL SET_SQL(SESSION)");
        return NS_ERROR;
    }

    sqlda = (IISQLDA *) ns_malloc(sizeof(IISQLDA));
    sqlda->sqln = IISQ_MAX_COLS;
    sqlda->sqld = 0;

    EXEC SQL PREPARE stmt FROM :query;
    if (sqlca.sqlcode < 0) {
        ns_free(sqlda);

        Ns_DbSetException(handle, "NSDB", "couldn't prepare query");
        LogError(handle, "DbExec", "SQL PREPARE");
        return NS_ERROR;
    }

    EXEC SQL DESCRIBE stmt INTO :sqlda;

    if (sqlda->sqld == 0) {
        EXEC SQL EXECUTE stmt;

        handle->fetchingRows = 0;
        status = NS_DML;
    } else {
        EXEC SQL OPEN curs;

        handle->fetchingRows = 1;
        status = NS_ROWS;
    }

    handle->statement = (void *) sqlda;

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbExec(%s, status) = %s",
            handle->datasource, status == NS_ROWS ? "NS_ROWS" : "NS_DML");

    return status;
}

static Ns_Set  *
DbBindRow(Ns_DbHandle *handle)
{
    DbContext *dbCtxPtr = (DbContext *) handle->context;
    ResultBuffer *resBufPtr = dbCtxPtr->resBufPtr;
    Ns_Set *row = (Ns_Set *) handle->row;
    IISQLDA *sqlda = (IISQLDA *) handle->statement;
    IISQLVAR *sqv;      /* Pointer to 'sqlvar */
    int i;              /* Index into 'sqlvar' */
    int base_type;      /* Base type w/o nullability */
    int res_cur_size;   /* Result size required */
    int round;          /* Alignment */

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbBindRow(%s) called.", handle->datasource);

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbBindRow(%s, numcols) = %u",
            handle->datasource, sqlda->sqld);

    /*
     * For each column print its title (and number), and accumulate
     * the size of the result data area.
     */

    for (res_cur_size = 0, i = 0; i < sqlda->sqld; i++) {
        sqv = &sqlda->sqlvar[i];
        sqv->sqlname.sqlnamec[sqv->sqlname.sqlnamel] = '\0';
        Ns_SetPut(row, sqv->sqlname.sqlnamec, NULL);

        /* Find the base-type of the result (non-nullable) */
        if ((base_type = sqv->sqltype) < 0)
            base_type = -base_type;

        /* Collapse different types into INT, FLOAT or CHAR */
        switch (base_type) {
        case IISQ_INT_TYPE:
            /* Always retrieve into a long integer */
            res_cur_size += sizeof(long);
            sqv->sqllen = sizeof(long);
            break;

        case IISQ_MNY_TYPE:
            /* Always retrieve into a double floating-point */
            if (sqv->sqltype < 0)
                sqv->sqltype = -IISQ_FLT_TYPE;
            else
                sqv->sqltype = IISQ_FLT_TYPE;
            res_cur_size += sizeof(double);
            sqv->sqllen = sizeof(double);
            break;

        case IISQ_FLT_TYPE:
            /* Always retrieve into a double floating-point */
            res_cur_size += sizeof(double);
            sqv->sqllen = sizeof(double);
            break;

        case IISQ_DTE_TYPE:
            sqv->sqllen = DATE_SIZE;
            /* Fall through to handle like CHAR */

        case IISQ_CHA_TYPE:
        case IISQ_VCH_TYPE:
            /*
             * Assume no binary data is returned from the CHAR type.
             * Also allocate one extra byte for the null terminator.
             */

            res_cur_size += sqv->sqllen + 1;

            /* Always round off to aligned data boundary */
            round = res_cur_size % 4;
            if (round)
                res_cur_size += 4 - round;

            if (sqv->sqltype < 0)
                sqv->sqltype = -IISQ_CHA_TYPE;
            else
                sqv->sqltype = IISQ_CHA_TYPE;
            break;
        }

        /* Save away space for the null indicator */
        if (sqv->sqltype < 0)
            res_cur_size += sizeof(int);
    }

    /*
     * At this point we've printed all column headers and converted all
     * types to one of INT, CHAR or FLOAT. Now we allocate a single
     * result buffer, and point all the result column data areas into it.
     *
     * If we have an old result data area that is not large enough then free
     * it and allocate a new one. Otherwise we can reuse the last one.
     */

    if (resBufPtr->length > 0 &&
            resBufPtr->length < res_cur_size) {

        ns_free(resBufPtr->data);
        resBufPtr->length = 0;
    }
    if (resBufPtr->length == 0) {
        resBufPtr->length = res_cur_size;
        resBufPtr->data = (char *) ns_malloc((unsigned) res_cur_size);
    }

    /*
     * Now for each column now assign the result address (sqldata) and
     * indicator address (sqlind) from the result data area.
     */

    for (res_cur_size = 0, i = 0; i < sqlda->sqld; i++) {
        sqv = &sqlda->sqlvar[i];

        /* Find the base-type of the result (non-nullable) */
        if ((base_type = sqv->sqltype) < 0)
            base_type = -base_type;

        /* Current data points at current offset */
        sqv->sqldata = (char *) &resBufPtr->data[res_cur_size];
        res_cur_size += sqv->sqllen;
        if (base_type == IISQ_CHA_TYPE) {
            res_cur_size++; /* Add one for null */
            round = res_cur_size % 4; /* Round to aligned boundary */
            if (round)
                res_cur_size += 4 - round;
        }

        /* Point at result indicator variable */
        if (sqv->sqltype < 0) {
            sqv->sqlind = (short *) &resBufPtr->data[res_cur_size];
            res_cur_size += sizeof(int);
        } else {
            sqv->sqlind = (short *)0;
        }
    }

    return row;
}


static int
DbSpStart(Ns_DbHandle *handle, char *procname)
{
    int status;

    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbSpStart(%s) called.", handle->datasource);

    status = DbExec(handle, procname);

    if (status != NS_ERROR)
        return NS_OK;
    else
        return NS_ERROR;
}

static int
DbSpExec(Ns_DbHandle *handle)
{
    if (handle->verbose)
        Ns_Log(Notice, "nsingres: DbSpExec(%s) called.", handle->datasource);
   
#if 0
    if (GET_TDS(handle)->res_info == NULL)
        return NS_DML;
    else
        return NS_ROWS;
#endif

    /* FIXME: do something here */
    return NS_ROWS;
}


/*
 *----------------------------------------------------------------------
 *
 * LogError --
 *
 *	Log an error message.
 *
 * Results:
 *	An error message is written to the server log.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
LogError(Ns_DbHandle *handle, const char *func, const char *msg) {
EXEC SQL BEGIN DECLARE SECTION;
    char buf[150];
EXEC SQL END DECLARE SECTION;

    EXEC SQL INQUIRE_INGRES (:buf = ERRORTEXT);

    Ns_Log(Error, "nsingres: %s(%s): (%d) %s: %s",
            func, handle->datasource, sqlca.sqlcode, msg, buf);
}


/*
 *----------------------------------------------------------------------
 *
 * ColumnToString --
 *
 *	Format the column value as a string.
 *
 * Results:
 *	Returns NULL if an invalid column is requested, otherwise
 *	returns the buf pointer where the value has been rendered.
 *	If the value is a SQL NULL, an empty string is rendered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static char *
ColumnToString(IISQLDA *sqlda, int i, char *buf, unsigned int len)
{
    IISQLVAR *sqv;
    int base_type; /* Base type w/o nullability */

    if (i >= sqlda->sqld) {
        return NULL;
    }

    sqv = &sqlda->sqlvar[i];

    if (sqv->sqlind && *sqv->sqlind < 0) {
        /* IS NULL */
        buf[0] = '\0';
    } else {
        /* Find the base-type of the result (non-nullable) */
        if ((base_type = sqv->sqltype) < 0) {
            base_type = -base_type;
        }

        switch (base_type) {
        case IISQ_INT_TYPE:
            /* All integers were retrieved into long integers */
            snprintf(buf, len, "%ld", *(long *) sqv->sqldata);
            break;

        case IISQ_FLT_TYPE:
            /* All floats were retrieved into doubles */
            snprintf(buf, len, "%g", *(double *) sqv->sqldata);
            break;

        case IISQ_CHA_TYPE:
            /* All characters were null terminated */
            snprintf(buf, len, "%s", (char *) sqv->sqldata);
            break;
        }
    }

    return buf;
}


long
Ns_Ingres_Rows_Affected(Ns_DbHandle *handle)
{
    IISQLCA *sqlcaPtr = (IISQLCA *) handle->connection;

    return sqlcaPtr->sqlerrd[2];
}


/*
 *----------------------------------------------------------------------
 *
 * DbCmd --
 *
 *	Implement the ns_ingres command.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Depends on command.
 *
 *----------------------------------------------------------------------
 */

static int
AddCmds(Tcl_Interp *interp, void *arg)
{
    Tcl_CreateObjCommand(interp, "ns_ingres", DbCmd, arg, NULL);
    return TCL_OK;
}

static int
DbCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Ns_DbHandle *handle;
    static CONST char *opts[] = {
        "rows_affected", "version", NULL
    };
    enum {
        IRowsAffectedIdx, IVersionIdx
    } opt;

    if (objc < 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "option handle ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 1,
                (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    if (Ns_TclDbGetHandle(interp, Tcl_GetString(objv[2]), &handle) != TCL_OK) {
        return TCL_ERROR;
    }

    if (!STREQ(Ns_DbDriverName(handle), DbName())) {
        Tcl_AppendResult(interp, "handle \"", Tcl_GetString(objv[2]),
                "\" is not of type \"", DbName(), "\"", NULL);
        return TCL_ERROR;
    }

    switch (opt) {
    case IRowsAffectedIdx:
        Tcl_SetObjResult(interp,
                Tcl_NewLongObj(Ns_Ingres_Rows_Affected(handle)));
        break;

    case IVersionIdx:
        Tcl_SetResult(interp, DRIVER_VERSION, TCL_STATIC);
        break;
    }
    return TCL_OK;
}


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
 * nspostgres.c
 *
 * This module implements a PostgreSQL database driver.
 *
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/Attic/nspostgres.c,v 1.1.1.1 2001/09/05 20:14:16 kriston Exp $, compiled: " __DATE__ " " __TIME__;




/* NOTE: for ACS/pg use, you need to define FOR_ACS_USE! */

#include "ns.h"
/* If we're under AOLserver 3, we don't need some things.
   the constant NS_AOLSERVER_3_PLUS is defined in AOLserver 3 and greater's
   ns.h */

#ifndef NS_AOLSERVER_3_PLUS

#include "nsdb.h"
#include "nstcl.h"
#endif /* NS_AOLSERVER_3_PLUS */

#include "libpq-fe.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define DRIVER_NAME             "PostgreSQL"
#define OID_QUOTED_STRING       " oid = '"
#define STRING_BUF_LEN          256

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

static void 	Ns_PgUnQuoteOidString(Ns_DString *sql);
static char    *Ns_PgName(Ns_DbHandle *handle);
static char    *Ns_PgDbType(Ns_DbHandle *handle);
static int      Ns_PgOpenDb(Ns_DbHandle *dbhandle);
static int      Ns_PgCloseDb(Ns_DbHandle *dbhandle);
static int      Ns_PgGetRow(Ns_DbHandle *handle, Ns_Set *row);
static int      Ns_PgFlush(Ns_DbHandle *handle);

/* Clunky construct follows :-) We want these statics for either AS 2.3
   OR for ACS/pg under AS3 -- plain AS3 doesn't get these */

/* Hack out the extended_table_info stuff if AOLserver 3, and add in our
   driver's reimplement of ns_table and ns_column */

#ifdef NS_AOLSERVER_3_PLUS
#ifdef FOR_ACS_USE

/* So that we don't have to do this clunky thing again, set a define */
#define NOT_AS3_PLAIN

/* A linked list to use when parsing SQL. */

typedef struct _string_list_elt {
  char *string;
  struct _string_list_elt *next;
} string_list_elt_t;


static Ns_DbTableInfo *Ns_PgGetTableInfo(Ns_DbHandle *handle, char *table);
static char    *Ns_PgTableList(Ns_DString *pds, Ns_DbHandle *handle, int includesystem);

static int pg_column_command (ClientData dummy, Tcl_Interp *interp, 
			       int argc, char *argv[]);
static int pg_table_command (ClientData dummy, Tcl_Interp *interp, 
			       int argc, char *argv[]);

static Ns_DbTableInfo *Ns_DbNewTableInfo (char *table);
static void Ns_DbFreeTableInfo (Ns_DbTableInfo *tinfo);
static void Ns_DbAddColumnInfo (Ns_DbTableInfo *tinfo, Ns_Set *column_info);
static int Ns_DbColumnIndex (Ns_DbTableInfo *tinfo, char *name);

#endif /* FOR_ACS_USE */

/* PLAIN AS 3! */
#define AS3_PLAIN  /* that is, AS3 without the ACS extensions */

#else /* NS_AOLSERVER_3_PLUS */

/* define NOT_AS3_PLAIN here as well, so that a single ifdef can be used */
#define NOT_AS3_PLAIN

static Ns_DbTableInfo *Ns_PgGetTableInfo(Ns_DbHandle *handle, char *table);
static char    *Ns_PgTableList(Ns_DString *pds, Ns_DbHandle *handle, int includesystem);

static char    *Ns_PgBestRowId(Ns_DString *pds, Ns_DbHandle *handle, char *table);

#endif /* NS_AOLSERVER_3_PLUS */

static int	Ns_PgServerInit(char *hServer, char *hModule, char *hDriver);
static void	Ns_PgSetErrorstate(Ns_DbHandle *handle);
static int      Ns_PgExec(Ns_DbHandle *handle, char *sql);
static Ns_Set  *Ns_PgBindRow(Ns_DbHandle *handle);
static int      Ns_PgResetHandle(Ns_DbHandle *handle);

static char	     *pgName = DRIVER_NAME;
static unsigned int   pgCNum = 0;

/*-
 * 
 * The NULL-terminated PgProcs[] array of Ns_DbProc structures is the
 * method by which the function pointers are passed to the nsdb module
 * through the Ns_DbRegisterDriver() function.  Each Ns_DbProc includes
 * the function id (i.e., DbFn_OpenDb, DbFn_CloseDb, etc.) and the
 * cooresponding driver function pointer (i.e., Ns_PgOpendb, Ns_PgCloseDb,
 * etc.).  See nsdb.h for a complete list of function ids.
 */
static Ns_DbProc PgProcs[] = {
    {DbFn_Name, (void *) Ns_PgName},
    {DbFn_DbType, (void *) Ns_PgDbType},
    {DbFn_OpenDb, (void *) Ns_PgOpenDb},
    {DbFn_CloseDb, (void *) Ns_PgCloseDb},
    {DbFn_BindRow, (void *) Ns_PgBindRow},
    {DbFn_Exec, (void *) Ns_PgExec},
    {DbFn_GetRow, (void *) Ns_PgGetRow},
    {DbFn_Flush, (void *) Ns_PgFlush},
    {DbFn_Cancel, (void *) Ns_PgFlush},

/* Excise for AS 3 */
#ifndef NS_AOLSERVER_3_PLUS
    {DbFn_GetTableInfo, (void *) Ns_PgGetTableInfo},
    {DbFn_TableList, (void *) Ns_PgTableList},
    {DbFn_BestRowId, (void *) Ns_PgBestRowId},
#endif /* NS_AOLSERVER_3_PLUS */

    {DbFn_ServerInit, (void *) Ns_PgServerInit},
    {DbFn_ResetHandle, (void *) Ns_PgResetHandle },
    {0, NULL}
};


/*
 * The NsPgConn structure is connection data specific
 * to PostgreSQL. 
 */ 
typedef struct NsPgConn {
    PGconn         *conn;
    unsigned int    cNum;
    PGresult       *res;
    int             nCols;
    int             nTuples;
    int             curTuple;
    int             in_transaction;
}               NsPgConn;

DllExport int   Ns_ModuleVersion = 1;

static char datestyle[STRING_BUF_LEN];

DllExport int
Ns_DbDriverInit(char *hDriver, char *configPath)
{
    char *t;
    char *e;

    /* 
     * Register the PostgreSQL driver functions with nsdb.
     * Nsdb will later call the Ns_PgServerInit() function
     * for each virtual server which utilizes nsdb. 
     */
    if (Ns_DbRegisterDriver(hDriver, &(PgProcs[0])) != NS_OK) {
        Ns_Log(Error, "Ns_DbDriverInit(%s):  Could not register the %s driver.",
               hDriver, pgName);
        return NS_ERROR;
    }
    Ns_Log(Notice, "%s loaded.", pgName);

    e = getenv("PGDATESTYLE");

    t = Ns_ConfigGetValue(configPath, "DateStyle");

    strcpy(datestyle, "");
    if (t) {
        if (!strcasecmp(t, "ISO") || !strcasecmp(t, "SQL") ||
            !strcasecmp(t, "POSTGRES") || !strcasecmp(t, "GERMAN") ||
            !strcasecmp(t, "NONEURO") || !strcasecmp(t, "EURO")) {
            strcpy(datestyle, "set datestyle to '");
            strcat(datestyle, t);
            strcat(datestyle, "'");
            if (e) {
                Ns_Log(Notice, "PGDATESTYLE overridden by datestyle param.");
             }
         } else {
            Ns_Log(Error, "Illegal value for datestyle - ignored");
          }
     } else {
        if (e) {
            Ns_Log(Notice, "PGDATESTYLE setting used for datestyle.");
         }
       }
    return NS_OK;

}


/*
 * Ns_PgName - Return the string name which identifies the PostgreSQL driver.
 */
static char    *
Ns_PgName(Ns_DbHandle *ignored) {
    return pgName;
}


/*
 * Ns_PgDbType - Return the string which identifies the PostgreSQL database.
 */
static char    *
Ns_PgDbType(Ns_DbHandle *ignored) {
    return pgName;
}

/*
 * Ns_PgOpenDb - Open an PostgreSQL connection on an nsdb handle. The
 * datasource for PostgreSQL is in the form "host:port:database".
 */
static int
Ns_PgOpenDb(Ns_DbHandle *handle) {

    static char *asfuncname = "Ns_PgOpenDb";
    NsPgConn       *nsConn;
    PGconn         *pgConn;
    char           *host;
    char           *port;
    char           *db;
    char            datasource[STRING_BUF_LEN];

    if (handle == NULL || handle->datasource == NULL ||
        strlen(handle->datasource) > STRING_BUF_LEN) {
        Ns_Log(Error, "%s: Invalid handle.", asfuncname);
        return NS_ERROR;
    }

    strcpy(datasource, handle->datasource);
    host = datasource;
    port = strchr(datasource, ':');
    if (port == NULL || ((db = strchr(port + 1, ':')) == NULL)) {
        Ns_Log(Error, "Ns_PgOpenDb(%s):  Malformed datasource:  %s.  Proper form is: (host:port:database).",
               handle->driver, handle->datasource);
        return NS_ERROR;
    } else {
        *port++ = '\0';
        *db++ = '\0';
	if (!strcmp(host, "localhost")) {
	    Ns_Log(Notice, "Opening %s on %s", db, host);
	    pgConn = PQsetdbLogin(NULL, port, NULL, NULL, db, handle->user,
                                  handle->password);
	} else {
	    Ns_Log(Notice, "Opening %s on %s, port %s", db, host, port);
	    pgConn = PQsetdbLogin(host, port, NULL, NULL, db, handle->user,
                             handle->password);
	}

        if (PQstatus(pgConn) == CONNECTION_OK) {
            Ns_Log(Notice, "Ns_PgOpenDb(%s):  Openned connection to %s.",
			    handle->driver, handle->datasource);
            nsConn = ns_malloc(sizeof(NsPgConn));
            if (!nsConn) {
                Ns_Log(Error, "ns_malloc() failed allocating nsConn");
                return NS_ERROR;
            }
            nsConn->in_transaction = FALSE;
            nsConn->cNum = pgCNum++;
            nsConn->conn = pgConn;
            nsConn->res = NULL;
            nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
            handle->connection = nsConn;

            if (strlen(datestyle)) { 
                return Ns_PgExec(handle, datestyle) == NS_DML ?
                  NS_OK : NS_ERROR;
            }
            return NS_OK;
        } else {
            Ns_Log(Error, "Ns_PgOpenDb(%s):  Could not connect to %s:  %s", handle->driver,
			    handle->datasource, PQerrorMessage(pgConn));
            PQfinish(pgConn);
            return NS_ERROR;
        }
    }
}

/*
 * Ns_PgCloseDb - Close an PostgreSQL connection on an nsdb handle.
 */
static int
Ns_PgCloseDb(Ns_DbHandle *handle) {

    static char    *asfuncname = "Ns_PgCloseDb";
    NsPgConn       *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (handle->verbose) {
        Ns_Log(Notice, "Ns_PgCloseDb(%d):  Closing connection:  %s",
               nsConn->cNum, handle->datasource);
    }

    PQfinish(nsConn->conn);

    /* DRB: Not sure why the driver zeroes these out before
     * freeing nsConn, but can't hurt I guess.
    */
    nsConn->conn = NULL;
    nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
    ns_free(nsConn);

    handle->connection = NULL;

    return NS_OK;
}

static void
Ns_PgSetErrorstate(Ns_DbHandle *handle)
{
    NsPgConn  *nsConn = handle->connection;
    Ns_DString        *nsMsg  = &(handle->dsExceptionMsg);

    Ns_DStringTrunc(nsMsg, 0);

    switch (PQresultStatus(nsConn->res)) {
        case PGRES_EMPTY_QUERY:
        case PGRES_COMMAND_OK:
        case PGRES_TUPLES_OK:
        case PGRES_COPY_OUT:
        case PGRES_COPY_IN:
        case PGRES_NONFATAL_ERROR:
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;

        case PGRES_FATAL_ERROR:
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;

        case PGRES_BAD_RESPONSE:
              Ns_DStringAppend(nsMsg, "PGRES_BAD_RESPONSE ");
              Ns_DStringAppend(nsMsg, PQresultErrorMessage(nsConn->res));
              break;
    }
}



/* Set the current transaction state based on the query pointed to by
 * "sql".  Should be called only after the query has successfully been
 * executed.
*/

static void
set_transaction_state(Ns_DbHandle *handle, char *sql, char *asfuncname) {
    NsPgConn *nsConn = handle->connection;
    while (*sql == ' ') sql++;
    if (!strncasecmp(sql, "begin", 5)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Entering transaction", asfuncname);
        }
        nsConn->in_transaction = TRUE;
    } else if (!strncasecmp(sql, "end", 3) ||
               !strncasecmp(sql, "commit", 6)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Committing transaction", asfuncname);
        }
        nsConn->in_transaction = FALSE;
    } else if (!strncasecmp(sql, "abort", 5) ||
               !strncasecmp(sql, "rollback", 8)) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Rolling back transaction", asfuncname);
        }
        nsConn->in_transaction = FALSE;
    }
}

/*
 * Ns_PgExec - Send a PostgreSQL query.  This function does not
 * implement an nsdb function but is used internally by the PostgreSQL
 * driver.
 */
static int
Ns_PgExec(Ns_DbHandle *handle, char *sql) {
    static char *asfuncname = "Ns_PgExec";
    NsPgConn       *nsConn;
    Ns_DString      dsSql;
    int             retry_count=2;
    
    if (sql == NULL) {
        Ns_Log(Error, "%s: Invalid SQL query.", asfuncname);
        return NS_ERROR;
    }

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        return NS_ERROR;
    } 

    nsConn = handle->connection;
       
    PQclear(nsConn->res);

    Ns_DStringInit(&dsSql);
    Ns_DStringAppend(&dsSql, sql);
    Ns_PgUnQuoteOidString(&dsSql);

    while (dsSql.length > 0 && isspace(dsSql.string[dsSql.length - 1])) {
        dsSql.string[--dsSql.length] = '\0';
    }

    if (dsSql.length > 0 && dsSql.string[dsSql.length - 1] != ';') {
        Ns_DStringAppend(&dsSql, ";");
    }

    if (handle->verbose) {
	Ns_Log(Notice, "Querying '%s'", dsSql.string);
    }

    nsConn->res = PQexec(nsConn->conn, dsSql.string);

    /* Set error result for exception message -- not sure that this
       belongs here in DRB-improved driver..... but, here it is
       anyway, as it can't really hurt anything :-) */
   
    Ns_PgSetErrorstate(handle);

    /* This loop should actually be safe enough, but we'll take no 
     * chances and guard against infinite retries with a counter.
     */

    while (PQstatus(nsConn->conn) == CONNECTION_BAD && retry_count--) {

        int in_transaction = nsConn->in_transaction;

        /* Backend returns a fatal error if it really crashed.  After a crash,
         * all other backends close with a non-fatal error because shared
         * memory might've been corrupted by the crash.  In this case, we
         * will retry the query.
         */

        int retry_query = PQresultStatus(nsConn->res) == PGRES_NONFATAL_ERROR;
        
        /* Reconnect messages need to be logged regardless of Verbose. */    

        Ns_Log(Notice, "%s: Trying to reopen database connection", asfuncname);

        PQfinish(nsConn->conn);

        /* We'll kick out with an NS_ERROR if we're in a transaction.
         * The queries within the transaction up to this point were
         * rolled back when the transaction crashed or closed itself
         * at the request of the postmaster.  If we were to allow the
         * rest of the transaction to continue, you'd blow transaction
         * semantics, i.e. the first portion of the transaction would've
         * rolled back and the rest of the transaction would finish its
         * inserts or whatever.  Not good!   So we return an error.  If
         * the programmer's catching transaction errors and rolling back
         * properly, there will be no problem - the rollback will be
         * flagged as occuring outside a transaction but there's no
         * harm in that.
         *
         * If the programmer's started a transaction with no "catch",
         * you'll find no sympathy on my part.
         */

        if (Ns_PgOpenDb(handle) == NS_ERROR || in_transaction || !retry_query) {
            if (in_transaction) {
                Ns_Log(Notice, "%s: In transaction, conn died, error returned",
                       asfuncname);
            }
            Ns_DStringFree(&dsSql);
            return NS_ERROR;
        }

        nsConn = handle->connection;

        Ns_Log(Notice, "%s: Retrying query", asfuncname);
        PQclear(nsConn->res);
        nsConn->res = PQexec(nsConn->conn, dsSql.string);

        /* This may again break the connection for two reasons: 
         * our query may be a back-end crashing query or another
         * backend may've crashed after we reopened the backend.
         * Neither's at all likely but we'll loop back and try
         * a couple of times if it does.
         */

    }

    Ns_DStringFree(&dsSql);

    if (nsConn->res == NULL) {
        Ns_Log(Error, "Ns_PgExec(%s):  Could not send query '%s':  %s",
	       handle->datasource, sql, PQerrorMessage(nsConn->conn));
        return NS_ERROR;
    }

    /* DRB: let's clean up nsConn a bit, if someone didn't read all
     * the rows returned by a query, did a dml query, then a getrow
     * the driver might get confused if we don't zap nCols and
     * curTuple.
    */
    nsConn->nCols=0;
    nsConn->curTuple=0;
    nsConn->nTuples=0;

    switch(PQresultStatus(nsConn->res)) {
    case PGRES_TUPLES_OK:
        handle->fetchingRows = NS_TRUE;
        return NS_ROWS;
        break;
    case PGRES_COPY_IN:
    case PGRES_COPY_OUT:
        return NS_DML;
        break;
    case PGRES_COMMAND_OK:
        set_transaction_state(handle, sql, asfuncname);
        sscanf(PQcmdTuples(nsConn->res), "%d", &(nsConn->nTuples));
        return NS_DML;
        break;
    default:
		Ns_Log(Error, "%s: result status: %d message: %s", asfuncname,
               PQresultStatus(nsConn->res), PQerrorMessage(nsConn->conn));
        return NS_ERROR;
    }

} /* end Ns_PgExec */


static int
Ns_PgResetHandle(Ns_DbHandle *handle) {
    static char *asfuncname = "Ns_PgResetHandle";
    NsPgConn       *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        return NS_ERROR;
    } 

    nsConn = handle->connection;

    if (nsConn->in_transaction) {
        if (handle->verbose) {
            Ns_Log(Notice, "%s: Rolling back transaction", asfuncname);
        }
        if (Ns_PgExec(handle, "rollback") != PGRES_COMMAND_OK) {
            Ns_Log(Error, "%s: Rollback failed", asfuncname);
        }
        return NS_ERROR;
    }
    return NS_OK;
}

/* EXCISE for PLAIN AS3 */
#ifdef NOT_AS3_PLAIN

/*
 * Ns_PgSelect - Send a query which should return rows.
 * 
 * DRB: though AOLserver never calls this directly because
 * we've registered Ns_PgExec, the functions to return basic table
 * information to pre-AOLserver3 versions use it. Also, the reimplementation of
 * ns_column and ns_table uses it.
 */

static Ns_Set  *
Ns_PgSelect(Ns_DbHandle *handle, char *sql) {

    static char *asfuncname = "Ns_PgSelect";
    Ns_Set         *row = NULL;
    NsPgConn       *nsConn;
    int             i;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        goto done;
    } 

    if (sql == NULL) {
        Ns_Log(Error, "%s: Invalid SQL query.", asfuncname);
        goto done;
    }

    nsConn = handle->connection;

    if (Ns_PgExec(handle, sql) != NS_ERROR) {

        if (PQresultStatus(nsConn->res) == PGRES_TUPLES_OK) {
            nsConn->curTuple = 0;
            nsConn->nCols = PQnfields(nsConn->res);
            nsConn->nTuples = PQntuples(nsConn->res);
            row = handle->row;
            
            for (i = 0; i < nsConn->nCols; i++) {
                Ns_SetPut(row, (char *)PQfname(nsConn->res, i), NULL);
            }
            
        } else {
            Ns_Log(Error, "Ns_PgSelect(%s):  Query did not return rows:  %s",
                   handle->datasource, sql);
        }
    }
  done:
    return (row);
}

#endif /*NOT_AS3_PLAIN */

/*
 * Ns_PgGetRow - Fetch rows after an Ns_PgSelect or Ns_PgExec.
 */
static int
Ns_PgGetRow(Ns_DbHandle *handle, Ns_Set *row) {

    static char    *asfuncname = "Ns_PgGetRow";
    NsPgConn       *nsConn;
    int             i;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        return NS_ERROR;
    } 

    if (row == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_Set -> row.", asfuncname);
        return NS_ERROR;
    }

    nsConn = handle->connection;

    if (nsConn->nCols == 0) {
        Ns_Log(Error, "Ns_PgGetRow(%s):  Get row called outside a fetch row loop.",
               handle->datasource);
        return NS_ERROR;
    } else if (nsConn->curTuple == nsConn->nTuples) {

        PQclear(nsConn->res);
        nsConn->res = NULL;
        nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
        return NS_END_DATA;

    } else {
        for (i = 0; i < nsConn->nCols; i++) {
            Ns_SetPutValue(row, i, (char *) PQgetvalue(nsConn->res,
				 nsConn->curTuple, i));
        }
        nsConn->curTuple++;
    }

    return NS_OK;
}


/*
 * Ns_PgFlush - Flush any waiting rows not needed after an Ns_DbSelect().
 */
static int
Ns_PgFlush(Ns_DbHandle *handle) {

    static char *asfuncname = "Ns_PgFlush";
    NsPgConn   *nsConn;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        return NS_ERROR;
    } 

    nsConn = handle->connection;

    if (nsConn->nCols > 0) {
        PQclear(nsConn->res);
        nsConn->res = NULL;
        nsConn->nCols = nsConn->nTuples = nsConn->curTuple = 0;
    }
    return NS_OK;
}

/* Not needed for a PLAIN AS3 driver */

#ifdef NOT_AS3_PLAIN
/*
 * Ns_DbTableInfo - Return system catalog information (columns, types, etc.)
 * about a table.
 */
static Ns_DbTableInfo *
Ns_PgGetTableInfo(Ns_DbHandle *handle, char *table) {
    static char *asfuncname = "Ns_PgGetTableInfo";
    Ns_Set         *row;
    Ns_Set         *col;
    Ns_DString      ds;
    Ns_DbTableInfo *tinfo = NULL;
    int             status;
    char           *name;
    char           *type;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        goto done;
    } 

    if (table == NULL) {
        Ns_Log(Error, "%s: Invalid table.", asfuncname);
        goto done;
    }

    Ns_DStringInit(&ds);
    Ns_DStringVarAppend(&ds, "SELECT a.attname, t.typname "
                        "FROM pg_class c, pg_attribute a, pg_type t "
                        "WHERE c.relname = '", table, "' "
                        "and a.attnum > 0 and a.attrelid = c.oid "
                        "and a.atttypid = t.oid ORDER BY attname", NULL);

    row = Ns_PgSelect(handle, ds.string);
    Ns_DStringFree(&ds);

    if (row != NULL) {
        while ((status = Ns_PgGetRow(handle, row)) == NS_OK) {
            name = row->fields[0].value;
            type = row->fields[1].value;
            if (name == NULL || type == NULL) {
                Ns_Log(Error, "Ns_PgGetTableInfo(%s):  Invalid 'pg_attribute' entry for table:  %s",
                       handle->datasource, table);
                break;
            }

            /*
             * NB:  Move the fields directly from the row
             * Ns_Set to the col Ns_Set to avoid a data copy.
             */
            col = Ns_SetCreate(NULL);
            col->name = name;
            Ns_SetPut(col, "type", NULL);
            col->fields[0].value = type;
            row->fields[0].value = NULL;
            row->fields[1].value = NULL;
            if (tinfo == NULL) {
                tinfo = Ns_DbNewTableInfo(table);
            }
            Ns_DbAddColumnInfo(tinfo, col);
        }
        if (status != NS_END_DATA && tinfo != NULL) {
            Ns_DbFreeTableInfo(tinfo);
            tinfo = NULL;
        }
    }
  done:
    return (tinfo);
}

/*
 * Ns_PgTableList - Return a list of tables in the database.
 */ 
static char    *
Ns_PgTableList(Ns_DString *pds, Ns_DbHandle *handle, int fSystemTables) {

    static char *asfuncname = "Ns_PgTableList";
    Ns_Set         *row;
    Ns_DString      ds;
    char           *table;
    int             status = NS_ERROR;

    if (pds == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_DString -> pds.", asfuncname);
        goto done;
    }

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        goto done;
    }   

    Ns_DStringInit(&ds);
    Ns_DStringAppend(&ds, "SELECT relname FROM pg_class "
                     "WHERE relkind = 'r' and relname !~ '^Inv' ");
    if (!fSystemTables) {
        Ns_DStringAppend(&ds, "and relname !~ '^pg_' ");
    }
    Ns_DStringAppend(&ds, "ORDER BY relname");
    row = Ns_PgSelect(handle, ds.string);
    Ns_DStringFree(&ds);

    if (row != NULL) {
        while ((status = Ns_DbGetRow(handle, row)) == NS_OK) {
            table = row->fields[0].value;
            if (table == NULL) {
                Ns_Log(Warning, "Ns_PgTableList(%s):  NULL relname in 'pg_class' table.",
                       handle->datasource);
            } else {
                Ns_DStringNAppend(pds, table, strlen(table) + 1);
            }
        }
    }
    if (status == NS_END_DATA) {
        return (pds->string);
    }
  done:
    return (NULL);
}

#endif /*NOT_AS3_PLAIN */

/* Excise for AOLserver 3*/
#ifndef NS_AOLSERVER_3_PLUS
/*
 * Ns_PgBestRowId - Return the primary key of a table.  If a table
 * has a primary key, AOLserver can perform row updates and
 * deletes.  In the case of PostgreSQL, the "oid" system column is alwasy
 * unique so we just return it instead of looking for an actual
 * primary key.
 */
static char    *
Ns_PgBestRowId(Ns_DString *pds, Ns_DbHandle *handle, char *table) {
    static char *asfuncname = "Ns_PgBestRowId";
    if (pds == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_DString -> pds.", asfuncname);
        return (NULL);
    }
    Ns_DStringNAppend(pds, "oid", 4);
    return (pds->string);
}

/* end of extended table info stuff */
#endif /* NS_AOLSERVER_3_PLUS */

/* ACS BLOBing stuff */
#ifdef FOR_ACS_USE

/*
 * This is a slight modification of the encoding scheme used by
 * uuencode.  It's quite efficient, using four bytes for each
 * three bytes in the input stream.   There's a slight hitch in
 * that apostrophe isn't legal in Postgres strings.
 * The uuencoding algorithm doesn't make use of lower case letters,
 * though, so we just map them to 'a'.
 *
 * This is a real hack, that's for sure, but we do want to be
 * able to pg_dump these, and this simple means of encoding
 * accomplishes that and is fast, besides.  And at some point
 * we'll be able to stuff large objects directly into Postgres
 * anyway.
 */

/* ENC is the basic 1-character encoding function to make a char printing */
#define ENC(c) (((c) & 077) + ' ')

static unsigned char
enc_one(unsigned char c)
{
	c = ENC(c);
	if (c == '\'') c = 'a';
	else if (c == '\\') c = 'b';
	return c;
}

static void
encode3(unsigned char *p, char *buf)

{
	*buf++ = enc_one(*p >> 2);
	*buf++ = enc_one(((*p << 4) & 060) | ((p[1] >> 4) & 017));
	*buf++ = enc_one(((p[1] << 2) & 074) | ((p[2] >> 6) & 03));
	*buf++ = enc_one(p[2] & 077);
}


/* single-character decode */
#define DEC(c)	(((c) - ' ') & 077)

static unsigned char
get_one(unsigned char c)
{
	if (c == 'a') return '\'';
	else if (c == 'b') return '\\';
	return c;
}

static void
decode3(unsigned char *p, char *buf, int n)
{
	char c1, c2, c3, c4;

	c1 = get_one(p[0]);
	c2 = get_one(p[1]);
	c3 = get_one(p[2]);
	c4 = get_one(p[3]);

	if (n >= 1)
		*buf++ = DEC(c1) << 2 | DEC(c2) >> 4;
	if (n >= 2)
		*buf++ = DEC(c2) << 4 | DEC(c3) >> 2;
	if (n >= 3)
		*buf++ = DEC(c3) << 6 | DEC(c4);
}

/* ns_pg blob_get db blob_id
 * returns the value of the blob to the Tcl caller.
 */

static int
blob_get(Tcl_Interp *interp, Ns_DbHandle *handle, char* lob_id)
{
    NsPgConn	*nsConn = (NsPgConn *) handle->connection;
	int			segment;
	char		query[100];
	char		*segment_pos;
	int			nbytes = 0;

	segment = 1;

	strcpy(query, "SELECT BYTE_LEN, DATA FROM LOB_DATA WHERE LOB_ID = ");
	strcat(query, lob_id);
	strcat(query, " AND SEGMENT = ");

	segment_pos = query + strlen(query);

	for (;;) {
		char	*data_column;
		char	*byte_len_column;
		int		i, j, n, byte_len;
		char	buf[6001];

		sprintf(segment_pos, "%d", segment);
		if (Ns_PgExec(handle, query) != NS_ROWS) {
			Tcl_AppendResult(interp, "Error selecting data from BLOB", NULL);
			return TCL_ERROR;
		}

		if (PQntuples(nsConn->res) == 0) break;

		byte_len_column = PQgetvalue(nsConn->res, 0, 0);
		data_column = PQgetvalue(nsConn->res, 0, 1);
		sscanf(byte_len_column, "%d", &byte_len);
		nbytes += byte_len;
		n = byte_len;
		for (i=0, j=0; n > 0; i += 4, j += 3, n -= 3) {
			decode3(&data_column[i], &buf[j], n);
		}
        buf[byte_len] = '\0';
		Tcl_AppendResult(interp, buf, NULL);
		segment++;
    }

	PQclear(nsConn->res);
	nsConn->res = NULL;

	return TCL_OK;
}


/** 
 * Write the contents of BUFP to a file descriptor or to
 * the network connection directly.
 *
 * Lifted from Oracle driver.
 */
static int
stream_actually_write (int fd, Ns_Conn *conn, void *bufp, int length, int to_conn_p)
{
  int bytes_written = 0;

  if (to_conn_p)
    {
      if (Ns_WriteConn (conn, bufp, length) == NS_OK) 
        {
	  bytes_written = length;
        } 
      else 
        {
	  bytes_written = 0;
        }
    }
  else
    {
      bytes_written = write (fd, bufp, length);
    }

  return bytes_written;
  
} /* stream_actually_write */


/* ns_pg blob_put blob_id value
 * Stuff the contents of value into the pseudo-blob blob_id
 */

static int
blob_put(Tcl_Interp *interp, Ns_DbHandle *handle, char* blob_id,
			char* value)
{
	int			i, j, segment, value_len, segment_len;
	char		out_buf[8001], query[100];
	char		*segment_pos, *value_ptr;

    value_len = strlen(value);
    value_ptr = value;

	strcpy(query, "INSERT INTO LOB_DATA VALUES(");
	strcat(query, blob_id);
	strcat(query, ",");
	segment_pos = query + strlen(query);
	segment = 1;

	while (value_len > 0) {
	    segment_len = value_len > 6000 ? 6000 : value_len;
        value_len -= segment_len;
		for (i = 0, j = 0; i < segment_len; i += 3, j+=4) {
			encode3(&value_ptr[i], &out_buf[j]);
		}
		out_buf[j] = '\0';
		sprintf(segment_pos, "%d, %d, '%s')", segment, segment_len, out_buf);
		if (Ns_PgExec(handle, query) != NS_DML) {
			Tcl_AppendResult(interp, "Error inserting data into BLOB", NULL);
			return TCL_ERROR;
		}
        value_ptr += segment_len;
		segment++;
	}
    Ns_Log(Notice, "done");
	return TCL_OK;
}

/* ns_pg blob_dml_file blob_id file_name
 * Stuff the contents of file_name into the pseudo-blob blob_id
 */

static int
blob_dml_file(Tcl_Interp *interp, Ns_DbHandle *handle, char* blob_id,
			char* filename)
{
	int			fd, i, j, segment, readlen;
	char		in_buf[6000], out_buf[8001], query[100];
	char		*segment_pos;

	fd = open (filename, O_RDONLY);

	if (fd == -1) 
	{
		Ns_Log (Error, " Error opening file %s: %d(%s)",
				filename, errno, strerror(errno));
		Tcl_AppendResult (interp, "can't open file ", filename,
						 " for reading. ", "received error ",
						 strerror(errno), NULL);
	}
  
	strcpy(query, "INSERT INTO LOB_DATA VALUES(");
	strcat(query, blob_id);
	strcat(query, ",");
	segment_pos = query + strlen(query);
	segment = 1;

	readlen = read (fd, in_buf, 6000);
	while (readlen > 0) {
		for (i = 0, j = 0; i < readlen; i += 3, j+=4) {
			encode3(&in_buf[i], &out_buf[j]);
		}
		out_buf[j] = '\0';
		sprintf(segment_pos, "%d, %d, '%s')", segment, readlen, out_buf);
		if (Ns_PgExec(handle, query) != NS_DML) {
			Tcl_AppendResult(interp, "Error inserting data into BLOB", NULL);
			return TCL_ERROR;
		}
		readlen = read(fd, in_buf, 6000);
		segment++;
	}
	close(fd);
	return TCL_OK;
}

/* ns_pg blob_select_file db blob_id filename
 * Write a pseudo-blob to the passed in temp file name.  Some of this
 * shamelessly lifted from ora8.c. 
 * DanW - This is just blob_write, except it doesn't send anything out the
 *        connection.
 * .
 * Combined blob_select_file and blob_write:
 * If you want to write to the network connection, set TO_CONN_P to TRUE
 * and pass a null filename.
 *
 * If you want to write the blob to a file, set TO_CONN_P = FALSE, and
 * pass the filename in.
 */

static int
blob_send_to_stream(Tcl_Interp *interp, Ns_DbHandle *handle, char* lob_id, 
		    int to_conn_p, char* filename)
{
  NsPgConn	*nsConn = (NsPgConn *) handle->connection;
  int		segment;
  char		query[100];
  int		fd;
  char		*segment_pos;
  Ns_Conn       *conn;

  if (to_conn_p) 
    {  
      conn = Ns_TclGetConn(interp);
      
      /* this Shouldn't Happen, but spew an error just in case */
      if (conn == NULL) 
        {
	  Ns_Log (Error, "blob_send_to_stream: No AOLserver conn available");

	  Tcl_AppendResult (interp, "No AOLserver conn available", NULL);
	  goto bailout;
        }
    } else {
      if (filename == NULL) 
	{
	  Tcl_AppendResult (interp, "could not create temporary file to spool "
			    "BLOB/CLOB result", NULL);
	  return TCL_ERROR;
	}


      fd = open (filename, O_CREAT | O_TRUNC | O_WRONLY, 0600);

      if (fd < 0) 
	{
	  Ns_Log (Error, "Can't open %s for writing. error %d(%s)",
		  filename, errno, strerror(errno));
	  Tcl_AppendResult (interp, "can't open file ", filename,
			    " for writing. ",
			    "received error ", strerror(errno), NULL);
	  return TCL_ERROR;
	}
    }

  segment = 1;

  strcpy(query, "SELECT BYTE_LEN, DATA FROM LOB_DATA WHERE LOB_ID = ");
  strcat(query, lob_id);
  strcat(query, " AND SEGMENT = ");

  segment_pos = query + strlen(query);

  for (;;) {
    char	*data_column;
    char	*byte_len_column;
    int		i, j, n, byte_len;
    char	buf[6000];

    sprintf(segment_pos, "%d", segment);
    if (Ns_PgExec(handle, query) != NS_ROWS) {
      Tcl_AppendResult(interp, "Error selecting data from BLOB", NULL);
      return TCL_ERROR;
    }

    if (PQntuples(nsConn->res) == 0) break;

    byte_len_column = PQgetvalue(nsConn->res, 0, 0);
    data_column = PQgetvalue(nsConn->res, 0, 1);
    sscanf(byte_len_column, "%d", &byte_len);
    n = byte_len;
    for (i=0, j=0; n > 0; i += 4, j += 3, n -= 3) {
      decode3(&data_column[i], &buf[j], n);
    }

    stream_actually_write (fd, conn, buf, byte_len, to_conn_p);
    segment++;
  }

 bailout:
  if (!to_conn_p)
    {
      close (fd);
    }

  PQclear(nsConn->res);
  nsConn->res = NULL;

  return TCL_OK;
}


#endif /* FOR_ACS_USE */

#ifdef FOR_ACS_USE

/*
 *----------------------------------------------------------------------
 * BadArgs --
 *
 *      Common routine that creates bad arguments message.
 *
 * Results:
 *      Return TCL_ERROR and set bad argument message as Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
BadArgs(Tcl_Interp *interp, char **argv, char *args)
{
    Tcl_AppendResult(interp, "wrong # args: should be \"",
        argv[0], " ", argv[1], NULL);
    if (args != NULL) {
        Tcl_AppendResult(interp, " ", args, NULL);
    }
    Tcl_AppendResult(interp, "\"", NULL);

    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 * DbFail --
 *
 *      Common routine that creates database failure message.
 *
 * Results:
 *      Return TCL_ERROR and set database failure message as Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
DbFail(Tcl_Interp *interp, Ns_DbHandle *handle, char *cmd, char* sql)
{
  Ns_Free(sql);
  Tcl_AppendResult(interp, "Database operation \"", cmd, "\" failed", NULL);
  if (handle->cExceptionCode[0] != '\0') {
    Tcl_AppendResult(interp, " (exception ", handle->cExceptionCode,
                     NULL);
    if (handle->dsExceptionMsg.length > 0) {
      Tcl_AppendResult(interp, ", \"", handle->dsExceptionMsg.string,
                       "\"", NULL);
    }
    Tcl_AppendResult(interp, ")", NULL);
  }

  return TCL_ERROR;
}

/* 
 * utility functions for dealing with string lists 
 */

static string_list_elt_t *
string_list_elt_new(char *string)
{
  string_list_elt_t *elt = 
    (string_list_elt_t *) Ns_Malloc(sizeof(string_list_elt_t));
  elt->string = string;
  elt->next = 0;

  return elt;

} /* string_list_elt_new */



static int 
string_list_len (string_list_elt_t *head)
{
  int i = 0;

  while (head != NULL) {
    i++;
    head = head->next;
  }

  return i; 

} /* string_list_len */



/* Free the whole list and the strings in it. */

static void 
string_list_free_list (string_list_elt_t *head)
{
  string_list_elt_t *elt;

  while (head) {
    Ns_Free(head->string);
    elt = head->next;
    Ns_Free(head);
    head = elt;
  }

} /* string_list_free_list */



/* Parse a SQL string and return a list of all
 * the bind variables found in it.
 */

static void
parse_bind_variables(char *input, 
                     string_list_elt_t **bind_variables, 
                     string_list_elt_t **fragments)
{
  char *p, lastchar;
  enum { base, instr, bind } state;
  char *bindbuf, *bp;
  char *fragbuf, *fp;
  string_list_elt_t *elt, *head=0, *tail=0;
  string_list_elt_t *felt, *fhead=0, *ftail=0;
  int current_string_length = 0;
  int first_bind = 0;

  fragbuf = (char*)Ns_Malloc((strlen(input)+1)*sizeof(char));
  fp = fragbuf;
  bindbuf = (char*)Ns_Malloc((strlen(input)+1)*sizeof(char));
  bp = bindbuf;

  for (p = input, state=base, lastchar='\0'; *p != '\0'; lastchar = *p, p++) {

    switch (state) {
    case base:
      if (*p == '\'') {
	state = instr;
        current_string_length = 0;
        *fp++ = *p;
      } else if ((*p == ':') && (*(p + 1) != ':') && (lastchar != ':')) {
	bp = bindbuf;
	state = bind;
        *fp = '\0';
        felt = string_list_elt_new(Ns_StrDup(fragbuf));
        if(ftail == 0) {
          fhead = ftail = felt;
        } else {
          ftail->next = felt;
          ftail = felt;
        }
      } else {
        *fp++ = *p;
      }
      break;

    case instr:
      if (*p == '\'' && (lastchar != '\'' || current_string_length == 0)) {
	state = base;
      }
      current_string_length++;
      *fp++ = *p;
      break;

    case bind:
      if (*p == '=') {
        state = base;
        bp = bindbuf;
        fp = fragbuf;
      } else if (!(*p == '_' || *p == '$' || *p == '#' || isalnum((int)*p))) {
	*bp = '\0';
	elt = string_list_elt_new(Ns_StrDup(bindbuf));
	if (tail == 0) {
	  head = tail = elt;
	} else {
	  tail->next = elt;
	  tail = elt;
	}
	state = base;
        fp = fragbuf;
	p--;
      } else {
	*bp++ = *p;
      }
      break;
    }
  }

  if (state == bind) {
    *bp = '\0';
    elt = string_list_elt_new(Ns_StrDup(bindbuf));
    if (tail == 0) {
      head = tail = elt;
    } else {
      tail->next = elt;
      tail = elt;
    }
  } else {
    *fp = '\0';
    felt = string_list_elt_new(Ns_StrDup(fragbuf));
    if (ftail == 0) {
      fhead = ftail = felt;
    } else {
      ftail->next = felt;
      ftail = felt;
    }
  }
  
  Ns_Free(fragbuf);
  Ns_Free(bindbuf);
  *bind_variables = head;
  *fragments      = fhead;  

  return;

} /* parse_bind_variables */


/*
 * PgBindCmd - This function implements the "ns_pg_bind" Tcl command 
 * installed into each interpreter of each virtual server.  It provides 
 * for the parsing and substitution of bind variables into the original 
 * sql query.  This is an emulation only. Postgresql doesn't currently 
 * support true bind variables yet.
 */

static int
PgBindCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv) {

  string_list_elt_t *bind_variables;
  string_list_elt_t *var_p;
  string_list_elt_t *sql_fragments;
  string_list_elt_t *frag_p;
  Ns_DString         ds;
  Ns_DbHandle       *handle;
  Ns_Set            *rowPtr;
  Ns_Set            *set   = NULL; 
  char              *cmd;
  char              *sql;
  char              *value = NULL;
  char              *p;
  int               value_frag_len = 0;

  if (argc < 4 || (!STREQ("-bind", argv[3]) && (argc != 4)) || 
       (STREQ("-bind", argv[3]) && (argc != 6))) {
    return BadArgs(interp, argv, "dbId sql");
  }

  if (Ns_TclDbGetHandle(interp, argv[2], &handle) != TCL_OK) {
    return TCL_ERROR;
  }

  Ns_DStringFree(&handle->dsExceptionMsg);
  handle->cExceptionCode[0] = '\0';

  /*
   * Make sure this is a PostgreSQL handle before accessing
   * handle->connection as an NsPgConn.
   */

  if (Ns_DbDriverName(handle) != pgName) {
    Tcl_AppendResult(interp, "handle \"", argv[1], "\" is not of type \"",
                     pgName, "\"", NULL);
    return TCL_ERROR;
  }

  cmd = argv[1];

  if (STREQ("-bind", argv[3])) {
    set = Ns_TclGetSet(interp, argv[4]);
    if (set == NULL) {
      Tcl_AppendResult (interp, "invalid set id `", argv[4], "'", NULL);
      return TCL_ERROR;	      
    }
    sql = Ns_StrDup(argv[5]);
  } else {
    sql = Ns_StrDup(argv[3]);
  }

  Ns_Log(Debug,"PgBindCmd: sql = %s", sql);

  /*
   * Parse the query string and find the bind variables.  Return
   * the sql fragments so that the query can be rebuilt with the 
   * bind variable values interpolated into the original query.
   */

  parse_bind_variables(sql, &bind_variables, &sql_fragments);  

  if (string_list_len(bind_variables) > 0) {

    Ns_DStringInit(&ds);

    /*
     * Rebuild the query and substitute the actual tcl variable values
     * for the bind variables.
     */

    for (var_p = bind_variables, frag_p = sql_fragments; 
         var_p != NULL || frag_p != NULL;) {
    
      if (frag_p != NULL) {
        Ns_DStringAppend(&ds, frag_p->string);
        frag_p = frag_p->next;
      }
   
      if (var_p != NULL) {
        if (set == NULL) {
          value = Tcl_GetVar(interp, var_p->string, 0);
        } else {
          value = Ns_SetGet(set, var_p->string);
        }
        if (value == NULL) {
          Tcl_AppendResult (interp, "undefined variable `", var_p->string,
                            "'", NULL);
          Ns_DStringFree(&ds);
          string_list_free_list(bind_variables);
          string_list_free_list(sql_fragments);
          Ns_Free(sql);
          return TCL_ERROR;
        }
        Ns_Log(Debug,"PgBindCmd: bind var: %s = %s", var_p->string, value);

        if ( strlen(value) == 0 ) {
            /*
             * DRB: If the Tcl variable contains the empty string, pass a NULL
             * as the value.
             */
            Ns_DStringAppend(&ds, "NULL");
        } else {
            /*
             * DRB: We really only need to quote strings, but there is one benefit
             * to quoting numeric values as well.  A value like '35 union select...'
             * substituted for a legitimate value in a URL to "smuggle" SQL into a
             * script will cause a string-to-integer conversion error within Postgres.
             * This conversion is done before optimization of the query, so indices are
             * still used when appropriate.
             */
            Ns_DStringAppend(&ds, "'");       

            /*
             * DRB: Unfortunately, we need to double-quote quotes as well...
             */ 
            for (p = value; *p; p++) {
                if (*p == '\'') {
                    if (p > value) {
                        Ns_DStringNAppend(&ds, value, p-value);
                    }
                    value = p;
                    Ns_DStringAppend(&ds, "'");
                }
            }

            if (p > value) {
                Ns_DStringAppend(&ds, value);
            }

            Ns_DStringAppend(&ds, "'");       
        }
        var_p = var_p->next;
      }
    }
  
    Ns_Free(sql);
    sql = Ns_DStringExport(&ds);
    Ns_DStringFree(&ds);
    Ns_Log(Debug, "PgBindCmd: query with bind variables substituted = %s",sql);
  }

  string_list_free_list(bind_variables);
  string_list_free_list(sql_fragments);

  if (STREQ(cmd, "dml")) {
    if (Ns_DbDML(handle, sql) != NS_OK) {
      return DbFail(interp, handle, cmd, sql);
    }
  } else if (STREQ(cmd, "1row")) {
    rowPtr = Ns_Db1Row(handle, sql);
    if (rowPtr == NULL) {
      return DbFail(interp, handle, cmd, sql);
    }
    Ns_TclEnterSet(interp, rowPtr, 1);

  } else if (STREQ(cmd, "0or1row")) {
    int nrows;

    rowPtr = Ns_Db0or1Row(handle, sql, &nrows);
    if (rowPtr == NULL) {
      return DbFail(interp, handle, cmd, sql);
    }
    if (nrows == 0) {
      Ns_SetFree(rowPtr);
    } else {
      Ns_TclEnterSet(interp, rowPtr, 1);
    }

  } else if (STREQ(cmd, "select")) {
    rowPtr = Ns_DbSelect(handle, sql);
    if (rowPtr == NULL) {
      return DbFail(interp, handle, cmd, sql);
    }
    Ns_TclEnterSet(interp, rowPtr, 0);

  } else if (STREQ(cmd, "exec")) {
    switch (Ns_DbExec(handle, sql)) {
    case NS_DML:
      Tcl_SetResult(interp, "NS_DML", TCL_STATIC);
      break;
    case NS_ROWS:
      Tcl_SetResult(interp, "NS_ROWS", TCL_STATIC);
      break;
    default:
      return DbFail(interp, handle, cmd, sql);
      break;
    }

  } else {
    return DbFail(interp, handle, cmd, sql);    
  } 
  Ns_Free(sql);

  return TCL_OK;
}

#endif /* FOR_ACS_USE */

/*
 * PgCmd - This function implements the "ns_pg" Tcl command installed into
 * each interpreter of each virtual server.  It provides access to features
 * specific to the PostgreSQL driver.
 */
static int
PgCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv) {

    Ns_DbHandle    *handle;
    NsPgConn        *pgconn;

    if (Ns_TclDbGetHandle(interp, argv[2], &handle) != TCL_OK) {
        return TCL_ERROR;
    }

 	pgconn = (NsPgConn *) handle->connection;

    /*
     * Make sure this is a PostgreSQL handle before accessing
     * handle->connection as an NsPgConn.
     */
    if (Ns_DbDriverName(handle) != pgName) {
        Tcl_AppendResult(interp, "handle \"", argv[1], "\" is not of type \"",
                         pgName, "\"", NULL);
        return TCL_ERROR;
    }

/* BLOBing functions only if FOR_ACS_USE */
#ifdef FOR_ACS_USE

    if (!strcmp(argv[1], "blob_write")) {
        if (argc != 4) {
        	Tcl_AppendResult(interp, "wrong # args: should be \"",
                         	argv[0], " command dbId blobId\"", NULL);
        	return TCL_ERROR;
		}
		return blob_send_to_stream(interp, handle, argv[3], TRUE, NULL);
	} else if (!strcmp(argv[1], "blob_get")) {
    	if (argc != 4) {
        	Tcl_AppendResult(interp, "wrong # args: should be \"",
                         	argv[0], " command dbId blobId\"", NULL);
        	return TCL_ERROR;
    	}
		if (!pgconn->in_transaction) {
        	Tcl_AppendResult(interp,
							 "blob_get only allowed in transaction", NULL);
        	return TCL_ERROR;
		}
		return blob_get(interp, handle, argv[3]);
	} else if (!strcmp(argv[1], "blob_put")) {
    	if (argc != 5) {
        	Tcl_AppendResult(interp, "wrong # args: should be \"",
                         	argv[0], " command dbId blobId value\"", NULL);
        	return TCL_ERROR;
    	}
		if (!pgconn->in_transaction) {
        	Tcl_AppendResult(interp,
							 "blob_put only allowed in transaction", NULL);
        	return TCL_ERROR;
		}
		return blob_put(interp, handle, argv[3], argv[4]);
	} else if (!strcmp(argv[1], "blob_dml_file")) {
    	if (argc != 5) {
        	Tcl_AppendResult(interp, "wrong # args: should be \"",
                         	argv[0], " command dbId blobId filename\"", NULL);
        	return TCL_ERROR;
    	}
		if (!pgconn->in_transaction) {
        	Tcl_AppendResult(interp,
							 "blob_dml_file only allowed in transaction", NULL);
        	return TCL_ERROR;
		}
		return blob_dml_file(interp, handle, argv[3], argv[4]);
        } else if (!strcmp(argv[1], "blob_select_file")) {
        if (argc != 5) {
        	Tcl_AppendResult(interp, "wrong # args: should be \"",
                         	argv[0], " command dbId blobId filename\"", NULL);          
        	return TCL_ERROR;
        }
                return blob_send_to_stream(interp, handle, argv[3], FALSE, argv[4]);
    }

#endif /* FOR_ACS_USE */

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
                         argv[0], " command dbId\"", NULL);
        return TCL_ERROR;
    }

    if (!strcmp(argv[1], "db")) {
        Tcl_SetResult(interp, (char *) PQdb(pgconn->conn), TCL_STATIC);
    } else if (!strcmp(argv[1], "host")) {
        Tcl_SetResult(interp, (char *) PQhost(pgconn->conn), TCL_STATIC);
    } else if (!strcmp(argv[1], "options")) {
        Tcl_SetResult(interp, (char *) PQoptions(pgconn->conn), TCL_STATIC);
    } else if (!strcmp(argv[1], "port")) {
        Tcl_SetResult(interp, (char *) PQport(pgconn->conn), TCL_STATIC);
    } else if (!strcmp(argv[1], "number")) {
        sprintf(interp->result, "%u", pgconn->cNum);
    } else if (!strcmp(argv[1], "error")) {
        Tcl_SetResult(interp, (char *) PQerrorMessage(pgconn->conn),
			 TCL_STATIC);
    } else if (!strcmp(argv[1], "status")) {
        if (PQstatus(pgconn->conn) == CONNECTION_OK) {
            interp->result = "ok";
        } else {
            interp->result = "bad";
        }
    } else if (!strcmp(argv[1], "ntuples")) {
	char string[16];
	sprintf(string, "%d", pgconn->nTuples);
	Tcl_SetResult(interp, string, TCL_VOLATILE);
    } else {
        Tcl_AppendResult(interp, "unknown command \"", argv[2],
                         "\": should be db, host, options, port, error, ntuples, ",
#ifdef FOR_ACS_USE
                         "blob_write, blob_dml_file, blob_select_file, blob_put, ",
#endif 
                         "or status.", NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}


/*
 * Ns_PgInterpInit - Add the "ns_pg" command to a single Tcl interpreter.
 */
static int
Ns_PgInterpInit(Tcl_Interp *interp, void *ignored) {
    Tcl_CreateCommand(interp, "ns_pg", PgCmd, NULL, NULL);
#ifdef FOR_ACS_USE
    Tcl_CreateCommand(interp, "ns_pg_bind", PgBindCmd, NULL, NULL);
#endif
#ifdef NS_AOLSERVER_3_PLUS
#ifdef FOR_ACS_USE
    Tcl_CreateCommand (interp, "ns_column", pg_column_command, NULL, NULL);
    Tcl_CreateCommand (interp, "ns_table", pg_table_command, NULL, NULL);

#endif /* FOR_ACS_USE */
#endif /* NS_AOLSERVER_3_PLUS */

    return NS_OK;
}

/*
 * Ns_PgServerInit - Have Ns_PgInterpInit called for each interpreter in
 * the virtual server which is being intialized.
 */
static int
Ns_PgServerInit(char *hServer, char *hModule, char *hDriver) {

    return Ns_TclInitInterps(hServer, Ns_PgInterpInit, NULL);
}

/*
 * Ns_PgBindRow - Retrieve the list of column names of rows.
 *
 */
static Ns_Set  *
Ns_PgBindRow(Ns_DbHandle *handle) {

    static char *asfuncname = "Ns_PgBindRow";
    int           i;
    NsPgConn      *nsConn;
    Ns_Set        *row = NULL;

    if (handle == NULL || handle->connection == NULL) {
        Ns_Log(Error, "%s: Invalid connection.", asfuncname);
        goto done;
    }

    if (!handle->fetchingRows) {
        Ns_Log(Error, "%s(%s):  No rows waiting to bind.", asfuncname, handle->datasource);
        goto done;
    }

    nsConn = handle->connection;    
    row = handle->row;

    if (PQresultStatus(nsConn->res) == PGRES_TUPLES_OK) {
        nsConn->curTuple = 0;
        nsConn->nCols = PQnfields(nsConn->res);
        nsConn->nTuples = PQntuples(nsConn->res);
        row = handle->row;
        
        for (i = 0; i < nsConn->nCols; i++) {
            Ns_SetPut(row, (char *) PQfname(nsConn->res, i), NULL);
        }
       
    }
    handle->fetchingRows = NS_FALSE;
    
  done:
    return (row);
}

static void
Ns_PgUnQuoteOidString(Ns_DString *sql) {

    static char *asfuncname = "Ns_PgUnQuoteOidString";
    char *ptr;

    if (sql == NULL) {
        Ns_Log(Error, "%s: Invalid Ns_DString -> sql.", asfuncname);
        return;
    }

/* Additions by Scott Cannon Jr. (SDL/USU):
 *
 * This corrects the problem of quoting oid numbers when requesting a
 * specific row.
 *
 * The quoting of oid's is currently ambiguous.
 */

    if ((ptr = strstr(sql->string, OID_QUOTED_STRING)) != NULL) {
        ptr += (strlen(OID_QUOTED_STRING) - 1);
        *ptr++ = ' ';
        while(*ptr != '\0' && *ptr != '\'') {
            ptr++;
        }
        if (*ptr == '\'') {
            *ptr = ' ';
        }
    }
}

/* Reimplement some things dropped from AOLserver 3 for the ACS */

#ifdef NS_AOLSERVER_3_PLUS
#ifdef FOR_ACS_USE

/* the AOLserver3 team removed some commands that are pretty vital
 * to the normal operation of the ACS (ArsDigita Community System).
 * We include definitions for them here
 */


static int
pg_get_column_index (Tcl_Interp *interp, Ns_DbTableInfo *tinfo,
		      char *indexStr, int *index)
{
    int result = TCL_ERROR;

    if (Tcl_GetInt (interp, indexStr, index)) 
      {
	goto bailout;
      }

    if (*index >= tinfo->ncolumns) 
      {
	char buffer[80];
	sprintf (buffer, "%d", tinfo->ncolumns);

	Tcl_AppendResult (interp, buffer, " is an invalid column "
			  "index.  ", tinfo->table->name, " only has ",
			  buffer, " columns", NULL);
	goto bailout;
      }

    result = TCL_OK;

  bailout:
    return (result);

} /* pg_get_column_index */



/* re-implement the ns_column command */
static int
pg_column_command (ClientData dummy, Tcl_Interp *interp, 
		    int argc, char *argv[])
{
    int result = TCL_ERROR;
    Ns_DbHandle	*handle;
    Ns_DbTableInfo *tinfo = NULL;
    int colindex = -1;

    if (argc < 4) 
      {
	Tcl_AppendResult (interp, "wrong # args:  should be \"",
			  argv[0], " command dbId table ?args?\"", NULL);
	goto bailout;
      }

    if (Ns_TclDbGetHandle (interp, argv[2], &handle) != TCL_OK) 
      {
	goto bailout;
      }

    /*!!! we should cache this */
    tinfo =  Ns_PgGetTableInfo(handle, argv[3]);
    if (tinfo == NULL) 
      {
	Tcl_AppendResult (interp, "could not get table info for "
			  "table ", argv[3], NULL);
	goto bailout;
      }

    if (!strcmp(argv[1], "count")) 
      {
	if (argc != 4) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1], " dbId table\"", NULL);
	    goto bailout;
	  }
	sprintf (interp->result, "%d", tinfo->ncolumns);

      } 
    else if (!strcmp(argv[1], "exists")) 
      {
	if (argc != 5) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1],
			      " dbId table column\"", NULL);
	    goto bailout;
	  }
	colindex = Ns_DbColumnIndex (tinfo, argv[4]);
	if (colindex < 0) 
	  { 
	    Tcl_SetResult (interp, "0", TCL_STATIC);
	  }
	else 
	  {
	    Tcl_SetResult (interp, "1", TCL_STATIC);
	  }
      } 
    else if (!strcmp(argv[1], "name")) 
      {
	if (argc != 5) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1],
			      " dbId table column\"", NULL);
	    goto bailout;
	  }
	if (pg_get_column_index (interp, tinfo, argv[4], &colindex) 
	    != TCL_OK) 
	  {
	    goto bailout;
	  }
	Tcl_SetResult (interp, tinfo->columns[colindex]->name, TCL_VOLATILE);
      } 
    else if (!strcmp(argv[1], "type")) 
      {
	if (argc != 5) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1],
			      " dbId table column\"", NULL);
	    goto bailout;
	  }
	colindex = Ns_DbColumnIndex (tinfo, argv[4]);
	if (colindex < 0) 
	  { 
	    Tcl_SetResult (interp, NULL, TCL_VOLATILE);
	  }
	else 
	  {
	    Tcl_SetResult (interp, 
			   Ns_SetGet(tinfo->columns[colindex], "type"),
			   TCL_VOLATILE);
	  }
      } 
    else if (!strcmp(argv[1], "typebyindex")) 
      {
	if (argc != 5) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1],
			      " dbId table column\"", NULL);
	    goto bailout;
	  }
	if (pg_get_column_index (interp, tinfo, argv[4], &colindex) 
	    != TCL_OK) 
	  {
	    goto bailout;
	  }
	if (colindex < 0) 
	  { 
	    Tcl_SetResult (interp, NULL, TCL_VOLATILE);
	  } 
	else 
	  {
	    Tcl_SetResult (interp, 
			   Ns_SetGet(tinfo->columns[colindex], "type"),
			   TCL_VOLATILE);
	  }

      } 
    else if (!strcmp(argv[1], "value")) 
      {
	/* not used in ACS AFAIK */
	Tcl_AppendResult (interp, argv[1], " value is not implemented.", 
			  NULL);
	goto bailout;

      } 
    else if (!strcmp(argv[1], "valuebyindex")) 
      {
	/* not used in ACS AFAIK */
	Tcl_AppendResult (interp, argv[1], " valuebyindex is not implemented.", 
			  NULL);
	goto bailout;
      } 
    else 
      {
	Tcl_AppendResult (interp, "unknown command \"", argv[1],
			  "\": should be count, exists, name, "
			  "type, typebyindex, value, or "
			  "valuebyindex", NULL);
	goto bailout;
      }

    result = TCL_OK;

  bailout:

    Ns_DbFreeTableInfo (tinfo);
    return (result);

} /* pg_column_command */



/* re-implement the ns_table command */

static int
pg_table_command (ClientData dummy, Tcl_Interp *interp, 
		   int argc, char *argv[])
{
    int result = TCL_ERROR;
    Ns_DString tables_string;
    char *tables, *scan;

    Ns_DbHandle	*handle;

    if (argc < 3) 
      {
	Tcl_AppendResult (interp, "wrong # args:  should be \"",
			  argv[0], " command dbId ?args?\"", NULL);
	goto bailout;
      }

    if (Ns_TclDbGetHandle (interp, argv[2], &handle) != TCL_OK) 
      {
	goto bailout;
      }

    if (!strcmp(argv[1], "bestrowid")) 
      {
	/* not used in ACS AFAIK */
	Tcl_AppendResult (interp, argv[1], " bestrowid is not implemented.", 
			  NULL);
	goto bailout;
      }
    else if (!strcmp(argv[1], "exists")) 
      {
	int exists_p = 0;

	if (argc != 4) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1], "dbId table\"", NULL);
	    goto bailout;
	  }

	Ns_DStringInit (&tables_string);

	scan = Ns_PgTableList (&tables_string, handle, 1);

	if (scan == NULL) 
	  {
	    Ns_DStringFree (&tables_string);
	    goto bailout;
	  }

	while (*scan != '\000') 
	  {
	    if (!strcmp(argv[3], scan)) 
	      {
		exists_p = 1;
		break;
	      }
	    scan += strlen(scan) + 1;
	  }

	Ns_DStringFree (&tables_string);
	
	if (exists_p) 
	  {
	    Tcl_SetResult (interp, "1", TCL_STATIC);
	  } 
	else 
	  {
	    Tcl_SetResult (interp, "0", TCL_STATIC);
	  }

      } 
    else if (!strncmp(argv[1], "list", 4)) 
      {
	int system_tables_p = 0;

	if (argc != 3) 
	  {
	    Tcl_AppendResult (interp, "wrong # of args: should be \"",
			      argv[0], " ", argv[1], "dbId\"", NULL);
	    goto bailout;
	  }

	if (!strcmp(argv[1], "listall")) 
	  {
	    system_tables_p = 1;
	  }

	Ns_DStringInit (&tables_string);

	tables = Ns_PgTableList (&tables_string, handle, system_tables_p);

	if (tables == NULL) 
	  {
	    Ns_DStringFree (&tables_string);
	    goto bailout;
	  }

	for (scan = tables; *scan != '\000'; scan += strlen(scan) + 1) 
	  {
	    Tcl_AppendElement (interp, scan);
	  }
	Ns_DStringFree (&tables_string);

      } 
    else if (!strcmp(argv[1], "value")) 
      {
	/* not used in ACS AFAIK */
	Tcl_AppendResult (interp, argv[1], " value is not implemented.", 
			  NULL);
	goto bailout;

      } 
    else 
      {
	Tcl_AppendResult (interp, "unknown command \"", argv[1],
			  "\": should be bestrowid, exists, list, "
			  "listall, or value", NULL);
	goto bailout;
      }

    result = TCL_OK;

  bailout:
    return (result);

} /* ora_table_command */



static Ns_DbTableInfo *
Ns_DbNewTableInfo (char *table)
{
    Ns_DbTableInfo *tinfo;

    tinfo = Ns_Malloc (sizeof(Ns_DbTableInfo));

    tinfo->table = Ns_SetCreate (table);
    tinfo->ncolumns = 0;
    tinfo->size = 5;
    tinfo->columns = Ns_Malloc (sizeof(Ns_Set *) * tinfo->size);

    return (tinfo);

} /* Ns_DbNewTableInfo */



static void
Ns_DbAddColumnInfo (Ns_DbTableInfo *tinfo, Ns_Set *column_info)
{
    tinfo->ncolumns++;

    if (tinfo->ncolumns > tinfo->size) 
      {
	tinfo->size *= 2;
	tinfo->columns = Ns_Realloc (tinfo->columns,
				     tinfo->size * sizeof(Ns_Set *));
      }
    tinfo->columns[tinfo->ncolumns - 1] = column_info;

} /* Ns_DbAddColumnInfo */



static void
Ns_DbFreeTableInfo (Ns_DbTableInfo *tinfo)
{
    int i;

    if (tinfo != NULL) 
      {
	for (i = 0; i < tinfo->ncolumns; i++) 
	  {
	    Ns_SetFree (tinfo->columns[i]);
	  }

	Ns_SetFree (tinfo->table);
	Ns_Free (tinfo->columns);
	Ns_Free (tinfo);
      }

} /* Ns_DbFreeTableInfo */


static int
Ns_DbColumnIndex (Ns_DbTableInfo *tinfo, char *name)
{
    int i;
    int result = -1;

    for (i = 0; i < tinfo->ncolumns; i++) 
      {
	char *cname = tinfo->columns[i]->name;
	if (   (cname == name)
	    || ((cname == NULL) && (name == NULL))
	    || (strcmp(cname, name) == 0)) 
	  {
	    result = i;
	    break;
	  }
      }

    return (result);

} /* Ns_DbColumnIndex */


#endif /* FOR_ACS_USE */
#endif /* NS_AOLSERVER_3_PLUS */

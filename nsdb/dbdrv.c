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
 * dbdrv.c --
 *
 *	Routines for handling the loadable db driver interface.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsdb/dbdrv.c,v 1.2 2004/06/20 15:54:01 seryakov Exp $, compiled: " __DATE__ " " __TIME__;

#include "db.h"

/*
 * The following typedefs define the functions provided by
 * loadable drivers.
 */

typedef int (InitProc) (char *server, char *module, char *driver);
typedef char *(NameProc) (Ns_DbHandle *);
typedef char *(TypeProc) (Ns_DbHandle *);
typedef int (OpenProc) (Ns_DbHandle *);
typedef void (CloseProc) (Ns_DbHandle *);
typedef int (DMLProc) (Ns_DbHandle *, char *sql);
typedef Ns_Set *(SelectProc) (Ns_DbHandle *, char *sql);
typedef int (ExecProc) (Ns_DbHandle *, char *sql);
typedef Ns_Set *(BindProc) (Ns_DbHandle *);
typedef int (GetProc) (Ns_DbHandle *, Ns_Set *);
typedef int (FlushProc) (Ns_DbHandle *);
typedef int (CancelProc) (Ns_DbHandle *);
typedef int (ResetProc) (Ns_DbHandle *);
typedef int (SpStartProc) (Ns_DbHandle *handle, char *procname);
typedef int (SpSetParamProc) (Ns_DbHandle *handle, char *args);
typedef int (SpExecProc) (Ns_DbHandle *handle);
typedef int (SpReturnCodeProc) (Ns_DbHandle *dbhandle, char *returnCode,
				int bufsize);
typedef Ns_Set *(SpGetParamsProc) (Ns_DbHandle *handle);

/*
 * The following structure specifies the driver-specific functions
 * to call for each Ns_Db routine.
 */

typedef struct DbDriver {
    char	*name;
    int		 registered;
    InitProc	*initProc;
    NameProc	*nameProc;
    TypeProc	*typeProc;
    OpenProc	*openProc;
    CloseProc 	*closeProc;
    DMLProc 	*dmlProc;
    SelectProc	*selectProc;
    ExecProc	*execProc;
    BindProc	*bindProc;
    GetProc 	*getProc;
    FlushProc	*flushProc;
    CancelProc	*cancelProc;
    ResetProc	*resetProc;
    SpStartProc *spstartProc;
    SpSetParamProc   *spsetparamProc;
    SpExecProc       *spexecProc;
    SpReturnCodeProc *spreturncodeProc;
    SpGetParamsProc  *spgetparamsProc;
} DbDriver;
    
/*
 * Static variables defined in this file
 */

static Tcl_HashTable driversTable;


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbRegisterDriver --
 *
 *	Register db procs for a driver.  This routine is called by
 *	driver modules when loaded.
 *
 * Results:
 *	NS_OK if procs registered, NS_ERROR otherwise.
 *
 * Side effects:
 *	Driver structure is allocated and function pointers are set
 *	to the given array of procs.
 *
 *----------------------------------------------------------------------
 */

static void
UnsupProcId(char *name)
{
    Ns_Log(Warning, "dbdrv: unsupported function id '%s'", name);
}

int
Ns_DbRegisterDriver(char *driver, Ns_DbProc *procs)
{
    Tcl_HashEntry *hPtr;
    DbDriver *driverPtr = NULL;

    hPtr = Tcl_FindHashEntry(&driversTable, driver);
    if (hPtr == NULL) {
        Ns_Log(Error, "dbdrv: no such driver '%s'", driver);
	return NS_ERROR;
    }
    driverPtr = (DbDriver *) Tcl_GetHashValue(hPtr);
    if (driverPtr->registered) {
        Ns_Log(Error, "dbdrv: a driver is already registered as '%s'",
	       driver);
        return NS_ERROR;
    }
    driverPtr->registered = 1;
    
    while (procs->func != NULL) {
	switch (procs->id) {
	    case DbFn_ServerInit:
		driverPtr->initProc = (InitProc *) procs->func;
		break;
	    case DbFn_Name:
		driverPtr->nameProc = (NameProc *) procs->func;
		break;
	    case DbFn_DbType:
		driverPtr->typeProc = (TypeProc *) procs->func;
		break;
	    case DbFn_OpenDb:
		driverPtr->openProc = (OpenProc *) procs->func;
		break;
	    case DbFn_CloseDb:
		driverPtr->closeProc = (CloseProc *) procs->func;
		break;
	    case DbFn_DML:
		driverPtr->dmlProc = (DMLProc *) procs->func;
		break;
	    case DbFn_Select:
		driverPtr->selectProc = (SelectProc *) procs->func;
		break;
	    case DbFn_GetRow:
		driverPtr->getProc = (GetProc *) procs->func;
		break;
	    case DbFn_Flush:
		driverPtr->flushProc = (FlushProc *) procs->func;
		break;
	    case DbFn_Cancel:
		driverPtr->cancelProc = (CancelProc *) procs->func;
		break;
	    case DbFn_Exec:
		driverPtr->execProc = (ExecProc *) procs->func;
		break;
	    case DbFn_BindRow:
		driverPtr->bindProc = (BindProc *) procs->func;
		break;
	    case DbFn_ResetHandle:
		driverPtr->resetProc = (ResetProc *) procs->func;
		break;

	    case DbFn_SpStart:
		driverPtr->spstartProc = (SpStartProc *) procs->func;
		break;
		
	    case DbFn_SpSetParam:
		driverPtr->spsetparamProc = (SpSetParamProc *) procs->func;
		break;

	    case DbFn_SpExec:
		driverPtr->spexecProc = (SpExecProc *) procs->func;
		break;

	    case DbFn_SpReturnCode:
		driverPtr->spreturncodeProc = (SpReturnCodeProc *) procs->func;
		break;

	    case DbFn_SpGetParams:
		driverPtr->spgetparamsProc = (SpGetParamsProc *) procs->func;
		break;

	    /*
	     * The following functions are no longer supported.
	     */

	    case DbFn_End:
		UnsupProcId("End");
		break;
		
	    case DbFn_GetTableInfo:
		UnsupProcId("GetTableInfo");
		break;
		
	    case DbFn_TableList:
		UnsupProcId("TableList");
		break;
		
	    case DbFn_BestRowId:
		UnsupProcId("BestRowId");
		break;
		
	    default:
		Ns_Log(Error, "dbdrv: unknown driver id '%d'", procs->id);
		return NS_ERROR;
		break;
	}
	++procs;
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDriverName --
 *
 *	Return the string name of the driver.
 *
 * Results:
 *	String name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DbDriverName(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    char *name = NULL;

    if (driverPtr != NULL && driverPtr->nameProc != NULL) {

    	name = (*driverPtr->nameProc)(handle);
    }

    return name;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDriverType --
 *
 *	Return the string name of the database type (e.g., "sybase").
 *
 * Results:
 *	String name.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DbDriverDbType(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    
    if (driverPtr == NULL ||
	driverPtr->typeProc == NULL ||
	handle->connected == NS_FALSE) {

    	return NULL;
    }

    return (*driverPtr->typeProc)(handle);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbDML --
 *
 *	Execute an SQL statement which is expected to be DML.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	SQL is sent to database for evaluation.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbDML(Ns_DbHandle *handle, char *sql)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (driverPtr != NULL && handle->connected) {

	if (driverPtr->execProc != NULL) {
    	    status = Ns_DbExec(handle, sql);
	    if (status == NS_DML) {
		status = NS_OK;
	    } else {
		if (status == NS_ROWS) {
        	    Ns_DbSetException(handle, "NSDB",
		    	    "Query was not a DML or DDL command.");
        	    Ns_DbFlush(handle);
		}
		status = NS_ERROR;
	    }
	} else if (driverPtr->dmlProc != NULL) {
    	    status = (*driverPtr->dmlProc)(handle, sql);
	    NsDbLogSql(handle, sql);
	}
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSelect --
 *
 *	Execute an SQL statement which is expected to return rows.
 *
 * Results:
 *	Pointer to Ns_Set of selected columns or NULL on error.
 *
 * Side effects:
 *	SQL is sent to database for evaluation.
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_DbSelect(Ns_DbHandle *handle, char *sql)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    Ns_Set *setPtr = NULL;

    if (driverPtr != NULL && handle->connected) {

	if (driverPtr->execProc != NULL) {
    	    if (Ns_DbExec(handle, sql) == NS_ROWS) {
    		setPtr = Ns_DbBindRow(handle);
	    } else {
            if(!handle->dsExceptionMsg.length)  
        	Ns_DbSetException(handle, "NSDB",
		    	"Query was not a statement returning rows.");
	    }
	} else if (driverPtr->selectProc != NULL) {
    	    Ns_SetTrunc(handle->row, 0);
    	    setPtr = (*driverPtr->selectProc)(handle, sql);	
	    NsDbLogSql(handle, sql);
	}
    }
    
    return setPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbExec --
 *
 *	Execute an SQL statement.
 *
 * Results:
 *	NS_DML, NS_ROWS, or NS_ERROR.
 *
 * Side effects:
 *	SQL is sent to database for evaluation.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbExec(Ns_DbHandle *handle, char *sql)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;
    
    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->execProc != NULL) {

    	status = (*driverPtr->execProc)(handle, sql);
	NsDbLogSql(handle, sql);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbBindRow --
 *
 *	Bind the column names from a pending result set.  This routine
 *	is normally called right after an Ns_DbExec if the result
 *	was NS_ROWS.
 *
 * Results:
 *	Pointer to Ns_Set.
 *
 * Side effects:
 *	Column names of result rows are set in the Ns_Set. 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_DbBindRow(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    Ns_Set *setPtr = NULL;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->bindProc != NULL) {

    	Ns_SetTrunc(handle->row, 0);
    	setPtr = (*driverPtr->bindProc)(handle);
    }
    
    return setPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbGetRow --
 *
 *	Fetch the next row waiting in a result set.  This routine
 *	is normally called repeatedly after an Ns_DbSelect or
 *	an Ns_DbExec and Ns_DbBindRow.
 *
 * Results:
 *	NS_END_DATA if there are no more rows, NS_OK or NS_ERROR
 *	otherwise.
 *
 * Side effects:
 *	The values of the given set are filled in with those of the
 *	next row.
 *
 *----------------------------------------------------------------------
 */
int
Ns_DbGetRow(Ns_DbHandle *handle, Ns_Set *row)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->getProc != NULL) {

    	status = (*driverPtr->getProc)(handle, row);
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbFlush --
 *
 *	Flush rows pending in a result set.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Rows waiting in the result set are dumped, perhaps by simply
 *	fetching them over one by one.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbFlush(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->flushProc != NULL) {

    	status = (*driverPtr->flushProc)(handle);
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbCancel --
 *
 *	Cancel the execution of a select and dump pending rows.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Depending on the driver, a running select call which executes
 *	as rows are fetched may be interrupted.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbCancel(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->cancelProc != NULL) {

    	status = (*driverPtr->cancelProc)(handle);
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbResetHandle --
 *
 *	Reset a handle after a cancel operation.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Handle is available for new commands.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbResetHandle (Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->resetProc != NULL) {

    	status = (*driverPtr->resetProc)(handle);
    }
    
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbLoadDriver --
 *
 *	Load a database driver for one or more pools.
 *
 * Results:
 *	Pointer to driver structure or NULL on error.
 *
 * Side effects:
 *	Driver module file may be mapped into the process.
 *
 *----------------------------------------------------------------------
 */

struct DbDriver *
NsDbLoadDriver(char *driver)
{
    Tcl_HashEntry  *hPtr;
    char           *module, *path;
    int             new;
    DbDriver	   *driverPtr;
    static int 	    initialized = NS_FALSE;

    if (initialized == NS_FALSE) {
	Tcl_InitHashTable(&driversTable, TCL_STRING_KEYS);
	initialized = NS_TRUE;
    }

    hPtr = Tcl_CreateHashEntry(&driversTable, driver, &new);
    if (new == 0) {
	driverPtr = (DbDriver *) Tcl_GetHashValue(hPtr);
    } else {
	driverPtr = ns_malloc(sizeof(DbDriver));
	memset(driverPtr, 0, sizeof(DbDriver));
	driverPtr->name = Tcl_GetHashKey(&driversTable, hPtr);
        Tcl_SetHashValue(hPtr, driverPtr);
        module = Ns_ConfigGetValue("ns/db/drivers", driver);
        if (module == NULL) {
	    Ns_Log(Error, "dbdrv: no such driver '%s'", driver);
	} else {
	    path = Ns_ConfigGetPath(NULL, NULL, "db", "driver", driver, NULL);
            if (Ns_ModuleLoad(driver, path, module, "Ns_DbDriverInit")
		    != NS_OK) {
		Ns_Log(Error, "dbdrv: failed to load driver '%s'",
		       driver);
            }
        }
    }
    if (driverPtr->registered == 0) {
	return NULL;
    }

    return driverPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbServerInit --
 *
 *	Invoke driver provided server init proc (e.g., to add driver
 *	specific Tcl commands).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsDbDriverInit(char *server, DbDriver *driverPtr)
{
    if (driverPtr->initProc != NULL &&
	((*driverPtr->initProc) (server, "db", driverPtr->name)) != NS_OK) {

	Ns_Log(Warning, "dbdrv: init proc failed for driver '%s'",
	       driverPtr->name);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbOpen --
 *
 *	Open a connection to the database.  This routine is called
 *	from the pool routines in dbinit.c.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Database may be connected by driver specific routine.
 *
 *----------------------------------------------------------------------
 */

int
NsDbOpen(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);

    Ns_Log(Notice, "dbdrv: opening database '%s:%s'", handle->driver,
	   handle->datasource);
    if (driverPtr == NULL ||
	driverPtr->openProc == NULL ||
	(*driverPtr->openProc) (handle) != NS_OK) {

	Ns_Log(Error, "dbdrv: failed to open database '%s:%s'",
	       handle->driver, handle->datasource);
	handle->connected = NS_FALSE;
        return NS_ERROR;
    }

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbClose --
 *
 *	Close a connection to the database.  This routine is called
 *	from the pool routines in dbinit.c
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
NsDbClose(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    
    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->closeProc != NULL) {

    	(*driverPtr->closeProc)(handle);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSpStart --
 *
 *	Start execution of a stored procedure. 
 *
 * Results:
 *	NS_OK/NS_ERROR. 
 *
 * Side effects:
 *	Begins an SP; see Ns_DbSpExec. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbSpStart(Ns_DbHandle *handle, char *procname)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->spstartProc != NULL) {

	status = (*driverPtr->spstartProc)(handle, procname);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSpSetParam --
 *
 *	Set a parameter in a store procedure; must have executed 
 *	Ns_DbSpStart first. paramname looks like "@x", paramtype is 
 *	like "int" or "varchar", inout is "in" or "out", value is 
 *	like "123". 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbSpSetParam(Ns_DbHandle *handle, char *paramname, char *paramtype,
                char *inout, char *value)
{
    DbDriver   *driverPtr = NsDbGetDriver(handle);
    int         status = NS_ERROR;
    Ns_DString  args;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->spsetparamProc != NULL) {

	Ns_DStringInit(&args);
	Ns_DStringVarAppend(&args, paramname, " ", paramtype, " ", inout, " ",
			    value, NULL);
	status = (*driverPtr->spsetparamProc)(handle, args.string);
	Ns_DStringFree(&args);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSpExec --
 *
 *	Run an Sp begun with Ns_DbSpStart 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbSpExec(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int       status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->spexecProc != NULL) {

	status = (*driverPtr->spexecProc)(handle);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSpReturnCode --
 *
 *	Get the return code from an SP after Ns_DbSpExec 
 *
 * Results:
 *	NS_OK/NSERROR 
 *
 * Side effects:
 *	The return code is put into the passed-in buffer, which must 
 *	be at least bufsize in length. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbSpReturnCode(Ns_DbHandle *handle, char *returnCode, int bufsize)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    int       status = NS_ERROR;

    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->spreturncodeProc != NULL) {

	status = (*driverPtr->spreturncodeProc)(handle, returnCode, bufsize);
    }

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSpGetParams --
 *
 *	Get output parameters after running an SP w/ Ns_DbSpExec. 
 *
 * Results:
 *	NULL or a newly allocated set with output params in it. 
 *
 * Side effects:
 *	Allocs its return value and its members. 
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_DbSpGetParams(Ns_DbHandle *handle)
{
    DbDriver *driverPtr = NsDbGetDriver(handle);
    Ns_Set   *aset = NULL;

    Ns_SetTrunc(handle->row, 0);
    if (handle->connected &&
	driverPtr != NULL &&
	driverPtr->spgetparamsProc != NULL) {

	aset = (*driverPtr->spgetparamsProc)(handle);
    }

    return aset;
}

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
 * dbtcl.c --
 *
 *	Tcl database access routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsdb/dbtcl.c,v 1.6 2005/03/28 00:13:54 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "db.h"

/*
 * The following structure maintains per-interp data.
 */

typedef struct InterpData {
    Tcl_Interp *interp;
    char *server;
    int   cleanup;
    Tcl_HashTable dbs;
} InterpData;

/*
 * Local functions defined in this file
 */

static void EnterRow(Tcl_Interp *interp, Ns_Set *row, int flags,
		     int *statusPtr);
static void EnterHandle(InterpData *idataPtr, Tcl_Interp *interp,
			  Ns_DbHandle *handle);
static int GetHandle(InterpData *idataPtr, char *id,
		     Ns_DbHandle **handle, int clear, Tcl_HashEntry **hPtrPtr);
static int GetHandleObj(InterpData *idataPtr, Tcl_Obj *obj,
		     Ns_DbHandle **handle, int clear, Tcl_HashEntry **hPtrPtr);
static Tcl_InterpDeleteProc FreeData;
static Ns_TclDeferProc ReleaseDbs;
static Tcl_ObjCmdProc DbObjCmd;
static Tcl_CmdProc QuoteListToListCmd, GetCsvCmd, DbErrorCodeCmd,
	DbErrorMsgCmd, GetCsvCmd, DbConfigPathCmd, PoolDescriptionCmd;
static char *datakey = "nsdb:data";


/*
 *----------------------------------------------------------------------
 * Ns_TclDbGetHandle --
 *
 *      Get database handle from its handle id.
 *
 * Results:
 *      See GetHandle().
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclDbGetHandle(Tcl_Interp *interp, char *id, Ns_DbHandle **handle)
{
    InterpData *idataPtr;

    idataPtr = Tcl_GetAssocData(interp, datakey, NULL);
    if (idataPtr == NULL) {
	return TCL_ERROR;
    }
    return GetHandle(idataPtr, id, handle, 0, NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * NsDbAddCmds --
 *
 *      Add the nsdb commands.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsDbAddCmds(Tcl_Interp *interp, void *arg)
{
    InterpData *idataPtr;

    /*
     * Initialize the per-interp data.
     */

    idataPtr = ns_malloc(sizeof(InterpData));
    idataPtr->server = arg;
    idataPtr->interp = interp;
    idataPtr->cleanup = 0;
    Tcl_InitHashTable(&idataPtr->dbs, TCL_STRING_KEYS);
    Tcl_SetAssocData(interp, datakey, FreeData, idataPtr);

    Tcl_CreateObjCommand(interp, "ns_db", DbObjCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_quotelisttolist", QuoteListToListCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_getcsv", GetCsvCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_dberrorcode", DbErrorCodeCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_dberrormsg", DbErrorMsgCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_getcsv", GetCsvCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_dbconfigpath", DbConfigPathCmd, idataPtr, NULL);
    Tcl_CreateCommand(interp, "ns_pooldescription", PoolDescriptionCmd, idataPtr, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * DbCmd --
 *
 *      Implement the AOLserver ns_db Tcl command.
 *
 * Results:
 *      Return TCL_OK upon success and TCL_ERROR otherwise.
 *
 * Side effects:
 *      Depends on the command.
 *
 *----------------------------------------------------------------------
 */

static int
DbObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
#define MAXHANDLES 4
    InterpData	   *idataPtr = data;
    Ns_DbHandle    *handle, **handlesPtrPtr, *staticHandles[MAXHANDLES];
    Ns_Set         *row;
    Tcl_HashEntry  *hPtr;
    Tcl_Obj	   *resultPtr;
    char           *arg, *pool, buf[32];
    int		    timeout, nhandles, n, status;
    static CONST char *opts[] = {
	"getrow", "gethandle", "releasehandle", "select", "dml",
	"1row", "0or1row", "bindrow", "exec", "sp_exec", "sp_getparams",
	"sp_returncode", "sp_setparam", "sp_start", "exception",
	"flush", "bouncepool", "cancel", "connected", "datasource",
	"dbtype", "disconnect", "driver", "interpretsqlfile",
	"password", "poolname", "pools", "resethandle", "setexception",
	"user", "verbose", NULL
    }; enum {
	Db_getrowIdx, Db_gethandleIdx, Db_releasehandleIdx,
	Db_selectIdx, Db_dmlIdx, Db_1rowIdx, Db_0or1rowIdx,
	Db_bindrowIdx, Db_execIdx, Db_sp_execIdx, Db_sp_getparamsIdx,
	Db_sp_returncodeIdx, Db_sp_setparamIdx, Db_sp_startIdx,
	Db_exceptionIdx, Db_flushIdx, Db_bouncepoolIdx, Db_cancelIdx,
	Db_connectedIdx, Db_datasourceIdx, Db_dbtypeIdx, Db_disconnectIdx,
	Db_driverIdx, Db_interpretsqlfileIdx, Db_passwordIdx,
	Db_poolnameIdx, Db_poolsIdx, Db_resethandleIdx, Db_setexceptionIdx,
	Db_userIdx, Db_verboseIdx
    } opt;
    static CONST char *spopts[] = {
	"in", "out", NULL
    };
    enum {
	Sp_inIdx, Sp_outIdx
    } spopt;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 1,
                (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    resultPtr = Tcl_GetObjResult(interp);
    handle = NULL;
    status = NS_OK;

    switch (opt) {

    /*
     * The following options require just a db arg and clears
     * any exception when getting the handle.
     */

    case Db_bindrowIdx:
    case Db_cancelIdx:
    case Db_connectedIdx:
    case Db_datasourceIdx:
    case Db_dbtypeIdx:
    case Db_disconnectIdx:
    case Db_driverIdx:
    case Db_flushIdx:
    case Db_passwordIdx:
    case Db_poolnameIdx:
    case Db_releasehandleIdx:
    case Db_resethandleIdx:
    case Db_sp_execIdx:
    case Db_sp_getparamsIdx:
    case Db_sp_returncodeIdx:
    case Db_userIdx:
    	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "dbId");
	    return 0;
    	}
    	if (GetHandleObj(idataPtr, objv[2], &handle, 1, &hPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	switch ((int) opt) {
    	case Db_bindrowIdx:
            row = Ns_DbBindRow(handle);
            EnterRow(interp, row, NS_TCL_SET_STATIC, &status);
	    break;

    	case Db_cancelIdx:
            status = Ns_DbCancel(handle);
	    break;

    	case Db_connectedIdx:
	    Tcl_SetBooleanObj(resultPtr, handle->connected);
	    break;

    	case Db_datasourceIdx:
	    Tcl_SetResult(interp, handle->datasource, TCL_STATIC);
	    break;

    	case Db_dbtypeIdx:
            Tcl_SetResult(interp, Ns_DbDriverDbType(handle), TCL_STATIC);
	    break;

    	case Db_disconnectIdx:
	    NsDbDisconnect(handle);
	    break;

    	case Db_driverIdx:
            Tcl_SetResult(interp, Ns_DbDriverName(handle), TCL_STATIC);
	    break;

    	case Db_flushIdx:
            status = Ns_DbFlush(handle);
	    break;

    	case Db_passwordIdx:
            Tcl_SetResult(interp, handle->password, TCL_VOLATILE);
	    break;

    	case Db_poolnameIdx:
            Tcl_SetResult(interp, handle->poolname, TCL_VOLATILE);
	    break;

    	case Db_releasehandleIdx:
	    Tcl_DeleteHashEntry(hPtr);
    	    Ns_DbPoolPutHandle(handle);
	    break;

    	case Db_resethandleIdx:
	    status = Ns_DbResetHandle(handle);
	    if (status == NS_OK) {
	    	Tcl_SetIntObj(resultPtr, NS_OK);
	    }
	    break;

    	case Db_sp_execIdx:
	    status = Ns_DbSpExec(handle);
	    if (status == NS_DML) {
		Tcl_SetResult(interp, "NS_DML", TCL_STATIC);
	    } else if (status == NS_ROWS) {
		Tcl_SetResult(interp, "NS_ROWS", TCL_STATIC);
	    }
	    break;

    	case Db_sp_getparamsIdx:
	    row = Ns_DbSpGetParams(handle);
	    EnterRow(interp, row, NS_TCL_SET_DYNAMIC, &status);
	    break;

    	case Db_sp_returncodeIdx:
	    status = Ns_DbSpReturnCode(handle, buf, 32);
	    if (status == NS_OK) {
		Tcl_SetResult(interp, buf, TCL_VOLATILE);
	    }
	    break;

    	case Db_userIdx:
            Tcl_SetResult(interp, handle->user, TCL_VOLATILE);
	    break;
	}
	break;

    /*
     * The following also requires just a db argument and
     * preserves any  exception.
     */

    case Db_exceptionIdx:
    	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "dbId");
	    return 0;
    	}
    	if (GetHandleObj(idataPtr, objv[2], &handle, 0, &hPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
        Tcl_AppendElement(interp, handle->cExceptionCode);
        Tcl_AppendElement(interp, handle->dsExceptionMsg.string);
	break;

    /*
     * The following options require a db and extra string arg and
     * clear any exception.
     */

    case Db_0or1rowIdx:
    case Db_1rowIdx:
    case Db_dmlIdx:
    case Db_execIdx:
    case Db_getrowIdx:
    case Db_interpretsqlfileIdx:
    case Db_selectIdx:
    case Db_sp_startIdx:
    	if (objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "dbId arg");
	    return 0;
    	}
    	if (GetHandleObj(idataPtr, objv[2], &handle, 1, &hPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	arg = Tcl_GetString(objv[3]);

	switch ((int) opt) {
    	case Db_0or1rowIdx:
            row = Ns_Db0or1Row(handle, arg, &n);
            if (row != NULL && n == 0) {
                Ns_SetFree(row);
	    } else {
		EnterRow(interp, row, NS_TCL_SET_DYNAMIC, &status);
	    }
	    break;

    	case Db_1rowIdx:
            row = Ns_Db1Row(handle, arg);
	    EnterRow(interp, row, NS_TCL_SET_DYNAMIC, &status);
	    break;

    	case Db_dmlIdx:
	    status = Ns_DbDML(handle, arg);
	    break;

        case Db_execIdx:
	    status = Ns_DbExec(handle, arg);
	    if (status == NS_DML) {
                Tcl_SetResult(interp, "NS_DML", TCL_STATIC);
	    } else if (status == NS_ROWS) {
                Tcl_SetResult(interp, "NS_ROWS", TCL_STATIC);
	    }
	    break;

    	case Db_getrowIdx:
            if (Ns_TclGetSet2(interp, arg, &row) != TCL_OK) {
                return TCL_ERROR;
            }
	    status = Ns_DbGetRow(handle, row);
	    if (status == NS_OK) {
		Tcl_SetBooleanObj(resultPtr, 1);
	    } else if (status == NS_END_DATA) {
		Tcl_SetBooleanObj(resultPtr, 0);
	    }
	    break;

    	case Db_interpretsqlfileIdx:
	    status = Ns_DbInterpretSqlFile(handle, arg);
	    break;

    	case Db_selectIdx:
            row = Ns_DbSelect(handle, arg);
            EnterRow(interp, row, NS_TCL_SET_STATIC, &status);
	    break;

    	case Db_sp_startIdx:
	    status = Ns_DbSpStart(handle, arg);
	    if (status == NS_OK) {
		Tcl_SetBooleanObj(resultPtr, 0);
	    }
	    break;
	}
	break;

    /*
     * The remaining options requiring specific args.
     */

    case Db_sp_setparamIdx:
	if (objc != 7) {
            Tcl_WrongNumArgs(interp, 2, objv,
			     "dbId paramname type in|out value");
	    return TCL_ERROR;
	}
    	if (Tcl_GetIndexFromObj(interp, objv[5], spopts, "option", 0,
                (int *) &spopt) != TCL_OK) {
            return TCL_ERROR;
        }
    	if (GetHandleObj(idataPtr, objv[2], &handle, 1, &hPtr) != TCL_OK) {
	    return TCL_ERROR;
	}
	status = Ns_DbSpSetParam(handle, Tcl_GetString(objv[3]),
				 Tcl_GetString(objv[4]),
				 spopts[spopt],
				 Tcl_GetString(objv[6]));
	if (status == NS_OK) {
	    Tcl_SetBooleanObj(resultPtr, 1);
	}
	break;

    case Db_gethandleIdx:
	timeout = -1;
	if (objc >= 4) {
	    arg = Tcl_GetString(objv[2]);
	    if (STREQ(arg, "-timeout")) {
		if (Tcl_GetIntFromObj(interp, objv[3], &timeout) != TCL_OK) {
		    return TCL_ERROR;
		}
		objv += 2;
		objc -= 2;
	    } else if (objc > 4) {
            	Tcl_WrongNumArgs(interp, 2, objv,
				 "?-timeout seconds? ?pool? ?nhandles?");
		return TCL_ERROR;
	    }
	}
	objv += 2;
	objc -= 2;

	/*
	 * Determine the pool and requested number of handles
	 * from the remaining args.
	 */
       
	if (objc > 0) {
	    pool = Tcl_GetString(objv[0]);
	} else {
	    pool = Ns_DbPoolDefault(idataPtr->server);
            if (pool == NULL) {
                Tcl_SetResult(interp, "no defaultpool configured", TCL_STATIC);
                return TCL_ERROR;
            }
        }
        if (Ns_DbPoolAllowable(idataPtr->server, pool) == NS_FALSE) {
            Tcl_AppendResult(interp, "no access to pool: \"", pool, "\"",
			     NULL);
            return TCL_ERROR;
        }
        if (objc < 2) {
	    nhandles = 1;
	} else {
            if (Tcl_GetIntFromObj(interp, objv[1], &nhandles) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (nhandles <= 0) {
                Tcl_AppendResult(interp, "invalid nhandles:  \"",
		    Tcl_GetString(objv[1]),
                    "\": should be greater than 0.", NULL);
                return TCL_ERROR;
            }
	}

    	/*
         * Allocate handles and enter them into Tcl.
         */

	if (nhandles > MAXHANDLES) {
	    handlesPtrPtr = ns_malloc(nhandles * sizeof(Ns_DbHandle *));
	} else {
    	    handlesPtrPtr = staticHandles;
	}
	status = Ns_DbPoolTimedGetMultipleHandles(handlesPtrPtr, pool, 
    	    	                                  nhandles, timeout);
    	if (status == NS_OK) {
	    while (--nhandles >= 0) {
                EnterHandle(idataPtr, interp, handlesPtrPtr[nhandles]);
            }
	}
	if (handlesPtrPtr != staticHandles) {
	    ns_free(handlesPtrPtr);
	}
	if (status != NS_TIMEOUT && status != NS_OK) {
	    sprintf(buf, "%d", nhandles);
            Tcl_AppendResult(interp, "could not allocate ", buf,
		" handle(s) from pool \"", pool, "\"", NULL);
            return TCL_ERROR;
        }
	break;

    case Db_bouncepoolIdx:
	if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "pool");
	    return TCL_ERROR;
	}
	status = Ns_DbBouncePool(Tcl_GetString(objv[2]));
	break;

    case Db_poolsIdx:
        if (objc > 2) {
            Tcl_WrongNumArgs(interp, 2, objv, NULL);
            return TCL_ERROR;
        }
	pool = Ns_DbPoolList(idataPtr->server);
	if (pool != NULL) {
	    while (*pool != '\0') {
		Tcl_AppendElement(interp, pool);
		pool = pool + strlen(pool) + 1;
	    }
	}
	break;

    case Db_setexceptionIdx:
        if (objc != 5) {
            Tcl_WrongNumArgs(interp, 2, objv, "dbId code message");
            return TCL_ERROR;
        }
    	if (GetHandleObj(idataPtr, objv[2], &handle, 1, NULL) != TCL_OK) {
	    return TCL_ERROR;
	}
	arg = Tcl_GetStringFromObj(objv[3], &n);
	if (n > 5) {
            Tcl_AppendResult(interp, "code \"", arg,
		    "\" more than 5 characters", NULL);
            return TCL_ERROR;
        }
        Ns_DbSetException(handle, arg, Tcl_GetString(objv[4]));
	break;

    case Db_verboseIdx:
        if (objc != 3 && objc != 4) {
            Tcl_WrongNumArgs(interp, 2, objv, "dbId ?bool?");
	    return TCL_ERROR;
	}
    	if (GetHandleObj(idataPtr, objv[2], &handle, 1, NULL) != TCL_OK) {
	    return TCL_ERROR;
	}
        if (objc == 4) {
            if (Tcl_GetBooleanFromObj(interp, objv[3], &n) != TCL_OK) {
                return TCL_ERROR;
	    }
            handle->verbose = n;
        }
	Tcl_SetIntObj(resultPtr, handle->verbose);
	break;
    }

    if (status == NS_ERROR) {
    	Tcl_AppendResult(interp, "Database operation \"", opts[opt],
		"\" failed", NULL);
	if (handle != NULL && handle->cExceptionCode[0] != '\0') {
            Tcl_AppendResult(interp, " (exception ",
			     handle->cExceptionCode, NULL);
            if (handle->dsExceptionMsg.length > 0) {
            	Tcl_AppendResult(interp, ", \"",
				 handle->dsExceptionMsg.string, "\"", NULL);
	    }
            Tcl_AppendResult(interp, ")", NULL);
        }
	return TCL_ERROR;
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * DbErrorCodeCmd --
 *
 *      Get database exception code for the database handle.
 *
 * Results:
 *      Returns TCL_OK and database exception code is set as Tcl result
 *	or TCL_ERROR if failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
ErrorCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv, int cmd)
{
    InterpData *idataPtr = arg;
    Ns_DbHandle *handle;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
            argv[0], " dbId\"", NULL);
        return TCL_ERROR;
    }
    if (GetHandle(idataPtr, (char*) argv[1], &handle, 0, NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    if (cmd == 'c') {
    	Tcl_SetResult(interp, handle->cExceptionCode, TCL_VOLATILE);
    } else {
    	Tcl_SetResult(interp, handle->dsExceptionMsg.string, TCL_VOLATILE);
    }
    return TCL_OK;
}

static int
DbErrorCodeCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    return ErrorCmd(arg, interp, argc, argv, 'c');
}

static int
DbErrorMsgCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    return ErrorCmd(arg, interp, argc, argv, 'm');
}


/*
 *----------------------------------------------------------------------
 * DbConfigPathCmd --
 *
 *      Get the database section name from the configuration file.
 *
 * Results:
 *      TCL_OK and the database section name is set as the Tcl result
 *	or TCL_ERROR if failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
DbConfigPathCmd(ClientData arg, Tcl_Interp *interp, int argc, CONST char **argv)
{
    InterpData *idataPtr = arg;
    char *section;

    if (argc != 1) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"", argv[0],
			 "\"", NULL);
        return TCL_ERROR;
    }
    section = Ns_ConfigGetPath(idataPtr->server, NULL, "db", NULL);
    Tcl_SetResult(interp, section, TCL_STATIC);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * PoolDescriptionCmd --
 *
 *      Get the pool's description string.
 *
 * Results:
 *      Return TCL_OK and the pool's description string is set as the 
 *	Tcl result string or TCL_ERROR if failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
PoolDescriptionCmd(ClientData arg, Tcl_Interp *interp, int argc, 
        CONST char **argv)
{
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"", 
             (char*)argv[0], " poolname\"", NULL);
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, Ns_DbPoolDescription((char*)argv[1]),TCL_STATIC);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * QuoteListToListCmd --
 *
 *      Remove space, \ and ' characters in a string.
 *
 * Results:
 *      TCL_OK and set the stripped string as the Tcl result or TCL_ERROR
 *	if failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
QuoteListToListCmd(ClientData arg, Tcl_Interp *interp, int argc,
			CONST char **argv)
{
    char       *quotelist;
    int         inquotes;
    Ns_DString  ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
	    argv[0], " quotelist\"", NULL);
        return TCL_ERROR;
    }
    quotelist = (char*)argv[1];
    inquotes = NS_FALSE;
    Ns_DStringInit(&ds);
    while (*quotelist != '\0') {
        if (isspace(UCHAR(*quotelist)) && inquotes == NS_FALSE) {
            if (ds.length != 0) {
                Tcl_AppendElement(interp, ds.string);
                Ns_DStringTrunc(&ds, 0);
            }
            while (isspace(UCHAR(*quotelist))) {
                quotelist++;
            }
        } else if (*quotelist == '\\' && (*(quotelist + 1) != '\0')) {
            Ns_DStringNAppend(&ds, quotelist + 1, 1);
            quotelist += 2;
        } else if (*quotelist == '\'') {
            if (inquotes) {
                /* Finish element */
                Tcl_AppendElement(interp, ds.string);
                Ns_DStringTrunc(&ds, 0);
                inquotes = NS_FALSE;
            } else {
                /* Start element */
                inquotes = NS_TRUE;
            }
            quotelist++;
        } else {
            Ns_DStringNAppend(&ds, quotelist, 1);
            quotelist++;
        }
    }
    if (ds.length != 0) {
        Tcl_AppendElement(interp, ds.string);
    }
    Ns_DStringFree(&ds);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * GetCsvCmd --
 *
 *	Implement the ns_getcvs command to read a line from a CSV file
 *	and parse the results into a Tcl list variable.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	One line is read for given open channel.
 *
 *----------------------------------------------------------------------
 */

static int
GetCsvCmd(ClientData arg, Tcl_Interp *interp, int argc, 
        CONST char **argv)
{
    int             ncols, inquote, quoted, blank;
    char            c, *p, buf[20];
    const char	   *result;
    Tcl_DString     line, cols, elem;
    Tcl_Channel	    chan;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
	    argv[0], " fileId varName\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, (char*)argv[1], 0, 0, &chan) == TCL_ERROR) {
        return TCL_ERROR;
    }
    
    Tcl_DStringInit(&line);
    if (Tcl_Gets(chan, &line) < 0) {
	Tcl_DStringFree(&line);
    	if (!Tcl_Eof(chan)) {
	    Tcl_AppendResult(interp, "could not read from ", argv[1],
	        ": ", Tcl_PosixError(interp), NULL);
	    return TCL_ERROR;
	}
	Tcl_SetResult(interp, "-1", TCL_STATIC);
	return TCL_OK;
    }

    Tcl_DStringInit(&cols);
    Tcl_DStringInit(&elem);
    ncols = 0;
    inquote = 0;
    quoted = 0;
    blank = 1;
    p = line.string;
    while (*p != '\0') {
        c = *p++;
loopstart:
        if (inquote) {
            if (c == '"') {
		c = *p++;
		if (c == '\0') {
		    break;
		}
                if (c == '"') {
                    Tcl_DStringAppend(&elem, &c, 1);
                } else {
                    inquote = 0;
                    goto loopstart;
                }
            } else {
                Tcl_DStringAppend(&elem, &c, 1);
            }
        } else {
            if ((c == '\n') || (c == '\r')) {
                while ((c = *p++) != '\0') {
                    if ((c != '\n') && (c != '\r')) {
			--p;
                        break;
                    }
                }
                break;
            }
            if (c == '"') {
                inquote = 1;
                quoted = 1;
                blank = 0;
            } else if ((c == '\r') || (elem.length == 0 && isspace(UCHAR(c)))) {
                continue;
            } else if (c == ',') {
                if (!quoted) {
                    Ns_StrTrimRight(elem.string);
                }
		Tcl_DStringAppendElement(&cols, elem.string);
                Tcl_DStringTrunc(&elem, 0);
                ncols++;
                quoted = 0;
            } else {
                blank = 0;
                Tcl_DStringAppend(&elem, &c, 1);
            }
        }
    }
    if (!quoted) {
        Ns_StrTrimRight(elem.string);
    }
    if (!blank) {
	Tcl_DStringAppendElement(&cols, elem.string);
        ncols++;
    }
    result = Tcl_SetVar(interp, argv[2], cols.string, TCL_LEAVE_ERR_MSG);
    Tcl_DStringFree(&line);
    Tcl_DStringFree(&cols);
    Tcl_DStringFree(&elem);
    if (result == NULL) {
	return TCL_ERROR;
    }
    sprintf(buf, "%d", ncols);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * GetHandle, GetHandleObj --
 *
 *      Get database handle from its handle id.
 *
 * Results:
 *      Return TCL_OK if handle is found or TCL_ERROR otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
GetHandle(InterpData *idataPtr, char *id, Ns_DbHandle **handlePtr,
	    int clear, Tcl_HashEntry **hPtrPtr)
{
    Ns_DbHandle *handle;
    Tcl_HashEntry  *hPtr;

    hPtr = Tcl_FindHashEntry(&idataPtr->dbs, id);
    if (hPtr == NULL) {
	Tcl_AppendResult(idataPtr->interp, "invalid database id:  \"", id,
			"\"", NULL);
	return TCL_ERROR;
    }
    handle = (Ns_DbHandle *) Tcl_GetHashValue(hPtr);
    if (clear) {
    	Ns_DStringFree(&handle->dsExceptionMsg);
    	handle->cExceptionCode[0] = '\0';
    }
    if (hPtrPtr != NULL) {
	*hPtrPtr = hPtr;
    }
    if (handlePtr != NULL) {
	*handlePtr = handle;
    }
    return TCL_OK;
}


static int
GetHandleObj(InterpData *idataPtr, Tcl_Obj *obj, Ns_DbHandle **handlePtr,
	    int clear, Tcl_HashEntry **hPtrPtr)
{
    return GetHandle(idataPtr, Tcl_GetString(obj), handlePtr, clear, hPtrPtr);
}


/*
 *----------------------------------------------------------------------
 * EnterHandle --
 *
 *      Enter a database handle and create its handle id.
 *
 * Results:
 *      The database handle id is returned as a Tcl result.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static void
EnterHandle(InterpData *idataPtr, Tcl_Interp *interp, Ns_DbHandle *handle)
{
    Tcl_HashEntry *hPtr;
    int            new, next;
    char	   buf[100];

    if (!idataPtr->cleanup) {
	Ns_TclRegisterDeferred(interp, ReleaseDbs, idataPtr);
	idataPtr->cleanup = 1;
    }
    next = idataPtr->dbs.numEntries;
    do {
        sprintf(buf, "nsdb%x", next++);
        hPtr = Tcl_CreateHashEntry(&idataPtr->dbs, buf, &new);
    } while (!new);
    Tcl_AppendElement(interp, buf);
    Tcl_SetHashValue(hPtr, handle);
}


/*
 *----------------------------------------------------------------------
 * FreeData --
 *
 *      Free per-interp data at interp delete time.
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
FreeData(ClientData arg, Tcl_Interp *interp)
{
    InterpData *idataPtr = arg;

    Tcl_DeleteHashTable(&idataPtr->dbs);
    ns_free(idataPtr);
}


/*
 *----------------------------------------------------------------------
 * ReleaseDbs --
 *
 *      Release any database handles still held.
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
ReleaseDbs(Tcl_Interp *interp, void *arg)
{
    Ns_DbHandle *handle;
    Tcl_HashEntry  *hPtr;
    Tcl_HashSearch  search;
    InterpData *idataPtr = arg;

    hPtr = Tcl_FirstHashEntry(&idataPtr->dbs, &search);
    while (hPtr != NULL) {
    	handle = Tcl_GetHashValue(hPtr);
   	Ns_DbPoolPutHandle(handle);
    	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(&idataPtr->dbs);
    Tcl_InitHashTable(&idataPtr->dbs, TCL_STRING_KEYS);
    idataPtr->cleanup = 0;
}


/*
 *----------------------------------------------------------------------
 * EnterRow --
 *
 *      Enter a db row set into interp.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Will set *statusPtr to NS_ERROR if row is null.
 *
 *----------------------------------------------------------------------
 */

static void
EnterRow(Tcl_Interp *interp, Ns_Set *row, int flags, int *statusPtr)
{
    if (row == NULL) {
	*statusPtr = NS_ERROR;
    } else {
        Ns_TclEnterSet(interp, row, flags);
	*statusPtr = NS_OK;
    }
}

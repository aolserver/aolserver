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
 * nsdb.h --
 *
 *      Public types and function declarations for the nsdb module.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/include/nsdb.h,v 1.1 2002/05/15 20:21:35 jgdavidson Exp $
 */

#ifndef NSDB_H
#define NSDB_H

#include "ns.h"

/*
 * The following are nsdb return codes.
 */

#define NS_DML  		  1
#define NS_ROWS 		  2
#define NS_END_DATA 		  4
#define NS_NO_DATA 		  8

/* 
 * The following enum defines known nsdb driver function ids.
 */

typedef enum {
    DbFn_Name,
    DbFn_DbType,
    DbFn_ServerInit,
    DbFn_OpenDb,
    DbFn_CloseDb,
    DbFn_DML,
    DbFn_Select,
    DbFn_GetRow,
    DbFn_Flush,
    DbFn_Cancel,
    DbFn_GetTableInfo,
    DbFn_TableList,
    DbFn_BestRowId,
    DbFn_Exec,
    DbFn_BindRow,
    DbFn_ResetHandle,
    DbFn_SpStart,
    DbFn_SpSetParam,
    DbFn_SpExec,
    DbFn_SpReturnCode,
    DbFn_SpGetParams,
    DbFn_End
} Ns_DbProcId;

/*
 * Database procedure structure used when registering
 * a driver. 
 */

typedef struct Ns_DbProc {
    Ns_DbProcId id;
    void       *func;
} Ns_DbProc;

/*
 * Database handle structure.
 */

typedef struct Ns_DbHandle {
    char       *driver;
    char       *datasource;
    char       *user;
    char       *password;
    void       *connection;
    char       *poolname;
    int         connected;
    int         verbose;
    Ns_Set     *row;
    char        cExceptionCode[6];
    Ns_DString  dsExceptionMsg;
    void       *context;
    void       *statement;
    int         fetchingRows;
} Ns_DbHandle;

/*
 * The following structure is no longer supported and only provided to
 * allow existing database modules to compile.  All of the TableInfo
 * routines now log an unsupported use error and return an error result.
 */

typedef struct {
    Ns_Set  *table;
    int      size;
    int      ncolumns;
    Ns_Set **columns;
} Ns_DbTableInfo;

/*
 * dbdrv.c:
 */

NS_EXTERN int Ns_DbRegisterDriver(char *driver, Ns_DbProc *procs);
NS_EXTERN char *Ns_DbDriverName(Ns_DbHandle *handle);
NS_EXTERN char *Ns_DbDriverDbType(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbDML(Ns_DbHandle *handle, char *sql);
NS_EXTERN Ns_Set *Ns_DbSelect(Ns_DbHandle *handle, char *sql);
NS_EXTERN int Ns_DbExec(Ns_DbHandle *handle, char *sql);
NS_EXTERN Ns_Set *Ns_DbBindRow(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbGetRow(Ns_DbHandle *handle, Ns_Set *row);
NS_EXTERN int Ns_DbFlush(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbCancel(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbResetHandle(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbSpStart(Ns_DbHandle *handle, char *procname);
NS_EXTERN int Ns_DbSpSetParam(Ns_DbHandle *handle, char *paramname,
			   char *paramtype, char *inout, char *value);
NS_EXTERN int Ns_DbSpExec(Ns_DbHandle *handle);
NS_EXTERN int Ns_DbSpReturnCode(Ns_DbHandle *handle, char *returnCode,
			     int bufsize);
NS_EXTERN Ns_Set *Ns_DbSpGetParams(Ns_DbHandle *handle);

/*
 * dbinit.c:
 */

NS_EXTERN char *Ns_DbPoolDescription(char *pool);
NS_EXTERN char *Ns_DbPoolDefault(char *server);
NS_EXTERN char *Ns_DbPoolList(char *server);
NS_EXTERN int Ns_DbPoolAllowable(char *server, char *pool);
NS_EXTERN void Ns_DbPoolPutHandle(Ns_DbHandle *handle);
NS_EXTERN Ns_DbHandle *Ns_DbPoolTimedGetHandle(char *pool, int wait);
NS_EXTERN Ns_DbHandle *Ns_DbPoolGetHandle(char *pool);
NS_EXTERN int Ns_DbPoolGetMultipleHandles(Ns_DbHandle **handles, char *pool,
				       int nwant);
NS_EXTERN int Ns_DbPoolTimedGetMultipleHandles(Ns_DbHandle **handles, char *pool,
					    int nwant, int wait);
NS_EXTERN int Ns_DbBouncePool(char *pool);

/*
 * dbtcl.c:
 */

NS_EXTERN int Ns_TclDbGetHandle(Tcl_Interp *interp, char *handleId,
			     Ns_DbHandle **handle);

/*
 * dbutil.c:
 */
    
NS_EXTERN void Ns_DbQuoteValue(Ns_DString *pds, char *string);
NS_EXTERN Ns_Set *Ns_Db0or1Row(Ns_DbHandle *handle, char *sql, int *nrows);
NS_EXTERN Ns_Set *Ns_Db1Row(Ns_DbHandle *handle, char *sql);
NS_EXTERN int Ns_DbInterpretSqlFile(Ns_DbHandle *handle, char *filename);
NS_EXTERN void Ns_DbSetException(Ns_DbHandle *handle, char *code, char *msg);

#endif /* NSDB_H */

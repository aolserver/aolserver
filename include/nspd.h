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


#ifndef NSPD_H
#define NSPD_H

#define DB_END_DATA 3
#define DB_ROWS 2
#define DB_DML 1
#define EXCEPTION_CODE_MAX 32
#define EXCEPTION_MSG_MAX  4096
#define END_DATA "-1\n"

#define NS_OK 0
#define NS_ERROR (-1)

typedef enum {
    Error,
    Notice,
    Trace
}               Ns_PdLogMsgType;

typedef struct Ns_PdRowData {
    int             elSize;
    char           *elData;
}               Ns_PdRowData;

typedef struct Ns_PdRowInfo {
    int             numColumns;
    Ns_PdRowData   *rowData;
}               Ns_PdRowInfo;

extern int      Ns_PdMain(int argc, char **argv);
extern Ns_PdRowInfo *Ns_PdNewRowInfo(int ncols);
extern void     Ns_PdFreeRowInfo(Ns_PdRowInfo * rowInfo, int fFreeData);
extern void     Ns_PdSendRowInfo(Ns_PdRowInfo * rowInfo);
extern void     Ns_PdSetRowInfoNumColumns(Ns_PdRowInfo * rowInfo, int numColumns);
extern void     Ns_PdSetRowInfoItem(Ns_PdRowInfo * rowInfo, int index, char *data, int size);
extern int      Ns_PdGetRowInfoNumColumns(Ns_PdRowInfo * rowInfo);
extern int      Ns_PdFindRowInfoValue(Ns_PdRowInfo * rowInfo, char *value, int len);
extern void     Ns_PdGetRowInfoItem(Ns_PdRowInfo * rowInfo, int index, char **data, int *size);
extern void    *Ns_PdDbInit(void);
extern void     Ns_PdDbFlush(void *dbhandle);
extern void     Ns_PdDbCancel(void *dbhandle);
extern void     Ns_PdDbTableList(void *dbhandle, char *includeSystem);
extern void     Ns_PdDbExec(void *dbhandle, char *sql);
extern void     Ns_PdDbBindRow(void *dbhandle);
extern void     Ns_PdDbGetRow(void *dbhandle, char *columnCount);
extern void     Ns_PdDbGetTableInfo(void *dbhandle, char *tableName);
extern void     Ns_PdDbBestRowId(void *dbhandle, char *tableName);
extern void     Ns_PdDbClose(void *dbhandle);
extern void     Ns_PdDbOpen(void *dbhandle, char *datasource);
extern void     Ns_PdDbCleanup(void *dbhandle);
extern void     Ns_PdDbIdentify(void *dbhandle);
extern void     Ns_PdDbGetTypes(void *dbhandle);
extern void     Ns_PdDbResultId(void *dbhandle);
extern void     Ns_PdDbResultRows(void *dbhandle);
extern void     Ns_PdDbSetMaxRows(void *dbhandle, char *maxRows);
extern void     Ns_PdDbResetHandle(void *dbhandle);

extern void     Ns_PdSendString(char *rsp);
extern void     Ns_PdSendData(char *data, int len);
extern void     Ns_PdSendException(char *code, char *msg);
extern int      Ns_PdCloseonexec(int fd);
extern void 
Ns_PdParseOpenArgs(char *openargs, char **datasource, char **user,
    char **password, char **param);
extern int      Ns_PdSqlbufEnough(char **sqlbuf, int *sqlbufsize, int howmuch);
extern void     Ns_PdLog(Ns_PdLogMsgType errtype, char *format,...);
extern char    *Ns_PdStringTrim(char *string);
extern void     Ns_PdDbSpReturnCode(void *handle);
extern void     Ns_PdDbSpStart(void *handle, char *procname);
extern void     Ns_PdDbSpSetParam (void *handle, char *args);
extern void     Ns_PdDbSpExec (void *handle);
extern void     Ns_PdDbSpGetParams(void *handle);

#endif                                  /* NSPD_H */

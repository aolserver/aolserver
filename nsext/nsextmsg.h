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


#ifndef NSEXTMSG_H
#define NSEXTMSG_H

typedef enum {
    Exec = 0,
    BindRow = 1,
    GetRow = 2,
    Flush = 3,
    Cancel = 4,
    GetTableInfo = 5,
    TableList = 6,
    BestRowId = 7,
    ResultId = 8,
    ResultRows = 9,
    SetMaxRows = 10,
    Close = 11,
    Open = 12,
    Ping = 13,
    Identify = 14,
    TraceOn = 15,
    TraceOff = 16,
    GetTypes = 17,
    OpenF = 18,
    CloseF = 19,
    ReadF = 20,
    WriteF = 21,
    DeleteF = 22,
    CreateTmpF = 23,
    ResetHandle = 24,
    SpStart = 25,
    SpSetParam = 26,
    SpExec = 27,
    SpReturnCode = 28,
    SpGetParams = 29
} Ns_ExtDbCommandCode;

#define OK_STATUS "ok"
#define SILENT_ERROR_STATUS "silentError"
#define NO_BESTROWID "__nobestrowid__"
#define ARG_TOKEN_DELIMITER '#'
#define EXEC_RET_ROWS "exec_rows"
#define EXEC_RET_DML "exec_dml"

extern int      Ns_ExtDbMsgNameToCode(char *msgname);
extern char    *Ns_ExtDbMsgCodeToName(Ns_ExtDbCommandCode code);
extern short    Ns_ExtDbMsgRequiresArg(Ns_ExtDbCommandCode code);

#endif                                  /* NSEXTMSG_H */

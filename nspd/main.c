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
 * main.c --
 *
 *	The proxy-side library for external drivers. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nspd/main.c,v 1.8 2003/03/07 18:08:49 vasiljevic Exp $, compiled: " __DATE__ " " __TIME__;

#include "pd.h"
#include "nspd.h"
#include <ctype.h>

#define CMDMAX 32768
#ifndef F_CLOEXEC
#define F_CLOEXEC 1
#endif
#define FILE_TEMPLATE "pdXXXXXX"
#define MAX_PATH      256
#define DIR_MAX       (MAX_PATH - 8)
#define UNIX_TMPDIR   "/var/tmp/"
#define MAXSTACK      1024

/*
 * Local functions defined in this file
 */

static int      RecvData(char *buf, int len);
static int      GetMsg(char *buf, char **cmdName, char **cmdArg);
static void     DispatchCmd(char *cmdName, char *cmdArg, void *dbhandle);
static void     PdPing(void);
static void     PdOpenFile(char *openParams);
static void     PdCloseFile(char *fdStr);
static void     PdReadFile(char *readParams);
static void     PdWriteFile(char *writeParams);
static void     PdDeleteFile(char *fileName);
static void     PdCreateTmpFile(void);
static char    *StringTrimRight(char *string);
static char    *StringTrimLeft(char *string);
static void     PdFindBin(char *pgm);
static int      PdPort(char *arg);
static void     PdCreateTmpFile(void);

/*
 * Static variables defined in this file
 */

static int      readInput = 1;
char           *pdBin;

/*
 *==========================================================================
 * API functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdMain --
 *
 *	This runs the main loop for the proxy daemon. 
 *
 * Results:
 *	0. 
 *
 * Side effects:
 *	Will listen on the port and run callbacks; also forks and exits.
 *
 *----------------------------------------------------------------------
 */

int
Ns_PdMain(int argc, char **argv)
{
    int port;

    if (getppid() != 1) {
        if (fork() > 0) {
            exit(0);
        }
        setsid();
    }

    if (argc > 0) {
	PdFindBin(argv[0]);
    } else {
	PdFindBin("nsextd");
    }
    OpenLog();

    if (argc > 1) {
        port = PdPort(argv[1]);
        PdListen(port);
    } else {
        PdMainLoop();
    }
    CloseLog();
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdExit --
 *
 *	Terminate with an exit code. 
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
Ns_PdExit(int code)
{
    if (code == 0) {
        exit(0);
    }
    _exit(code);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSendData --
 *
 *	Write data to the external driver. 
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
Ns_PdSendData(char *data, int len)
{
    int writeReturn;

    do {
        writeReturn = write(1, data, (size_t)len);
    } while ((writeReturn < 0) && (errno == EINTR));
    if (writeReturn != len) {
        Ns_PdLog(Error, "nspd: "
		 "error '%s' writing %d bytes", strerror(errno), len);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSendException --
 *
 *	Send an error message back to the external driver. 
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
Ns_PdSendException(char *code, char *msg)
{
    char  exceptionBuf[EXCEPTION_CODE_MAX + EXCEPTION_MSG_MAX + 2];
    char *p = msg;

    /*
     * map all \n's in exception msg to ' '
     */
    
    while ((p = strchr(p, (int) '\n')) != NULL) {
        *p++ = ' ';
    }
    sprintf(exceptionBuf, "%s%c%s", code,
	    ARG_TOKEN_DELIMITER, msg);
    Ns_PdSendString(exceptionBuf);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdCloseonexec --
 *
 *	Set a file descriptor to close on exec. 
 *
 * Results:
 *	See fcntl. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PdCloseonexec(int fd)
{
    int i;

    i = fcntl(fd, F_GETFD);
    if (i != -1) {
        i |= F_CLOEXEC;
        i = fcntl(fd, F_SETFD, i);
    }
    return i;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdNewRowInfo --
 *
 *	Allocate a new RowInfo structure with space for ncols 
 *	columns. 
 *
 * Results:
 *	An empty Ns_PdRowInfo structure. 
 *
 * Side effects:
 *	The result is malloc'ed.
 *
 *----------------------------------------------------------------------
 */

Ns_PdRowInfo *
Ns_PdNewRowInfo(int ncols)
{
    Ns_PdRowInfo *rowInfo;

    if ((rowInfo = malloc(sizeof(Ns_PdRowInfo))) == NULL) {
        Ns_PdLog(Error, "nspd: rowinfo malloc(%d) error: '%s'",
		 sizeof(Ns_PdRowInfo), strerror(errno));
    } else {
        rowInfo->numColumns = ncols;
	rowInfo->rowData = malloc(sizeof(Ns_PdRowData) * ncols);
	if (rowInfo->rowData == NULL) {
            Ns_PdLog(Error, "nspd: "
		     "rowdata malloc(%d) error: '%s'",
		     sizeof(Ns_PdRowData) * ncols, strerror(errno));
            free(rowInfo);
            rowInfo = NULL;
        }
    }
    
    return rowInfo;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdFreeRowInfo --
 *
 *	Free a Ns_PdRowInfo structure allocated with Ns_PdNewRowInfo. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Frees the structure, and if fFreeData is true, the data as well.
 *
 *----------------------------------------------------------------------
 */

void
Ns_PdFreeRowInfo(Ns_PdRowInfo * rowInfo, int fFreeData)
{
    int i;

    if (fFreeData) {
        for (i = 0; i < rowInfo->numColumns; i++) {
            free(rowInfo->rowData[i].elData);
        }
    }
    free(rowInfo->rowData);
    free(rowInfo);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSendRowInfo --
 *
 *	Send a Ns_PdRowInfo structure to the external driver. 
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
Ns_PdSendRowInfo(Ns_PdRowInfo * rowInfo)
{
    int  i;
    int  size;
    char countbuf[64];

    for (i = 0; i < rowInfo->numColumns; i++) {
        size = rowInfo->rowData[i].elSize;
        sprintf(countbuf, "%d", size);
        Ns_PdSendString(countbuf);
        if (size > 0) {
            Ns_PdSendData(rowInfo->rowData[i].elData, size);
        }
    }
    Ns_PdSendData(END_DATA, (int)strlen(END_DATA));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSetRowInfoNumColumns --
 *
 *	Set the number of columns in a Ns_PdRowInfo structure. 
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
Ns_PdSetRowInfoNumColumns(Ns_PdRowInfo * rowInfo, int numColumns)
{
    rowInfo->numColumns = numColumns;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdGetRowInfoNumColumns --
 *
 *	Returns the number of columns in a Ns_PdRowInfo structure. 
 *
 * Results:
 *	# of cols.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PdGetRowInfoNumColumns(Ns_PdRowInfo * rowInfo)
{
    return rowInfo->numColumns;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSetRowInfoItem --
 *
 *	Sets row info for one item (by index). 
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
Ns_PdSetRowInfoItem(Ns_PdRowInfo * rowInfo, int index, char *data, int size)
{
    rowInfo->rowData[index].elData = data;
    rowInfo->rowData[index].elSize = size;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdGetRowInfoItem --
 *
 *	Get a rowinfo item by index 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Fills in data and size appropriately. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_PdGetRowInfoItem(Ns_PdRowInfo * rowInfo, int index, char **data, int *size)
{
    *data = rowInfo->rowData[index].elData;
    *size = rowInfo->rowData[index].elSize;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdFindRowInfoValue --
 *
 *	Find a value in a Ns_PdRowInfo. Currently, 'value' will 
 *	always be a NULL-terminated string, We're using the len arg 
 *	and memcmp to support binary data (future). 
 *
 * Results:
 *	Index number of item, or -1 if not found.
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PdFindRowInfoValue(Ns_PdRowInfo * rowInfo, char *value, int len)
{
    int i;

    for (i = 0; i < rowInfo->numColumns; i++) {
        if (memcmp(rowInfo->rowData[i].elData, value, (size_t)len) == 0) {
            return i;
        }
    }
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdParseOpenArgs --
 *
 *	Parses datasource#user#password#param, leaving missing 
 *	elements as NULL. 
 *
 * Results:
 *	None. 
 *
 * Side effects:
 *	Sets datasource, user, password, and param appopriately. 
 *
 *----------------------------------------------------------------------
 */

void
Ns_PdParseOpenArgs(char *openargs, char **datasource, char **user,
		   char **password, char **param)
{
    char *p, *lastp;
    char  dl = ARG_TOKEN_DELIMITER;

    Ns_PdLog(Trace, "Ns_PdParseOpenArgs: |%s|", openargs);
    *datasource = *user = *password = *param = NULL;
    lastp = openargs;
    if ((p = strchr(lastp, dl)) != NULL) {
        if (p != lastp) {
            *p = '\0';
            *datasource = strdup(lastp);
        }
        lastp = ++p;
        if ((p = strchr(lastp, dl)) != NULL) {
            if (p != lastp) {
                *p = '\0';
                *user = strdup(lastp);
            }
            lastp = ++p;
            if ((p = strchr(lastp, dl)) != NULL) {
                if (p != lastp) {
                    *p = '\0';
                    *password = strdup(lastp);
                }
                lastp = ++p;
                if (*lastp != '\0') {
                    *param = strdup(lastp);
                }
            }
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSqlbufEnough --
 *
 *	Reallocate a buffer if need be. 
 *
 * Results:
 *	NS_OK/NS_ERROR 
 *
 * Side effects:
 *	May realloc *buf and set buf and size by ref 
 *
 *----------------------------------------------------------------------
 */

int
Ns_PdSqlbufEnough(char **buf, int *size, int howmuch)
{
    int status = NS_OK;

    if (*size < howmuch) {
        Ns_PdLog(Notice, "nspd: "
		 "reallocing sqlbuf from %d to %d", *size, howmuch);
        if ((*buf = (char *) realloc(*buf, (size_t)howmuch)) == NULL) {
            Ns_PdLog(Error, "nspd: realloc error '%s'", strerror(errno));
            status = NS_ERROR;
        } else {
            *size = howmuch;
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_PdStringTrim --
 *
 *	Trim leading and trailing white space from a string. 
 *
 * Results:
 *	The modified string. 
 *
 * Side effects:
 *	Will modify the string. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_PdStringTrim(char *string)
{
    return StringTrimLeft(StringTrimRight(string));
}

/*
 *==========================================================================
 * Exported functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * PdMainLoop --
 *
 *	Loop forever and ever, running callbacks as needed. 
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
PdMainLoop(void)
{
    static char    buf[CMDMAX];
    char           *cmdName;
    char           *cmdArg;
    void           *dbhandle;

    Ns_PdLog(Notice, "nspd: starting");
    if ((dbhandle = Ns_PdDbInit()) == NULL) {
        Ns_PdLog(Error, "nspd: initialization failure");
    } else {
        while (readInput) {
            if (GetMsg(buf, &cmdName, &cmdArg) == NS_OK) {
                DispatchCmd(cmdName, cmdArg, dbhandle);
            } else {
                readInput = 0;
            }
        }
        Ns_PdDbCleanup(dbhandle);
    }
}

/*
 *==========================================================================
 * Static functions
 *==========================================================================
 */


/*
 *----------------------------------------------------------------------
 *
 * PdPort --
 *
 *	Convert a string port number to an integer, and make sure 
 *	it's valid. 
 *
 * Results:
 *	A port number. 
 *
 * Side effects:
 *	Exits if invalid. 
 *
 *----------------------------------------------------------------------
 */

static int
PdPort(char *arg)
{
    int             port;

    port = atoi(arg);
    if (port <= 0 || port >= 65535) {
        Ns_PdLog(Error, "nspd: invalid port '%s'", arg);
        Ns_PdExit(1);
    }
    return port;
}


/*
 *----------------------------------------------------------------------
 *
 * GetMsg --
 *
 *	Read a message from the external driver. 
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
GetMsg(char *buf, char **cmdName, char **cmdArg)
{
    int             status = NS_ERROR;
    int             bytesRead;  
    int             argsize;
    char           *pargsize;
    char           *p;

    if ((bytesRead = RecvData(buf, CMDMAX)) > 0) {
        if ((p = strchr(buf, '\n')) == NULL) {
            Ns_PdLog(Error, "nspd: protocol error: "
		     "no newline terminator for command name");
        } else {
            *p++ = '\0';
            *cmdName = buf;
            pargsize = p;
            if ((p = strchr(pargsize, '\n')) == NULL) {
                Ns_PdLog(Error, "nspd: protocol error: "
			 "no newline terminator for arglen of command: '%s'",
			 *cmdName);
            } else {
                *p++ = '\0';
                argsize = atoi(pargsize);
		if (argsize > (CMDMAX - (p - buf))) {
                    Ns_PdLog(Error, "nspd: "
			     "arglen of %d for %s command is greater than "
			     "configured max %d", argsize, *cmdName, CMDMAX);
                } else {
		    int msgSize;
		    int readError = 0;
		    int readRet;
		    /*
		     * total msg size is sum of cmd\nargsize\nargdata
		     */
		    
		    msgSize = strlen(*cmdName) + strlen(pargsize) +
			argsize + 2;
		    while (bytesRead < msgSize && !readError) {
			if ((readRet = RecvData(&buf[bytesRead],
						msgSize - bytesRead)) > 0) {
			    
			    bytesRead += readRet;
			} else {
			    readError = 1;
			}
		    }
		    if (!readError) {
			*(p + argsize) = '\0';
			*cmdArg = p;
			status = NS_OK;
		    }
                }
            }
        }
    }
    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * DispatchCmd --
 *
 *	Run proxy callbacks when a command comes in. 
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
DispatchCmd(char *cmd, char *arg, void *dbhandle)
{
    Ns_ExtDbCommandCode cmdcode;

    if ((cmdcode = Ns_ExtDbMsgNameToCode(cmd)) == NS_ERROR) {
        Ns_PdLog(Error, "nspd: unknown dispatch message '%s'", cmd);
    } else if (arg == NULL && Ns_ExtDbMsgRequiresArg(cmdcode)) {
        Ns_PdLog(Error, "nspd: dispatch message requires argument");
    } else {
        switch (cmdcode) {
        case Flush:
            Ns_PdDbFlush(dbhandle);
            break;
        case Cancel:
            Ns_PdDbCancel(dbhandle);
            break;
        case Exec:
            Ns_PdDbExec(dbhandle, arg);
            break;
        case BindRow:
            Ns_PdDbBindRow(dbhandle);
            break;
        case GetRow:
            Ns_PdDbGetRow(dbhandle, arg);
            break;
        case GetTableInfo:
	    Ns_PdLog(Error, "nspd: unsupported dispatch command: GetTableInfo");
            break;
        case TableList:
	    Ns_PdLog(Error, "nspd: unsupported dispatch command: TableList");
            break;
        case BestRowId:
	    Ns_PdLog(Error, "nspd: unsupported dispatch command: BestRowId");
            break;
        case Close:
            Ns_PdDbClose(dbhandle);
            readInput = 0;
            break;
        case Open:
            Ns_PdDbOpen(dbhandle, arg);
            break;
        case Identify:
            Ns_PdDbIdentify(dbhandle);
            break;
        case GetTypes:
            Ns_PdDbGetTypes(dbhandle);
            break;
        case ResultId:
            Ns_PdDbResultId(dbhandle);
            break;
        case ResultRows:
            Ns_PdDbResultRows(dbhandle);
            break;
        case SetMaxRows:
            Ns_PdDbSetMaxRows(dbhandle, arg);
            break;
	case ResetHandle:
            Ns_PdDbResetHandle(dbhandle); 
            break;
	case SpStart:
	    Ns_PdDbSpStart(dbhandle, arg);
	    break;
	case SpSetParam:
	    Ns_PdDbSpSetParam(dbhandle, arg);
	    break;
	case SpExec:
	    Ns_PdDbSpExec(dbhandle);
	    break;
	case SpReturnCode:
	    Ns_PdDbSpReturnCode(dbhandle);
	    break;
        case SpGetParams:
	    /*
	     * Should arg be passed here? Possible bug--the original
	     * implementation had it here but not in nssybpd.
	     */
            Ns_PdDbSpGetParams(dbhandle);
            break;
            /*
             * The commands below are not DB-specific, so they're handled in
             * this file
             */
        case Ping:
            PdPing();
            break;
        case TraceOn:
            PdTraceOn(arg);
            break;
        case TraceOff:
            PdTraceOff();
            break;
        case OpenF:
            PdOpenFile(arg);
            break;
        case CloseF:
            PdCloseFile(arg);
            break;
        case ReadF:
            PdReadFile(arg);
            break;
        case WriteF:
            PdWriteFile(arg);
            break;
        case DeleteF:
            PdDeleteFile(arg);
            break;
        case CreateTmpF:
            PdCreateTmpFile();
            break;
        default:
            assert(0);
	    /*
	     * msg validity verified in
	     * DbMsgNameToCode above
	     */
            break;
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdPing --
 *
 *	Respond to a ping request. 
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
PdPing()
{
    Ns_PdLog(Trace, "nspd: responding to ping");
    Ns_PdSendString(OK_STATUS);
}


/*
 *----------------------------------------------------------------------
 *
 * PdOpenFile --
 *
 *	Open a local file, as requested by external driver. 
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
PdOpenFile(char *openParams)
{
    int  fd;
    char errbuf[512];
    char fdStr[8];
    int  matchedArgs;
    char pathName[512];
    int  oflags;

    Ns_PdLog(Trace, "PdOpenFile:");
    matchedArgs = sscanf(openParams, "%d#%s", &oflags, pathName);
    if (matchedArgs != 2) {
        sprintf(errbuf, "Error parsing open parameters: %s",
            openParams);
        Ns_PdLog(Error, "nspd: '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else if ((fd = open(pathName, oflags, 0)) < 0) {
        sprintf(errbuf, "Can't open file %s (oflags=%d): %s",
            pathName, oflags, strerror(errno));
        Ns_PdLog(Error, "nspd: '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else {
        Ns_PdSendString(OK_STATUS);
        sprintf(fdStr, "%d", fd);
        Ns_PdSendString(fdStr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdCloseFile --
 *
 *	Close a file, as requested by the external driver. 
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
PdCloseFile(char *fdStr)
{
    int  fd;
    char errbuf[512];

    Ns_PdLog(Trace, "PdCloseFile:");
    fd = atoi(fdStr);
    if (close(fd) < 0) {
        sprintf(errbuf, "Close error on file descriptor %d: %s",
            fd, strerror(errno));
        Ns_PdLog(Error, "nspd: failed closing file '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else {
        Ns_PdSendString(OK_STATUS);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdReadFile --
 *
 *	Read a file and send it to the external driver. 
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
PdReadFile(char *readParams)
{
    char errbuf[512];
    char readBuf[4096];
    int  bytesRead;
    char bytesReadStr[64];
    int  matchedArgs;
    int  fd, offset, bytecount;

    Ns_PdLog(Trace, "PdReadFile:");
    matchedArgs = sscanf(readParams, "%d#%d#%d",
			 &fd, &offset, &bytecount);
    if (matchedArgs != 3) {
        sprintf(errbuf, "Error parsing read parameters: %s",
		readParams);
        Ns_PdLog(Error, "nspd: failed reading file: '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else {
        lseek(fd, offset, SEEK_SET);
        if ((bytesRead = read(fd, readBuf, (size_t)bytecount)) < 0) {
            sprintf(errbuf, "Read error on fd %d (%d bytes): %s",
		    fd, bytecount, strerror(errno));
            Ns_PdLog(Error, "failed reading file: '%s'", errbuf);
            Ns_PdSendString(errbuf);
        } else {
            Ns_PdSendString(OK_STATUS);
            sprintf(bytesReadStr, "%d", bytesRead);
            Ns_PdSendString(bytesReadStr);
            Ns_PdSendData(readBuf, bytesRead);
            Ns_PdSendData(END_DATA, (int)strlen(END_DATA));
        }
    }

}


/*
 *----------------------------------------------------------------------
 *
 * PdWriteFile --
 *
 *	Write a file from the external driver. 
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
PdWriteFile(char *writeParams)
{
    char  errbuf[512];
    int   matchedArgs;
    int   fd, offset, bytecount, hashcount = 0;
    char *pw;

    Ns_PdLog(Trace, "PdWriteFile:");
    matchedArgs = sscanf(writeParams, "%d#%d#%d#",
			 &fd, &offset, &bytecount);
    if (matchedArgs != 3) {
        sprintf(errbuf, "Error parsing write parameters: %s",
		writeParams);
        Ns_PdLog(Error, "nspd: failed writing file: '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else {
        for (pw = writeParams; *pw;) {
            if (*pw++ == '#') {
                if (++hashcount == matchedArgs) {
                    break;
                }
            }
        }
        lseek(fd, offset, SEEK_SET);
        if (write(fd, pw, (size_t)bytecount) != bytecount) {
            sprintf(errbuf, "Write error to fd %d (%d bytes): %s",
		    fd, bytecount, strerror(errno));
            Ns_PdLog(Error, "nspd: failed writing file: '%s'", errbuf);
            Ns_PdSendString(errbuf);
        } else {
            Ns_PdSendString(OK_STATUS);
        }
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdDeleteFile --
 *
 *	Delete a file, as requested by the external driver. 
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
PdDeleteFile(char *fileName)
{
    char errbuf[512];

    Ns_PdLog(Trace, "PdDeleteFile:");
    if (unlink(fileName) < 0) {
        sprintf(errbuf, "Can't delete file %s: %s",
            fileName, strerror(errno));
        Ns_PdLog(Error, "nspd: failed deleting file: '%s'", errbuf);
        Ns_PdSendString(errbuf);
    } else {
        Ns_PdSendString(OK_STATUS);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * PdCreateTmpFile --
 *
 *	Create a temp file, as requested by the external driver. 
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
PdCreateTmpFile()
{
    char tmpName[MAX_PATH];
    char errbuf[256];
    int  fd;
    int  status = NS_OK;

    Ns_PdLog(Trace, "PdCreateTmpFile:");

    sprintf(tmpName, "%s%s", UNIX_TMPDIR, FILE_TEMPLATE);

    if (status == NS_OK) {
        mktemp(tmpName);
        if (tmpName[0] == '\0') {
            sprintf(errbuf, "mktemp of %s failed", tmpName);
            Ns_PdSendString(errbuf);
            Ns_PdLog(Error, "nspd: failed to create temp file: '%s'", errbuf);
        } else if ((fd = open(tmpName,
			      O_CREAT | O_TRUNC | O_RDWR, 0644)) < 0) {
	    
            sprintf(errbuf, "open/create of %s failed: %s",
		    tmpName, strerror(errno));
            Ns_PdSendString(errbuf);
            Ns_PdLog(Error, "nspd: "
		     "failed to open/create temp file: '%s'", errbuf);
        } else {
            close(fd);
            Ns_PdSendString(OK_STATUS);
            Ns_PdSendString(tmpName);
        }
    }
}



/*
 *----------------------------------------------------------------------
 *
 * Ns_PdSendString --
 *
 *	Send a string, followed by \n per the protocol. Since we're 
 *	tacking on the \n, the string is copied into another (larger) 
 *	buffer. (This is still more efficient than a separate write 
 *	system call for the the additional '\n'. 
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
Ns_PdSendString(char *rsp)
{
    char  outbuf[MAXSTACK];
    char *pout = outbuf;
    int   alloced = 0;
    int   len;

    len = strlen(rsp);
    if (len > MAXSTACK - 1) {
	/*
	 * rare event
	 */
        if ((pout = malloc((size_t)(len + 1))) == NULL) {
            Ns_PdLog(Error, "nspd: "
		     "data truncated; malloc failed to get %d bytes", len + 1);
            rsp[MAXSTACK - 1] = '\0';
            pout = outbuf;
        } else {
            alloced = 1;
        }
    }
    sprintf(pout, "%s\n", rsp);
    Ns_PdSendData(pout, len + 1);
    if (alloced) {
        free(pout);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * RecvData --
 *
 *	Recieve data from the external driver. 
 *
 * Results:
 *	Numberof bytes read, or <0 on error. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static int
RecvData(char *buf, int len)
{
    int readReturn;

    do {
        readReturn = read(0, buf, (size_t)len);
    } while ((readReturn < 0) && (errno == EINTR));
    if (readReturn < 0) {
        Ns_PdLog(Error, "nspd: read error while reading %d bytes: '%s'",
		 strerror(errno), len);
    }
    return readReturn;
}


/*
 *----------------------------------------------------------------------
 *
 * StringTrimLeft --
 *
 *	Trim leading white space from a string. 
 *
 * Results:
 *	A pointer into the original string after whitespace. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

static char *
StringTrimLeft(char *string)
{
    assert(string != NULL);
    while (isspace((unsigned int)*string)) {
        ++string;
    }
    return string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StrTrimRight --
 *
 *	Trim trailing white space from a string. 
 *
 * Results:
 *	A pointer to the original string. 
 *
 * Side effects:
 *	Modifies string. 
 *
 *----------------------------------------------------------------------
 */

static char *
StringTrimRight(char *string)
{
    int len;

    assert(string != NULL);

    len = strlen(string);
    while ((len-- >= 0) 
	   && (isspace((unsigned int)string[len]) || string[len] == '\n')) {
        string[len] = '\0';
    }
    return string;
}


/*
 *----------------------------------------------------------------------
 *
 * PdFindBin --
 *
 *	Set the bin directory and cd /. 
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
PdFindBin(char *pgm)
{

    pdBin = pgm;
    chdir("/");
}

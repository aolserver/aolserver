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

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/dbtcl.c,v 1.7 2001/01/16 18:13:24 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Local functions defined in this file
 */

static int EnableCmds(Tcl_Interp *interp, void *context);
static int BadArgs(Tcl_Interp *interp, char **argv, char *args);
static int DbFail(Tcl_Interp *interp, Ns_DbHandle *handle, char *cmd);
static void EnterDbHandle(Tcl_Interp *interp, Ns_DbHandle *handle);
static int DbGetHandle(Tcl_Interp *interp, char *handleId,
		       Ns_DbHandle **handle, Tcl_HashEntry **phe);
static Tcl_HashTable *GetTable(Tcl_Interp *);
static Ns_Callback ReleaseDbs;

/*
 * Local variables defined in this file
 */

static int cmdsEnabled;


/*
 *----------------------------------------------------------------------
 * Ns_TclDbGetHandle --
 *
 *      Get database handle from its handle id.
 *
 * Results:
 *      See DbGetHandle().
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
Ns_TclDbGetHandle(Tcl_Interp *interp, char *handleId, Ns_DbHandle **handle)
{
    return DbGetHandle(interp, handleId, handle, NULL);
}


/*
 *----------------------------------------------------------------------
 * NsDbTclInit --
 *
 *      Initialization routine.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *     None.
 *
 *----------------------------------------------------------------------
 */

void
NsDbTclInit(char *server)
{
    Ns_TclInitInterps(server, EnableCmds, NULL);
    if (Ns_TclInitModule(server, "nsdb") != NS_OK) {
	Ns_Log(Warning, "dbtcl: failed to initialize nsdb tcl commands");
    }
}


/*
 *----------------------------------------------------------------------
 * NsTclDbCmd --
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

int
NsTclDbCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DbHandle    *handlePtr;
    Ns_Set         *rowPtr;
    char           *cmd;
    char           *pool;

    if (argc < 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
            argv[0], " command ?args ...?", NULL);
        return TCL_ERROR;
    }
    if (cmdsEnabled == NS_FALSE) {
        Tcl_AppendResult(interp, "command \"", argv[0],
			 "\" is not enabled", NULL);
        return TCL_ERROR;
    }

    cmd = argv[1];

    if (STREQ(cmd, "open") || STREQ(cmd, "close")) {
    	Tcl_AppendResult(interp, "unsupported ns_db command: ", cmd, NULL);
    	return TCL_ERROR;

    } else if (STREQ(cmd, "pools")) {
        if (argc != 2) {
            return BadArgs(interp, argv, NULL);
        }
        pool = Ns_DbPoolList(Ns_TclInterpServer(interp));
        if (pool != NULL) {
            while (*pool != '\0') {
                Tcl_AppendElement(interp, pool);
                pool = pool + strlen(pool) + 1;
            }
        }

    } else if (STREQ(cmd, "bouncepool")) {
	if (argc != 3) {
	    return BadArgs(interp, argv, "pool");
	}
	if (Ns_DbBouncePool(argv[2]) == NS_ERROR) {
	    Tcl_AppendResult(interp, "could not bounce: ", argv[2], NULL);
	    return TCL_ERROR;
	}

    } else if (STREQ(cmd, "gethandle")) {
	int timeout, nhandles, result;
	Ns_DbHandle **handlesPtrPtr;

    	timeout = 0;
	if (argc >= 4) {
	    if (STREQ(argv[2], "-timeout")) {
		if (Tcl_GetInt(interp, argv[3], &timeout) != TCL_OK) {
		    return TCL_ERROR;
		}
		argv += 2;
		argc -= 2;
	    } else if (argc > 4) {
		return BadArgs(interp, argv,
		    "?-timeout timeout? ?pool? ?nhandles?");
	    }
        }
	argv += 2;
	argc -= 2;

    	/*
         * Determine the pool and requested number of handles
         * from the remaining args.
         */
       
        pool = argv[0];
        if (pool == NULL) {
            pool = Ns_DbPoolDefault(Ns_TclInterpServer(interp));
            if (pool == NULL) {
                Tcl_SetResult(interp, "no defaultpool configured", TCL_STATIC);
                return TCL_ERROR;
            }
        }
        if (Ns_DbPoolAllowable(Ns_TclInterpServer(interp), pool) == NS_FALSE) {
            Tcl_AppendResult(interp, "no access to pool: \"", pool, "\"",
			     NULL);
            return TCL_ERROR;
        }
        if (argc < 2) {
	    nhandles = 1;
	} else {
            if (Tcl_GetInt(interp, argv[1], &nhandles) != TCL_OK) {
                return TCL_ERROR;
            }
	    if (nhandles <= 0) {
                Tcl_AppendResult(interp, "invalid nhandles \"", argv[1],
                    "\": should be greater than 0.", NULL);
                return TCL_ERROR;
            }
	}

    	/*
         * Allocate handles and enter them into Tcl.
         */

	if (nhandles == 1) {
    	    handlesPtrPtr = &handlePtr;
	} else {
	    handlesPtrPtr = ns_malloc(nhandles * sizeof(Ns_DbHandle *));
	}
	result = Ns_DbPoolTimedGetMultipleHandles(handlesPtrPtr, pool, 
    	    	                                  nhandles, timeout);
    	if (result == NS_OK) {
	    Tcl_DString ds;
	    int i;
	    
            Tcl_DStringInit(&ds);
	    for (i = 0; i < nhandles; ++i) {
                EnterDbHandle(interp, handlesPtrPtr[i]);
                Tcl_DStringAppendElement(&ds, interp->result);
            }
            Tcl_DStringResult(interp, &ds);
	}
	if (handlesPtrPtr != &handlePtr) {
	    ns_free(handlesPtrPtr);
	}
	if (result != NS_TIMEOUT && result != NS_OK) {
            Tcl_AppendResult(interp, "could not allocate ",
	    	nhandles > 1 ? argv[1] : "1", " handle",
		nhandles > 1 ? "s" : "", " from pool \"",
		pool, "\"", NULL);
            return TCL_ERROR;
        }

    } else if (STREQ(cmd, "exception")) {
        if (argc != 3) {
            return BadArgs(interp, argv, "dbId");
        }
        if (DbGetHandle(interp, argv[2], &handlePtr, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_AppendElement(interp, handlePtr->cExceptionCode);
        Tcl_AppendElement(interp, handlePtr->dsExceptionMsg.string);

    } else {
	Tcl_HashEntry  *hPtr;

    	/*
         * All remaining commands require a valid database
         * handle.  The exception message is cleared first.
         */
	
        if (argc < 3) {
            return BadArgs(interp, argv, "dbId ?args?");
        }
        if (DbGetHandle(interp, argv[2], &handlePtr, &hPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_DStringFree(&handlePtr->dsExceptionMsg);
        handlePtr->cExceptionCode[0] = '\0';

    	/*
         * The following commands require just the handle.
         */

        if (STREQ(cmd, "poolname") ||
	    STREQ(cmd, "password") ||
	    STREQ(cmd, "user") ||
	    STREQ(cmd, "datasource") ||
	    STREQ(cmd, "disconnect") ||
	    STREQ(cmd, "dbtype") ||
	    STREQ(cmd, "driver") ||
	    STREQ(cmd, "cancel") ||
	    STREQ(cmd, "bindrow") ||
	    STREQ(cmd, "flush") ||
	    STREQ(cmd, "releasehandle") ||
	    STREQ(cmd, "resethandle") ||
	    STREQ(cmd, "connected") ||
	    STREQ(cmd, "sp_exec") ||
	    STREQ(cmd, "sp_getparams") ||
	    STREQ(cmd, "sp_returncode")) {
	    
            if (argc != 3) {
                return BadArgs(interp, argv, "dbId");
            }

	    if (STREQ(cmd, "poolname")) {
                Tcl_SetResult(interp, handlePtr->poolname, TCL_VOLATILE);

	    } else if (STREQ(cmd, "password")) {
                Tcl_SetResult(interp, handlePtr->password, TCL_VOLATILE);

	    } else if (STREQ(cmd, "user")) {		    
                Tcl_SetResult(interp, handlePtr->user, TCL_VOLATILE);

	    } else if (STREQ(cmd, "dbtype")) {
                Tcl_SetResult(interp, Ns_DbDriverDbType(handlePtr), 
			      TCL_STATIC);

	    } else if (STREQ(cmd, "driver")) {
                Tcl_SetResult(interp, Ns_DbDriverName(handlePtr), TCL_STATIC);

	    } else if (STREQ(cmd, "datasource")) {
		Tcl_SetResult(interp, handlePtr->datasource, TCL_STATIC);

	    } else if (STREQ(cmd, "disconnect")) {
		NsDbDisconnect(handlePtr);

	    } else if (STREQ(cmd, "flush")) {
                if (Ns_DbFlush(handlePtr) != NS_OK) {
                    return DbFail(interp, handlePtr, cmd);
                }

    	    } else if (STREQ(cmd, "bindrow")) {
                rowPtr = Ns_DbBindRow(handlePtr);
                if (rowPtr == NULL) {
                    return DbFail(interp, handlePtr, cmd);
                }
                Ns_TclEnterSet(interp, rowPtr, 0);

    	    } else if (STREQ(cmd, "releasehandle")) {
		Tcl_DeleteHashEntry(hPtr);
    		Ns_DbPoolPutHandle(handlePtr);

    	    } else if (STREQ(cmd, "resethandle")) {
		if (Ns_DbResetHandle(handlePtr) != NS_OK) {
		  return DbFail(interp, handlePtr, cmd);
		}
		sprintf(interp->result, "%d", NS_OK);

    	    } else if (STREQ(cmd, "cancel")) {
                if (Ns_DbCancel(handlePtr) != NS_OK) {
                    return DbFail(interp, handlePtr, cmd);
                }

	    } else if (STREQ(cmd, "connected")) {
                sprintf(interp->result, "%d", handlePtr->connected);
	    } else if (STREQ(cmd, "sp_exec")) {
		switch (Ns_DbSpExec(handlePtr)) {
		case NS_DML:
		    Tcl_SetResult(interp, "NS_DML", TCL_STATIC);
		    break;
		case NS_ROWS:
		    Tcl_SetResult(interp, "NS_ROWS", TCL_STATIC);
		    break;
		default:
		    return DbFail(interp, handlePtr, cmd);
		    break;
		}
	    } else if (STREQ(cmd, "sp_returncode")) {
		char *tmpbuf;

		tmpbuf = ns_malloc(32);
		if (Ns_DbSpReturnCode(handlePtr, tmpbuf, 32) != NS_OK) {
		    ns_free(tmpbuf);
		    return DbFail(interp, handlePtr, cmd);
		} else {
		    Tcl_SetResult(interp, tmpbuf, TCL_VOLATILE);
		    ns_free(tmpbuf);
		}
	    } else if (STREQ(cmd, "sp_getparams")) {
		rowPtr = Ns_DbSpGetParams(handlePtr);
		if (rowPtr == NULL) {
		    return DbFail(interp, handlePtr, cmd);
		}
		Ns_TclEnterSet(interp, rowPtr, 0);
	    }
		

    	/*
         * The following commands require a 3rd argument.
      	 */

        } else if (STREQ(cmd, "getrow") ||
		   STREQ(cmd, "dml") ||
		   STREQ(cmd, "1row") ||
		   STREQ(cmd, "0or1row") ||
		   STREQ(cmd, "exec") ||
		   STREQ(cmd, "select") ||
		   STREQ(cmd, "sp_start") ||
		   STREQ(cmd, "interpretsqlfile")) {

            if (argc != 4) {
    	    	if (STREQ(cmd, "interpretsqlfile")) {
                    return BadArgs(interp, argv, "dbId sqlfile");

		} else if (STREQ(cmd, "getrow")) {
		    return BadArgs(interp, argv, "dbId row");

		} else {
		    return BadArgs(interp, argv, "dbId sql");
		}
	    }

    	    if (STREQ(cmd, "dml")) {
                if (Ns_DbDML(handlePtr, argv[3]) != NS_OK) {
                    return DbFail(interp, handlePtr, cmd);
                }

    	    } else if (STREQ(cmd, "1row")) {
                rowPtr = Ns_Db1Row(handlePtr, argv[3]);
                if (rowPtr == NULL) {
                    return DbFail(interp, handlePtr, cmd);
                }
                Ns_TclEnterSet(interp, rowPtr, 1);

    	    } else if (STREQ(cmd, "0or1row")) {
    	    	int nrows;

                rowPtr = Ns_Db0or1Row(handlePtr, argv[3], &nrows);
                if (rowPtr == NULL) {
                    return DbFail(interp, handlePtr, cmd);
                }
                if (nrows == 0) {
                    Ns_SetFree(rowPtr);
                } else {
                    Ns_TclEnterSet(interp, rowPtr, 1);
                }

    	    } else if (STREQ(cmd, "select")) {
                rowPtr = Ns_DbSelect(handlePtr, argv[3]);
                if (rowPtr == NULL) {
                    return DbFail(interp, handlePtr, cmd);
                }
                Ns_TclEnterSet(interp, rowPtr, 0);

    	    } else if (STREQ(cmd, "exec")) {
                switch (Ns_DbExec(handlePtr, argv[3])) {
                case NS_DML:
                    Tcl_SetResult(interp, "NS_DML", TCL_STATIC);
                    break;
                case NS_ROWS:
                    Tcl_SetResult(interp, "NS_ROWS", TCL_STATIC);
                    break;
                default:
                    return DbFail(interp, handlePtr, cmd);
                    break;
                }

    	    } else if (STREQ(cmd, "interpretsqlfile")) {
                if (Ns_DbInterpretSqlFile(handlePtr, argv[3]) != NS_OK) {
                    return DbFail(interp, handlePtr, cmd);
                }

	    } else if (STREQ(cmd, "sp_start")) {
		if (Ns_DbSpStart(handlePtr, argv[3]) != NS_OK) {
		    return DbFail(interp, handlePtr, cmd);
		}
		Tcl_SetResult(interp, "0", TCL_STATIC);
		
    	    } else { /* getrow */
                if (Ns_TclGetSet2(interp, argv[3], &rowPtr) != TCL_OK) {
                    return TCL_ERROR;
                }
                switch (Ns_DbGetRow(handlePtr, rowPtr)) {
                case NS_OK:
                    Tcl_SetResult(interp, "1", TCL_STATIC);
                    break;
                case NS_END_DATA:
                    Tcl_SetResult(interp, "0", TCL_STATIC);
                    break;
                default:
                    return DbFail(interp, handlePtr, cmd);
                    break;
                }
            }

        } else if (STREQ(cmd, "verbose")) {
	    int verbose;

            if (argc != 3 && argc != 4) {
                return BadArgs(interp, argv, "dbId ?on|off?");
            }
            if (argc == 4) {
                if (Tcl_GetBoolean(interp, argv[3], &verbose) != TCL_OK) {
                    return TCL_ERROR;
                }
                handlePtr->verbose = verbose;
            }
            sprintf(interp->result, "%d", handlePtr->verbose);

        } else if (STREQ(cmd, "setexception")) {
            if (argc != 5) {
                return BadArgs(interp, argv, "dbId code message");
            }
            if (strlen(argv[3]) > 5) {
                Tcl_AppendResult(interp, "code \"", argv[3],
		    "\" more than 5 characters", NULL);
                return TCL_ERROR;
            }
            Ns_DbSetException(handlePtr, argv[3], argv[4]);
	} else if (STREQ(cmd, "sp_setparam")) {
	    if (argc != 7) {
		return BadArgs(interp, argv,
			       "dbId paramname type in|out value");
	    }
	    if (!STREQ(argv[5], "in") && !STREQ(argv[5], "out")) {
		Tcl_SetResult(interp, "inout parameter of setparam must "
			      "be \"in\" or \"out\"", TCL_STATIC);
		return TCL_ERROR;
	    }
	    if (Ns_DbSpSetParam(handlePtr, argv[3], argv[4], argv[5],
				argv[6]) != NS_OK) {
		return DbFail(interp, handlePtr, cmd);
	    } else {
		Tcl_SetResult(interp, "1", TCL_STATIC);
	    }
        } else {
            Tcl_AppendResult(interp, argv[0], ":  Unknown command \"",
			     argv[1], "\":  should be "
			     "0or1row, "
			     "1row, "
			     "bindrow, "
			     "cancel, "
			     "connected, "
			     "datasource, "
			     "dbtype, "
			     "disconnect, "
			     "dml, "
			     "driver, "
			     "exception, "
			     "exec, "
			     "flush, "
			     "gethandle, "
			     "getrow, "
			     "interpretsqlfile, "
			     "password, "
			     "poolname, "
			     "pools, "
			     "releasehandle, "
			     "select, "
			     "setexception, "
			     "sp_start, "
			     "sp_setparam, "
			     "sp_exec, "
			     "sp_returncode, "
			     "sp_getparams, "
			     "user, "
			     "bouncepool"
			     " or verbose", NULL);
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * NsTclDbErrorCodeCmd --
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

int
NsTclDbErrorCodeCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		    char **argv)
{
    Ns_DbHandle *handle;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
            argv[0], " dbId\"", NULL);
        return TCL_ERROR;
    }
    if (cmdsEnabled == NS_FALSE) {
        Tcl_AppendResult(interp, "command \"", argv[0],
			 "\" is not enabled", NULL);
        return TCL_ERROR;
    }

    if (Ns_TclDbGetHandle(interp, argv[1], &handle) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, handle->cExceptionCode, TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * NsTclDbErrorMsgCmd --
 *
 *      Get database exception message for the database handle.
 *
 * Results:
 *      Returns TCL_OK and database exception message is set as Tcl result
 *	or TCL_ERROR if failure.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclDbErrorMsgCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Ns_DbHandle    *handle;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
            argv[0], " dbId\"", NULL);
        return TCL_ERROR;
    }
    if (cmdsEnabled == NS_FALSE) {
        Tcl_AppendResult(interp, "command \"", argv[0],
			 "\" is not enabled", NULL);
        return TCL_ERROR;
    }

    if (Ns_TclDbGetHandle(interp, argv[1], &handle) != TCL_OK) {
        return TCL_ERROR;
    }
    Tcl_SetResult(interp, handle->dsExceptionMsg.string, TCL_VOLATILE);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * NsTclDbConfigPathCmd --
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

int
NsTclDbConfigPathCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		     char **argv)
{
    char *configSection;

    if (argc != 1) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"", argv[0],
			 "\"", NULL);
        return TCL_ERROR;
    }
    if (cmdsEnabled == NS_FALSE) {
        Tcl_AppendResult(interp, "command \"", argv[0],
			 "\" is not enabled", NULL);
        return TCL_ERROR;
    }

    configSection = Ns_ConfigPath(Ns_TclInterpServer(interp), NULL, "db",
				  NULL);
    Tcl_SetResult(interp, configSection, TCL_STATIC);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * NsTclPoolDescriptionCmd --
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

int
NsTclPoolDescriptionCmd(ClientData dummy, Tcl_Interp *interp, int argc,
			char **argv)
{
    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"", argv[0],
			 " poolname\"", NULL);
        return TCL_ERROR;
    }
    if (cmdsEnabled == NS_FALSE) {
        Tcl_AppendResult(interp, "command \"", argv[0],
			 "\" is not enabled", NULL);
        return TCL_ERROR;
    }

    Tcl_SetResult(interp, Ns_DbPoolDescription(argv[1]), TCL_STATIC);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * NsTclQuoteListToListCmd --
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

int
NsTclQuoteListToListCmd(ClientData cd, Tcl_Interp *interp, int argc,
			char **argv)
{
    char       *quotelist;
    int         inquotes;
    Ns_DString  ds;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
	    argv[0], " quotelist\"", NULL);
        return TCL_ERROR;
    }
    quotelist = argv[1];
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
 * NsTclGetCsvCmd --
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

int
NsTclGetCsvCmd(ClientData cd, Tcl_Interp *interp, int argc, char **argv)
{
    int             ncols, inquote, quoted, blank;
    char            c, *p, buf[20];
    Tcl_DString     line, cols, elem;
    Tcl_Channel	    chan;

    if (argc != 3) {
        Tcl_AppendResult(interp, "wrong # of args: should be \"",
	    argv[0], " fileId varName\"", NULL);
        return TCL_ERROR;
    }
    if (Ns_TclGetOpenChannel(interp, argv[1], 0, 0, &chan) == TCL_ERROR) {
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
    p = Tcl_SetVar(interp, argv[2], cols.string, TCL_LEAVE_ERR_MSG);
    Tcl_DStringFree(&line);
    Tcl_DStringFree(&cols);
    Tcl_DStringFree(&elem);
    if (p == NULL) {
	return TCL_ERROR;
    }
    sprintf(buf, "%d", ncols);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclUnsupDbCmd --
 *
 *	Return an error for various unsupported Tcl commands.
 *
 * Results:
 *	A standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclUnsupDbCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_AppendResult(interp, "unsupported nsdb command: ", argv[0], NULL);

    return TCL_ERROR;
}

 
/*
 *----------------------------------------------------------------------
 * EnableCmds --
 *
 *      Add database Tcl commands during initialization.
 *
 * Results:
 *      Return NS_OK.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

static int
EnableCmds(Tcl_Interp *interp, void *context)
{
    cmdsEnabled = NS_TRUE;

    /*
     * Tcl commands were added in tclcmds.c
     */

    return NS_OK;
}


/*
 *----------------------------------------------------------------------
 * DbGetHandle --
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
DbGetHandle(Tcl_Interp *interp, char *handleId, Ns_DbHandle **handle,
	    Tcl_HashEntry **hPtrPtr)
{
    Tcl_HashEntry  *hPtr;
    Tcl_HashTable  *tablePtr;

    tablePtr = GetTable(interp);
    hPtr = Tcl_FindHashEntry(tablePtr, handleId);
    if (hPtr == NULL) {
	Tcl_AppendResult(interp, "invalid database id:  \"", handleId, "\"",
	    NULL);
	return TCL_ERROR;
    }
    *handle = (Ns_DbHandle *) Tcl_GetHashValue(hPtr);
    if (hPtrPtr != NULL) {
	*hPtrPtr = hPtr;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 * EnterDbHandle --
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
EnterDbHandle(Tcl_Interp *interp, Ns_DbHandle *handle)
{
    Tcl_HashTable *tablePtr;
    Tcl_HashEntry *he;
    int            new, next;
    char	   buf[100];

    tablePtr = GetTable(interp);
    next = tablePtr->numEntries;
    do {
        sprintf(buf, "nsdb%x", next++);
        he = Tcl_CreateHashEntry(tablePtr, buf, &new);
    } while (!new);
    Tcl_SetResult(interp, buf, TCL_VOLATILE);
    Tcl_SetHashValue(he, handle);
}


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
DbFail(Tcl_Interp *interp, Ns_DbHandle *handle, char *cmd)
{
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
 *----------------------------------------------------------------------
 * GetTable --
 *
 *      Return the table of allocated db handles in the interp.
 *
 * Results:
 *      Pointer to dynamic, initialized Tcl_HashTable of handle ids.
 *
 * Side effects:
 *      See ReleaseDbs.
 *
 *----------------------------------------------------------------------
 */

static Tcl_HashTable *
GetTable(Tcl_Interp *interp)
{
    Tcl_HashTable *tablePtr;

    tablePtr = NsTclGetData(interp, NS_TCL_DBS_KEY);
    if (tablePtr == NULL) {
	tablePtr = ns_malloc(sizeof(Tcl_HashTable));
	Tcl_InitHashTable(tablePtr, TCL_STRING_KEYS);
	NsTclSetData(interp, NS_TCL_DBS_KEY, tablePtr, ReleaseDbs);
    }
    return tablePtr;
}


/*
 *----------------------------------------------------------------------
 * ReleaseDbs --
 *
 *      Tcl interp data callback to free db data.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Returns all handles not already released and frees the table.
 *
 *----------------------------------------------------------------------
 */

static void
ReleaseDbs(void *arg)
{
    Tcl_HashTable *tablePtr = arg;
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    Ns_DbHandle *handle;

    hPtr = Tcl_FirstHashEntry(tablePtr, &search);
    while (hPtr != NULL) {
	handle = Tcl_GetHashValue(hPtr);
    	Ns_DbPoolPutHandle(handle);
	hPtr = Tcl_NextHashEntry(&search);
    }
    Tcl_DeleteHashTable(tablePtr);
    ns_free(tablePtr);
}

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
 * dbutil.c --
 *
 *	Utility db routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsdb/dbutil.c,v 1.1 2002/05/15 20:17:49 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "db.h"

/*
 * The following constants are defined for this file.
 */

#define NS_SQLERRORCODE "NSINT" /* SQL error code for AOLserver exceptions. */


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbQuoteValue --
 *
 *	Add single quotes around an SQL string value if necessary.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copy of the string, modified if needed, is placed in the 
 *	given Ns_DString.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DbQuoteValue(Ns_DString *pds, char *string)
{
    while (*string != '\0') {
        if (*string == '\'') {
            Ns_DStringNAppend(pds, "'", 1);
        }
        Ns_DStringNAppend(pds, string, 1);
        ++string;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Db0or1Row --
 *
 *	Send an SQL statement which should return either no rows or
 *	exactly one row.
 *
 * Results:
 *	Pointer to new Ns_Set which must be eventually freed.  The
 *	set includes the names of the columns and, if a row was
 *	fetched, the values for the row.  On error, returns NULL.
 *
 * Side effects:
 *	Given nrows pointer is set to 0 or 1 to indicate if a row
 *	was actually returned.
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_Db0or1Row(Ns_DbHandle *handle, char *sql, int *nrows)
{
    Ns_Set *row;

    row = Ns_DbSelect(handle, sql);
    if (row != NULL) {
        if (Ns_DbGetRow(handle, row) == NS_END_DATA) {
            *nrows = 0;
        } else {
	    switch (Ns_DbGetRow(handle, row)) {
		case NS_END_DATA:
		    *nrows = 1;
		    break;

		case NS_OK:
		    Ns_DbSetException(handle, NS_SQLERRORCODE,
			"Query returned more than one row.");
		    Ns_DbFlush(handle);
		    /* FALLTHROUGH */

		case NS_ERROR:
		    /* FALLTHROUGH */

		default:
		    return NULL;
		    break;
	    }
        }
        row = Ns_SetCopy(row);
    }

    return row;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_Db1Row --
 *
 *	Send a SQL statement which is expected to return exactly 1 row.
 *
 * Results:
 *	Pointer to Ns_Set with row data or NULL on error.  Set must
 *	eventually be freed.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Ns_Set *
Ns_Db1Row(Ns_DbHandle *handle, char *sql)
{
    Ns_Set         *row;
    int             nrows;

    row = Ns_Db0or1Row(handle, sql, &nrows);
    if (row != NULL) {
        if (nrows != 1) {
            Ns_DbSetException(handle, NS_SQLERRORCODE,
                "Query did not return a row.");
            row = NULL;
        }
    }

    return row;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbInterpretSqlFile --
 *
 *	Parse DML statements from an SQL file and send them to the
 *	database for execution.
 *
 * Results:
 *	NS_OK or NS_ERROR.
 *
 * Side effects:
 *	Stops on first error.  Transaction protection is provided for
 *	Illustra and "\n-- comments are handled correctly.
 *
 *----------------------------------------------------------------------
 */

int
Ns_DbInterpretSqlFile(Ns_DbHandle *handle, char *filename)
{
    FILE           *fp;
    Ns_DString      dsSql;
    int             i, status, inquote;
    char            c, lastc;
    char           *p;

    fp = fopen(filename, "rt");
    if (fp == NULL) {
        Ns_DbSetException(handle, NS_SQLERRORCODE,
            "Could not read file");
        return NS_ERROR;
    }

    Ns_DStringInit(&dsSql);
    status = NS_OK;
    inquote = 0;
    c = '\n';
    while ((i = getc(fp)) != EOF) {
        lastc = c;
        c = (char) i;
 loopstart:
        if (inquote) {
            if (c != '\'') {
                Ns_DStringNAppend(&dsSql, &c, 1);
            } else {
                if ((i = getc(fp)) == EOF) {
                    break;
                }
                lastc = c;
                c = (char) i;
                if (c == '\'') {
                    Ns_DStringNAppend(&dsSql, "''", 2);
                    continue;
                } else {
                    Ns_DStringNAppend(&dsSql, "'", 1);
                    inquote = 0;
                    goto loopstart;
                }
            }
        } else {
            /* Check to see if it is a comment */
            if ((c == '-') && (lastc == '\n')) {
                if ((i = getc(fp)) == EOF) {
                    break;
                }
                lastc = c;
                c = (char) i;
                if (c != '-') {
                    Ns_DStringNAppend(&dsSql, "-", 1);
                    goto loopstart;
                }
                while ((i = getc(fp)) != EOF) {
                    lastc = c;
                    c = (char) i;
                    if (c == '\n') {
                        break;
                    }
                }
            } else if (c == ';') {
                if (Ns_DbExec(handle, dsSql.string) == NS_ERROR) {
                    status = NS_ERROR;
                    break;
                }
                Ns_DStringTrunc(&dsSql, 0);
            } else {
                Ns_DStringNAppend(&dsSql, &c, 1);
                if (c == '\'') {
                    inquote = 1;
                }
            }
        }
    }
    fclose(fp);

    /*
     * If dstring contains anything but whitespace, return error
     */
    if (status != NS_ERROR) {
        for (p = dsSql.string; *p != '\0'; p++) {
            if (isspace(UCHAR(*p)) == 0) {
                Ns_DbSetException(handle, NS_SQLERRORCODE,
                    "File ends with unterminated SQL");
                status = NS_ERROR;
            }
        }
    }
    Ns_DStringFree(&dsSql);

    return status;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DbSetException --
 *
 *	Set the stored SQL exception code and message in the handle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Code and message are updated.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DbSetException(Ns_DbHandle *handle, char *code, char *msg)
{
    strcpy(handle->cExceptionCode, code);
    Ns_DStringFree(&(handle->dsExceptionMsg));
    Ns_DStringAppend(&(handle->dsExceptionMsg), msg);
}


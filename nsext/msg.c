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
 * nsextmsg.c --
 *
 *	This file encapsulates all external-database message-related data and
 * 	access/conversion.  It is used by both the server's external driver and
 * 	the database proxy daemons. It is included in source form for reference
 * 	only, since these messages cannot be changed without
 * 	augmenting/rebuilding the server's external driver.
 */

#define NUM_EXTDB_COMMANDS (sizeof (dbcommandinfo) / sizeof (struct Ns_ExtDbCommandInfo))
#define ExtDbCodeOk(code) ((code >= (Ns_ExtDbCommandCode)0) && \
                           (code <= (Ns_ExtDbCommandCode)NUM_EXTDB_COMMANDS))

static char     rcsid[] = "$Id: msg.c,v 1.1 2002/06/05 22:57:11 jgdavidson Exp $";

#include "ns.h"
#include "nsextmsg.h"

static struct Ns_ExtDbCommandInfo {
    Ns_ExtDbCommandCode code;
    short           requiresArg;
    char           *msgname;
} dbcommandinfo[] = {
    {
        Exec, 1, "exec"
    },
    {
        BindRow, 0, "bindrow"
    },
    {
        GetRow, 1, "getrow"
    },
    {
        Flush, 0, "flush"
    },
    {
        Cancel, 0, "cancel"
    },
    {
        GetTableInfo, 1, "gettableinfo"
    },
    {
        TableList, 1, "tablelist"
    },
    {
        BestRowId, 1, "bestrowid"
    },
    {
        ResultId, 0, "resultid"
    },
    {
        ResultRows, 0, "resultrows"
    },
    {
        SetMaxRows, 0, "setmaxrows"
    },
    {
        Close, 0, "close"
    },
    {
        Open, 1, "open"
    },
    {
        Ping, 0, "ping"
    },
    {
        Identify, 0, "identify"
    },
    {
        TraceOn, 1, "traceon"
    },
    {
        TraceOff, 0, "traceoff"
    },
    {
        GetTypes, 0, "gettypes"
    },

    /*
     * The commands below support proxy daemons working in a file space that
     * is separate from the server.  I.e., an external proxy daemon that does
     * not have a mounted file system in common with the server.  These
     * commands are necessary to support SQL statements that assume common
     * file space between client (web server) and server (database server).
     */
    
    {
        OpenF, 1, "openfile"
    },
    {
        CloseF, 1, "closefile"
    },
    {
        ReadF, 1, "readfile"
    },
    {
        WriteF, 1, "writefile"
    },
    {
        DeleteF, 1, "deletefile"
    },
    {
        CreateTmpF, 0, "createtmpfile"
    },

    /*
     * This was added after the above xxxF functions.
     */
    
    {
        ResetHandle, 0, "resethandle"
    },
    {
        SpStart, 1, "spstart"
    },
    {
        SpSetParam, 1, "spsetparam"
    },
    {
        SpExec, 0, "spexec"
    },
    {
        SpReturnCode, 0, "spreturncode"
    },
    {
        SpGetParams, 0, "spgetparams"
    }
};


/*
 *----------------------------------------------------------------------
 *
 * Ns_ExtDbMsgNameToCode --
 *
 *	Convert a message name to a numerical code. 
 *
 * Results:
 *	A numerical code or NS_ERROR. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ExtDbMsgNameToCode(char *msgname)
{
    int             i;
    int             code = NS_ERROR;
    struct Ns_ExtDbCommandInfo *cmdinfo;

    /*
     * tiny list: linear seach
     */
    
    for (i = 0, cmdinfo = dbcommandinfo; i < NUM_EXTDB_COMMANDS;
        i++, cmdinfo++) {
	
        if (strcasecmp(msgname, cmdinfo->msgname) == 0) {
            code = i;
            break;
        }
    }
    return code;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ExtDbMsgCodeToName --
 *
 *	Convert a numerical code to a string name. 
 *
 * Results:
 *	A string name or NULL. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

char *
Ns_ExtDbMsgCodeToName(Ns_ExtDbCommandCode code)
{
    char *retmsg = (char *) NULL;

    if (ExtDbCodeOk(code)) {
        retmsg = dbcommandinfo[code].msgname;
    }
    return retmsg;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ExtDbMsgRequiresArg --
 *
 *	Does a message require an argument? 
 *
 * Results:
 *	true: yes, false: no 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

short
Ns_ExtDbMsgRequiresArg(Ns_ExtDbCommandCode code)
{
    return dbcommandinfo[code].requiresArg;
}

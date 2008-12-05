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
 * limits.c --
 *
 *  Routines to manage resource limits.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/limits.c,v 1.11 2008/12/05 08:51:43 gneumann Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * Static functions defined in this file.
 */

static int LimitsResult(Tcl_Interp *interp, Limits *limitsPtr);
static int AppendLimit(Tcl_Interp *interp, char *limit, unsigned int val);
static int GetLimits(Tcl_Interp *interp, Tcl_Obj *objPtr,
        Limits **limitsPtrPtr, int create);
static Limits *FindLimits(char *limits, int create);

/*
 * Static variables defined in this file.
 */

static int            limid;
static Limits        *defLimitsPtr;
static Tcl_HashTable  limtable;


/*
 *----------------------------------------------------------------------
 *
 * NsInitLimits --
 *
 *	Initialize request limits.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Will create the default limits.
 *
 *----------------------------------------------------------------------
 */

void
NsInitLimits(void)
{
    limid = Ns_UrlSpecificAlloc();
    Tcl_InitHashTable(&limtable, TCL_STRING_KEYS);
    defLimitsPtr = FindLimits("default", 1);
}


/*
 *----------------------------------------------------------------------
 *
 *NsTclLimitsObjCmd --
 *
 *	Implements ns_limits command to create and query request limit
 *	structures.
 *
 * Results:
 *	Standard Tcl result. 
 *
 * Side effects:
 *	May create a new limit structure.
 *
 *----------------------------------------------------------------------
 */

int
NsTclLimitsObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv)
{
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch search;
    Limits *limitsPtr, saveLimits;
    char *limits, *pattern;
    int i, val;
    static CONST char *opts[] = {
        "get", "set", "list", "register", NULL
    };
    enum {
        LGetIdx, LSetIdx, LListIdx, LRegisterIdx
    } opt;
    static CONST char *cfgs[] = {
        "-maxrun", "-maxwait", "-maxupload", "-timeout", NULL
    };
    enum {
        LCRunIdx, LCWaitIdx, LCUploadIdx, LCTimeoutIdx
    } cfg;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?args?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
                (int *) &opt) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (opt) {
    case LListIdx:
        if (objc != 2 && objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "?pattern?");
            return TCL_ERROR;
        }
        if (objc == 2) {
            pattern = NULL;
        } else {
            pattern = Tcl_GetString(objv[2]);
        }
        hPtr = Tcl_FirstHashEntry(&limtable, &search);
        while (hPtr != NULL) {
            limits = Tcl_GetHashKey(&limtable, hPtr);
            if (pattern == NULL || Tcl_StringMatch(limits, pattern)) {
                Tcl_AppendElement(interp, limits);
            }
            hPtr = Tcl_NextHashEntry(&search);
        }
        break;

    case LGetIdx:
        if (objc != 3) {
            Tcl_WrongNumArgs(interp, 2, objv, "limit");
            return TCL_ERROR;
        }
        if (GetLimits(interp, objv[2], &limitsPtr, 0) != TCL_OK ||
                LimitsResult(interp, limitsPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case LSetIdx:
        if (objc < 3 || (((objc - 3) % 2) != 0)) {
            Tcl_WrongNumArgs(interp, 2, objv, "limit ?opt val opt val...?");
            return TCL_ERROR;
        }
        (void) GetLimits(interp, objv[2], &limitsPtr, 1);
        saveLimits = *limitsPtr;
        for (i = 3; i < objc; i += 2) {
            if (Tcl_GetIndexFromObj(interp, objv[i], cfgs, "cfg", 0,
                        (int *) &cfg) != TCL_OK || 
                    Tcl_GetIntFromObj(interp, objv[i+1], &val) != TCL_OK) {
                *limitsPtr = saveLimits;
                return TCL_ERROR;
            }
            switch (cfg) {
                case LCRunIdx:
                    limitsPtr->maxrun = val;
                    break;

                case LCWaitIdx:
                    limitsPtr->maxwait = val;
                    break;

                case LCUploadIdx:
                    limitsPtr->maxupload = val;
                    break;

                case LCTimeoutIdx:
                    limitsPtr->timeout = val;
                    break;

            }
        }
        if (LimitsResult(interp, limitsPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        break;

    case LRegisterIdx:
        if (objc != 6) {
            Tcl_WrongNumArgs(interp, 2, objv, "limit server method url");
            return TCL_ERROR;
        }
        if (GetLimits(interp, objv[2], &limitsPtr, 0) != TCL_OK) {
            return TCL_ERROR;
        }
        Ns_UrlSpecificSet(Tcl_GetString(objv[3]),
                Tcl_GetString(objv[4]),
                Tcl_GetString(objv[5]), limid, limitsPtr, 0, NULL);
        break;
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsGetRequestLimits --
 *
 *	Return the limits structure for a given request.
 *
 * Results:
 *	Pointer to limits.
 *
 * Side effects:
 *	May return the default limits if no more specific limits
 *	have been created.
 *
 *----------------------------------------------------------------------
 */

Limits *
NsGetRequestLimits(char *server, char *method, char *url)
{
    Limits *limitsPtr;

    limitsPtr = Ns_UrlSpecificGet(server, method, url, limid);
    return (limitsPtr ? limitsPtr : defLimitsPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * FindLimits --
 *
 *	Return the limits by name.
 *
 * Results:
 *	Pointer to Limits or NULL if no such limits and create is zero.
 *
 * Side effects:
 *	If create is not zero, will create new default limits.
 *
 *----------------------------------------------------------------------
 */

static Limits *
FindLimits(char *limits, int create)
{
    Limits *limitsPtr;
    Tcl_HashEntry *hPtr;
    int new;
    
    if (!create) {
        hPtr = Tcl_FindHashEntry(&limtable, limits);
    } else {
        hPtr = Tcl_CreateHashEntry(&limtable, limits, &new);
        if (new) {
            limitsPtr = ns_malloc(sizeof(Limits));
            limitsPtr->name = Tcl_GetHashKey(&limtable, hPtr);
	        Ns_MutexInit(&limitsPtr->lock);
                Ns_MutexSetName(&limitsPtr->lock, "ns:limits");
	        limitsPtr->nrunning = limitsPtr->nwaiting = 0;
	        limitsPtr->ntimeout = limitsPtr->ndropped = limitsPtr->noverflow = 0;
	        limitsPtr->maxrun = limitsPtr->maxwait = 100;
	        limitsPtr->maxupload = 10 * 1024 * 1000; /* NB: 10meg limit. */
	        limitsPtr->timeout = 60;
            Tcl_SetHashValue(hPtr, limitsPtr);
        }
    }
    return (hPtr ? Tcl_GetHashValue(hPtr) : NULL);
}


/*
 *----------------------------------------------------------------------
 *
 * GetLimits --
 *
 *	Utility routing to find Limits by Tcl_Obj for Tcl.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	Will update limitsPtrPtr with pointer to Limits or leave
 *	an error message in given interp if no limits found and
 *	create is zero. 
 *
 *----------------------------------------------------------------------
 */

static int
GetLimits(Tcl_Interp *interp, Tcl_Obj *objPtr, Limits **limitsPtrPtr,
      int create)
{
    char *limits = Tcl_GetString(objPtr);
    Limits *limitsPtr;

    limitsPtr = FindLimits(limits, create);
    if (limitsPtr == NULL) {
        Tcl_AppendResult(interp, "no such limits: ", limits, NULL);
        return TCL_ERROR;
    }
    *limitsPtrPtr = limitsPtr;
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * LimitsResult --
 *
 *	Return a list of info about a given limits.
 *
 * Results:
 *	TCL_ERROR if list could not be appended, TCL_OK otherwise.
 *
 * Side effects:
 *	Will leave list in given interp result.
 *
 *----------------------------------------------------------------------
 */

static int
LimitsResult(Tcl_Interp *interp, Limits *limitsPtr)
{
    if (!AppendLimit(interp, "nrunning", limitsPtr->nrunning) ||
            !AppendLimit(interp, "nwaiting", limitsPtr->nwaiting) ||
            !AppendLimit(interp, "ntimeout", limitsPtr->ntimeout) ||
            !AppendLimit(interp, "ndropped", limitsPtr->ndropped) ||
            !AppendLimit(interp, "noverflow", limitsPtr->noverflow) ||
            !AppendLimit(interp, "maxwait", limitsPtr->maxwait) ||
            !AppendLimit(interp, "maxupload", limitsPtr->maxupload) ||
            !AppendLimit(interp, "timeout", limitsPtr->timeout) ||
            !AppendLimit(interp, "maxrun", limitsPtr->maxrun)) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

static int
AppendLimit(Tcl_Interp *interp, char *limit, unsigned int val)
{
    Tcl_Obj *result = Tcl_GetObjResult(interp);

    if (Tcl_ListObjAppendElement(interp, result, Tcl_NewStringObj(limit, -1))
            != TCL_OK ||
            Tcl_ListObjAppendElement(interp, result, Tcl_NewIntObj((int) val))
            != TCL_OK) {
        return 0;
    }
    return 1;
}

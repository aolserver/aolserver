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
 * tclconf.c --
 *
 *	Tcl commands for reading config file info. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclconf.c,v 1.7 2001/12/05 22:46:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * NsTclConfigCmd --
 *
 *	Implements ns_config. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConfigCmd(ClientData dummy, Tcl_Interp *interp, int argc, char **argv)
{
    char *value;
    int   i;
    int   fHasDefault = NS_FALSE;
    int	  defaultIndex = 0;

    if (argc < 3 || argc > 5) {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
            argv[0], " ?-exact | -bool | -int? section key ?default?\"", NULL);
        return TCL_ERROR;
    }

    if (argv[1][0] == '-') {
	if (argc == 5) {
	    fHasDefault = NS_TRUE;
	    defaultIndex = 4;
	}
    } else if (argc == 4) {
	fHasDefault = NS_TRUE;
	defaultIndex = 3;
    }
   
    if (STREQ(argv[1], "-exact")) {
        value = Ns_ConfigGetValueExact(argv[2], argv[3]);

	if (value == NULL && fHasDefault) {
	    value = argv[defaultIndex];
	}
    } else if (STREQ(argv[1], "-int")) {
        if (Ns_ConfigGetInt(argv[2], argv[3], &i)) {
            Tcl_SetObjResult(interp, Tcl_NewIntObj(i));
            return TCL_OK;
        } else if (fHasDefault) {
	    if (Tcl_GetInt(interp, argv[defaultIndex], &i) != TCL_OK) {
		return TCL_ERROR;
	    }
            Tcl_SetObjResult(interp, Tcl_NewIntObj(i));
	    return TCL_OK;
	}
        value = NULL;
    } else if (STREQ(argv[1], "-bool")) {
        int             iValue;

        if (Ns_ConfigGetBool(argv[2], argv[3], &iValue) == NS_FALSE) {
	    if (fHasDefault) {
		if (   Tcl_GetBoolean(interp, argv[defaultIndex], &iValue)
		    != TCL_OK) {
		    return TCL_ERROR;
		}

		value = (iValue) ? "1" : "0";
		
	    } else {
		value = NULL;
	    }
        } else {
	    value = (iValue) ? "1" : "0";
        }
    } else if (argc == 3 || argc == 4) {
        value = Ns_ConfigGetValue(argv[1], argv[2]);

	if (value == NULL && fHasDefault) {
	    value = argv[defaultIndex];
	}
    } else {
        Tcl_AppendResult(interp, "wrong # args:  should be \"",
            argv[0], " ?-exact | -bool | -int? section key ?default?\"", NULL);
        return TCL_ERROR;
    }
    if (value != NULL) {
	Tcl_SetResult(interp, value, TCL_STATIC);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConfigSectionCmd --
 *
 *	Implements ns_configsection. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConfigSectionCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		      char **argv)
{
    Ns_Set *set;

    if (argc != 2) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " key\"", NULL);
        return TCL_ERROR;
    }
    set = Ns_ConfigGetSection(argv[1]);
    if (set == NULL) {
        Tcl_SetResult(interp, "", TCL_STATIC);
    } else {
        Ns_TclEnterSet(interp, set, NS_TCL_SET_STATIC);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclConfigSectionsCmd --
 *
 *	Implements ns_configsections. 
 *
 * Results:
 *	Tcl result. 
 *
 * Side effects:
 *	See docs. 
 *
 *----------------------------------------------------------------------
 */

int
NsTclConfigSectionsCmd(ClientData dummy, Tcl_Interp *interp, int argc,
		       char **argv)
{
    Ns_Set **sets;
    int      i;

    if (argc != 1) {
        Tcl_AppendResult(interp, "wrong # args: should be \"",
            argv[0], " key\"", NULL);
        return TCL_ERROR;
    }
    sets = Ns_ConfigGetSections();
    for (i = 0; sets[i] != NULL; i++) {
        Ns_TclEnterSet(interp, sets[i], NS_TCL_SET_STATIC);
    }
    ns_free(sets);
    return TCL_OK;
}

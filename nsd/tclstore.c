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
 * tclstore.c --
 *
 *	Thread and connection local storage Tcl commands.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/tclstore.c,v 1.1 2009/12/08 04:12:20 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/* 
 * Static functions defined in this file.
 */

static int StoreObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv, int tls);


/*
 *----------------------------------------------------------------------
 *
 * NsTclTlsObjCmd, NsTclClsObjCmd --
 *
 *	Implements ns_tls and ns_cls as obj commands. 
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
NsTclTlsObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv, Ns_Conn *conn)
{
    return StoreObjCmd(data, interp, objc, objv, 1);
}

int
NsTclClsObjCmd(ClientData data, Tcl_Interp *interp, int objc, Tcl_Obj **objv, Ns_Conn *conn)
{
    return StoreObjCmd(data, interp, objc, objv, 0);
}

static int
StoreObjCmd(ClientData arg, Tcl_Interp *interp, int objc, Tcl_Obj **objv, int tls)
{
    static CONST char *opts[] = {
	"alloc", "get", "set", NULL
    };
    enum {
	AllocIdx, GetIdx, SetIdx
    } _nsmayalias opt;
    Ns_Conn *conn;
    char *val;
    int id;

    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "option ?arg ...?");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObj(interp, objv[1], opts, "option", 0,
			    (int *) &opt) != TCL_OK) {
	return TCL_ERROR;
    }
    if (opt == AllocIdx) {
	if (objc != 2) {
	    Tcl_WrongNumArgs(interp, 2, objv, NULL);
	    return TCL_ERROR;
	}
	if (tls) {
	    Ns_TlsAlloc((Ns_Tls *) &id, ns_free);
	} else {
	    Ns_ClsAlloc((Ns_Cls *) &id, ns_free);
	}
	Tcl_SetObjResult(interp, Tcl_NewIntObj(id));
    } else {
	if (!tls && NsTclGetConn((NsInterp *) arg, &conn) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (objc < 3) {
	    Tcl_WrongNumArgs(interp, 2, objv, "index");
	    return TCL_ERROR;
	}
	if (Tcl_GetIntFromObj(interp, objv[2], &id) != TCL_OK) {
	    return TCL_ERROR;
	}
	if (id < 1 || id >= (tls ? NS_THREAD_MAXTLS : NS_CONN_MAXCLS)) {
	    Tcl_AppendResult(interp, "invalid id: ", Tcl_GetString(objv[2]), NULL);
	    return TCL_ERROR;
	}
	if (tls) {
	    val = Ns_TlsGet((Ns_Tls *) &id);
	} else {
	    val = Ns_ClsGet((Ns_Cls *) &id, conn);
	}
	if (opt == GetIdx) {
	    Tcl_SetResult(interp, val, TCL_VOLATILE);
	} else {
	    if (objc != 4) {
		Tcl_WrongNumArgs(interp, 2, objv, "index value");
		return TCL_ERROR;
	    }
	    if (val) {
		ns_free(val);
	    }
	    val = ns_strdup(Tcl_GetString(objv[3]));
	    if (tls) {
		Ns_TlsSet((Ns_Tls *) &id, (void **) val);
	    } else {
		Ns_ClsSet((Ns_Cls *) &id, conn, (void **) val);
	    }
	}
    }
    return TCL_OK;
}

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


/*
 * tcl.h --
 *
 *      Stub to include one of tcl76.h or tcl82.h.  The default
 *	is tcl76.h on Unix, tcl82.h on Win32.
 */

#ifndef _TCL

#ifdef WIN32
#ifndef USE_TCL8X
#define USE_TCL8X
#endif
#ifndef _CLIENTDATA
#define _CLIENTDATA
typedef void *ClientData;
#endif
#endif

#if defined(TCL82) || defined(_TCL82) || defined(TCL_81_PLEASE)
#define USE_TCL8X
#endif

#if defined(USE_TCL8X)
#ifndef TCL_THREADS
#define TCL_THREADS 1
#endif
#include "tcl83.h"
#else
#include "tcl76.h"
#endif

#ifndef NSTHREAD_EXPORTS
EXTERN int Tcl_GetChannelNames(Tcl_Interp *interp);
#endif

#endif	/* _TCL */

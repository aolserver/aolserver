/*
 * tclPort.h --
 *
 *	This header file handles porting issues that occur because
 *	of differences between systems.  It reads in platform specific
 *	portability files.
 *
 * Copyright (c) 1994-1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * RCS: @(#) $Id: tclPort.h,v 1.1 2001/11/05 20:06:33 jgdavidson Exp $
 */

#ifndef _TCLPORT
#define _TCLPORT

#include "tcl.h"

#if defined(__WIN32__)
#   include "../win/tclWinPort.h"
#else
#   if defined(MAC_TCL)
#	include "tclMacPort.h"
#    else
#	include "../unix/tclUnixPort.h"
#    endif
#endif

#endif /* _TCLPORT */

/*
 * dirent.h --
 *
 *	This file is a replacement for <dirent.h> in systems that
 *	support the old BSD-style <sys/dir.h> with a "struct direct".
 *
 * Copyright (c) 1991 The Regents of the University of California.
 * Copyright (c) 1994 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) dirent.h 1.4 96/02/15 14:43:50
 */

#ifndef _DIRENT
#define _DIRENT

static const char *RCSID_DIRENT = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/tcl7.6/compat/Attic/dirent.h,v 1.1.1.1 2000/05/02 13:48:25 kriston Exp $, compiled: " __DATE__;

#include <sys/dir.h>

#define dirent direct

#endif /* _DIRENT */

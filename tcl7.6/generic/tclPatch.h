/*
 * tclPatch.h --
 *
 * This file does nothing except define a "patch level" for Tcl.
 * The patch level has the form "X.YpZ" where X.Y is the base
 * release, and Z is a serial number that is used to sequence
 * patches for a given release.  Thus 7.4p1 is the first patch
 * to release 7.4, 7.4p2 is the patch that follows 7.4p1, and
 * so on.  The "pZ" is omitted in an original new release, and
 * it is replaced with "bZ" for beta releases or "aZ for alpha
 * releases.  The patch level ensures that patches are applied
 * in the correct order and only to appropriate sources.
 *
 * Copyright (c) 1993-1994 The Regents of the University of California.
 * Copyright (c) 1994-1996 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclPatch.h 1.23 96/10/02 14:36:15
 */

#define TCL_PATCH_LEVEL "7.6"

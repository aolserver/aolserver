/* 
 * tclFHandle.c --
 *
 *	This file contains functions for manipulating Tcl file handles.
 *
 * Copyright (c) 1995 Sun Microsystems, Inc.
 *
 * See the file "license.terms" for information on usage and redistribution
 * of this file, and for a DISCLAIMER OF ALL WARRANTIES.
 *
 * SCCS: @(#) tclFHandle.c 1.9 96/07/01 15:41:26
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/tcl7.6/generic/Attic/tclFHandle.c,v 1.3 2000/08/08 20:51:21 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "tcl.h"
#include "tclInt.h"
#include "tclPort.h"

/*
 * Modified for AOLserver by simply removing the fileTable hash
 * table which served no purpose aside from a potential crash
 * when ../unix/tclUnixFile.c:TclCloseFile() would close()
 * before calling Tcl_FreeFile, resulting in potential re-use
 * or double free of the FileHandle.
 */

typedef struct FileHandle {
    int type;			/* File handle type. */
    ClientData osHandle;	/* Platform specific OS file handle. */
    ClientData data;		/* Platform specific notifier data. */
    Tcl_FileFreeProc *proc;	/* Callback to invoke when file is freed. */
} FileHandle;


/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetFile --
 *
 *	This function retrieves the file handle associated with a
 *	platform specific file handle of the given type.  It creates
 *	a new file handle if needed.
 *
 * Results:
 *	Returns the file handle associated with the file descriptor.
 *
 * Side effects:
 *	Initializes the file handle table if necessary.
 *
 *----------------------------------------------------------------------
 */

Tcl_File
Tcl_GetFile(osHandle, type)
    ClientData osHandle;	/* Platform specific file handle. */
    int type;			/* Type of file handle. */
{
    FileHandle *handlePtr;
    Tcl_File file;

    handlePtr = (FileHandle *) ckalloc(sizeof(FileHandle));
    handlePtr->osHandle = osHandle;
    handlePtr->type = type;
    handlePtr->data = NULL;
    handlePtr->proc = NULL;
    return (Tcl_File) handlePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_FreeFile --
 *
 *	Deallocates an entry in the file handle table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_FreeFile(handle)
    Tcl_File handle;
{
    FileHandle *handlePtr = (FileHandle *) handle;
    
    if (handlePtr->proc) {
	(*handlePtr->proc)(handlePtr->data);
    }
    ckfree((char *) handlePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetFileInfo --
 *
 *	This function retrieves the platform specific file data and
 *	type from the file handle.
 *
 * Results:
 *	If typePtr is not NULL, sets *typePtr to the type of the file.
 *	Returns the platform specific file data.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ClientData
Tcl_GetFileInfo(handle, typePtr)
    Tcl_File handle;
    int *typePtr;
{
    FileHandle *handlePtr = (FileHandle *) handle;

    if (typePtr) {
	*typePtr = handlePtr->type;
    }
    return handlePtr->osHandle;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_SetNotifierData --
 *
 *	This function is used by the notifier to associate platform
 *	specific notifier information and a deletion procedure with
 *	a file handle.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Updates the data and delProc slots in the file handle.
 *
 *----------------------------------------------------------------------
 */

void
Tcl_SetNotifierData(handle, proc, data)
    Tcl_File handle;
    Tcl_FileFreeProc *proc;
    ClientData data;
{
    FileHandle *handlePtr = (FileHandle *) handle;
    handlePtr->proc = proc;
    handlePtr->data = data;
}

/*
 *----------------------------------------------------------------------
 *
 * Tcl_GetNotifierData --
 *
 *	This function is used by the notifier to retrieve the platform
 *	specific notifier information associated with a file handle.
 *
 * Results:
 *	Returns the data stored in a file handle by a previous call to
 *	Tcl_SetNotifierData, and places a pointer to the free proc
 *	in the location referred to by procPtr.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ClientData
Tcl_GetNotifierData(handle, procPtr)
    Tcl_File handle;
    Tcl_FileFreeProc **procPtr;
{
    FileHandle *handlePtr = (FileHandle *) handle;
    if (procPtr != NULL) {
	*procPtr = handlePtr->proc;
    }
    return handlePtr->data;
}

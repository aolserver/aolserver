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
 * tclstats.c --
 *
 * 	Tcl command usage stats routines.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/Attic/tclstats.c,v 1.5 2000/10/13 01:16:44 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is used to buffer command counts in the
 * thread to avoid lock contention around the global stats table.
 */

typedef struct Buf {
    int nbuf;
    Tcl_HashTable table;
} Buf;

static Ns_Callback FreeBuf;
static void FlushBuf(Buf *bufPtr);
static Ns_TclInterpInitProc AddCmds;
static Ns_TclTraceProc CreateTrace;
static Ns_TclTraceProc FlushBufTrace;
static Tcl_CmdTraceProc StatsTrace;
static Tcl_HashTable statsTable;
static void IncrCount(Tcl_HashTable *tablePtr, char *cmd, unsigned long count);
static int SortName(const void *a1, const void *a2);
static int SortCount(const void *a1, const void *a2);
static Ns_Tls tls;
static Ns_Mutex lock;


/*
 *----------------------------------------------------------------------
 *
 * NsTclStatsInit --
 *
 *	Initialize the Tcl stats, registering a callback called when
 *	an interp is created so that the stats trace can be created.
 *	If buffering is enabled, also register a cleanup procedure
 *	to flush buffered counts when the interp is deallocated (e.g.,
 *	at the end of a connection).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Callbacks, TLS may be created.
 *
 *----------------------------------------------------------------------
 */

void
NsTclStatsInit(void)
{
    Tcl_InitHashTable(&statsTable, TCL_STRING_KEYS);
    Ns_MutexSetName2(&lock, "ns", "tclstats");
    if (nsconf.tcl.statlevel > 0) {
	Ns_Log(Warning, "tclstats: tracing to level %d", nsconf.tcl.statlevel);
	Ns_TclRegisterAtCreate(CreateTrace, NULL);
	if (nsconf.tcl.statmaxbuf > 0) {
	    Ns_TlsAlloc(&tls, FreeBuf);
	    Ns_TclRegisterAtCleanup(FlushBufTrace, NULL);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * NsTclStatsCmds --
 *
 *	Implemented the ns_stats command.
 *
 * Results:
 *	Standard Tcl result.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
NsTclStatsCmd(ClientData ignored, Tcl_Interp *interp, int argc, char **argv)
{
    Tcl_HashEntry *hPtr, **list;
    Tcl_HashSearch search;
    Tcl_DString ds;
    char buf[100];
    int i, nlist;
    int (*compare)(const void *, const void *);
    char *pattern, *path;

    if (argc > 3) {
	Tcl_AppendResult(interp, "wrong # args: should be \"",
	    argv[0], " ?option? ?pattern?\"", NULL);
	return TCL_ERROR;
    }

    compare = SortCount;
    if (argc == 1) {
	pattern = NULL;
    } else {
	pattern = argv[2];
	if (!strcmp(argv[1], "-count")) {
	    compare = SortCount;
	} else if (!strcmp(argv[1], "-name")) {
	    compare = SortName;
	} else if (argc == 3) {
	    Tcl_AppendResult(interp, "invalid option \"", argv[1],
		"\": should be -count or -name", NULL);
	    return TCL_ERROR;
	} else {
	    pattern = argv[1];
	}
    }

    Tcl_DStringInit(&ds);
    if (statsTable.numEntries > 0) {
	list = ns_malloc(sizeof(Tcl_HashEntry *) * statsTable.numEntries);
	nlist = 0;
	hPtr = Tcl_FirstHashEntry(&statsTable, &search);
	while (hPtr != NULL) {
	    path = Tcl_GetHashKey(&statsTable, hPtr);
	    if (pattern == NULL || Tcl_StringMatch(path, pattern)) {
	    	list[nlist++] = hPtr;
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
	if (nlist > 0) {
	    qsort(list, nlist, sizeof(Tcl_HashEntry *), compare);
	    for (i = 0; i < nlist; ++i) {
	        hPtr = list[i];
	    	path = Tcl_GetHashKey(&statsTable, hPtr);
    	    	sprintf(buf, "%ld", (unsigned long) Tcl_GetHashValue(hPtr));
	    	Tcl_DStringAppendElement(&ds, path);
	    	Tcl_DStringAppendElement(&ds, buf);
	    	Tcl_AppendElement(interp, ds.string);
	    	Tcl_DStringTrunc(&ds, 0);
	    }
	}
	ns_free(list);
    }
    Tcl_DStringFree(&ds);

    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * CreateTrace --
 *
 *	Create the stats trace when a thread's interp is created.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	StatsTrace will be called before each command.
 *
 *----------------------------------------------------------------------
 */

static int
CreateTrace(Tcl_Interp *interp, void *ignored)
{
    Tcl_CreateTrace(interp, nsconf.tcl.statlevel, StatsTrace, NULL);
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * StatsTrace --
 *
 *	Call a user-supplied Tcl initialization procedure in the parent
 *  	interp.
 *
 *  	NOTE:  This routine only allows execution in the initial thread
 *  	during startup.
 *
 * Results:
 *	Tcl result from user's procedure.
 *
 * Side effects:
 *	Parent interp used as a template for all other per-thread interps
 *	is created.
 *
 *----------------------------------------------------------------------
 */

static void
StatsTrace(ClientData clientData, Tcl_Interp *interp, int level,
	char *command, Tcl_CmdProc *cmdProc, ClientData cmdClientData,
	int argc, char **argv)
{
    Buf *bufPtr;

    /*
     * If buffering is not enabled, update the global table directly
     * (this could be a source of lock contention).  Otherwise, update
     * this thread's table, flushing if the buffer limit is exceeded.
     */

    if (nsconf.tcl.statmaxbuf <= 0) {
	Ns_MutexLock(&lock);
	IncrCount(&statsTable, argv[0], 1);
	Ns_MutexUnlock(&lock);
    } else {
    	bufPtr = Ns_TlsGet(&tls);
    	if (bufPtr == NULL) {
	    bufPtr = ns_malloc(sizeof(Buf));
	    bufPtr->nbuf = 0;
	    Tcl_InitHashTable(&bufPtr->table, TCL_STRING_KEYS);
	    Ns_TlsSet(&tls, bufPtr);
    	}
	IncrCount(&bufPtr->table, argv[0], 1);
    	if (++bufPtr->nbuf > nsconf.tcl.statmaxbuf) {
	    FlushBuf(bufPtr);
    	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * IncrCount --
 *
 *	Increment the usage count in a table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
IncrCount(Tcl_HashTable *tablePtr, char *cmd, unsigned long count)
{
    Tcl_HashEntry *hPtr;
    int new;

    hPtr = Tcl_CreateHashEntry(tablePtr, cmd, &new);
    count += (unsigned long) Tcl_GetHashValue(hPtr);
    Tcl_SetHashValue(hPtr, (void *) count);
}


/*
 *----------------------------------------------------------------------
 *
 * FreeBuf --
 *
 *	TLS callback to cleanup a thread's buffer.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Remaining counts (if any) are flushed.
 *
 *----------------------------------------------------------------------
 */

static void
FreeBuf(void *arg)
{
    Buf *bufPtr = arg;

    FlushBuf(bufPtr);
    Tcl_DeleteHashTable(&bufPtr->table);
    ns_free(bufPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * FlushBufTrace --
 *
 *	Tcl interp trace to flush buffer called when interp is
 *	deallocated, e.g., at the end of a connction.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stats may be flushed.
 *
 *----------------------------------------------------------------------
 */

static int
FlushBufTrace(Tcl_Interp *interp, void *arg)
{
    Buf *bufPtr = arg;

    bufPtr = Ns_TlsGet(&tls);
    if (bufPtr != NULL) {
	FlushBuf(bufPtr);
    }
    return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * FlushBuf --
 *
 *	Flush a thread's buffered stats to the global table.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Global stats table updated.
 *
 *----------------------------------------------------------------------
 */

static void
FlushBuf(Buf *bufPtr)
{
    Tcl_HashSearch search;
    Tcl_HashEntry *hPtr;
    unsigned long count;

    hPtr = Tcl_FirstHashEntry(&bufPtr->table, &search);
    if (hPtr != NULL) {
	Ns_MutexLock(&lock);
	while (hPtr != NULL) {
	    count = (unsigned long) Tcl_GetHashValue(hPtr);
	    if (count > 0) {
	    	IncrCount(&statsTable, Tcl_GetHashKey(&bufPtr->table, hPtr), count);
	    	Tcl_SetHashValue(hPtr, (ClientData) 0);
	    }
	    hPtr = Tcl_NextHashEntry(&search);
	}
	Ns_MutexUnlock(&lock);
	bufPtr->nbuf = 0;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SortName, SortCount --
 *
 *	qsort() routine to sort stats by name/count.
 *
 * Results:
 *	Standard -1, 0, 1 result for qsort().
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
SortName(const void *a1, const void *a2)
{
    Tcl_HashEntry **h1, **h2;
    char *f1, *f2;

    h1 = (Tcl_HashEntry **) a1;
    h2 = (Tcl_HashEntry **) a2;
    f1 = Tcl_GetHashKey(&statsTable, *h1);
    f2 = Tcl_GetHashKey(&statsTable, *h2);

    return strcmp(f1, f2);
}

static int
SortCount(const void *a1, const void *a2)
{
    Tcl_HashEntry **h1, **h2;
    unsigned long c1, c2;

    h1 = (Tcl_HashEntry **) a1;
    h2 = (Tcl_HashEntry **) a2;
    c1 = (unsigned long) Tcl_GetHashValue(*h1);
    c2 = (unsigned long) Tcl_GetHashValue(*h2);
    if (c1 < c2) {
	return 1;
    } else if (c1 > c2) {
	return -1;
    }	
    return SortName(a1, a2);
}

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
 * dstring.c --
 *
 *	Ns_DString routines.  Ns_DString's are not compatible 
 *	with Tcl_DString's.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/dstring.c,v 1.9 2001/01/10 18:35:25 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"

/*
 * The following structure is used to maintain the per-thread
 * DString cache.
 */
 
typedef struct {
    Ns_DString *firstPtr;
    int ncached;
} Stack;

static Ns_Tls tls;  	    	    /* Cache TLS. */
static Ns_Callback FlushDStrings;   /* Cache TLS cleannup. */
static void GrowDString(Ns_DString *dsPtr, int length);


/*
 *----------------------------------------------------------------------
 * Ns_DStringInit --
 *
 *      Initialize a Ns_DString object.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringInit(Ns_DString *dsPtr)
{
    dsPtr->string = dsPtr->staticSpace;
    dsPtr->length = 0;
    dsPtr->spaceAvl = NS_DSTRING_STATIC_SIZE;
    dsPtr->staticSpace[0] = 0;
    dsPtr->addr = NULL;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringVarAppend --
 *
 *	Append a variable number of string arguments to a dstring.
 *
 * Results:
 *	Pointer to current dstring value.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringVarAppend(Ns_DString *dsPtr, ...)
{
    register char   *s;
    va_list         ap;

    va_start(ap, dsPtr);
    while ((s = va_arg(ap, char *)) != NULL) {
	Ns_DStringAppend(dsPtr, s);
    }
    va_end(ap);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringAppendElement --
 *
 *	Append a list element to the current value of a dynamic string.
 *
 * Results:
 *	The return value is a pointer to the dynamic string's new value.
 *
 * Side effects:
 *	String is reformatted as a list element and added to the current
 *	value of the string.  Memory gets reallocated if needed to
 *	accomodate the string's new size.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringAppendElement(Ns_DString *dsPtr, char *string)
{
    int newSize, flags;
    char *dst;
    extern int TclNeedSpace(char *, char *);

    newSize = Tcl_ScanElement(string, &flags) + dsPtr->length + 1;

    /*
     * Grow the string if necessary.
     */

    if (newSize >= dsPtr->spaceAvl) {
	GrowDString(dsPtr, newSize * 2);
    }

    /*
     * Convert the new string to a list element and copy it into the
     * buffer at the end, with a space, if needed.
     */

    dst = dsPtr->string + dsPtr->length;
    if (TclNeedSpace(dsPtr->string, dst)) {
	*dst = ' ';
	dst++;
	dsPtr->length++;
    }
    dsPtr->length += Tcl_ConvertElement(string, dst, flags);
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringExport --
 *
 *	Return a copy of the string value on the heap.
 *	Ns_DString is left in an initialized state.
 *
 * Results:
 *	Pointer to ns_malloc'ed string which must be eventually freed.
 *
 * Side effects:
 *	None.
 *
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringExport(Ns_DString *dsPtr)
{
    char *s;

    if (dsPtr->string != dsPtr->staticSpace) {
	s = dsPtr->string;
	dsPtr->string = dsPtr->staticSpace;
    } else {
	s = ns_malloc(dsPtr->length+1);
	memcpy(s, dsPtr->string, dsPtr->length+1);  
    }
    Ns_DStringFree(dsPtr);
    return s;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringPrintf --
 *
 *      Append a sequence of values using a format string
 *
 * Results:
 *	Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringPrintf(Ns_DString *dsPtr, char *fmt,...)
{
    char            buf[NS_DSTRING_PRINTF_MAX+1];
    va_list         ap;

    va_start(ap, fmt);
#if NO_VSNPRINTF
    /* NB: Technically unsafe. */
    vsprintf(buf, fmt, ap);
#else
    vsnprintf(buf, sizeof(buf)-1, fmt, ap);
#endif
    va_end(ap);
    return Ns_DStringAppend(dsPtr, buf);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringFree --
 *
 *	Reset to its initialized state and deallocate any allocated
 *	memory.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringFree(Ns_DString *dsPtr)
{
    if (dsPtr->string != dsPtr->staticSpace) {
	ns_free(dsPtr->string);
    }
    dsPtr->string = dsPtr->staticSpace;
    dsPtr->length = 0;
    dsPtr->spaceAvl = NS_DSTRING_STATIC_SIZE;
    dsPtr->staticSpace[0] = 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_DStringSetLength --
 *
 *	Change the length of a dynamic string.  This can cause the
 *	string to either grow or shrink, depending on the value of
 *	length.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The length of dsPtr is changed to length and a null byte is
 *	stored at that position in the string.  If length is larger
 *	than the space allocated for dsPtr, then a panic occurs.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringSetLength(Ns_DString *dsPtr, int length)
{
    if (length < 0) {
	length = 0;
    }
    if (length >= dsPtr->spaceAvl) {
	GrowDString(dsPtr, length+1);
    }
    dsPtr->length = length;
    dsPtr->string[length] = 0;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringTrunc --
 *
 *      Truncate the value to a null string.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringTrunc(Ns_DString *dsPtr, int length)
{
    Ns_DStringSetLength(dsPtr, length);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringNAppend --
 *
 *      Append a byte copy of the first length bytes of string.
 *
 * Results:
 *	Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringNAppend(Ns_DString *dsPtr, char *string, int length)
{
    int newSize;

    if (length < 0) {
	length = strlen(string);
    }
    newSize = length + dsPtr->length;

    /*
     * Grow the string if necessary.
     */

    if (newSize >= dsPtr->spaceAvl) {
	GrowDString(dsPtr, newSize * 2);
    }

    /*
     * Copy the new string into the buffer at the end of the old one.
     */

    memcpy(dsPtr->string + dsPtr->length, string, length);
    dsPtr->length += length;
    dsPtr->string[dsPtr->length] = '\0';
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringAppendArg --
 *
 *      Append a string including its terminating null byte.
 *
 * Results:
 *	Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

char *
Ns_DStringAppendArg(Ns_DString *dsPtr, char *string)
{
    return Ns_DStringNAppend(dsPtr, string, strlen(string) + 1);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringAppend --
 *
 *      Append a string.
 *
 * Results:
 *	Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

#undef Ns_DStringAppend

char *
Ns_DStringAppend(Ns_DString *dsPtr, char *string)
{
    return Ns_DStringNAppend(dsPtr, string, -1);
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringLength --
 *
 *      Return the length of a string.
 *
 * Results:
 *      The length of a string.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

#undef Ns_DStringLength

int
Ns_DStringLength(Ns_DString *dsPtr)
{
    return dsPtr->length;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringValue --
 *
 *	Return a pointer to the current string value.
 *
 * Results:
 *	Pointer to the current string value.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */

#undef Ns_DStringValue

char *
Ns_DStringValue(Ns_DString *dsPtr)
{
    return dsPtr->string;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringPop --
 *
 *	Pop a DString from the per-thread cache, allocating one
 *  	if necessary.
 *
 * Results:
 *	Pointer to initialized DString.
 *
 * Side effects:
 *      Will initialize TLS and config parameters on first use.
 *
 *----------------------------------------------------------------------
 */

Ns_DString *
Ns_DStringPop(void)
{
    Stack *sPtr;
    Ns_DString *dsPtr;

    if (tls == NULL) {
	Ns_MasterLock();
	if (tls == NULL) {
	    Ns_TlsAlloc(&tls, FlushDStrings);
	}
	Ns_MasterUnlock();
    }
    sPtr = Ns_TlsGet(&tls);
    if (sPtr == NULL) {
	sPtr = ns_calloc(1, sizeof(Stack));
	Ns_TlsSet(&tls, sPtr);
    }
    if (sPtr->firstPtr == NULL) {
	dsPtr = ns_malloc(sizeof(Ns_DString));
	Ns_DStringInit(dsPtr);
    } else {
	dsPtr = sPtr->firstPtr;
	sPtr->firstPtr = *((Ns_DString **) dsPtr->staticSpace);
    	dsPtr->staticSpace[0] = 0;
	--sPtr->ncached;
    }
    return dsPtr;
}


/*
 *----------------------------------------------------------------------
 * Ns_DStringPush --
 *
 *	Push a DString back on the per-thread cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      DString may be free'ed if the max entries and/or size has been
 *  	exceeded.
 *
 *----------------------------------------------------------------------
 */

void
Ns_DStringPush(Ns_DString *dsPtr)
{
    Stack *sPtr = Ns_TlsGet(&tls);

    if (sPtr->ncached >= nsconf.dstring.maxentries) {
	Ns_DStringFree(dsPtr);
	ns_free(dsPtr);
    } else {
	if (dsPtr->spaceAvl > nsconf.dstring.maxsize) {
	    Ns_DStringFree(dsPtr);
	} else {
    	    Ns_DStringTrunc(dsPtr, 0);
	}
	*((Ns_DString **) dsPtr->staticSpace) = sPtr->firstPtr;
	sPtr->firstPtr = dsPtr;
	++sPtr->ncached;
    }
}


/*
 *----------------------------------------------------------------------
 * FlushDStrings --
 *
 *	TLS callback to flush the DString cache.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	None.
 *
 *----------------------------------------------------------------------
 */

static void
FlushDStrings(void *arg)
{
    Stack *sPtr = arg;
    Ns_DString *dsPtr;

    while ((dsPtr = sPtr->firstPtr) != NULL) {
	sPtr->firstPtr = *((Ns_DString **) dsPtr->staticSpace);
	Ns_DStringFree(dsPtr);
	ns_free(dsPtr);
    }
    ns_free(sPtr);
}


/*
 *----------------------------------------------------------------------
 * GrowDString --
 *
 *	Increase the space available in a dstring.  Note that
 *	memcpy() is used instead of strcpy() because the string
 *	may have embedded nulls.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *  	New memory will be allocated.
 *
 *----------------------------------------------------------------------
 */

static void
GrowDString(Ns_DString *dsPtr, int length)
{
    char *newString;

    if (dsPtr->string != dsPtr->staticSpace) {
	newString = ns_realloc(dsPtr->string, (size_t) length);
    } else {
	newString = ns_malloc(length);
	memcpy(newString, dsPtr->string, (size_t) dsPtr->length);
    }
    dsPtr->string = newString;
    dsPtr->spaceAvl = length;
}

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
 * lisp.c --
 *
 *	Commands for manipulating lists. 
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/nsd/lisp.c,v 1.6 2003/03/07 18:08:27 vasiljevic Exp $, compiled: " __DATE__ " " __TIME__;

#include "nsd.h"


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListNconc --
 *
 *	Append l2 to l1 
 *
 * Results:
 *	A pointer to the head of the new list. 
 *
 * Side effects:
 *	May modify l1.
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListNconc(Ns_List *l1Ptr, Ns_List *l2Ptr)
{
    Ns_List *lPtr;

    if (l1Ptr != NULL) {
        for (lPtr = l1Ptr; ((lPtr->rest) != NULL); lPtr = lPtr->rest)
	    ;
        lPtr->rest = l2Ptr;
        return (l1Ptr);
    } else {
        return (l2Ptr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListCons --
 *
 *	Prepend element to list. 
 *
 * Results:
 *	A new list. 
 *
 * Side effects:
 *	A new node will be allocated for the new element. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListCons(void *elem, Ns_List *lPtr)
{
    Ns_List *newlPtr;

    newlPtr = (Ns_List *) ns_malloc(sizeof(Ns_List));
    newlPtr->first = elem;
    newlPtr->rest = lPtr;
    return newlPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListNreverse --
 *
 *	Reverse the order of a list. 
 *
 * Results:
 *	A pointer to the new head. 
 *
 * Side effects:
 *	Changes all the links in the list.
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListNreverse(Ns_List *lPtr)
{
    Ns_List *nextPtr, *nextRestPtr;

    if (lPtr != NULL) {
        nextPtr = lPtr->rest;
        lPtr->rest = NULL;
        while (nextPtr != NULL) {
            nextRestPtr = nextPtr->rest;
            nextPtr->rest = lPtr;
            lPtr = nextPtr;
            nextPtr = nextRestPtr;
        }
    }
    
    return lPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListLast --
 *
 *	Find the last element in a list. 
 *
 * Results:
 *	Returns a pointer to the last element. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListLast(Ns_List *lPtr)
{
    if (lPtr == NULL) {
        return NULL;
    } else {
        for (; lPtr->rest != NULL; lPtr = lPtr->rest)
	    ;
        return lPtr;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListFree --
 *
 *	Frees the elements of the list with the freeing function that 
 *	is passed in. 
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
Ns_ListFree(Ns_List *lPtr, Ns_ElemVoidProc *freeProc)
{
    Ns_List *nextPtr;

    for (; lPtr != NULL; lPtr = nextPtr) {
        nextPtr = lPtr->rest;
        if (freeProc != NULL) {
            (*freeProc) (lPtr->first);
        }
        ns_free(lPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_IntPrint --
 *
 *	Print an integer to stdout. 
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
Ns_IntPrint(int d)
{
    fprintf(stdout, "%d", d);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_StringPrint --
 *
 *	Print a string to stdout. 
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
Ns_StringPrint(char *s)
{
    fputs(s, stdout);
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListPrint --
 *
 *	Print a list to standard out. 
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
Ns_ListPrint(Ns_List *lPtr, Ns_ElemVoidProc *printProc)
{
    Ns_StringPrint("(");
    for (; lPtr != NULL; lPtr = lPtr->rest) {
        (*printProc) (lPtr->first);
        if (lPtr->rest != NULL)
            Ns_StringPrint(" ");
    }
    Ns_StringPrint(")\n");
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListCopy --
 *
 *	Make a copy of a list. 
 *
 * Results:
 *	A ponter to the head of the new list. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListCopy(Ns_List *lPtr)
{
    Ns_List *curPtr = NULL, *newPtr = NULL, *headPtr = NULL;

    if (lPtr == NULL) {
        return NULL;
    }
    headPtr = curPtr = Ns_ListCons(lPtr->first, NULL);
    for (lPtr = lPtr->rest; lPtr != NULL; lPtr = lPtr->rest) {
        newPtr = Ns_ListCons(lPtr->first, NULL);
        curPtr->rest = newPtr;
        curPtr = newPtr;
    }
    newPtr->rest = NULL;
    
    return headPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListLength --
 *
 *	Find the number of elements in a list. 
 *
 * Results:
 *	Number of elements. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

int
Ns_ListLength(Ns_List *lPtr)
{
    int i;

    for (i = 0; lPtr != NULL; lPtr = lPtr->rest) {
        i++;
    }
    
    return i;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListWeightSort --
 *
 *	Quicksort a list by the weight element of each node. 
 *
 * Results:
 *	A pointer to the new list head. 
 *
 * Side effects:
 *	Rearranges pointers in the list.
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListWeightSort(Ns_List *wPtr)
{
    Ns_List   *curPtr, *mPtr, *nPtr;
    Ns_List   *axisnodePtr;
    Ns_List **lastmPtrPtr, **lastnPtrPtr;
    float     axis;

    if ((wPtr == NULL) || (wPtr->rest == NULL)) {
        return wPtr;
    }

    curPtr = wPtr->rest;

    axisnodePtr = wPtr;
    axisnodePtr->rest = NULL;
    axis = wPtr->weight;

    mPtr = NULL;
    nPtr = NULL;

    /*
     * Loop over the whole list and build two sublists, one with
     * elements < axis, the other with elements >= axis.
     */
    
    lastmPtrPtr = &mPtr;
    lastnPtrPtr = &nPtr;
    while (curPtr != NULL) {
        if (curPtr->weight >= axis) {
            *lastmPtrPtr = curPtr;
            lastmPtrPtr = &(curPtr->rest);
        } else {
            *lastnPtrPtr = curPtr;
            lastnPtrPtr = &(curPtr->rest);
        }
        curPtr = curPtr->rest;
    }
    *lastmPtrPtr = NULL;
    *lastnPtrPtr = NULL;

    /*
     * Sort the list of larger elements and append it to axis
     */
    
    Ns_ListNconc(axisnodePtr, Ns_ListWeightSort(nPtr));

    /*
     * Sort the list of smaller elements and append axis to it.
     */
    
    return (Ns_ListNconc(Ns_ListWeightSort(mPtr), axisnodePtr));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListSort --
 *
 *	Quicksort a list using a comparison callback. 
 *
 * Results:
 *	Pointer to the new list head. 
 *
 * Side effects:
 *	Will rearrange links. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListSort(Ns_List *wPtr, Ns_SortProc *sortProc)
{
    Ns_List  *curPtr, *mPtr, *nPtr;
    Ns_List  *axisnodePtr;
    void     *axisPtr;
    Ns_List **lastmPtrPtr, **lastnPtrPtr;

    if ((wPtr == NULL) || (wPtr->rest == NULL)) {
        return wPtr;
    }
    
    curPtr = wPtr->rest;

    axisnodePtr = wPtr;
    axisnodePtr->rest = NULL;
    axisPtr = wPtr->first;

    mPtr = NULL;
    nPtr = NULL;

    lastmPtrPtr = &mPtr;
    lastnPtrPtr = &nPtr;
    while (curPtr != NULL) {
        if ((*sortProc) (curPtr->first, axisPtr) <= 0) {
            *lastmPtrPtr = curPtr;
            lastmPtrPtr = &(curPtr->rest);
        } else {
            *lastnPtrPtr = curPtr;
            lastnPtrPtr = &(curPtr->rest);
        }
        curPtr = curPtr->rest;
    }
    *lastmPtrPtr = NULL;
    *lastnPtrPtr = NULL;

    Ns_ListNconc(axisnodePtr, Ns_ListSort(nPtr, sortProc));
    return (Ns_ListNconc(Ns_ListSort(mPtr, sortProc), axisnodePtr));
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListDeleteLowElements --
 *
 *	Delete elements in a list with a lower-than-specified weight. 
 *
 * Results:
 *	Pointer to a new list head. 
 *
 * Side effects:
 *	May free elements. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListDeleteLowElements(Ns_List *mPtr, float minweight)
{
    Ns_List **lastPtrPtr = &mPtr;
    Ns_List  *curPtr = mPtr;
    Ns_List  *nextPtr;

    while (curPtr != NULL) {
        nextPtr = curPtr->rest;
        if (curPtr->weight < minweight) {
            *lastPtrPtr = curPtr->rest;
            ns_free(curPtr);
        } else {
            lastPtrPtr = &(curPtr->rest);
        }
        curPtr = nextPtr;
    }
    
    return mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListDeleteWithTest --
 *
 *	Delete elements that pass an equivalency test run by a 
 *	callback, between each node and 'elem'. 
 *
 * Results:
 *	New list head. 
 *
 * Side effects:
 *	May free nodes. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListDeleteWithTest(void *elem, Ns_List *lPtr, Ns_EqualProc *equalProc)
{
    Ns_List  *mPtr = lPtr;
    Ns_List **lastPtrPtr = &lPtr;

    while (mPtr != NULL) {
        if ((*equalProc) (elem, mPtr->first)) {
            *lastPtrPtr = mPtr->rest;
            ns_free(mPtr);
            mPtr = *lastPtrPtr;
        } else {
            lastPtrPtr = &(mPtr->rest);
            mPtr = mPtr->rest;
        }
    }
    
    return lPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListDeleteIf --
 *
 *	Delete elements from a list if a callback says to. 
 *
 * Results:
 *	A new list head. 
 *
 * Side effects:
 *	May free nodes. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListDeleteIf(Ns_List *lPtr, Ns_ElemTestProc *testProc)
{
    Ns_List  *mPtr = lPtr;
    Ns_List **lastPtrPtr = &lPtr;

    while (mPtr != NULL) {
        if ((*testProc) (mPtr->first)) {
            *lastPtrPtr = mPtr->rest;
            ns_free(mPtr);
            mPtr = *lastPtrPtr;
        } else {
            lastPtrPtr = &(mPtr->rest);
            mPtr = mPtr->rest;
        }
    }
    return lPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListDeleteDuplicates --
 *
 *	Delete duplicate items from a list, using an equivalency test 
 *	callback. 
 *
 * Results:
 *	A new list head. 
 *
 * Side effects:
 *	May free nodes. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListDeleteDuplicates(Ns_List *lPtr, Ns_EqualProc *equalProc)
{
    Ns_List *mPtr = lPtr;

    for (; lPtr != NULL; lPtr = lPtr->rest) {
        lPtr->rest = Ns_ListDeleteWithTest(lPtr->first, lPtr->rest, equalProc);
    }
    
    return mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListNmapcar --
 *
 *	Apply a procedure to every member of a list. 
 *
 * Results:
 *	A pointer to the list head. 
 *
 * Side effects:
 *	None. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListNmapcar(Ns_List *lPtr, Ns_ElemValProc *valProc)
{
    Ns_List *mPtr = lPtr;

    for (; lPtr != NULL; lPtr = lPtr->rest) {
        lPtr->first = (*valProc) (lPtr->first);
    }
    
    return mPtr;
}


/*
 *----------------------------------------------------------------------
 *
 * Ns_ListMapcar --
 *
 *	Apply a procedure to every member of a list. 
 *
 * Results:
 *	A pointer to the new list head. 
 *
 * Side effects:
 *	Generates a new list from the results of the procedure. 
 *
 *----------------------------------------------------------------------
 */

Ns_List *
Ns_ListMapcar(Ns_List *lPtr, Ns_ElemValProc *valProc)
{
    Ns_List *mPtr = NULL;

    for (; lPtr != NULL; lPtr = lPtr->rest) {
        Ns_ListPush((*valProc) (lPtr->first), mPtr);
    }
    
    return Ns_ListNreverse(mPtr);
}

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
 * EXPORT NOTICE 
 * 
 * This source code is subject to the U.S. Export Administration
 * Regulations and other U.S. law, and may not be exported or
 * re-exported to certain countries (currently Afghanistan
 * (Taliban-controlled areas), Cuba, Iran, Iraq, Libya, North Korea,
 * Serbia (except Kosovo), Sudan and Syria) or to persons or entities
 * prohibited from receiving U.S. exports (including Denied Parties,
 * Specially Designated Nationals, and entities on the Bureau of
 * Export Administration Entity List).
 */


/* 
 * t_stdlib.c --
 *
 *      AOLserver replacements for Standard C library functions used by
 *      BSAFE.
 *
 *      The original copyright notice is retained.
 *
 */

static const char *RCSID = "@(#): $Header: /Users/dossy/Desktop/cvs/aolserver/nsssl/t_stdlib.c,v 1.1 2001/04/23 21:06:01 jgdavidson Exp $, compiled: " __DATE__ " " __TIME__;

/* Copyright (C) RSA Data Security, Inc. created 1992.

   This file is used to demonstrate how to interface to an
   RSA Data Security, Inc. licensed development product.

   You have a royalty-free right to use, modify, reproduce and
   distribute this demonstration file (including any modified
   version), provided that you agree that RSA Data Security,
   Inc. has no warranty, implied or otherwise, or liability
   for this demonstration file or any modified version.
 */

#include "ns.h"

#if defined (BSAFE4) || defined (BSAFE5)
/*
 * BSAFE 4/5 headers
 */
#include "aglobal.h"
#include "bsafe.h"
#else
/*
 * BSAFE 3 headers
 */
#include "global.h"
#include "bsafe2.h"
#endif

/* If the standard C library comes with a memmove() that correctly
     handles overlapping buffers, MEMMOVE_PRESENT should be defined as
     1, else 0.
   The following defines MEMMOVE_PRESENT as 1 if it has not already been
     defined as 0 with C compiler flags.
 */
#ifndef MEMMOVE_PRESENT
#define MEMMOVE_PRESENT 1
#endif

#if defined (BSAFE4) || defined (BSAFE5)

void CALL_CONV T_memset (p, c, count)
POINTER p;
int c;
unsigned int count;
{
  if (count != 0)
    memset (p, c, count);
}

void CALL_CONV T_memcpy (d, s, count)
POINTER d, s;
unsigned int count;
{
  if (count != 0)
    memcpy (d, s, count);
}

void CALL_CONV T_memmove (d, s, count)
POINTER d, s;
unsigned int count;
{
#if MEMMOVE_PRESENT
  if (count != 0)
    memmove (d, s, count);
#else
  unsigned int i;

  if ((char *)d == (char *)s)
    return;
  else if ((char *)d > (char *)s) {
    for (i = count; i > 0; i--)
      ((char *)d)[i-1] = ((char *)s)[i-1];
  }
  else {
    for (i = 0; i < count; i++)
      ((char *)d)[i] = ((char *)s)[i];
  }
#endif
}

int CALL_CONV T_memcmp (s1, s2, count)
POINTER s1, s2;
unsigned int count;
{
  if (count == 0)
    return (0);
  else
    return (memcmp (s1, s2, count));
}

POINTER CALL_CONV T_malloc (size)
unsigned int size;
{
  return ((POINTER)ns_malloc (size == 0 ? 1 : size));
}

POINTER CALL_CONV T_realloc (p, size)
POINTER p;
unsigned int size;
{
  POINTER result;
  
  if (p == NULL_PTR)
    return (T_malloc (size));

  if ((result = (POINTER)ns_realloc (p, size == 0 ? 1 : size)) == NULL_PTR)
    ns_free (p);
  return (result);
}

void CALL_CONV T_free (p)
POINTER p;
{
  if (p != NULL_PTR)
    ns_free (p);
}


unsigned int CALL_CONV T_strlen(p)
char *p;
{
    return strlen(p);
}

void CALL_CONV T_strcpy(dest, src)
char *dest;
char *src;
{
    strcpy(dest, src);
}

int CALL_CONV T_strcmp (a, b)
char *a, *b;
{
  return (strcmp (a, b));
}

#else
/*
 * BSAFE 3.0
 */

void T_CALL T_memset (p, c, count)
     POINTER p;
     int c;
     unsigned int count;
{
    if (count != 0)
	memset (p, c, count);
}

void T_CALL T_memcpy (d, s, count)
     POINTER d, s;
     unsigned int count;
{
    if (count != 0)
	memcpy (d, s, count);
}

void T_CALL T_memmove (d, s, count)
     POINTER d, s;
     unsigned int count;
{
#if MEMMOVE_PRESENT
    if (count != 0)
	memmove (d, s, count);
#else
    unsigned int i;

    if ((char *)d == (char *)s)
	return;
    else if ((char *)d > (char *)s) {
	for (i = count; i > 0; i--)
	    ((char *)d)[i-1] = ((char *)s)[i-1];
    }
    else {
	for (i = 0; i < count; i++)
	    ((char *)d)[i] = ((char *)s)[i];
    }
#endif
}

int T_CALL T_memcmp (s1, s2, count)
     POINTER s1, s2;
     unsigned int count;
{
    if (count == 0)
	return (0);
    else
	return (memcmp (s1, s2, count));
}

POINTER T_CALL T_malloc (size)
     unsigned int size;
{
    return ((POINTER)ns_malloc (size == 0 ? 1 : size));
}

POINTER T_CALL T_realloc (p, size)
     POINTER p;
     unsigned int size;
{
    POINTER result;
  
    if (p == NULL_PTR)
	return (T_malloc (size));

    if ((result = (POINTER)ns_realloc (p, size == 0 ? 1 : size)) == NULL_PTR)
	ns_free (p);
    return (result);
}

void T_CALL T_free (p)
     POINTER p;
{
    if (p != NULL_PTR)
	ns_free (p);
}

#endif

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
 * memory.c --
 *
 *	Memory allocation routine wrappers which abort the process on error.
 */

static const char *RCSID = "@(#) $Header: /Users/dossy/Desktop/cvs/aolserver/thread/Attic/memory.c,v 1.1.1.1 2000/05/02 13:48:40 kriston Exp $, compiled: " __DATE__ " " __TIME__;

#include "thread.h"

int nsMemPools = 0;        /* Should memory pools be used? This is usually
			    * changed in main(). */

void *
ns_realloc(void *ptr, size_t size)
{
    if (nsMemPools) {
	ptr = (ptr ? NsPoolRealloc(ptr, size) : NsPoolMalloc(size));
    } else {
	ptr = (ptr ? realloc(ptr, size) : malloc(size));
    }	
    if (ptr == NULL) {
	NsThreadAbort("ns_realloc: could not allocate %d bytes", size);
    }

    return ptr;
}

char *
ns_strcopy(char *old)
{
    return (old == NULL ? NULL : ns_strdup(old));
}

char *
ns_strdup(char *old)
{
    char *new;

    new = ns_malloc(strlen(old) + 1);
    strcpy(new, old);

    return new;
}

void *
ns_malloc(size_t size)
{
    return ns_realloc(NULL, size);
}

void
ns_free(void *ptr)
{
    if (ptr != NULL) {
	if (nsMemPools) {
	    NsPoolFree(ptr);
	} else {
	    free(ptr);
	}
    }
}

void *
ns_calloc(size_t num, size_t esize)
{
    void *new;
    size_t size;

    size = num * esize;
    new = ns_malloc(size);
    memset(new, 0, size);

    return new;
}

/*
 * Backward compatible wrappers.
 */

#ifdef Ns_Malloc
#undef Ns_Malloc
#endif

void *
Ns_Malloc(size_t size)
{
    return ns_malloc(size);
}

#ifdef Ns_Realloc
#undef Ns_Realloc
#endif

void *
Ns_Realloc(void *ptr, size_t size)
{
    return ns_realloc(ptr, size);
}

#ifdef Ns_Calloc
#undef Ns_Calloc
#endif

void *
Ns_Calloc(size_t nelem, size_t elsize)
{
    return ns_calloc(nelem, elsize);
}

#ifdef Ns_Free
#undef Ns_Free
#endif

void 
Ns_Free(void *ptr)
{
    ns_free(ptr);
}

#ifdef Ns_StrDup
#undef Ns_StrDup
#endif

char *
Ns_StrDup(char *str)
{
    return ns_strdup(str);
}

#ifdef Ns_StrCopy
#undef Ns_StrCopy
#endif

char *
Ns_StrCopy(char *str)
{
    return ns_strcopy(str);
}

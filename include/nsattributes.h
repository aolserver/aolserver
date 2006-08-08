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
 * nsattrs.h --
 *      
 *	Type and programmer error checking attributes for GCC compiler.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/include/nsattributes.h,v 1.4 2006/08/08 01:15:26 dossy Exp $
 */

#ifndef NSATTRS_H
#define NSATTRS_H

# define _nsmalloc
# define _nspure
# define _nsconst
# define _nslikely(x) x
# define _nsunlikely(x) x
# define _nsunused
# define _nsnoreturn
# define _nsprintflike(fmtarg, firstvararg)
# define _nsscanflike(fmtarg, firstvararg)
# define _nsfmtarg(x)
# define _nsused
# define _nsdeprecated
# define _nsnonnull
# define _nswarnunused
# define _nsmayalias

#ifdef __GNUC_MINOR__
#ifdef __GNUC_PREREQ
#undef __GNUC_PREREQ
#endif
# define __GNUC_PREREQ(maj, min) \
        ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif

#ifdef __GNUC_PREREQ

#if __GNUC_PREREQ(2,96)
# undef _nsmalloc
# define _nsmalloc       __attribute__ ((malloc))
# undef _nspure
# define _nspure         __attribute__ ((pure))
# undef _nsconst
# define _nsconst        __attribute__ ((const))
# undef _nslikely
# define _nslikely(x)    __builtin_expect((x),1)
# undef _nsunlikely
# define _nsunlikely(x)  __builtin_expect((x),0)
#endif

#if __GNUC_PREREQ(2,7)
# undef _nsunused
# define _nsunused       __attribute__ ((unused))
# undef _nsnoreturn
# define _nsnoreturn     __attribute__ ((noreturn))
# undef _nsprintflike
# define _nsprintflike(fmtarg, firstvararg) \
             __attribute__((format (printf, fmtarg, firstvararg)))
# undef _nsscanflike
# define _nsscanflike(fmtarg, firstvararg) \
             __attribute__((format (scanf, fmtarg, firstvararg)))
#endif

#if __GNUC_PREREQ(2,8)
# undef _nsfmtarg
# define _nsfmtarg(x)    __attribute__ ((format_arg(x)))
#endif

#if __GNUC_PREREQ(3,1)
# undef _nsused
# define _nsused         __attribute__ ((used))
#endif

#if __GNUC_PREREQ(3,2)
# undef _nsdeprecated
# define _nsdeprecated   __attribute__ ((deprecated))
#endif

#if __GNUC_PREREQ(3,3)
# undef _nsnonnull
# define _nsnonnull	__attribute__ ((nonnull))
# undef _nswarnunused
# define _nswarnunused   __attribute__ ((warn_unused_result))
# undef _nsmayalias
# define _nsmayalias     __attribute__ ((may_alias))
#endif

#endif /* __GNUC__PREREQ */

#define NS_RCSID(string) static const char *RCSID _nsunused = string \
    ", compiled: " __DATE__ " " __TIME__

#endif /* NSATTRS_H */

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
 *	Type and programmer error checking attributes.
 *
 *	$Header: /Users/dossy/Desktop/cvs/aolserver/include/nsattributes.h,v 1.1 2005/08/23 21:41:31 jgdavidson Exp $
 */

#ifndef NSATTRS_H
#define NSATTRS_H



#undef  __GNUC_PREREQ
#if defined __GNUC__ && defined __GNUC_MINOR__
# define __GNUC_PREREQ(maj, min) \
        ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif


#if __GNUC_PREREQ(2,96)
# define _nsmalloc       __attribute__ ((malloc))
# define _nspure         __attribute__ ((pure))
# define _nsconst        __attribute__ ((const))
# define _nslikely(x)    __builtin_expect((x),1)
# define _nsunlikely(x)  __builtin_expect((x),0)
#else
# define _nsmalloc
# define _nspure
# define _nsconst
# define _nslikely(x) x
# define _nsunlikely(x) x
#endif


#if __GNUC_PREREQ(2,7)
# define _nsunused       __attribute__ ((unused))
# define _nsnoreturn     __attribute__ ((noreturn))
# define _nsprintflike(fmtarg, firstvararg) \
             __attribute__((format (printf, fmtarg, firstvararg)))
# define _nsscanflike(fmtarg, firstvararg) \
             __attribute__((format (scanf, fmtarg, firstvararg)))
#else
# define _nsunused
# define _nsnoreturn
# define _nsprintflike(fmtarg, firstvararg)
# define _nsscanflike(fmtarg, firstvararg)
#endif


#if __GNUC_PREREQ(2,8)
# define _nsfmtarg(x)    __attribute__ ((format_arg(x)))
#else
# define _nsfmtarg(x)
#endif


#if __GNUC_PREREQ(3,1)
# define _nsused         __attribute__ ((used))
#else
# define _nsused
#endif


#if __GNUC_PREREQ(3,2)
# define _nsdeprecated   __attribute__ ((deprecated))
#else
# define _nsdeprecated
#endif


#if __GNUC_PREREQ(3,3)
# define _nsnonnull(...) __attribute__ ((nonnull(__VA_ARGS__)))
# define _nswarnunused   __attribute__ ((warn_unused_result))
# define _nsmayalias     __attribute__ ((may_alias))
#else
# define _nsnonnull(...)
# define _nswarnunused
# define _nsmayalias
#endif


#define NS_RCSID(string) static const char *RCSID _nsunused = string \
    ", compiled: " __DATE__ " " __TIME__


#endif /* NSATTRS_H */

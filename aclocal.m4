#
# The contents of this file are subject to the AOLserver Public License
# Version 1.1 (the "License"); you may not use this file except in
# compliance with the License. You may obtain a copy of the License at
# http://aolserver.com/.
#
# Software distributed under the License is distributed on an "AS IS"
# basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
# the License for the specific language governing rights and limitations
# under the License.
#
# The Original Code is AOLserver Code and related documentation
# distributed by AOL.
# 
# The Initial Developer of the Original Code is America Online,
# Inc. Portions created by AOL are Copyright (C) 1999 America Online,
# Inc. All Rights Reserved.
#
# Alternatively, the contents of this file may be used under the terms
# of the GNU General Public License (the "GPL"), in which case the
# provisions of GPL are applicable instead of those above.  If you wish
# to allow use of your version of this file only under the terms of the
# GPL and not to allow others to use your version of this file under the
# License, indicate your decision by deleting the provisions above and
# replace them with the notice and other provisions required by the GPL.
# If you do not delete the provisions above, a recipient may use your
# version of this file under either the License or the GPL.
# 
#
# $Header: /Users/dossy/Desktop/cvs/aolserver/aclocal.m4,v 1.4 2004/09/21 23:26:09 dossy Exp $
#

#
# configure.in --
#
#	AOLserver autoconf include which simply includes Tcl's tcl.m4.
#

builtin(include,../tcl8.4/unix/tcl.m4)


dnl
dnl Check to see what variant of gethostbyname_r() we have.  Defines
dnl HAVE_GETHOSTBYNAME_R_{6, 5, 3} depending on what variant is found.
dnl
dnl Based on David Arnold's example from the comp.programming.threads
dnl FAQ Q213.
dnl

AC_DEFUN(AC_HAVE_GETHOSTBYNAME_R,
[saved_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -lnsl"
AC_CHECK_FUNC(gethostbyname_r, [
  AC_MSG_CHECKING([for gethostbyname_r with 6 args])
  AC_TRY_COMPILE([
    #include <netdb.h>
  ], [
    char *name;
    struct hostent *he, *res;
    char buffer[2048];
    int buflen = 2048;
    int h_errnop;

    (void) gethostbyname_r(name, he, buffer, buflen, &res, &h_errnop);
  ], [
    AC_DEFINE(HAVE_GETHOSTBYNAME_R)
    AC_DEFINE(HAVE_GETHOSTBYNAME_R_6)
    AC_MSG_RESULT(yes)
  ], [
    AC_MSG_RESULT(no)
    AC_MSG_CHECKING([for gethostbyname_r with 5 args])
    AC_TRY_COMPILE([
      #include <netdb.h>
    ], [
      char *name;
      struct hostent *he;
      char buffer[2048];
      int buflen = 2048;
      int h_errnop;

      (void) gethostbyname_r(name, he, buffer, buflen, &h_errnop);
    ], [
      AC_DEFINE(HAVE_GETHOSTBYNAME_R)
      AC_DEFINE(HAVE_GETHOSTBYNAME_R_5)
      AC_MSG_RESULT(yes)
    ], [
      AC_MSG_RESULT(no)
      AC_MSG_CHECKING([for gethostbyname_r with 3 args])
      AC_TRY_COMPILE([
        #include <netdb.h>
      ], [
        char *name;
        struct hostent *he;
        struct hostent_data data;

        (void) gethostbyname_r(name, he, &data);
      ], [
        AC_DEFINE(HAVE_GETHOSTBYNAME_R)
        AC_DEFINE(HAVE_GETHOSTBYNAME_R_3)
        AC_MSG_RESULT(yes)
      ], [
        AC_MSG_RESULT(no)
      ])
    ])
  ])
])
CFLAGS="$saved_CFLAGS"])

AC_DEFUN(AC_HAVE_GETHOSTBYADDR_R,
[saved_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -lnsl"
AC_CHECK_FUNC(gethostbyaddr_r, [
  AC_MSG_CHECKING([for gethostbyaddr_r with 7 args])
  AC_TRY_COMPILE([
    #include <netdb.h>
  ], [
    char *addr;
    int length;
    int type;
    struct hostent *result;
    char buffer[2048];
    int buflen = 2048;
    int h_errnop;

    (void) gethostbyaddr_r(addr, length, type, result, buffer, buflen, &h_errnop);
  ], [
    AC_DEFINE(HAVE_GETHOSTBYADDR_R)
    AC_DEFINE(HAVE_GETHOSTBYADDR_R_7)
    AC_MSG_RESULT(yes)
  ], [
    AC_MSG_RESULT(no)
  ])
])
CFLAGS="$saved_CFLAGS"])


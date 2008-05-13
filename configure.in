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
# $Header: /Users/dossy/Desktop/cvs/aolserver/configure.in,v 1.25 2008/05/13 21:25:23 dvrsn Exp $
#

#
# configure.in --
#
#    AOLserver configure script.
#

AC_INIT(include/ns.h)
AC_PREREQ(2.13)
AC_PREFIX_DEFAULT(/usr/local/aolserver)

#
# Include and verify Tcl config located by --with-tcl=
#

SC_PATH_TCLCONFIG
SC_LOAD_TCLCONFIG
if test "${TCL_THREADS}" != "1"; then
    AC_MSG_ERROR([must build tcl with --enable-threads])
fi
SC_ENABLE_SYMBOLS

AC_SUBST(TCLSH)
AC_SUBST(TCL_LIB_SPEC)
AC_SUBST(TCL_LIBS)
AC_SUBST(TCL_INCLUDE_SPEC)
AC_SUBST(TCL_RANLIB)
AC_SUBST(TCL_EXEC_PREFIX)

AC_SUBST(TCL_SHLIB_SUFFIX)
AC_SUBST(TCL_CC_SEARCH_FLAGS)
AC_SUBST(TCL_LD_SEARCH_FLAGS)
AC_SUBST(TCL_LD_FLAGS)

AC_SUBST(TCL_CC)
AC_SUBST(TCL_CFLAGS_DEBUG)
AC_SUBST(TCL_CFLAGS_OPTIMIZE)
AC_SUBST(TCL_CFLAGS_WARNING)
AC_SUBST(TCL_EXTRA_CFLAGS)
AC_SUBST(TCL_SHLIB_CFLAGS)
AC_SUBST(TCL_DEFS)

if test "${CC+set}" != "set"; then
    CC=$TCL_CC
fi
AC_PROG_CC

#
# TCL_SHLIB_LD from tclConfig.sh is not quite right for aolserver.
#

test "x$prefix" = "xNONE" && prefix="/usr/local/aolserver"
system=`uname`
case "$system" in
    Darwin)
	LDLIB="\$(CC) -dynamiclib -install_name \$(INSTLIB)/\$(DLLBIN)"
	LDSO="\$(CC) -bundle"
	CCRFLAG=
	LDRFLAG=
	CCRPATHS=
	LDRPATHS=
	;;
    *)
	LDLIB="$TCL_SHLIB_LD"
	case "$LDLIB" in
	    *gcc*)
		LDLIB="$LDLIB -nostartfiles"
		;;
	    *shared*)
		LDLIB="$LDLIB -nostartfiles"
		;;
	esac
	CCRPATH="\$(CCRFLAG)\$(INSTLIB)"
	LDRPATH="\$(LDRFLAG)\$(INSTLIB)"
	if test "$TCL_EXEC_PREFIX" != "$prefix"
	then
		CCRPATH="$CCRPATH \$(CCRFLAG)\$(TCL_EXEC_PREFIX)/lib"
		LDRPATH="$LDRPATH \$(LDRFLAG)\$(TCL_EXEC_PREFIX)/lib"
	fi
	if test -d /usr/lib/lwp
	then
		CCRPATH="$CCRPATH \$(CCRFLAG)/usr/lib/lwp"
		LDRPATH="$LDRPATH \$(LDRFLAG)/usr/lib/lwp"
	fi
	LDSO="\$(LDLIB)"
	CCRPATHS="\$(CCRPATH)"
	LDRPATHS="\$(LDRPATH)"
	CCRFLAG=$TCL_CC_SEARCH_FLAGS
	LDRFLAG=$TCL_LD_SEARCH_FLAGS
        if test "$CCRFLAG" = "" ; then
            CCRPATH=
        fi
        if test "$LDRFLAG" = "" ; then
            LDRPATH=
        fi
	;;
esac
AOLSERVER=$prefix
AC_SUBST(LDLIB)
AC_SUBST(LDSO)
AC_SUBST(CCRFLAG)
AC_SUBST(LDRFLAG)
AC_SUBST(CCRPATH)
AC_SUBST(LDRPATH)
AC_SUBST(CCRPATHS)
AC_SUBST(LDRPATHS)
AC_SUBST(AOLSERVER)

#
# Additional AOLserver checks.
#

AC_CHECK_HEADERS(inttypes.h)
AC_CHECK_FUNCS(timegm fork1 drand48 random _NSGetEnviron)

#
# Poll is currently broken on OS/X.
#

system=`uname`
if test "x$system" != "xDarwin"
then
    AC_CHECK_FUNCS(poll)
fi

AC_CHECK_HEADER(dl.h, AC_DEFINE(USE_DLSHL),)
AC_MSG_CHECKING([need for dup high])
AC_TRY_RUN([
#include <stdio.h>
int
main(int argc, char **argv)
{
    return (sizeof(stdout->_file) == 1) ? 0 : 1;
}], AC_DEFINE(USE_DUPHIGH) as_ok=yes, as_ok=no, as_ok=no)
AC_MSG_RESULT($as_ok)

AC_CACHE_CHECK([for shared libgcc], ac_cv_shared_libgcc, [
    ac_save_LIBS="$LIBS"
    LIBS="-lgcc_s $LIBS"
    AC_TRY_LINK(, [return 0], ac_cv_shared_libgcc=yes, ac_cv_shared_libgcc=no)
    LIBS="$ac_save_LIBS"
])
if test "$ac_cv_shared_libgcc" = "yes" ; then
    LIBS="-lgcc_s $LIBS"
fi

has_threadsafe_dns=no
AC_CHECK_LIB(socket, getaddrinfo)
AC_CHECK_LIB(socket, getnameinfo)
AC_CHECK_FUNCS(getaddrinfo getnameinfo)
if test "${ac_cv_func_getaddrinfo}" = "yes" \
    -a "${ac_cv_func_getnameinfo}" = "yes" ; then
    has_threadsafe_dns=yes
fi
if test "${has_threadsafe_dns}" != "yes" ; then
    AC_HAVE_GETHOSTBYNAME_R
    AC_HAVE_GETHOSTBYADDR_R
    if test "${ac_cv_func_gethostbyname_r}" = "yes" \
        -a "${ac_cv_func_gethostbyaddr_r}" = "yes" ; then
        has_threadsafe_dns=yes
    fi
fi
if test "${has_threadsafe_dns}" != "yes" ; then
  AC_MSG_WARN([dns queries will use non-threadsafe calls which could result in server instability])
fi

#
# Create Makefile.global.
#

AC_OUTPUT(include/ns.mak)

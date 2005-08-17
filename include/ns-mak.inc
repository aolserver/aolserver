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
# $Header: /Users/dossy/Desktop/cvs/aolserver/include/ns-mak.inc,v 1.1 2005/08/17 21:18:46 jgdavidson Exp $

#
# ns.mak -- 
#
# 	Common nmake file for AOLserver Win32 builds.
#
# Usage:
#
#	Create a stub makefile which defines one or more of the following
#	and includes this file:
#
#	DLL	Basename of a DLL, e.g., "DLL=myapi" builds "myapi.dll" and
#		"myapi.lib".
#
#	PGM	Basename of a program, e.g., "PGM=mybin" builds "mybin.exe".
#
#	MOD	On Windows, equivalent to a DLL along with support of MODINIT.
#
#	OBJS	List of objects to be built for DLL or MOD.
#
#	PGMOBJS	List of objects for PGM.  The program will also be linked
#		against a DLL, if any.
#
#	MODINIT	Name of module entry point, e.g., "MODINIT=MyMod_ModInit".
#		Causes _modinit.o to be added to OBJS list.
#
#	In addition, you can also define:
#
#	AOLSERVER
#		Installation directory of AOLserver (default "c:\aolserver").
#	TCLINC	Path to Tcl header files.
#	TCLLIB	Path to Tcl library.
#	TCLSH	Path to tclsh binary.
#	DEBUG	Set to 1 for debug build, 0 for release (default 1).
#	DEFS	Additional CFLAGS, e.g., "/D MYDLL_EXPORTS.".
#	LIBS	Additional libraries, e.g., "/LIBPATH:c:\fun fun.lib"
#	INSTALL	Additional install directives, e.g., "install-config".
#	SRCDIR	Location of AOLserver sources used during build of core.
#


#
#
# Defaults from configure.bat
#

!ifndef AOLSERVER
AOLSERVER = $(CONFIG_AOLSERVER)
!endif

!ifndef DEBUG
DEBUG = $(CONFIG_DEBUG)
!endif

!ifndef TCLSH
TCLSH =$(CONFIG_TCLSH)
!endif

!ifndef TCLINC
TCLINC = $(CONFIG_TCLINC)
!endif

!ifndef TCLLIB
TCLLIB =$(CONFIG_TCLLIB)
!endif

#
# Install directories.
#

INSTBIN	= "$(AOLSERVER)\bin"
INSTLIB	= "$(AOLSERVER)\lib"
INSTINC	= "$(AOLSERVER)\include"


#
# Variables for building a DLL, including module DLL's.
#

!ifdef MOD
DLL	= $(MOD)
!ifdef MODINIT
OBJS	= $(OBJS) $(MODOBJ)
!endif
!endif

!ifdef DLL
DLLBIN  = $(DLL).dll
DLLLIB  = $(DLL).lib
BUILD	= $(DLLBIN)
INSTALL = $(INSTALL) install-dll
CLEAN	= $(CLEAN) clean-dll
OBJS	= $(OBJS) $(WINOBJS)
HDRS	= $(HDRS) $(INCDIR)/ns.h $(INCDIR)/nsthread.h
PGMLIBS	 = $(PGMLIBS) $(DLLLIB)
!endif


#
# Variables for building a program, perhaps using cooresponding DLL.
#

!ifdef PGM
PGMBIN	 = $(PGM).exe
BUILD	 = $(BUILD) $(PGMBIN)
INSTALL	 = $(INSTALL) install-pgm
CLEAN  	 = $(CLEAN) clean-pgm
!ifdef DLL
PGMLIBS  = $(PGMLIBS) $(DLLLIB)
!endif
!endif


#
# Common compiler and linker options.
#

COPTS	= /EHsc /W3 /nologo /c
DEFS	= $(DEFS) /D "_WINDOWS" /D "TCL_THREADS=1" /D "WIN32" \
	  /D "FD_SETSIZE=128" /D "NO_CONST=1" /D "_MBCS" 
LOPTS   = /NOLOGO /SUBSYSTEM:CONSOLE /OPT:NOREF /OPT:NOWIN98 /OPT:NOICF


#
# Update options when building from sources or installed location.
#

!ifndef SRCDIR
INCDIR = $(AOLSERVER)\include
LIBS = $(LIBS) /LIBPATH:"$(AOLSERVER)\lib" nsd.lib nsthread.lib
!else
INCDIR = $(SRCDIR)\include
!if "$(DLL)" != "nsthread"
LIBS = $(LIBS) /LIBPATH:"$(SRCDIR)\nsthread" nsthread.lib
!if "$(DLL)" != "nsd"
LIBS = $(LIBS) /LIBPATH:"$(SRCDIR)\nsd" nsd.lib
!endif
!endif
!endif
MODSRC = $(INCDIR)\nsmodinit.c
MODOBJ = nsmodinit.o


#
# Update options for release or debug build.
#

!if "$(DEBUG)" == "0"
COPTS	= $(COPTS) /MD /O2
DEFS	= $(DEFS) /D "NDEBUG"
!else
COPTS	= $(COPTS) /MDd /Od /Zi /RTCcsu
DEFS	= $(DEFS) /D "_DEBUG"
LOPTS	= $(LOPTS) /DEBUG 
!endif

INCS	= /I "$(INCDIR)" /I "$(TCLINC)"
CFLAGS  = $(COPTS) $(DEFS) $(INCS)
LIBS	= $(LIBS) $(TCLLIB) advapi32.lib ws2_32.lib


#
# Utility commands.
#

RM	= $(TCLSH) $(SRCDIR)\util\nsremove.tcl
MAKEALL	= $(TCLSH) $(SRCDIR)\util\nsmakeall.tcl
INST	= $(TCLSH) $(SRCDIR)\util\nsinstall.tcl

#
# Rules.
#

all build: $(BUILD)

install: $(INSTALL)

clean: $(CLEAN)

!ifdef DLL
$(DLLBIN): $(OBJS)
	link.exe $(LOPTS) /DLL /OUT:$(DLLBIN) /IMPLIB:$(DLLLIB) $(OBJS) $(LIBS)
!endif

$(MODOBJ): $(MODSRC)
	$(CC) $(CFLAGS) -DNS_MODINIT=$(MODINIT) /Fo$(MODOBJ) $(MODSRC)

!ifdef OBJS
$(OBJS): $(HDRS)
!endif

.c.o:
	$(CC) $(CFLAGS) /c /Fo$*.o $*.c

!ifdef PGM
$(PGMBIN): $(PGMOBJS) $(DLLLIB)
	link.exe $(LOPTS) /OUT:$(PGMBIN) $(PGMOBJS) $(PGMLIBS) $(LIBS)
!endif

install-dll: $(DLLBIN) $(DLLLIB)
	$(INST) -d $(INSTBIN) $(DLLBIN)
	$(INST) -d $(INSTLIB) $(DLLLIB)

install-pgm: $(PGMBIN)
	$(INST) -d $(INSTBIN) $(PGMBIN)

clean-dll:
	$(RM) $(DLLBIN) $(DLLLIB) $(DLL).exp $(DLL).ilk $(DLL).pdb $(MODOBJ) $(OBJS)

clean-pgm:
	$(RM) $(PGMBIN) $(PGMOBJS) $(PGM).ilk $(PGM).pdb

.PHONY: clean-dll clean-pgm install-dll install-pgm

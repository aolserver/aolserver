# Microsoft Developer Studio Project File - Name="nsthread" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=nsthread - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "nsthread.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "nsthread.mak" CFG="nsthread - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "nsthread - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "nsthread - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "nsthread - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSTHREAD_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\include" /I "..\..\tcl8.4\generic" /D "_WINDOWS" /D "_USRDLL" /D "NSTHREAD_EXPORTS" /D TCL_THREADS=1 /D "NDEBUG" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /D NO_CONST=1 /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib tcl84t.lib /nologo /dll /machine:I386 /libpath:"..\..\tcl8.4\win\Release"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=if not exist ..\release mkdir ..\release	for %%m in (dll) do copy release\nsthread.%%m ..\release\nsthread.%%m	for %%m in (dll) do copy ..\..\tcl8.4\win\Release\tcl84t.%%m ..\release\tcl84t.%%m	for %%m in (exe) do copy ..\..\tcl8.4\win\Release\tclsh84t.%%m ..\release\tclsh84t.%%m
# End Special Build Tool

!ELSEIF  "$(CFG)" == "nsthread - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSTHREAD_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\include" /I "..\..\tcl8.4\generic" /D "_WINDOWS" /D "_USRDLL" /D "NSTHREAD_EXPORTS" /D TCL_THREADS=1 /D "_DEBUG" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /D NO_CONST=1 /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib tcl84td.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /libpath:"..\..\tcl8.4\win\Debug"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=if not exist ..\debug mkdir ..\debug	for %%m in (dll pdb) do copy debug\nsthread.%%m ..\debug\nsthread.%%m	for %%m in (dll pdb) do copy ..\..\tcl8.4\win\Debug\tcl84td.%%m ..\debug\tcl84td.%%m	for %%m in (exe pdb) do copy ..\..\tcl8.4\win\Debug\tclsh84td.%%m ..\debug\tclsh84td.%%m
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "nsthread - Win32 Release"
# Name "nsthread - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\nsthread\compat.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\cslock.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\error.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\master.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\memory.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\mutex.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\reentrant.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\rwlock.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\sema.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\thread.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\time.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\tls.c
# End Source File
# Begin Source File

SOURCE=..\..\nsthread\winthread.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\include\nsthread.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project

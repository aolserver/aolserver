# Microsoft Developer Studio Project File - Name="nstcl" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=nstcl - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "nstcl.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "nstcl.mak" CFG="nstcl - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "nstcl - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "nstcl - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "nstcl - Win32 Release"

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
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSTCL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /O2 /I "..\..\include" /I "..\..\tcl8.3.1\generic" /D "NDEBUG" /D "BUILD_tcl" /D TCL_THREADS=1 /D USE_TCLALLOC=0 /D "_WINDOWS" /D "_USRDLL" /D "NSTCL_EXPORTS" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 ..\nsthread\release\nsthread.lib kernel32.lib user32.lib gdi32.lib wsock32.lib advapi32.lib /nologo /dll /machine:I386

!ELSEIF  "$(CFG)" == "nstcl - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSTCL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\include" /I "..\..\tcl8.3.1\generic" /D "_DEBUG" /D "BUILD_tcl" /D TCL_THREADS=1 /D USE_TCLALLOC=0 /D "_WINDOWS" /D "_USRDLL" /D "NSTCL_EXPORTS" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ..\nsthread\debug\nsthread.lib kernel32.lib user32.lib gdi32.lib wsock32.lib advapi32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "nstcl - Win32 Release"
# Name "nstcl - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\regcomp.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\regerror.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\regexec.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\regfree.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\compat\strftime.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclAsync.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclBasic.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclBinary.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCkalloc.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclClock.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCmdAH.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCmdIL.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCmdMZ.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCompCmds.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCompExpr.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclCompile.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclDate.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclEncoding.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclEnv.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclEvent.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclExecute.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclFCmd.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclFileName.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclGet.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclHash.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclHistory.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclIndexObj.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclInterp.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclIO.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclIOCmd.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclIOSock.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclIOUtil.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclLink.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclListObj.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclLiteral.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclLoad.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclMain.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclNamesp.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclNotify.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclObj.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclPanic.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclParse.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclParseExpr.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclPipe.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclPkg.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclPosixStr.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclPreserve.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclProc.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclRegexp.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclResolve.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclResult.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclScan.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclStringObj.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclStubInit.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclStubLib.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclThread.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclTimer.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclUniData.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclUtf.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclUtil.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\generic\tclVar.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWin32Dll.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinChan.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinConsole.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinError.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinFCmd.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinFile.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinInit.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinLoad.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinMtherr.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinNotify.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinPipe.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinSerial.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinSock.c
# End Source File
# Begin Source File

SOURCE=..\..\tcl8.3.1\win\tclWinTime.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project

# Microsoft Developer Studio Project File - Name="nsd" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=nsd - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "nsd.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "nsd.mak" CFG="nsd - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "nsd - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "nsd - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "nsd - Win32 Release"

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
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSD_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX /Zi /O2 /I "..\..\tcl8.4\generic" /I "..\..\include" /D "NDEBUG" /D "_WINDOWS" /D "_USRDLL" /D "NSD_EXPORTS" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /D TCL_THREADS=1 /D NO_CONST=1 /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 tcl84t.lib nsthread.lib kernel32.lib advapi32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /libpath:"..\..\tcl8.4\win\Release" /libpath:"..\nsthread\release" /OPT:REF
# SUBTRACT LINK32 /pdb:none
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=for %%m in (dll) do copy release\nsd.%%m ..\release\nsd.%%m	copy ..\..\nsd\init.tcl ..\release\init.tcl
# End Special Build Tool

!ELSEIF  "$(CFG)" == "nsd - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "NSD_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /GX /ZI /Od /I "..\..\tcl8.4\generic" /I "..\..\include" /D "_DEBUG" /D "NS_NOCOMPAT" /D "_WINDOWS" /D "_USRDLL" /D "NSD_EXPORTS" /D "WIN32" /D "_MBCS" /D FD_SETSIZE=128 /D TCL_THREADS=1 /D NO_CONST=1 /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 tcl84tg.lib nsthread.lib kernel32.lib advapi32.lib ws2_32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept /libpath:"..\..\tcl8.4\win\Debug" /libpath:"..\nsthread\debug"
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Cmds=for %%m in (dll pdb) do copy debug\nsd.%%m ..\debug\nsd.%%m	copy ..\..\nsd\init.tcl ..\debug\init.tcl
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "nsd - Win32 Release"
# Name "nsd - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Source File

SOURCE=..\..\nsd\adpcmds.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\adpeval.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\adpparse.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\adprequest.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\auth.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\cache.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\callbacks.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\cls.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\config.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\conn.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\connio.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\crypt.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\dns.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\driver.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\dsprintf.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\dstring.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\encoding.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\exec.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\fastpath.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\fd.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\filter.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\form.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\getopt.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\httptime.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\index.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\info.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\init.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\lisp.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\listen.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\log.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\mimetypes.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\modload.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\nsconf.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\nsmain.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\nsthread.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\nswin32.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\op.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\pathname.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\pidfile.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\proc.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\queue.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\quotehtml.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\random.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\request.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\return.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\rollfile.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\sched.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\server.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\set.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\sock.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\sockcallback.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\stamp.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\str.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclatclose.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclcmds.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclconf.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclenv.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclfile.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclhttp.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclimg.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclinit.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tcljob.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclmisc.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclobj.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclrequest.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclresp.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclsched.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclset.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclshare.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclsock.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclthread.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclvar.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\tclxkeylist.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\url.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\urlencode.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\urlopen.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\urlspace.c
# End Source File
# Begin Source File

SOURCE=..\..\nsd\uuencode.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\include\ns.h
# End Source File
# Begin Source File

SOURCE=..\..\nsd\nsd.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project

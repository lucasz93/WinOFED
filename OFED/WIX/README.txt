[06-17-2013]

How to generate a OpenFabrics Enterprise Distribution for Windows Release (winOFED)
using the open source installer tool set WIX 3.5 (stable released Jan 31, 2011) 
(http://wix.sourceforge.net/).

WIX References:
	WIX Tutorial http://wix.tramontana.co.hu/
	WIX sourceforge project http://wix.sourceforge.net/
	WIX Introduction http://wix.sourceforge.net/manual-wix3/main.htm
	WIX collection http://www.dalun.com/wix/default.htm
        WIX download http://wix.codeplex.com/releases/view/60102


WinOF Revisions: (based on)
  1.0      svn.614
  1.0.1    svn.864
  1.1      svn.1177
  2.0      svn.1763
  2.0.2    svn.1975
  2.1      svn.2476
  2.2      svn.2739
  2.2.1    svn.2778
WinOF renamed to winOFED
  2.3      svn.3041
  3.0      svn.3376
  3.1      svn.3414
  3.2      svn.3635


Creating a binary release tree
------------------------------

As of WinOF 2.1 release [Aug'2008] the build environment has been switched over
to Microsoft's WDK (Windows Driver Kit) version C:\WinDDK\7600.16385.0.

See gen1\trunk\OFED\BuildRelease.bat file to generate a Wix installers (.msi
file) containing signed driver files; one .msi installer for each OS/processor.

The OS flavor win7\bin\ folder will be populated with the correct folder
structure such that a WIX installer (.msi) files can be generated; either
cd into OS\arch dir and run buildmsi.bat or use WinOF\BuildRelease.bat.

Warning - buildrelease.bat is not generic, some modifications are required
as the folder structure is assumed; see SVN to set build SVN (aka OPENIB_REV).

BuildRelease.bat will by default deposit 7 .msi files in
'%SystemRoot%\temp\OFED_OS_arch.msi'.


The other approach to creating a binary release tree is to generate the
contents of WIX\bin\ yourself from a WDK/SDK build window which can run
'nmake'.

  1) Generate binaries for each supported architecture: x86, x64 and ia64.
     cd to trunk; build /wg from a WDK OS and arch specific command window; all
     are required by etc\makebin.bat.

  2) from trunk: execute 'etc\makebin %CD% dest-dir OS-flavor' for each OS
     flavor: wnet, wlh and wxp.
     Say your svn repository is at C:\open-ib\, then to populate the WIX bin
     folder for Server 2008 binaries from a command window:
        makebin C:\open-ib\gen1\trunk C:\open-ib\gen1\WinOF\Wix\WLH\bin WLH

With the arrival of Windows Server 2008 & Vista (WLH - Windows LongHorn) driver
signing is a requirement. The WIX\sign-all-drivers.bat script will create a .cat
file for each driver .inf located. The generation of the .cat file is driven
from the corresponding driver.inf file via inf2cat.exe creating the .cat file
and signtool.exe signing the .cat and .sys files.

A SW publisher's digital-ID certificate is required in order for WinOF
installers to be created. A test certificate can be generated for local use,
requires overhead during installation ('bcdedit -set testsigning on', reboot &
local certificate store updates).
The MS prescribed procedure is to obtain a SW publisher's certificate from
VeriSign or other CA agency; if your company is producing SW drivers for
SVR2008/Vista, then you will likely have access to a cert file.
The OFA will be purchasing a certificate for WinOF publication.
Scripts for signing drivers assume the MS cross-certification .cer file will be
resident in 'trunk\WinOF\Wix\*.cer'; your company Cert must be placed in the
local cert store under the default personal 'My' store. 
see trunk\winof\buildrelease.bat for an example of how to invoke driver
signing or 'WIX\sign-all-drivers.bat'.
Also see the Microsoft 'Kernel Mode Code Signing' document
'KMCS_Walkthrough.doc'; goggle for current URL.


Creating a WIX tool set
-------------------------

Download the WIX v3.5 (stable) tool set (http://wix.codeplex.com/releases/view/60102/)
to ‘OFED\WIX\WIX_tools\’.

Unzip the archive to a folder within 'WIX_tools\' as this folder represents the
version of the tool set.

Something like unzip wix35-binaries.zip into 'wix35-binaries\'.
You would now have the following structure:
	OFED\WIX\WIX_tools\wix35-binaries\{candle.exe, light.exe,...}

Point being the following files reference the WIX tool set path.
    Trunk\OFED\buildRelease.bat
    Trunk\OFED\bld1.bat
    Trunk\OFED\buildOS.bat
    Trunk\OFED\WIX\common\Makefile.inc


Updating Release Files
---------------------

Update Release_notes.htm file.

        The file 'Release_notes.htm' represents the next to be released
        WinOF version, as reflected by is Release ID.

	Release ID number (e.g., 1.0, point releases are 1.0.x)

	New features

	Know issues

Update the trunk\docs\Manual.htm file for new features.


BUILDING a .msi installer image file
------------------------------------

Easy way:
 place MS cross certificate file (.cer) in WIX\ folder; 'My' cert store needs
 to contain your company cert file; OFED\BuildRelease.bat needs the name of
 your company cert file; OFA case 'OpenFabrics Alliance'.
 .
 cd trunk\
 From a standard DOS cmd window, not a WDK cmd window, say 

   buildrelease all		# .msi files created in %windir%\temp\*.msi

   Build an installer in steps:

   buildrelease makebin - assumes trunk\bin\* built,
                          populates WIX\{wlh,win7}\bin folders.
   buildrelease sign - sign driver files & exit, assumes makebin has been run.
   buildrelease msi - signs & creates installers assuming makebin has been run.

   buildrelease wix - creates .msi installers - assumes all bin\ folders
                      populated and drivers signed.

CPU specific builds

CD to the WIX OS-flavor and architecture specific directory. 'nmake.exe' needs
to be in your command window search path. Build three arch specific installers
(.msi files) for WLH and WNET; WXP is x86 only. 

WARNING:
   assupmtion: .\bin is populated correctly from makebin.bat or
   'BuildRelease makebin'.

cd gen1\trunk\OFED\WIX\wlh\x86 & nmake
	Results in a .\OFED_wlh_x86.msi installer image.

cd gen1\trunk\OFED\WIX\wlh\x64 & nmake
	Results in a OFED_wlh_x64.msi installer image.

cd gen1\trunk\OFED\WIX\wlh\ia64 & nmake
	Results in a OFED_wlh_ia64.msi installer image.


DEBUG Installation
------------------
Create a log file for a given .msi installation:

  msiexec /I "OFED_x86.msi" /Lv \temp\msi.log

  Also see %windir%\inf\setupapi.dev.log on Svr08 & Vista for driver load
  logging.

Command line way to set an interface's IP address, netmask, no gateway:

  netsh interface ip set \
         address "Local Area Connection 3" static 10.10.4.200 255.255.255.0
  netsh interface ip show address "Local Area Connection 3"



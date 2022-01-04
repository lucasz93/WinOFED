@echo off
setlocal

if "%WDK_PATH%"=="" goto use
if "%OPENIB_REV%"=="" goto use
if "%PLATFORM_SDK_PATH%"=="" goto use

if "%1"=="chk" (
	set wo_bld=chk
	goto set_arch
)
if "%1"=="fre" (
	set wo_bld=fre
	goto set_arch
)
goto use

:set_arch
if "%2"=="x86" (
	set wo_arch=x86
	set wo_arch_dir=i386
	goto set_os
)
if "%2"=="x64" (
	set wo_arch=amd64
	set wo_arch_dir=amd64
	goto set_os
)
if "%2"=="ia64" (
	set wo_arch=ia64
	set wo_arch_dir=ia64
	goto set_os
)
goto use

:set_os
if "%3"=="win7" (
	set wo_os=win7
	goto deploy
)
if "%3"=="2003" (
	set wo_os=wnet
	goto deploy
)
if "%3"=="2008" (
	set wo_os=WLH
	goto deploy
)
if "%3"=="xp" (
	if not "%2"=="x86" goto use
	set wo_os=WXP
	set wo_arch=
	goto deploy
)
goto use

:deploy
if "%4"=="" goto use

@echo on
pushd %WINOF_PATH%
if not exist \\%4\c$\winof         mkdir \\%4\c$\winof
if not exist \\%4\c$\winof\install mkdir \\%4\c$\winof\install

xcopy /S /D /Y install\%3\%2\%1 \\%4\c$\winof\install

rem -- HCA drivers automatically install other packages
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\ibbus\*.* \\%4\c$\winof\install\mlx4_hca
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\winmad\*.* \\%4\c$\winof\install\mlx4_hca
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\winverbs\*.* \\%4\c$\winof\install\mlx4_hca
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\ibbus\*.* \\%4\c$\winof\install\mthca
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\winmad\*.* \\%4\c$\winof\install\mthca
xcopy /S /D /Y /EXCLUDE:etc\nomerge.txt install\%3\%2\%1\winverbs\*.* \\%4\c$\winof\install\mthca

rem -- Copy files to support test signing
xcopy /S /D /Y %WDK_PATH%\tools\devcon\%wo_arch_dir%\devcon.exe \\%4\c$\winof\install
xcopy /S /D /Y etc\addcert.bat \\%4\c$\winof\install
xcopy /S /D /Y etc\sign*.bat \\%4\c$\winof\install
xcopy /D /Y %WDK_PATH%\bin\selfsign\* \\%4\c$\winof\install

@echo off
popd
goto end

:use
echo -
echo depwo - deploy winof installation files to cluster head node
echo depwo {chk : fre} {x86 : x64 : ia64} {xp : 2003 : 2008 : win7} headnode
echo -
echo You should run bldwo and pkgwo before running this batch file.
echo You must customize for your system by setting the following environment
echo variables:
echo -
echo WDK_PATH          (example set WDK_PATH=c:\winddk\6001.18001)
echo WINOF_PATH        (example set WINOF_PATH=c:\ofw\trunk)
echo OPENIB_REV        (example set OPENIB_REV=0)
echo PLATFORM_SDK_PATH (example set PLATFORM_SDK_PATH=c:\progra~1\mi2578~1)
echo -
echo Use:
echo depwo {chk : fre} {x86 : x64 : ia64} {xp : 2003 : 2008 : win7} headnode
echo xp only supports x86 build
echo -
echo You must have privileges to copy into \\headnode\c$\winof\install
echo -
echo Examples:
echo depwo chk x64 2008 win08-0 - copies 2008 checked x64 files to win08-0
echo depwo fre x64 win7 win7-0  - copies windows 7 free x64 files to win7-0
echo -
echo Files are copied under c:\winof\install on the target system
echo See docs\build.txt for additional information on building the tree.
echo Also see pkgwo and bldwo batch scripts.

:end
endlocal
@echo on

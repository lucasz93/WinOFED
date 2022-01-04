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
	goto set_os
)
if "%2"=="x32" (
	set wo_arch=x86
	goto set_os
)
if "%2"=="x64" (
	set wo_arch=x64
	goto set_os
)
if "%2"=="ia64" (
	set wo_arch=64
	goto set_os
)
goto use

:set_os
if "%3"=="win7" (
	set wo_os=win7
	goto set_bld
)
if "%3"=="2003" (
	set wo_os=wnet
	goto set_bld
)
if "%3"=="2008" (
	set wo_os=WLH
	goto set_bld
)
if "%3"=="xp" (
	if not "%2"=="x86" if not "%2"=="x32" goto use
	set wo_os=WXP
	set wo_arch=
	goto set_bld
)
goto use

:set_bld
if "%4"=="" set wo_bld_opt=-wg & goto do_build

:loop
if "%4"=="" goto do_build
set wo_bld_opt=%wo_bld_opt% %4
shift
goto loop

:do_build
set DDKBUILDENV=
pushd .
call %WDK_PATH%\bin\setenv.bat %WDK_PATH%\ %wo_bld% %wo_arch% %wo_os% no_oacr
popd
build %wo_bld_opt%
goto end

:use
echo -
echo bldwo - build winof
echo -
echo Allows building any OS/processor architecture from a single command window.
echo You must customize for your system by setting the following environment
echo variables:
echo -
echo WDK_PATH          (example set WDK_PATH=c:\winddk\6001.18001)
echo WINOF_PATH        (example set WINOF_PATH=c:\ofw\trunk)
echo OPENIB_REV        (example set OPENIB_REV=0)
echo PLATFORM_SDK_PATH (example set PLATFORM_SDK_PATH=c:\progra~1\mi2578~1)
echo -
echo Use:
echo bldwo {chk : fre} {x86 : x64 : ia64} {xp : 2003 : 2008 : win7} [-options]
echo Default build options are 'wg'.
echo xp only supports x86 build
echo -
echo Examples:
echo bldwo chk x86 2003           - builds checked x86 version for 2003 using -wg
echo bldwo chk x64 2003           - builds checked x64 version for 2003 using -wg
echo bldwo fre x64 win7 -wgc      - builds free    x64 version for Win7 using -wgc
echo bldwo fre x64 2008 -wgc      - builds free    x64 version for 2008 using -wgc
echo bldwo fre x64 2008 -w -g -c  - builds free    x64 version for 2008 using -w -g -c
echo -
echo Also see docs\build.txt

:end
endlocal
@echo on

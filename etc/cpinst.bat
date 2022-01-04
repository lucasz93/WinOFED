@echo off
setlocal

if "%WINOF_PATH%"=="" goto use

if "%1"=="x86" (
	set wo_arch=x86
	set wo_arch_dir=i386
	goto set_os
)
if "%1"=="x64" (
	set wo_arch=amd64
	set wo_arch_dir=amd64
	goto set_os
)
if "%1"=="ia64" (
	set wo_arch=ia64
	set wo_arch_dir=ia64
	goto set_os
)
goto use

:set_os
if "%2"=="2003" (
	set wo_os=wnet
	goto inst
)
if "%2"=="2008" (
	set wo_os=WLH
	goto inst
)
if "%2"=="xp" (
	if not "%1"=="x86" goto use
	set wo_os=WXP
	goto inst
)
goto use

:inst
pushd %WINOF_PATH%
if not exist install       mkdir install
if not exist install\%2    mkdir install\%2
if not exist install\%2\%1 mkdir install\%2\%1

xcopy /D /Y bin\kernel\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%  install\%2\%1
xcopy /D /Y bin\user\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%    install\%2\%1
xcopy /D /Y bin\kernel\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%  install\%2\%1
xcopy /D /Y bin\user\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%    install\%2\%1

for /f "usebackq" %%i in (`dir /s /b *.inf`) do (
	xcopy /D /Y %%i install\%2\%1
)

popd
goto end

:use
echo -
echo cpinst - copy installation files
echo -
echo Copies drivers, libraries, executables, etc. into an install directory.
echo Files from this directory may be used to install drivers on a given
echo target system.  You must customize for your development system by setting
echo the following environment variable:
echo -
echo WINOF_PATH:	(example WINOF_PATH=c:\ofw\trunk)
echo -
echo This will create WINOF_PATH\install\OS\ARCH
echo -
echo Use:
echo cpinst {x86 : x64 : ia64} {xp : 2003 : 2008}
echo xp requires x86 build
echo -
echo You must have built both the free and checked versions of the code
echo for the target platform.  The files with the most recent date will be kept.
echo -
echo Examples:
echo cpinst x86 2003  - creates WINOF_PATH\install\2003\x86
echo cpinst x64 2003  - creates WINOF_PATH\install\2003\x64
echo cpinst x64 2008  - creates WINOF_PATH\install\2008\x64

:end
endlocal
@echo on

@echo off
setlocal
rem tabstop=4
rem
rem version 1.3
rem
rem Build a single OFED installer (.msi) file for a specific Windows OS version.
rem    Binary release tree is constructed in OFED\Wix\OS\bin on an arch basis.
rem    Processor architecture specific WIX installers are constructed in %IDIR%
rem
rem BldOS options
rem		win8, win8chk, win7,win7chk,wlh,wlhchk
rem		arch:x86,x64,ia64
rem		all | allnf | compile | compilenoforce |
rem          compf path | compnf path | makebin | msi |sign | wix | clean

rem This script is an 'example' of a one-command IB stack build to a single
rem installer (.msi) for a specific OS type and arch.
rem
rem Script is designed to be invoked from the <whatever>\trunk folder with
rem <whatever>\OFED\WIX\* present.

rem example: bld1 win7 x64 all -or- bld1 win7chk x64 all
rem
rem Verify the following env vars are suitible for your system configuration.
rem     _DDK, _PSDK, SVN, IDIR

if "%1" == "" goto usage
if "%2" == "" goto usage

if "%1" == "/?" goto usage
if "%1" == "-h" goto usage

rem Wix tools

set WIX=%CD%\OFED\WIX
set WIX_BIN=wix35-binaries

rem validate OS arg

if "%1" == "win8" (
    set __OS=%1
    set __BOS=%1
    set BLD_TYPE=free
    goto OK_OS
)

if "%1" == "win8chk" (
    set __OS=win8
    set __BOS=win8
    set BLD_TYPE=checked
    goto OK_OS
)

if "%1" == "win7" (
    set __OS=%1
    set __BOS=%1
    set BLD_TYPE=free
    goto OK_OS
)

if "%1" == "win7chk" (
    set __OS=win7
    set __BOS=win7
    set BLD_TYPE=checked
    goto OK_OS
)

if "%1" == "wlh" (
    set __OS=%1
    set __BOS=2008
    set BLD_TYPE=free
    goto OK_OS
)
if "%1" == "wlhchk" (
    set __OS=wlh
    set __BOS=2008
    set BLD_TYPE=checked
    goto OK_OS
)

echo $0: BAD OS type '%1' use one of win8, win7, win7chk, wlh, wlhchk
exit /B 1

:OK_OS

rem WIX Installer files (.msi) destination folder - set for local environment.

if "%USERNAME%" == "Administrator" (
    set IBASE=%SystemRoot%\temp
) else (
    set IBASE=%CD%
)

if "%BLD_TYPE%" == "checked" (
    set IDIR=%IBASE%\checked
    if Not Exist "%IDIR%" (
        mkdir %IDIR%
        if %ERRORLEVEL% NEQ 0 (
           echo ERR: mkdir %IDIR% ?
           exit /B 1
        )
    )
) else (
    set IDIR=%IBASE%
)

set RBIN=%WIX%\%__OS%\bin

if /I "%2"=="x86"  (
    set __ARCH=%2
    set __ARCH_MS=i386
    set RBIN_KF=%WIX%\%__OS%\bin\bin\kernel\objfre_%__OS%_%2
    goto OK_ARCH
)

if /I "%2"=="ia64" (
    set X86_REBUILD=true
    set __ARCH=%2
    set __ARCH_MS=%2
    set RBIN_KF=%WIX%\%__OS%\bin\bin\kernel\objfre_%__OS%_%2
    goto OK_ARCH
)

if /I "%2"=="x64"  (
    set X86_REBUILD=true
    set __ARCH=%2
    set __ARCH_MS=amd64
    set RBIN_KF=%WIX%\%__OS%\bin\bin\kernel\objfre_%__OS%_amd64
    goto OK_ARCH
)

rem archK implies existing x86 binaries for SYSWOW64 are good to use.
rem   skip x86 rebuild. Case of being called from BldOS.bat or buildrelease.bat

if "%2"=="x64K"  (
    set __ARCH=x64
    set __ARCH_MS=amd64
    set RBIN_KF=%WIX%\%__OS%\bin\bin\kernel\objfre_%__OS%_amd64
    goto OK_ARCH
)

if "%2"=="ia64K" (
    set __ARCH=ia64
    set __ARCH_MS=ia64
    set RBIN_KF=%WIX%\%__OS%\bin\bin\kernel\objfre_%__OS%_ia64
    goto OK_ARCH
)

echo %0: Invalid Arch type '%2' use one of x86, x64, ia64
exit /B 1

:OK_ARCH

rem process build option
if "%3" == "" goto usage
if "%3" == "all" goto OK
if "%3" == "allnoforce" goto OK
if "%3" == "allnf" goto allf
if "%3" == "allf" (
:allf
    if "%4" == "" goto usage
    if exist "%4" goto OK
	echo %0 Err - path .\%4 does not exist?
	exit /B 1
)
if "%3" == "compile" goto OK
if "%3" == "compilenoforce" goto OK
if "%3" == "compf" goto allf
if "%3" == "compnf" goto allf
if "%3" == "makebin" goto OK
if "%3" == "msi" goto OK
if "%3" == "sign" goto OK
if "%3" == "wix" goto OK
if "%3" == "clean" goto OK

echo Unknown option '%3' ?

:usage

echo usage: Build1 OS Arch cmd {build-this-path}
echo   OS - [win8, win7, win7chk, wlh, wlhchk]
echo   Arch - [x86,x64,ia64]
echo  'option' may be:
echo    all - force recompile, install binaries to WIX tree(makebin),
echo          sign drivers and build one installer.
echo    allnoforce - recompile only if needed, makebin, sign drivers and 
echo                 build one installer.
echo    allf path - force recompile the specified folder, makebin,
echo                sign drivers and build installers.
echo    allnf path - No-force recompile (all arch*) specified folder
echo    compile - force a recompile/link of everything then exit.
echo    compilenoforce - recompile/link only if needed then exit.
echo    compf path - force recompile (all arch*) specified folder
echo    compnf path - No-force recompile (all arch*) specified folder
echo    makebin - assumes binaries are built, installs binaries to WIX tree
echo              then exit.
echo    msi - assumes binaries are installed in WIX tree(makebin), signs drivers
echo          then creates installers (.msi files) in IDIR.
echo    sign - assumes binaries are built and installed, sign drivers, exit.
echo    wix - build .msi installers, assumes (drivers signed) .cat files exist
echo    clean - remove build artifacts for a clean build: .obj, .sys, ...
echo.
echo		example: bld1 win8 x64 all -or- bld1 win7chk x64 all
exit /B 1 

:OK

if not "%WDM_INC_PATH%" == "" (
    echo %0: Error - unable to run from WDK window,
    echo     use %comspec%
    exit /B 1
)

rem assumes %CD% == '<...>\gen1\trunk'

set BSE=%CD%

set MKBIN=%BSE%\etc\makebin.bat
if not EXIST "%MKBIN%" (
    echo %0: Error - missing file %MKBIN%
    echo   invoke bld1.bat from Trunk\ not OFED?
    exit /B 1
)

rem remove build artifacts
if "%3" == "clean" (
    echo Removing build artifacts for %__OS% %__ARCH%

    if "%X86_REBUILD%" == "true" (
        call %CD%\etc\clean-build.bat %__OS% x86
    )

    if not "%__ARCH%" == "x86" (
        call %CD%\etc\clean-build.bat %__OS% %__ARCH%
    )

    if exist %IDIR%\OFED_%__OS%_%__ARCH%.msi (
        echo  Removing %IDIR%\OFED_%__OS%_%__ARCH%.msi
        del /F %IDIR%\OFED_%__OS%_%__ARCH%.msi
    )
    exit /B 0
)

rem Driver Signing Certificate filename, assumes Microsoft cross-cert file
rem %WIX%\%CERTFILE% is valid.
rem set CERTFILE=noCert
set CERTFILE=MSCV-VSClass3.cer

set SW_PUBLISHER="OpenFabrics Alliance"

rem A Digital driver signing certificate store name may be required.

if "%3" == "all" goto chk_cert
if "%3" == "allf" goto chk_cert
if "%3" == "msi" goto chk_cert
if "%3" == "sign" goto chk_cert

goto cert_OK

:chk_cert

if "%CERTFILE%" == "noCert" set /P CERTFILE=[Enter Cross Certificate FileName] 

if "%CERTFILE%" == "" (
    echo %0
    echo %0: Err - MS cross certificate %CERTFILE% required.
    echo %0: see certmgr.exe
    exit /B 1
)

if Not EXIST "%WIX%\%CERTFILE%" (
    echo %0
    echo %0: Err - MS cross certificate %WIX%\%CERTFILE% required.
    exit /B 1
)
	
rem Required WIX files
if Not EXIST "%WIX%\banner.bmp" (
    echo %0
    echo %0: Err - %WIX%\banner.bmp required.
    exit /B 1
)

if Not EXIST "%WIX%\dialog.bmp" (
    echo %0
    echo %0: Err - %WIX%\dialog.bmp required.
    exit /B 1
)

:cert_OK

rem Use this WDK

rem Windows 7 WDK
set _DDK_VER=7600.16385.1
set _COIN_VER=01009

rem Full DDK root path
set _DDK=%SystemDrive%\WinDDK\%_DDK_VER%

if NOT EXIST %_DDK% (
    echo Missing WDK @ %_DDK%
    exit /B 1
)

rem Platform SDK path - watchout for missing LoadPerf.h (installsp.c)

if DEFINED PLATFORM_SDK_PATH (
    set _PSDK=%PLATFORM_SDK_PATH%
) else (
    set _PSDK=C:\PROGRA~1\MICROS~3\Windows\v7.1
)
if NOT EXIST %_PSDK% (
    echo Missing PLATFORM SDK @ %_PSDK%
    exit /B 1
)

if not DEFINED ND_SDK_PATH (
    set ND_SDK_PATH=C:\PROGRA~1\MI1D7C~1\NetDirect
)
if NOT EXIST %ND_SDK_PATH% (
    echo Missing Network Direct SDK @ %ND_SDK_PATH%
    exit /B 1
)
rem for building tests\ndtests
set ND_INC=%ND_SDK_PATH%\include

rem SVN value is used as part of the file version # string
rem set USE_SVN to be the current svn commit number.
rem if 'file' then use file OFED\WIX\build_SVN.txt as the definition.

rem set USE_SVN=1748
set USE_SVN=file

if "%3" == "allf" (
:fp
	set FPATH=%4
	goto svn
)
if "%3" == "compf" goto fp
if "%3" == "compnf" goto fp

:svn

rem Note - OPENIB_REV is assigned SVN in a child script.

if "%USE_SVN%" == "file" (
	if not exist "OFED\WIX\build_SVN.txt" (
		echo Missing file OFED\WIX\build_SVN.txt ?
		echo 3 options: comment out 'set USE_SVN=file' in this file -or-
		echo create the file which contains the SVN commit number.
		echo  single-line of SVN commit number without quotes '2346' -or-
		echo in this file, set USE_SVN=xxx where xxx is your SVN commit #.
		exit /B 1
	)
	set /P SVN=< OFED\WIX\build_SVN.txt
) else (
	set SVN=%USE_SVN%
)
set LBL=_svn.!SVN!

if NOT EXIST "%WIX%\build-all-MSI.bat" (
    echo %0 - Missing .msi installer build script
    echo    %WIX%\build-all-MSI.bat
    exit /B 1
)

if NOT EXIST "%IDIR%" (
    echo %0 - Missing Installer file destination folder %IDIR%, creating...
    mkdir %IDIR%
    if ERRORLEVEL 1 (
        echo %0 - unable to create %IDIR% ?
        exit /B 1
    )
)

echo %0 - Building %__OS% with WDK @ %_DDK%
echo Building for OPENIB_REV %SVN%, installer files (.msi) @ %IDIR%
echo   Drivers signed using Certificate '%CERTFILE%'

rem pause thoughtfully.
if exist %windir%\system32\timeout.exe (
    timeout /T 10
) else (
    pause
)

rem Verify WIX toolset is available - if not, download from
rem http://sourceforge.net/project/showfiles.php?group_id=105970&package_id=114109
rem   <...>\WinOF\WIX\WIX_tools\
rem
if NOT EXIST %WIX%\WIX_tools\%WIX_BIN% (
    echo %0 - Missing WIX tools @ %WIX%\WIX_tools\%WIX_BIN% 
    exit /B 1
)

set STIME=%TIME%

rem skip build - assumes binaries already built and installed.

if "%3" == "wix" (
    set MSI_CMD=msi
    goto mk_msi
)

if "%3" == "makebin" goto InstallBin

if "%3" == "sign" (
    set MSI_CMD=%3
    goto do_msi_chk
) else (
    set MSI_CMD=all
)
if "%3" == "msi" goto do_msi_chk
goto compile

:do_msi_chk

rem make sure building a msi has files to work with.
if not EXIST %RBIN_KF% (
  echo missing %RBIN_KF%
  echo redo 'makebin'?
  pause
  goto InstallBin
)
goto mk_msi

:compile

set OPS=-wgcPM 3
if "%3" == "allnoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%3" == "compilenoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%3" == "allf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%3" == "compf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%3" == "compnf" (
    rem Force Compile everything
    set OPS=-wgPM 3
)

if "%3" == "all" (
    echo Removing build artifacts and folders for %__OS% %__ARCH%
    if not "%__ARCH%" == "x86" (
        call %CD%\etc\clean-build.bat %__OS% %__ARCH%
        echo Removing build artifacts and folders for %__OS% x86 
    )
    if "%X86_REBUILD%" == "true" (
        call %CD%\etc\clean-build.bat %__OS% x86
    )
    if exist %IDIR%\OFED_%__OS%_%__ARCH%.msi (
        echo  Removing %IDIR%\OFED_%__OS%_%__ARCH%.msi
        del /F %IDIR%\OFED_%__OS%_%__ARCH%.msi
    )
)

rem ************ Setup Env for Building 

set WDK_PATH=%_DDK%
set WINOF_PATH=%CD%
set OPENIB_REV=%SVN%
if not DEFINED PLATFORM_SDK_PATH  set PLATFORM_SDK_PATH=%_PSDK%

rem Compile in a specific folder? compf | compnf | allf
if EXIST "%FPATH%" pushd %FPATH%

rem **** Compile for specific OS & architecture 

rem always build x86 as sysWOW64 x86 binaries are needed.
rem Unless arch specified as x64K or ia64K which imply x86 does not need regen.

if "%BLD_TYPE%" == "checked" ( 
    if Not "%__ARCH%" == "x86" (
        if "%X86_REBUILD%" == "true" (
	        echo   Build x86 binaries for sysWOW64
            %COMSPEC% /C "%BSE%\etc\bldwo.bat chk x86 %__BOS% %OPS%"
            if ERRORLEVEL 1 exit /B 1
        ) 
    )

    echo %0 - Checked build %__OS% %__BOS% %__ARCH%
    %COMSPEC% /C "%BSE%\etc\bldwo.bat chk %__ARCH% %__BOS% %OPS%"
    if ERRORLEVEL 1 exit /B 1
    rem Skip free compile if doing checked
    goto skip_free_compile
)

rem Free build

if Not "%__ARCH%" == "x86" (
    if "%X86_REBUILD%" == "true" (
	    echo   free build x86 binaries for sysWOW64
        %COMSPEC% /C "%BSE%\etc\bldwo.bat fre x86 %__BOS% %__ARCH% %OPS%"
        if ERRORLEVEL 1 exit /B 1
    )
)

echo %0 - Free build %__OS% %__ARCH%
%COMSPEC% /C "%BSE%\etc\bldwo.bat fre %__ARCH% %__BOS% %OPS%"
if ERRORLEVEL 1 exit /B 1

:skip_free_compile

rem compnf | compf | allf
if EXIST "%FPATH%" popd

if "%3" == "compf" goto finito
if "%3" == "compnf" goto finito
if "%3" == "compile" goto finito
if "%3" == "compilenoforce" goto finito

rem Install binaries into WIX environment, build msi installers.

:InstallBin

echo Create binary release tree - suitible for OFED-WIX installer build.

if not EXIST %RBIN% (
    mkdir %RBIN%
	goto populate
)

if "%X86_REBUILD%" == "true" (
    rem clean out OS & arch files.

    pushd %RBIN%

    call %BSE%\etc\clean-build.bat %__OS% x86

	if not "%__ARCH%" == "x86" (
    	call %BSE%\etc\clean-build.bat %__OS% %__ARCH%
	)
    for /F %%i in ('dir /S/B x86') DO (
        if exist "%%i"  rmdir /S/Q %%i
    )
	if not "%__ARCH%" == "x86" (
        for /F %%i in ('dir /S/B %__ARCH%') DO (
            if exist "%%i"  rmdir /S/Q %%i
        )
        for /F %%i in ('dir /S/B %__ARCH_MS%') DO (
            if exist "%%i"  rmdir /S/Q %%i
        )
    )
    popd
)

:populate

if not "%__ARCH%" == "x86" (
    rem populate for SysWow64 binaries
    call %MKBIN% %BSE% %RBIN% %__OS% x86 %_DDK% %_COIN_VER% %BLD_TYPE%
    if ERRORLEVEL 1 (
        echo %0: Err: %MKBIN% %BSE% %RBIN% %__OS% x86 %_DDK% %_COIN_VER% %BLD_TYPE%
        exit /B 1
    )
)

call %MKBIN% %BSE% %RBIN% %__OS% %__ARCH% %_DDK% %_COIN_VER% %BLD_TYPE%
if ERRORLEVEL 1 (
    echo %0: Err: %MKBIN% %BSE% %RBIN% %__OS% %__ARCH% %_DDK% %_COIN_VER% %BLD_TYPE%
    exit /B 1
)

if "%3" == "makebin" goto finito

:mk_msi

echo %0 - Drivers Signed with %CERTFILE%
echo   Binary release tree created in
echo     %RBIN%

rem sign drivers & build WIX installers --> see WinOF\WIX

%COMSPEC% /V:on /E:on /C "%_DDK%\bin\setenv.bat %_DDK% fre X64 WNET no_oacr & cd /D %WIX% & build-all-MSI %MSI_CMD% %CERTFILE% %SW_PUBLISHER% %IDIR% %__OS% %__ARCH%"


:finito

echo.
echo %0: Finished %0 %*
echo %0:   Started  %STIME%
echo %0:   Finished %TIME%

:xit

endlocal

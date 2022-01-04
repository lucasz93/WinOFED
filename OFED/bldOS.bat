@echo off
setlocal
rem tabstop=4
rem
rem Build OFED release installers (.msi) files for a specific Windows OS version.
rem    Binary release is constructed in OFED\Wix\OS\bin.
rem    Processor architecture specific WIX installers are constructed in %IDIR%
rem
rem BldOS options
rem  option: [win7,wlh] [all | allnf | compile | compilenoforce |
rem          compf path | compnf path | makebin | msi |sign | wix | clean]

rem This script is an 'example' of a one-command IB stack build to single-file
rem installer for a single OS type (3 msi files are produces, 1 per arch)
rem
rem Script is designed to be invoked from the <whatever>\gen1\trunk folder with
rem <whatever>\gen1\trunk\WinOF\Wix\* present.
rem
rem Verify the following env vars are suitible for your system configuration.
rem     _DDK, _PSDK, SVN, IDIR

rem WIX Installer files (.msi) destination folder - set for local environment.
set IDIR=%SystemRoot%\temp

if "%1" == "" goto usage
if "%2" == "" goto usage

if "%1" == "/?" goto usage
if "%1" == "-h" goto usage

if "%1" == "win7" (
    set __OS=%1
    set __BOS=%1
    goto OK_OS
)
if "%1" == "wlh" (
    set __OS=%1
    set __BOS=2008
    goto OK_OS
)

echo BuildOne: bad OS type '%1' use one of win7, wlh
exit /B 1

:OK_OS
set __ARCH=all

rem process build option
if "%2" == "" goto usage
if "%2" == "all" goto OK
if "%2" == "allnoforce" goto OK
if "%2" == "allnf" goto allf
if "%2" == "allf" (
:allf
    if "%3" == "" goto usage
    if exist "%3" goto OK
	echo %0 Err - path .\%3 does not exist?
	exit /B 1
)
if "%2" == "compile" goto OK
if "%2" == "compilenoforce" goto OK
if "%2" == "compf" goto allf
if "%2" == "compnf" goto allf
if "%2" == "makebin" goto OK
if "%2" == "msi" goto OK
if "%2" == "sign" goto OK
if "%2" == "wix" goto OK
if "%2" == "clean" goto OK

echo Unknown option '%2' ?

:usage

echo usage: BldOS OS cmd {build-this-path}
echo   OS - [win7,wlh]
echo  'cmd' may be:
echo    all - force recompile, install binaries to WIX tree(makebin),
echo          sign drivers and build one installer.
echo    allnoforce - recompile only if needed, makebin, sign drivers and 
echo                 build one installer.
echo    allf path - force recompile the specified folder, makebin,
echo                sign drivers and build installers.
echo    alnf path - No-force recompile (all arch*) specified folder
echo    compile - force a recompile/link of everything then exit.
echo    compilenoforce - recompile/link only if needed then exit.
echo    compf path - force recompile (all arch*) specified folder
echo    compnf path - No-force recompile (all arch*) specified folder
echo    makebin - assumes binaries are built, installs binaries to WIX tree
echo              then exit.
echo    msi - assumes binaries are installed in WIX tree, signs drivers and
echo          create installers (.msi files) in IDIR.
echo    sign - assumes binaries are built and installed, sign drivers, exit.
echo    wix - build .msi installers, assumes (drivers signed) .cat files exist
echo    clean - remove build artifacts for a clean build: .obj, .sys, ...
echo.
echo		example: BldOS wlh all
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
    exit /B 1
)

set WIX=%CD%\OFED\WIX

rem Wix 3.5 tools
set WIX_BIN=wix35-binaries

if "%__OS%" == "win7" (
    set RBIN=%WIX%\win7\bin%
    goto OK_RBIN
)
if "%__OS%" == "wlh" (
    set RBIN=%WIX%\wlh\bin%
    goto OK_RBIN
)
echo %0: internal error - bad OS value '%__OS% ?
exit /B 1

:OK_RBIN

rem remove build artifacts
if "%2" == "clean" (
    echo Removing build artifacts for %__OS% %__ARCH%

    call %CD%\etc\clean-build.bat %__OS% %__ARCH%

    for %%a in ( x64 ia64 x86 ) DO (
        if exist %IDIR%\OFED_%__OS%_%%a.msi (
            echo  Removing %IDIR%\OFED_%__OS%_%%a.msi
            del /F %IDIR%\OFED_%__OS%_%%a.msi
        )
    )
    exit /B 0
)

rem Driver Signing Certificate filename, assumes Microsoft cross-cert file
rem %WIX%\%CERTFILE% is valid.
rem set CERTFILE=noCert
set CERTFILE=MSCV-VSClass3.cer

set SW_PUBLISHER="OpenFabrics Alliance"

rem A Digital driver signing certificate store name may be required.

if "%2" == "all" goto chk_cert
if "%2" == "allf" goto chk_cert
if "%2" == "msi" goto chk_cert
if "%2" == "sign" goto chk_cert

goto cert_OK

:chk_cert
    if "%CERTFILE%" == "noCert" set /P CERTFILE=[Enter Cross Certificate FileName] 
    if "%CERTFILE%" == "" (
        echo %0
        echo %0: Err - MS cross certificate %CERTFILE% required.
        echo %0: see certmgr.exe
        exit /B 1
    )
)

:cert_OK

set WIN7=yes

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
    set _PSDK=C:\PROGRA~1\MICROS~3\Windows\v6.1
)
if NOT EXIST %_PSDK% (
    echo Missing PLATFORM SDK @ %_PSDK%
    exit /B 1
)

if not DEFINED ND_SDK_PATH (
    set ND_SDK_PATH=C:\PROGRA~1\MI1D7C~1\NetDirect
    set ND_INC=%ND_SDK_PATH%\Include
)
if NOT EXIST %ND_SDK_PATH% (
    echo Missing Network Direct SDK @ %ND_SDK_PATH%
    exit /B 1
)

rem SVN value is used as part of the file version # string
rem set USE_SVN to be the current svn commit number.
rem if 'file' then use file OFED\WIX\build_SVN.txt as the definition.

rem set USE_SVN=1748
set USE_SVN=file

if "%2" == "allf" (
:fp
	set FPATH=%3
	goto svn
)
if "%2" == "compf" goto fp
if "%2" == "compnf" goto fp

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

echo %0 - Building %__OS% with WDK @ %_DDK%
echo Building for OPENIB_REV %SVN%, installer files (.msi) @ %IDIR%
echo   Drivers signed using Certificate '%CERTFILE%'

rem pause thoughtfully.
if exist %windir%\system32\timeout.exe (
    timeout /T 10
) else (
    pause
)

if NOT EXIST "%IDIR%" (
    echo %0 - Missing Installer file destination folder
	echo %0     %IDIR%
    exit /B 1
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

if "%2" == "wix" (
    set MSI_CMD=msi
    goto mk_msi
)

if "%2" == "makebin" goto InstallBin

if "%2" == "sign" (
    set MSI_CMD=%2
    goto do_msi_chk
) else (
    set MSI_CMD=all
)
if "%2" == "msi" goto do_msi_chk
goto compile

:do_msi_chk

rem make sure building a msi has files to work with.
if not EXIST "%RBIN%"  goto InstallBin
goto mk_msi

:compile

set OPS=-wgcPM 3
if "%2" == "allnoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%2" == "compilenoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%2" == "allf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%2" == "compf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%2" == "compnf" (
    rem Force Compile everything
    set OPS=-wgPM 3
)

if "%2" == "all" (
    echo Removing build artifacts and folders for %__OS% %__ARCH%
    call %CD%\etc\clean-build.bat %__OS% %__ARCH%
)

rem ************ Setup Env for Building 

set WDK_PATH=%_DDK%
set WINOF_PATH=%CD%
set OPENIB_REV=%SVN%
if not DEFINED PLATFORM_SDK_PATH  set PLATFORM_SDK_PATH=%_PSDK%

rem Compile in a specific folder? compf | compnf | allf
if EXIST "%FPATH%" pushd %FPATH%

rem ********* Compile for all architectures

echo %0 - Build %__OS% Free

%COMSPEC% /C "%BSE%\etc\bldwo.bat fre x86 %__BOS% %OPS%"
if ERRORLEVEL 1 exit /B 1

%COMSPEC% /C "%BSE%\etc\bldwo.bat fre x64 %__BOS% %OPS%"
if ERRORLEVEL 1 exit /B 1
%COMSPEC% /C "%BSE%\etc\bldwo.bat fre ia64 %__BOS% %OPS%"
if ERRORLEVEL 1 exit /B 1

rem compnf | compf | allf
if EXIST "%FPATH%" popd

if "%2" == "compf" goto finito
if "%2" == "compnf" goto finito
if "%2" == "compile" goto finito
if "%2" == "compilenoforce" goto finito

rem Install binaries into WIX environment, build msi installers.

:InstallBin

echo Create binary release tree - suitible for WinOF-WIX installer build.

if EXIST "%RBIN%"   (rmdir /S /Q %RBIN% &  echo %0 - removed %RBIN%)

mkdir %RBIN%

for %%a in ( x64 x86 ia64 ) DO (
    call %MKBIN% %BSE% %RBIN% %__OS% %%a %_DDK% %_COIN_VER%
    if ERRORLEVEL 1 (
        echo %0: Err in %MKBIN% %BSE% %RBIN% %__OS% %%a %_DDK% %_COIN_VER%
        exit /B 1
    )
)

if "%2" == "makebin" goto finito

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

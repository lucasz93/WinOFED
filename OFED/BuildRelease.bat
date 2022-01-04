@echo off
setlocal
rem tabstop=4

rem version: 2.5.0

rem EXAMPLE - Build entire winOFED release & WIX installers (.msi) files.
rem           Binary release is constructed in OFED\Wix\OS\bin.
rem           Processor architecture specific WIX installers are constructed
rem           in %IDIR% + %IDIR%\Checked
rem
rem BuildRelease option
rem  option: all | allnoforce | allf | allnf | compile | compilenoforce |
rem          compf path | compnf path | makebin | msi |sign | wix | clean |
rem          msi-label | msi-del | msi-dir {OPENIB_REV}

rem This script is an 'example' of a one-command entire IB stack build to
rem single-file installer; used to build a OFED releases.
rem Script is designed to be invoked from the <whatever>\gen1\trunk folder with
rem <whatever>\gen1\trunk\OFED\Wix\* present.
rem
rem Verify the following env vars are suitible for your system configuration.
rem     _DDK, _PSDK, SVN, IDIR, CERTFILE, SW_PUBLISHER

rem 'nf path' command variants are required due to a bug in the WDK build env.
rem ipoib\ & ipoib_ndis6_cm\ both build ipoib.sys just for different OS
rem versions. The problem arises when a compile is forced on one folder or the
rem other, all instances of ipoib.sys are deleted

rem WIX Installer files (.msi) destination folder - set for local environment.
set IDIR=%SystemRoot%\temp

if "%1" == "" goto usage
if "%1" == "/?" goto usage
if "%1" == "-h" goto usage
if "%1" == "all" goto OK
if "%1" == "allnoforce" goto OK
if "%1" == "allf" (
:allf
    if "%2" == "" goto usage
    set FPATH=%2
    if exist "%2" goto OK
	echo %0 Err - path .\%2 does not exist?
	exit /B 1
)
if "%1" == "allnf" goto allf
if "%1" == "compile" goto OK
if "%1" == "compilenoforce" goto OK
if "%1" == "compf" (
:cpf
    if "%2" == "" goto usage
    set FPATH=%2
    if exist "%2" goto OK
	echo %0 Err - path .\%2 does not exist?
	exit /B 1
)
if "%1" == "compnf" goto cpf
if "%1" == "makebin" goto OK
if "%1" == "msi" goto OK
if "%1" == "sign" goto OK
if "%1" == "wix" goto OK
if "%1" == "clean" goto OK
if "%1" == "msi-label" goto OK
if "%1" == "msi-del" goto OK
if "%1" == "msi-dir" goto OK

echo Unknown arg '%1' ?

:usage

echo "usage: BuildRelease command {OPENIB_REV value}"
echo where 'command' may be:
echo    all - force recompile, install binaries to WIX tree(makebin),
echo          sign drivers and build installers.
echo    allnoforce - recompile only if needed, makebin, sign drivers and 
echo                 build installers.
echo    allf path - force recompile the specified folder, makebin,
echo                sign drivers and build installers.
echo    allnf path - recompile specified folder ONLY if required, makebin,
echo                sign drivers and build installers.
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
echo    msi-label {OPENIB_REV}
echo           rename WOF_os*_arch*.msi to WOF_os*_arch*_svn#.msi 
echo           Uniquely identify installers just created.
echo           If OPENIB_REV arg used, then rename WOF_os*_arch*argVal.msi
echo           otherwise, use SVN# from path.
echo    msi-del - del %windir%\temp\WOF_os*_arch*.msi
echo    msi-dir - del %windir%\temp\WOF_os*_arch*.msi
echo :
echo    {OPENIB_REV}
echo       optional, if set then OPENIB_REV is assigned this value.
echo		example: BuildRelease all 1414

exit /B 1 

:OK

if not "%WDM_INC_PATH%" == "" (
    echo %0: Error - %0 unable to run from WDK window,
    echo     use %comspec%
    exit /B 1
)

set SH=%COMSPEC% /V:on /E:on /C
rem assumes %CD% == '<...>\gen1\trunk'
set BSE=%CD%
set WIX=%CD%\OFED\WIX

rem Setup Wix 3.5 tools
set WIX_BIN=wix35-binaries

set RBIN_W7=%WIX%\win7\bin%
set RBIN_W8=%WIX%\win8\bin%

rem remove build artifacts
if "%1" == "clean" (
    echo Removing build artifacts and folders...
    call %CD%\etc\clean-build.bat
    if exist %WIX%\win7\bin (
		echo Removing %WIX%\win7\bin
		rmdir /Q /S %WIX%\win7\bin
    )
    if exist %WIX%\win8\bin (
		echo Removing %WIX%\win8\bin
		rmdir /Q /S %WIX%\win8\bin
    )
    exit /B 0
)

rem Driver Signing Certificate filename, assumes %WIX%\%CERTFILE% is valid.
rem set CERTFILE=noCert
set CERTFILE=MSCV-VSClass3.cer
set SW_PUBLISHER="OpenFabrics Alliance"

rem A Digital driver signing certificate store name may be required.

if "%1" == "all" goto chk_cert
if "%1" == "allf" goto chk_cert
if "%1" == "allnf" goto chk_cert
if "%1" == "msi" goto chk_cert
if "%1" == "sign" goto chk_cert

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

rem WDK setup

rem Windows 7 WDK 7600_1, 7600_0 was the last.
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
    rem Svr 2008 ND SDK ND_SDK_PATH=C:\PROGRA~1\MICROS~4\NetworkDirect
	rem R2 ND DDK @ C:\Program Files\Microsoft HPC Pack 2008 R2 SDK\NetDirect 
	rem ND DDK unzipped by hand, not explicit part of ND SDK R2 install; by design.
    set ND_SDK_PATH=C:\PROGRA~1\MI1D7C~1\NetDirect
)

if NOT EXIST %ND_SDK_PATH% (
    echo Missing Network Direct SDK @ %ND_SDK_PATH%
    exit /B 1
)

rem SVN value is used as part of the file version # string
rem set USE_SVN to be the current svn commit number.
rem If 'file' then use OFED\WIX\build_SVN.txt for SVN definition

rem set USE_SVN=1748
set USE_SVN=file

rem %2 can be either a file spec or OPENIB_REV value.
rem Based on %1 command, FPATH will/will-not be set to a file spec.

if Not "%FPATH%" == "" goto svn

rem setup value for OPENIB_REV assignment
if not "%2" == "" (
	rem set SVN commit number.
   	set SVN=%2
   	set LBL=%2
	goto svn_set
)

:svn

rem SVN value is used as part of the file version # string
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

:svn_set

if NOT EXIST "%WIX%\build-all-MSI.bat" (
    echo %0 - Missing .msi installer build script
    echo    %WIX%\build-all-MSI.bat
    exit /B 1
)

if "%1" == "msi-label" (
    pushd %IDIR%
    if exist WOF_wlh_x86.msi (
        if exist WOF_wlh_x86%LBL%.msi del /F/Q WOF_wlh_x86%LBL%.msi
        rename WOF_wlh_x86.msi WOF_wlh_x86%LBL%.msi
    )
    if exist WOF_wlh_x64.msi  (
        if exist WOF_wlh_x64%LBL%.msi del /F/Q WOF_wlh_x64%LBL%.msi
        rename WOF_wlh_x64.msi WOF_wlh_x64%LBL%.msi
    )
    if exist WOF_wlh_ia64.msi  (
        if exist WOF_wlh_ia64%LBL%.msi del /F/Q WOF_wlh_ia64%LBL%.msi
        rename WOF_wlh_ia64.msi WOF_wlh_ia64%LBL%.msi
    )
    dir WOF_*%LBL%.msi
    popd
    exit /B 0
)

if "%1" == "msi-del" (
    echo Deleting OFED_{win7,wlh}_{x86,x64,ia64}%LBL%.msi
    pushd %IDIR%
    if exist OFED_win7_x86%LBL%.msi del /F/P OFED_win7_x86%LBL%.msi
    if exist OFED_win7_x64%LBL%.msi del /F/P OFED_win7_x64%LBL%.msi
    if exist OFED_win7_ia64%LBL%.msi del /F/P OFED_win7_ia64%LBL%.msi

    if exist OFED_wlh_x86%LBL%.msi del /F/P OFED_wlh_x86%LBL%.msi
    if exist OFED_wlh_x64%LBL%.msi del /F/P OFED_wlh_x64%LBL%.msi
    if exist OFED_wlh_ia64%LBL%.msi del /F/P OFED_wlh_ia64%LBL%.msi

    dir /N/OD WOF_*.msi
    popd
    exit /B 0
)

if "%1" == "msi-dir" (
    pushd %IDIR%
    dir /N/OD WOF_*.msi
    popd
    exit /B 0
)

echo %0 - Building with WDK @ %_DDK%
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
rem   <...>\OFED\WIX\WIX_tools\
rem
if NOT EXIST %WIX%\WIX_tools\%WIX_BIN% (
    echo %0 - Missing WIX tools @ %WIX%\WIX_tools\%WIX_BIN% 
    exit /B 1
)

set MKBIN=%BSE%\etc\makebin.bat

if NOT EXIST "%MKBIN%" (
    echo %0 - Missing %MKBIN%, script must run from trunk\ equivalent.
    exit /B 1
)

set STIME=%TIME%

rem skip build - assumes binaries already built and installed.

if "%1" == "wix" (
    set MSI_CMD=msi
    goto mk_msi
)

if "%1" == "makebin" goto InstallBin

rem poor man's OR
if "%1" == "sign" (
    set MSI_CMD=%1
    goto do_msi_chk
) else (
    set MSI_CMD=all
)
if "%1" == "msi" goto do_msi_chk
goto compile

:do_msi_chk

rem make sure building a msi has files to work with.
if not EXIST "%RBIN_W7%"  goto InstallBin

rem WIN8 enable XXX
rem if not EXIST "%RBIN_W8%"  goto InstallBin

if EXIST "%IDIR%\checked" (
    rmdir /S /Q %IDIR%\checked
)
mkdir %IDIR%\checked
goto mk_msi

:compile

set OPS=-wgcPM 3
if "%1" == "allnoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%1" == "compilenoforce" (
    rem Compile everything only if needed.
    set OPS=-wgPM 3
)
if "%1" == "allf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%1" == "allnf" (
    rem Compile only if necessary
    set OPS=-wgPM 3
)
if "%1" == "compf" (
    rem Force Compile everything
    set OPS=-wgcfPM 3
)
if "%1" == "compnf" (
    rem Compile only if necessary
    set OPS=-wgPM 3
)

if "%1" == "all" (
    echo Removing build artifacts and folders...
    call %CD%\etc\clean-build.bat
    if exist %WIX%\win7\bin (
		echo Removing %WIX%\win7\bin
		rmdir /Q /S %WIX%\win7\bin
    )
    if exist %WIX%\win8\bin (
		echo Removing %WIX%\win8\bin
		rmdir /Q /S %WIX%\win8\bin
    )
)

rem ************ Setup Env for Building 

set WDK_PATH=%_DDK%
set WINOF_PATH=%CD%
set OPENIB_REV=%SVN%
if not DEFINED PLATFORM_SDK_PATH  set PLATFORM_SDK_PATH=%_PSDK%

rem Compile in a specific folder? compf | compnf | allf | allnf
if EXIST "%FPATH%" pushd %FPATH%

rem ********* Compile for win7 - Windows 7

rem win7 x86
echo %0 - Build win7 x86 Checked
%COMSPEC% /C "%BSE%\etc\bldwo.bat chk x86 win7 %OPS%"
if ERRORLEVEL 1 exit /B 1

echo %0 - Build win7 x86 Free
%COMSPEC% /C "%BSE%\etc\bldwo.bat fre x86 win7 %OPS%"
if ERRORLEVEL 1 exit /B 1

rem win7 x64
echo %0 - Build win7 x64 Checked
%COMSPEC% /C "%BSE%\etc\bldwo.bat chk x64 win7 %OPS%"
if ERRORLEVEL 1 exit /B 1

echo %0 - Build win7 x64 Free
%COMSPEC% /C "%BSE%\etc\bldwo.bat fre x64 win7 %OPS%"
if ERRORLEVEL 1 exit /B 1

rem compnf | compf | allf | allnf
if EXIST "%FPATH%" popd

if "%1" == "compf" goto finito
if "%1" == "compnf" goto finito
if "%1" == "compile" goto finito
if "%1" == "compilenoforce" goto finito

rem Install binaries into WIX environment, build msi installers.

:InstallBin

echo Create binary release tree - suitible for OFED-WIX installer build.

if EXIST "%RBIN_W7%"   (rmdir /S /Q %RBIN_W7% &  echo %0 - removed %RBIN_W7%)
mkdir %RBIN_W7%

for %%i in ( x86 x64 ) do (
    call %MKBIN% %BSE% %RBIN_W7% win7 %%i %_DDK% %_COIN_VER% free
    if ERRORLEVEL 1 (
       echo %0: Err in %MKBIN% %BSE% %RBIN_W7% wlh %%i %_DDK% %_COIN_VER%
       exit /B 1
    )
)

if "%1" == "makebin" goto finito

:mk_msi

echo %0 - Drivers Signed with %CERTFILE%
echo   Binary release trees in
echo     %RBIN_W7%
echo     %RBIN_W8%

rem build Free WIX installers --> see OFED\WIX

%SH% "%_DDK%\bin\setenv.bat %_DDK% fre X64 win7 no_oacr & cd /D %WIX% & build-all-MSI %MSI_CMD% %CERTFILE% %SW_PUBLISHER% %IDIR% win7 x64"

%SH% "%_DDK%\bin\setenv.bat %_DDK% fre X64 win7 no_oacr & cd /D %WIX% & build-all-MSI %MSI_CMD% %CERTFILE% %SW_PUBLISHER% %IDIR% win7 x86"


rem Build Checked installers in \checked, debug version already compiled (above)
echo %0 - Building win7 Checked
for %%o in ( x64 x86 ) do (
  %COMSPEC% /C "cd /D %BSE% & OFED\bld1 win7chk %%o makebin"
  %COMSPEC% /C "cd /D %BSE% & OFED\bld1 win7chk %%o msi"
)

:finito

echo .
echo %0: Finished %0 %*
echo %0:   Started  %STIME%
echo %0:   Finished %TIME%

endlocal

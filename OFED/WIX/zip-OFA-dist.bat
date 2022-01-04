@echo off
setlocal
rem
rem ****WARNING - this script is 'generally' invoked by 'build-OFA-dist.bat'
rem
rem Construct & populate an OFA-OFED distribution tree to be used at
rem  the OFA-OFED download site.
rem    http://www.openfabrics.org/downloads/OFED/
rem
rem  ASSUMES %CD% gen1\branches\OFED\Wix
rem
rem zip-OFA-dist release_ID OSname {target_path}
rem
rem  example zip-OFA-dist 1_1 C:\tmp
rem	# if target_path is null then default %SystemRoot%\temp\v%1
rem     # otherwise %3\v%1

if "%1" == "" (
  echo "Missing release ID, example %0 1.1"
  echo "usage: %0 release_ID OSname {target_path, default: %SystemRoot%\temp}
  exit /B 1
)

set RID=%1
if not "%3" == "" (
  set DST=%3\v%RID%_%2
) else (
  set DST=%SystemRoot%\temp\v%RID%_%2
)

rem where gen1\trunk\buildRelease.bat created the .msi installer files.
set MSI=%SystemRoot%\temp

rem MUST execute from gen1\branches\OFED\WIX
set RBIN=%CD%\%2\bin

if NOT EXIST %RBIN%\bin (
    echo Must execute from gen1\branches\OFED\WIX -- and --
    echo .\bin\ must be populated - run gen1\trunk\buildRelease.bat
    exit /B 1
)

set PGM="C:\Program Files\Debugging Tools for Windows 64-bit"\symstore.exe
set SS=%DST%\SymStor

if NOT EXIST %PGM% (
    echo %0 - Missing installation of MS Debug tools @
    echo   %PGM%
    exit /B 1
)

rem Assumption is the arch specific installer zip packages have been previously
rem constructed.

set WIXLAND=%CD%

if not EXIST OFED_%RID%_%2_x86.zip (
    echo %0 - missing OFED arch package: OFED_%RID%_%2_x86.zip  
    exit /B 1
)
if not "%2" = "wxp" (
    if not EXIST OFED_%RID%_%2_x64.zip (
        echo %0 - missing WinOF zip package: OFED_%RID%_%2_x64.zip  
        exit /B 1
    )
    if not EXIST OFED_%RID%_%2_ia64.zip (
        echo %0 - missing OFED arch package: OFED_%RID%_%2_ia64.zip  
        exit /B 1
    )
)

echo "Creating OFED downloadable distribution in"
echo "   %DST%"
echo " from binaries in"
echo "   %RBIN%"

if EXIST "%DST%" (
    echo rmdir %DST%
    rmdir /S /Q %DST%
)
if EXIST "%DST%" (
    echo %0 - Failed to remove %DST%
    exit /B 1
)
mkdir %DST%
if ERRORLEVEL 1 (
    echo "%0 - %DST% error %ERRORLEVEL% ?"
    exit /B 1
)

mkdir %SS%
mkdir %DST%\Installers

echo Generating Symbol store

%PGM% add /r /f %RBIN% /s %SS%  /t "OFED" /v "version %RID%"
if ERRORLEVEL 1 (
    echo symstore.exe failure rc %ERRORLEVEL%
    exit /B 1
)

echo Generating arch releases

copy OFED_%RID%_%2_x86.zip %DST%\Installers
if not "%2" = "wxp" (
    copy OFED_%RID%_%2_x64.zip %DST%\Installers
    copy OFED_%RID%_%2_ia64.zip %DST%\Installers
)

echo "%0 - Results in %DST%"

endlocal
exit /B 0

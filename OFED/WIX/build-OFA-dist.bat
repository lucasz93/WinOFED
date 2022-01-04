@echo off
setlocal

rem version 1.6

rem TabStop=4

rem Create a ZIP file of an OFED distribution, such that the zip file can be
rem pushed to the OFA OFED for windows download site and unzipped for
rem distribution.

rem Populate arch specific distribution folders, zip'em, populate symbols 
rem folder and zip the entire package for transmission to OFA download website.
rem calls .\zip-OFA-dist.bat script.

rem Operating assumptions:
rem 1) current arch specific installers are in %systemroot%\temp\*.msi
rem    resultant from trunk\buildrelease.bat execution.
rem
rem  ASSUMES %CD% ==  <...>\OFED\Wix
rem
rem build-OFA-dist release_ID {target_path}
rem
rem  example build-OFA-dist 1-1 %windir%\temp
rem	# if target_path is null then default %SystemRoot%\temp\v%1
rem     # otherwise %2\v%1

rem cmd.exe /V:on (delayed environment variable expansion) is required!
set F=on
set F=off
if not "!F!" == "off" (
   echo Err: cmd.exe /V:on [delayed environment variable expansion] Required!
   exit /B 1
)

if "%1" == "" (
:usage
  echo "Missing release ID, example %0 1-1"
  echo "usage: %0 release_ID {Full_target_path, default: %windir%\temp}
  exit /B 1
)

if "%1" == "/?" goto usage

set ID=%1

rem Final zip archive name
set ZIP=OFED_dist_v%ID%.zip

rem where gen1\trunk\buildRelease.bat created the .msi installer files.
rem or where specified. goto's resultant of MSI not correctly set inside
rem of if clause?

if Not "%2" == ""  goto non_std_msi

set MSI=%SystemRoot%\temp
goto msi_ok

:non_std_msi
set MSI=%2

:msi_ok:

if NOT EXIST "%MSI%" (
    echo Folder %MSI% does not exist?
    exit /B 1
)

if NOT EXIST "%MSI%\checked" (
    echo Folder %MSI%\checked does not exist?
    exit /B 1
)

set MSI_DST=%MSI%\v%ID%

rem start fresh
if exist %MSI_DST%  rmdir /S /Q %MSI_DST%
mkdir %MSI_DST%
if ERRORLEVEL 1 (
    echo Err - unable to create %MSI_DST% ?
    exit /B 1
)

mkdir %MSI_DST%\Checked_Installers
if ERRORLEVEL 1 (
    echo Err - unable to create %MSI_DST%\Checked_Installers ?
    exit /B 1
)
copy /B/Y %MSI%\checked\*.msi %MSI_DST%\Checked_Installers\ 
if ERRORLEVEL 1 (
    echo Err - unable to copy %MSI%\checked\*.msi %MSI_DST%\Checked_Installers ?
    exit /B 1
)

mkdir %MSI_DST%\SymStor

set WZ="C:\Program Files (x86)\WinZip\WZZIP.EXE"
if not exist %WZ% (
    echo "Missing WinZip pro [cmd-line interface]"
    echo "Please manually create the archives."
    exit /B 1 
)

set SYMST=%_NTDRIVE%\%_NTROOT%\Debuggers\symstore.exe
if not exist %SYMST% (
    echo %0 - Missing installation of MS Debug tools @
    echo      %SYMST%
    echo      Use a WDK build window [%_NTROOT% is valid].
    echo      Install latest MS WDK [Windows Driver Kit] package
    echo        www.microsoft.com/whdc/Devtools/wdk/default.mspx 
    echo         or www.microsoft.com/whdc/devtools/debugging/install64bit.mspx
    exit /B 1 
)

FOR %%s IN ( win7 ) DO (
    if exist %CD%\%%s\bin\Misc\Manual.htm (
		set OSF=%%s !OSF!
	)
)
echo Packaging installers for !OSF!

if "!OSF!" == "" (
    echo "Missing components?"
    echo "Must execute from ...\OFED\WIX -- and --"
    echo "  .\win7\bin\ must be populated."
    echo "  run ...\OFED\BuildRelease.bat -or- ...\etc\makebin"
    exit /B 1
)

FOR %%s IN ( !OSF! ) DO (
	echo Building %%s installers.

    rem create target structure
    mkdir %MSI_DST%\%%s
    if ERRORLEVEL 1 (
        echo Err - unable to create %MSI_DST%\%%s\... ?
        exit /B 1
    )

    set DSTx86=OFED_%ID%_%%s_x86
    set DSTx64=OFED_%ID%_%%s_x64

    echo Building target !DSTx86!

    if exist !DSTx86!  rmdir /S /Q !DSTx86! 
    mkdir !DSTx86!

    IF NOT EXIST !DSTx86! (
        echo Unable to create !DSTx86! ?
        exit /B 1
    )

    copy README_release.txt !DSTX86!\README.txt
    if ERRORLEVEL 1 (
        echo Err - missing file README_release.txt ?
        exit /B 1
    )

    copy /B openfabrics.gif !DSTX86!\openfabrics.gif
    copy release_notes.htm !DSTX86!\release_notes.htm

    rem would like to use a goto although the target label destroys the
    rem scope for %%s ... sigh.
    rem Scope is preserved by if not

    echo Building target !DSTx64!
    if exist !DSTx64!\.   rmdir /S /Q !DSTx64! 
    mkdir !DSTx64!

    if NOT EXIST !DSTx64! (
        echo Unable to create !DSTx64! ?
        exit /B 1
    )

    copy README_release.txt !DSTX64!\README.txt
    copy /B openfabrics.gif !DSTX64!\openfabrics.gif
    copy release_notes.htm !DSTX64!\release_notes.htm

    echo Copying installers for %%s

    copy /B /Y %MSI%\OFED_%%s_x86.msi !DSTx86!\OFED_%ID%_%%s_x86.msi
    if ERRORLEVEL 1 (
        echo Err - unable to copy %MSI%\OFED_%%s_x86.msi
        exit /B 1
    )

    copy /B /Y %MSI%\OFED_%%s_x64.msi !DSTx64!\OFED_%ID%_%%s_x64.msi
    if ERRORLEVEL 1 (
        echo Err - unable to copy %MSI%\OFED_%%s_x64.msi
        exit /B 1
    )

    echo Building ZIP archives of the architecture specific folders

    if EXIST !DSTx86!.zip   del /F /Q !DSTx86!.zip
    if EXIST !DSTx64!.zip   del /F /Q !DSTx64!.zip

    %WZ% -P -r %MSI_DST%\%%s\!DSTx86!.zip !DSTx86!
    if ERRORLEVEL 1 (
        echo Err - unable to create %MSI_DST%\%%s\!DSTx86!.zip
        exit /B 1
    )

    %WZ% -P -r %MSI_DST%\%%s\!DSTx64!.zip !DSTx64!
    if ERRORLEVEL 1 (
        echo Err - unable to create %MSI_DST%\%%s\!DSTx64!.zip
        exit /B 1
    )

    rem create the symbol store for the OS flavor
    echo Generating %MSI_DST%\SymStor Symbol store

    %SYMST% add /r /f %CD%\%%s\bin\bin /s %MSI_DST%\SymStor  /t "WinOFED" /v "version %ID%"
    if ERRORLEVEL 1 (
        echo %%s symstore.exe failure rc %ERRORLEVEL%
        exit /B 1
    )
    rem OS flavor cleanup

    if exist !DSTx86!\.    rmdir /S /Q !DSTx86!
    if exist !DSTx64!\.    rmdir /S /Q !DSTx64!
)

rem create a ZIP file of the entire distribution

echo OFED v%ID% distribution @ %ZIP%

pushd %MSI%

rem rename MS Operating System code-names to common Retail names
pushd v%ID%
rename win7 Win7-or-Svr_2008_R2-or-HPC_Edition
popd

IF EXIST %ZIP%  del /F/Q %ZIP%

%WZ% -P -r %ZIP% v%ID%

rmdir /Q /S v%ID%

popd

echo.
echo -----
echo OFED v%ID% distribution @ %MSI%\%ZIP%
echo -----

endlocal


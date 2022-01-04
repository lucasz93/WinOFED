@echo off
setlocal
rem Build WIX installers (.msi) - OFED for Windows distribution
rem
rem usage:
rem   %0 cmd CrossCert SW_PUB msi-dest {OS ARCH}
rem	 cmd - if 'msi' then assume drivers already signed, only sign .msi
rem		otherwise 'sign' indicates sign all drivers and installers (.msi files)
rem	 CrossCert - a Cross Certificate filename only
rem  SW_PUB - Software Publisher name in 'MY' Cert Store, see signtool /n switch
rem  msi-dest - full path to folder where .msi files are written.
rem  OS - target Windows OS: all, wlh, win7
rem  ARCH - target architecture: all, x86, x64, ia64

rem *** REQUIRES nmake, common invocation from Visual C or WDK command window
rem *** Assumes current folder is WIX\

set USE=usage build-all-msi all/msi CrossCert-Fname SW_Publisher[see signtool /n] msi-path {target-OS[all,wlh,win7] target-arch[all,x86,x64,ia64]}

set TS=/t http://timestamp.verisign.com/scripts/timstamp.dll

if "%1" == "" (
    echo %USE%
    exit /B 1
)

if "%1" == "all" (
    goto ok
)
if "%1" == "msi" (
    goto ok
)
if "%1" == "sign" (
    goto ok
)
echo %0 - Unknown command '%1' - 'all' or 'msi'
echo %0: %USE%
exit /B 1

:ok

if NOT EXIST %2 (
    echo %0: Certificate-Filename not found?
    echo %0 -   %2
    exit /B 1
)

rem need a Cert subject name string - name is passed-in quoted!
if %3 == "" (
    echo %0: %USE%
    exit /B 1
)

if "%4" == "" (set DST=%windir%\temp) else (set DST=%4)

if "%5" == "all"  (
    rem Type-Of-OperatingSystem
    set TOS=win7 wlh
    rem Selection
    set OSarg=all
    goto OK_OS
)

if "%5" == "win7" goto os_valid
if "%5" == "wlh" goto os_valid

echo %0 - Bad OS '%5'?, abort.
exit /B 1

:os_valid

set TOS=%5
set OSarg=%5

:OK_OS

if "%6" == "all"  (
    set TARCH=x86 x64 ia64
    set ARCHarg=all
    goto OK_arch
)

if "%6" == "x86"  goto OK_valid
if "%6" == "x64"  goto OK_valid
if "%6" == "ia64"  goto OK_valid
echo %0 - BAD arch '%6'?
exit /B 1

:OK_valid

set TARCH=%6
set ARCHarg=%6

:OK_arch

echo.
echo Building installers for %TOS% %TARCH%

if NOT EXIST %DST% (
    echo %0: Installer output path %DST% not found?
    exit /B 1
)

rem make sure nmake is accessible in our path
where -q nmake
if ERRORLEVEL 1 (
    echo %0: nmake not in PATH?
    exit /B 1
)

if "%1" == "msi" goto mk_msi

rem Sign drivers for specified OSes & arches. Convert CertFilename to full path.

call sign-all-drivers %CD%\%2 %3 %OSarg% %ARCHarg%

if ERRORLEVEL 1 (
    echo %0: Error signing drivers?
    exit /B 1
)

if "%1" == "sign" (
    echo %0: Drivers Signed. 
    exit /B 0
)

:mk_msi

rem build x86, x64 & ia64 Installers for each of
rem	   Windows 7/Server 2008 R2
rem    Vista/Server 2008

echo %0 - Building About file
call mk-about.bat

echo %0 - Building .msi files

for %%o in ( %TOS% ) do (
	if Not exist %%o\bin\HCA (
		echo Missing %%o files?
		exit /B 1
	)
	pushd %%o
	call build-MSI %DST% %ARCHarg%
	if ERRORLEVEL 1 exit /B 1
	popd
)

rem Digitally Sign the installer .msi files

echo %0 - Signing Installer .msi files
for %%o in ( %TOS% ) do (
    for %%a in ( %TARCH% ) do (
        if exist %DST%\OFED_%%o_%%a.msi (
            echo  Signing installer %DST%\OFED_%%o_%%a.msi
            signtool sign /ac %CD%\%2 /n %3 %TS% %DST%\OFED_%%o_%%a.msi
            if ERRORLEVEL 1 (
                echo %0 signtool sign %DST%\OFED_%%o_%%a.msi failed?
                exit /B 1
            )
            signtool verify /pa %DST%\OFED_%%o_%%a.msi
            if ERRORLEVEL 1 (
                echo %0 signtool verify %DST%\OFED_%%o_%%a.msi failed?
                exit /B 1
            )
        )
    )
)

dir %DST%\*.msi

echo.
echo Done - OFED installers in %DST%

@endlocal

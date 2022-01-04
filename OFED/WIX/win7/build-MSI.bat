@echo off
setlocal
rem Build WIX installer (.msi) for specified architecture(s)
rem
rem usage: %0 {dest-path-for-msi-files Arch}

rem *** REQUIRES nmake, common invocation from Visual C or DDK command window

set OS=win7

if "%1" == "" (
    set DST=%SystemRoot%\temp
) else (
    set DST=%1
)
if "%2" == "" (
:all_arch
    set ARCH=ia64 x64 x86
    goto OK_arch
)
if "%2" == "all"  goto all_arch
set ARCH=%2

:OK_arch

if NOT EXIST %DST% (
    echo %0: Installer output path %DST% not found?
    exit /B 1
)
where -q nmake 
if ERRORLEVEL 1 (
    echo %0 missing nmake.exe in PATH?
    exit /B 1
)

for %%a in ( %ARCH% ) do (
    if "%%a" == "x64" (set HWN=amd64) else (set HWN=%%a)
    if NOT EXIST %CD%\bin\HCA\!HWN! (
        echo %0 - %CD%\bin\HCA\!HWN! not populated correctly?, abort.
        exit /B 1
    )
    if EXIST %%a\OFED_%OS%_%%a.msi del /Q /F %%a\OFED_%OS%_%%a.msi
    pushd %%a
    nmake /NOLOGO full
    if ERRORLEVEL 1 (
        echo %0 - Error building OFED_%OS%_%%a.msi ?
        exit /B 1
    )
    echo move /Y OFED_%OS%_%%a.msi %DST%
    move /Y OFED_%OS%_%%a.msi %DST%
    popd
)

rem if run from top-level %1 will not be null, otherwise assume run from
rem cmd line.
if "%1" == ""   dir %DST%\*.msi

echo ----
echo Done - %OS% WIX installers in %DST%
echo ----

endlocal

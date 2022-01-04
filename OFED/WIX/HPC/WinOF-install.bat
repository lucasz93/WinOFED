@echo off
setlocal

rem Example Local winOFED install
rem all files in the current folder; setup by provisioning template.
rem excute under Administrator login.

set MSI=OFED_3-0_wlh_x64.msi

set TARGET=c:\WOF

cd /d %TARGET%
if ERRORLEVEL 1 (
    echo %0 - unable to CD to folder %TARGET% ?
    exit /B %ERRORLEVEL%
)

if not exist %MSI% (
    echo %0 Missing winOFED installer %MSI%, ABORT.
    exit /B 1
)

echo on
start/wait msiexec /I %MSI% /quiet /qn /Lv msi.log

@endlocal
@exit /B %ERRORLEVEL%


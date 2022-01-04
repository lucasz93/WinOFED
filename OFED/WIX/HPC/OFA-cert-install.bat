@echo off
rem Install OpenFabrics Alliance (OFA) Trusted SW Publisher certificate
rem in the node local TRUSTEDPUBLISHER certificate store.
rem This file is a no-args wrapper for rem-cert-ADD.bat; both in same folder.

setlocal
set OfACertID=05f3567a6252395d0a9ea1877a56f83d

rem This is where the provisioning step copied the WOF folder.
set TARGET=c:\WOF

cd /d %TARGET%
if ERRORLEVEL 1 (
    echo %0 - unable to CD to folder %TARGET% ?
    exit /B %ERRORLEVEL%
)

if not exist rem-cert-add.bat (
    echo %0 Missing file rem-cert-add.bat, ABORT.
    exit /B 1
)
rem Trusted Publisher cert file extracted from head-node
if not exist OFA_TP.cer (
    echo %0 Missing file OFA_TP.cert, ABORT.
    exit /B 1
)
echo on
call rem-cert-add.bat %OfACertID% %CD%\OFA_TP.cer

@if %ERRORLEVEL% NEQ 0  echo %0 exit with error %ERRORLEVEL%

@endlocal
@exit /B %ERRORLEVEL%


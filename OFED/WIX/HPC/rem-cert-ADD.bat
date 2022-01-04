@echo off
setlocal

rem Remote Certificate add

rem Install specified Digital Certificate in the local TrustedPublishers store.
rem Intended to be called from clusrun in cert-add.bat script.

rem	usage: %0 CertID TrustedPublisherCertFilename

where certutil > Nul
if ERRORLEVEL 1 (
    echo %0 Must be run on server 2008 HPC [clusrun.exe] ?
    exit /B 1
)

if "%1" == "" (
:usage
    echo usage: %0 CertID TrustedPublisherCertFilename
    echo designed to be called from cert-add.bat
    exit /B 1
)

rem test for cert already in the TrustedPublisher cert store
certutil -verifystore TRUSTEDPUBLISHER %1  1> Nul
if %ERRORLEVEL% EQU 0 (
    echo.
    echo %computername% OFA TrustedPublisher Cert already installed.
    exit /B 0
)
echo Installing %2 Cert on %computername%
echo.
certutil -f -addstore TRUSTEDPUBLISHER "%2" 1> Nul
if %ERRORLEVEL% EQU 0 (
    echo %computername% SUCCESS: OFA TrustedPublisher cert installed
) else (
    echo %computername% FAILURE: OFA TrustedPublisher cert install err %ERRORLEVEL%
    echo.
) 
endlocal

@echo off
setlocal

rem clusrun execute the 'rem-cert-ADD.bat' script on specified nodes to install the OFA
rem public cert in the remote node's TrustedPublisher Certificate Store.
rem Sets the 'OpenFabric Alliance' as a Trusted 3rd party Software Publisher which then
rem allows unattened winOFED installs to complete.
rem Assumes the OFA cert is in the local Trusted Publisher certificate Store.

if "%1" == "" (
:usage
    echo usage: %0 remote-node-visiable-share-path [remote-node-hostnames]
    echo   Valid ONLY for Server 2008 HPC.
    echo.
    echo Will use OFA Cert file named 'OFA_TP.cer' if present.
    echo Otherwise extract the OFA cert from the local Trusted Publisher store
    echo  for injection into remote node certificate store.
    echo.
    echo example: install OFA certificate on remote nodes.
    echo   %0 \\head-node\remShar cn1 cn2 cn3 cn4
    echo example: extract OFA certificate to \\head-node\remShar\OFA_TP.cer
    echo   %0 \\head-node\remShar
    exit /B 1
)

if "%2" == ""  echo cert extraction only.

where clusrun > Nul
if ERRORLEVEL 1 (
    echo %0 Must be run on server 2008 HPC [clusrun.exe] only?
    exit /B 1
)
where certutil > Nul
if ERRORLEVEL 1 (
    echo %0 missing file Windows utility certutil.exe ?
    exit /B 1
)

rem worker script file can be local or in the winOFED install folder.
set CISF=rem-cert-ADD.bat
if exist "%CISF%"  (
    set CIS="%CD%\%CISF%"
) else (
    set CIS="%ProgramFiles%\OFED\%CISF%"
)

if not exist %CIS% (
    echo missing winOFED script %CIS% ?
    exit /B 1
)

rem Certificate ID's (Serial Number) for the OFA winOFED driver signing cert

rem [expires 8/20/2013]
set OFA_CERT_ID=05f3567a6252395d0a9ea1877a56f83d

rem Openfabrics Alliance Trusted SW Publisher
set CERTNAME=OFA_TP.cer

if exist "%CERTNAME%" (
    echo Using OFA cert file %CERTNAME%
    goto have_cert
)

rem extract OFA cert from local Trusted Publisher Cert Store.
rem test for OFA cert in local TrustedPublisher cert store

certutil -store TRUSTEDPUBLISHER %OFA_CERT_ID%  1> Nul
IF %ERRORLEVEL% NEQ 0 (
    echo 'OpenFabrics Alliance' Trusted SW Publisher certificate not in local Cert Store.
    echo Install WinOF on head-node or Install OFA Cert from WinOF installer .msi file.
    echo Install OFA Cert from .msi file:
    echo Right-click .msi file, select Properties -\> Digital Signatures tab'
    echo Highlight 'OpenFabrics Alliance', select 'Details' -\> View Certificate
    echo select 'Install Certificate' -\> Next -\> Place Certificate in Trusted Publishers store -\> Browse
    echo Next -\> Finish. rerun this script.
    exit /B 1
)

rem Extract OFA cert to %CERTNAME% file.
if EXIST "%CERTNAME%"  del /F/Q %CERTNAME%
certutil -store TRUSTEDPUBLISHER %OFA_CERT_ID% %CERTNAME% 1> Nul
IF %ERRORLEVEL% NEQ 0 (
    echo Unable to extract OFA cert from local Trusted Publisher Store err %ERRORLEVEL%
    exit /B 1
)
echo Extracted OFA cert to %CERTNAME%
set EXTRACTED=1

:have_cert

if not EXIST "%1\%CERTNAME%" (
    echo Copying %CERTNAME% to compute node visible shar %1
    copy /B/Y %CERTNAME% %1\%CERTNAME%
    if ERRORLEVEL 1 (
        echo copy %CERTNAME% ERR %ERRORLEVEL% ?
        del /F/Q %CERTNAME%
        exit /B 1
    )
    set CPY_CERT=1
)

if not EXIST "%1\%CISF%" (
    echo Copying %CISF% to compute node visible shar %1
    copy /B/Y %CIS% %1\%CISF%
    if ERRORLEVEL 1 (
        echo copy %CIS% ERR %ERRORLEVEL% ?
        del /F/Q %CERTNAME%
        exit /B 1
    )
    set CPY_CISF=1
)

rem install OFA Trusted 3rd Party SW Publisher cert in remote nodes cert store
rem using clusrun.

rem Cert extract only?
if "%2" == "" (
    echo Extract Cert ONLY!
    goto xit
)

:again

rem  echo call clusrun /node:%1 %1\%CISF% %OFA_CERT_ID% %1\%CERTNAME%
  clusrun /nodes:%2 %1\%CISF% %OFA_CERT_ID% %1\%CERTNAME%
  shift /2
  if "%2" == "" goto cpy_cleanup
  timeout /T 5
  goto again


:cpy_cleanup

if %CPY_CERT%  EQU 1   del /F %1\%CERTNAME%
if %CPY_CISF%  EQU 1   del /F %1\%CISF%
if %EXTRACTED% EQU 1   del /F %CERTNAME%

:xit

endlocal

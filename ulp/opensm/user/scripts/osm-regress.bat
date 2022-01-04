@echo off
setlocal

rem requires cmd.exe /E /V, make it so... JLP
set F=on
set F=off
if "!F!" == "off" goto OK

%comspec% /E:on /V:on /C %0 %1 %2
exit /B %ERRORLEVEL%

:OK

if "%1" == "" (
:usage
    echo usage: osm-regress results-filename {exit-on-error}
    exit /B 1
)
if exist "%1"  del /Q "%1"

rem if not exist "osmtest.dat" (
rem    echo missing inventory file .\osmtest.dat ?
rem    exit /B 1
rem )

rem set T=..\..\..\bin\user\objchk_wlh_amd64\amd64\osmtest.exe
set T=osmtest.exe

rem Event forwarding test 'e' is not yet implemented [3.3.5]

set TESTS="c" "v" "m -M1" "m -M2" "m -M3" "m -M4" "f -s1" "f -s2" "f -s3" "f -s4" "s" "a"
rem set TESTS="c" "v" "a" "f -s1" "f -s2" "f -s3" "f -s4" "s"

for %%t in ( %TESTS% ) DO (
    echo TEST: osmtest -f %%~t
    echo TEST: osmtest -f %%~t >> %1
    %T% -f %%~t >> %1
    if !ERRORLEVEL! NEQ 0  (
      echo Error !ERRORLEVEL! reported in osmtest -f %%~t ?
      if Not "%2" == ""  exit /B 1
    )
    echo. >> %1
    echo    PASS.
)
endlocal

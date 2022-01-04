@echo off
setlocal
rem
rem Digitally sign all drivers present in specified OS & arch folders
rem
rem usage: sign-all-drivers CertFilename CertSubjName {OS arch}
rem   CertFilename - full path to MSCV-VSClass3.cer file
rem                  example ...\trunk\OFED\wix\MSCV-VSClass3.cer
rem   CertSubjName - "OpenFabricsAlliance" Your Company CertName in CertStore. 
rem   OS - one of: all, wlh, win7
rem   arch - all, x64, x86, ia64
rem
rem XXX defeat TimeStamping until net access resolved.
rem set TS=noTimeStamp

if "%1" == "" (
    echo %0 - Missing CertStore Filename?
    exit /B 1
)

if not EXIST %1 (
    echo %0 - Missing Certificate file?
    echo %0 -    %1
    exit /B 1
)

rem already quoted
if %2 == "" (
    echo %0 - Missing Cert Subject name?
    exit /B 1
)

if "%3" == "" (
:all_os
    set OS_names=win7 wlh
    goto OK_os
)
if "%3" == "all"  goto all_os
set OS_names=%3

:OK_os

if "%4" == "" (
:arch_all
    set ArchNames=amd64 x86 ia64
    set ArchArg=all
    goto OK_arch
)
if "%4" == "all" goto arch_all
if "%4" == "x64" (set ArchNames=amd64) else (set ArchNames=%4)
set ArchArg=%ArchNames%

:OK_arch

echo.

for %%p in ( %OS_names% ) do (
    echo %0 - Signing %%p drivers arch %ArchArg%
    pushd %%p
    if ERRORLEVEL 1 (
        echo %0 - Error in pushd %%p folder ?
        exit /B 1
    )
    rem Sign free HCA drivers
    call signDrivers %1 %2 bin\HCA %ArchArg% %TS%
    if ERRORLEVEL 1 (
        echo %0 - Error signing %%p\bin\HCA\%ArchArg% drivers?
        exit /B 1
    )
	
    rem Sign free: IPoIB & VNIC drivers
    call signDrivers %1 %2 bin\net %ArchArg% %TS%
    if ERRORLEVEL 1 (
        echo %0 - Error signing %%p\bin\net\%ArchArg% drivers?
        exit /B 1
    )
	
    rem Sign free SRP drivers
    call signDrivers %1 %2 bin\storage %ArchArg% %TS%
    if ERRORLEVEL 1 (
        echo %0 - Error signing %%p\bin\storage\%ArchArg% drivers?
        exit /B 1
    )
    popd
)

rem sign executables used in installation so Win7 doesn't complain

set TISTMP=/t http://timestamp.verisign.com/scripts/timstamp.dll
set DU=/du http://www.openfabrics.org

for %%p in ( %OS_names% ) do (
	pushd %%p
	echo.
	echo Sign %%p Executables for arch %ArchNames%
    for %%a in ( %ArchNames% ) do (
		for %%f in ( bin\net\%%a\ndinstall.exe bin\net\%%a\installsp.exe ) do (
			if exist %%f (
            	signtool sign /ac %1 /n %2 %TISTMP% %DU% %%f
            	if ERRORLEVEL 1 (
                	echo %0 signtool sign %%a\%%f failed?
					popd
                	exit /B 1
            	)
            	signtool verify /pa %%f
            	if ERRORLEVEL 1 (
                	echo %0 signtool verify %%a\%%f failed?
					popd
                	exit /B 1
            	)
			)
		)
	)
	popd
)

rem Sign devman.exe for win7 device cleanup operation.

for %%a in ( x64 x86 ia64 ) do (
	signtool verify /q /pa %%a\devman.exe
	if ERRORLEVEL 1 (
		signtool sign /ac %1 /n %2 %TISTMP% %DU% %%a\devman.exe
		if ERRORLEVEL 1 (
			echo %0 signtool sign %%a\devman.exe failed?
			exit /B 1
		)
	)
)

endlocal
echo Done %0 %1

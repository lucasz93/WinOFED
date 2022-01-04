@echo off
setlocal
rem Sign device drivers for architectures specified

rem usage:
rem   signDrivers CrossCertFname CertStoreName path-2-drivers arch {noTimeStamp}
rem		CrossCertFname - fully qualified path\filename of cross cert.
rem		CertStoreName - name of certificate in 'MY' Cert store (certmgr)
rem		path-2-drivers
rem		arch - all,x86,x64,ia64
rem		noTimeStamp - blank implies no TimeStamp.

rem example: signDrivers %CD%\Cross-Cert SWPublisher bin\hca all

rem cmd.exe /V:on (delayed environment variable expansion) is required!
set F=on
set F=off
if not "!F!" == "off" (
   echo Err: cmd.exe /V:on [delayed environment variable expansion] Required!
   exit /B 1
)

set OE=Win7
set DU=/du http://www.openfabrics.org

set Usage='usage: signDrivers CrossCertFilename CertStoreName path-2-drivers arch {noTimeStamp}'

if "%1" == "" (
    echo %0 - missing CertFileName?
    echo %0 - %Usage%
    exit /B 1
)

if not EXIST %1 (
    echo %0 - Cert file missing?
    echo %0 - %Usage%
    exit /B 1
)

rem %2 is already quoted.
if %2 == "" (
    echo %0 - missing Cert Subject Name?
    echo %0 - %Usage%
    exit /B 1
)

if "%3" == "" (
    echo %0 - missing path-2-driver files?
    echo %0 - %Usage%
    exit /B 1
)

if "%4" == "" (
:all
    set ArchNames=amd64 x86 ia64
    goto OK_arch
)
if "%4" == "all" goto all
if "%4" == "x64" (set ArchNames=amd64) else (set ArchNames=%4)

:OK_arch

rem Timestamp the signed file unless instructed not to.
if "%5" == "" (
    set TS=/t http://timestamp.verisign.com/scripts/timstamp.dll
) else (
    set TS=
)

rem make sure signtool is accessible in our path
where -q signtool
if ERRORLEVEL 1 (
    echo %0: signtool not in PATH?
    exit /B 1
)

rem move to drivers folder
cd %3
if ERRORLEVEL 1 (
    echo %0 - missing relative path %3
    exit /B 1
)
echo cwd !CD!
echo Sign drivers for %ArchNames%
rem sign drivers for all architectures found

for %%d in ( %ArchNames% ) do (

    if not exist %%d (
       echo %0 - skipping arch folder %%d
    ) else (
        pushd  %%d
        echo %0 - Delete existing %%d .cat files
        for %%f in ( *.cat ) do (
            if exist %%f del /F /Q %%f
        )

		rem set OS type for inf2cat
		if "%%d" == "x86"   set OEA=7_X86
		if "%%d" == "amd64" set OEA=7_X64,Server2008R2_X64
		if "%%d" == "ia64"  set OEA=Server2008R2_IA64

        echo %0 [%OE%] Generating %%d .cat files for !OEA!
        inf2cat /driver:!CD! /os:!OEA!
        if !ERRORLEVEL! NEQ 0 (
            echo %CD% inf2cat failed
            exit /B 1
        )
        echo %0 [%OE%] Signing %%d .cat files
        for %%f in ( *.cat ) do (
            echo %0 [%OE%] Signing %%d\%%f
            signtool sign /ac %1 /s "My" /n %2 %TS% %DU% %%f
            if ERRORLEVEL 1 (
                echo %0 signtool sign %%f failed?
                echo %0    file %CD%\%%f
                echo %0 signtool sign /ac %1 /s "My" /n %2 %TS% %DU% %%f
                exit /B 1
            )
            signtool verify /kp %%f
            if ERRORLEVEL 1 (
                echo %0 signtool verify %%f failed?
                echo %0    file %CD%\%%f
                exit /B 1
            )
			echo +
        )

        echo %0 [%OE%] Signing %%d .sys files
        for %%f in ( *.sys ) do (
            echo %0 [%OE%] Signing %%d\%%f
            signtool sign /ac %1 /s "My" /n %2 %TS% %DU% %%f
            if ERRORLEVEL 1 (
                echo %0 signtool sign %%f failed?
                echo %0    file %CD%\%%f
                exit /B 1
            )
            signtool verify /kp %%f
            if ERRORLEVEL 1 (
                echo %0 signtool verify %%f failed?
                echo %0    file %CD%\%%f
                exit /B 1
            )
			echo +
        )

        echo %0 [%OE%] Verify %%d .cat + .sys files
        for %%f in ( *.sys ) do (
			set D=%%f
			set C=!D:sys=cat!
			if exist !C! (
				echo %0 [%OE%] Verify %%d\!C! %%d\%%f
 				signtool verify /q /kp /c !C! %%f
				if ERRORLEVEL 1 (
					echo %0 signtool verify /kp /c !C! %%f failed?
					exit /B 1
				)
				signtool verify /pa /c !C! %%f
				if ERRORLEVEL 1 (
					echo %0 signtool verify /pa /c !C! %%f failed?
					exit /B 1
				)
				echo +
            )
        )
        popd
    )
)

echo %0 [%OE%] Finished: %0 %1 %2
echo +
endlocal

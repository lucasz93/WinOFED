@echo off
setlocal

rem usage: clean-build {Target-OS} {target-Arch} {scan-only}
rem no args - remove build specific folders and files:
rem		*_win7_* *_wlh_*
rem arg1 == Target OS name: win7 | wlh | all
rem arg2 == Target Arch: x86 | x64 | ia64 | all
rem arg3 != "" - then report matched folders & files - no delete.

set ALLOS=_win7_ _wlh_
set ALLARCH=x86 amd64 ia64

if "%1" == "/?" (
:usage
    echo usage:
    echo  clean-build {OS:win7,wlh,all} {arch:x86,x64,ia64,all} {scan-only}
    echo   no args - remove all OS build specific folders and files: *_OS_*
    echo   otherwise 'win7 x86' only removes files * folders matching '*_win7_x86'
    echo   arg3 != "" - then report matched folders and files - no delete.
    exit /B 0
)

if "%1" == "" (
    set TOS=%ALLOS%
    goto OK_OS
)
if "%1" == "all" (
    set TOS=%ALLOS%
) else (
    if "%1" == "win7"  goto set_OS
    if "%1" == "wlh"  goto set_OS
    echo %0 - BAD OS specification '%1'?
    goto usage
rem set Target OS
:set_OS
    set TOS=_%1_
)
:OK_OS

if "%2" == "" (
:all_arch
    set TARCH=
    goto OK_ARCH
)
if "%2" == "all" goto all_arch

if "%2" == "x64"  (
    set TARCH=amd64
    goto OK_ARCH
)
if "%2" == "x86"  goto set_ARCH
if "%2" == "ia64"  goto set_ARCH
echo %0 - BAD Arch specification '%2'?
goto usage

rem set Target OS
:set_ARCH
    set TARCH=%2

:OK_ARCH

set T=%TEMP%\flist.txt

rem delete OS flavor {wlh,win7} specific build files to ensure a clean build

rem The story behind the for loop need for the fake 'delims=,' is the need to
rem override the default delimiters of <space> & <tab>, anything but <space>
rem or <tab>. Problems occur with a folder name like
rem 'c:\svn\trunk\ulp\ipoib - copy(2)\objfre_wlh_x86' as the default delimiters
rem in for loop file read return 'c:\svn\trunk\ulp\ipoib', bad juju.

rem check/remove directories

for %%d in ( %TOS% ) do (
	echo  Folder Scan for *%%d%TARCH%*
	dir /B /S /A:D *%%d%TARCH%* > %T% 2>&1
	if ERRORLEVEL 1 (
		del /Q/F %T%
	) else (
		for /f "delims=," %%f in ( %T% ) do (
			if EXIST "%%f" (
				if "%3" == "" (
					rmdir /S /Q "%%f" 1>nul
				) else (
					echo   found "%%f"
				)
			)
		)
		del /Q/F %T%
	)
)

rem check/remove files

for %%d in ( %TOS% ) do (
	echo  File Scan for *%%d%TARCH%*
	dir /B /S *%%d%TARCH%* > %T% 2>&1
	if ERRORLEVEL 1 (
		del /Q/F %T%
	) else (
		for /f "delims=," %%f in ( %T% ) do (
			if EXIST "%%f" (
				if "%3" == "" (
					del /F /Q "%%f" 1>nul
				) else (
					echo   found %%f
				)
			)
		)
		del /Q/F %T%
	)
)
endlocal


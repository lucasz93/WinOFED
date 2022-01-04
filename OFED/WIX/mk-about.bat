@echo off
rem Create about-OFED.txt file from about-base.txt + timestamp.
setlocal

set BASE=about-OFED-base.txt
set TARGET=about-OFED.txt

if NOT EXIST "%BASE%" (
   echo ERR: missing file '%BASE%'
   exit /B 1
)

copy /A /Y %BASE% %TARGET%
if NOT EXIST "%TARGET%" (
   echo ERR: unable to create file '%TARGET%' error %ERRORLEVEL%
   exit /B 1
)

rem assume %BASE% does not contain a blank last line!

rem %TIME% and time /T differ in format as %TIME% begins with a space or 1
rem which later creates @<space><space> or @<space>1.
rem while time /T begins with '0' or '1', consistent format.

time /T > foo.tim
set /P NOW=<foo.tim
del /F /Q foo.tim

if EXIST "build_SVN.txt" (
    set /P SVN=<build_SVN.txt
) else (
    set SVN=unknown
)

rem add: blank line + date/time stamp.
echo.>> %TARGET%

rem preserve leading spaces for alignment.
echo     Release built from SVN %SVN% on %DATE% @ %NOW% >> %TARGET%

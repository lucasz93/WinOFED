@echo off
rem
rem Sample dtest2 client invocation - usage: dtc {hostname}
rem

SETLOCAL

rem socket CM testing
rem   set DAT_OVERRIDE=C:\dapl2\dat.conf
rem   set P=ibnic0v2_scm
rem   set DT=dtest2.exe

rem IBAL CM testing
rem   set DAT_OVERRIDE=C:\dapl2\dat.conf
rem   set P=ibnic0v2
rem   set DT=dtest2.exe
rem set X=0xfffff

set X=

if not "%X%" == "" (
    set DAT_DBG_LEVEL=%X%
    set DAT_DBG_TYPE=%X%
    set DAT_OS_DBG_TYPE=%X%
    set DAPL_DBG_TYPE=%X%
)

IF "%1" == "" (
  set H=10.10.4.201
) ELSE (
  set H=%1
)

echo %DT% -P %P% -h %H%

%DT% -P %P% -h %H%

echo %0 - dtest client exit...

ENDLOCAL

@echo on

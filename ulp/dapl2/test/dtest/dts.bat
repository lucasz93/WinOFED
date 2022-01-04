@echo off
rem
rem dtest2 server invocation - usage: dts {-D {0x?}}
rem

SETLOCAL

rem Socket CM testing
rem   set DAT_OVERRIDE=C:\dapl2\dat.conf
rem   set DT=dtest2.exe
rem   set P=ibnic0v2_scm

rem IBAL CM testing
rem   set DAT_OVERRIDE=C:\dapl2\dat.conf
set DT=dtest2.exe
set P=ibnic0v2


if "%1" == "-D" (
  if "%2" == "" (
    set X=0xfffff
    rem set X=0x48
  ) else (
    set X=%2
  )
) else (
  set X=
)

if not "%X%" == "" (
    set DAT_DBG_LEVEL=%X%
    set DAT_DBG_TYPE=%X%
    set DAT_OS_DBG_TYPE=%X%
    set DAPL_DBG_TYPE=%X%
    set DAPL_DBG_LEVEL=%X%
)

echo %DT% -s -P %P%

%DT% -s -P %P%

echo %0 - %DT% server exit...

ENDLOCAL

@echo on
exit /B

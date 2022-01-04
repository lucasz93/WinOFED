@echo off
setlocal
if "%1" == "free" (
    set B=..\..\..\..\bin\user\objfre_win7_amd64\amd64
    echo setup - FREE
) else (
    set B=..\..\..\..\bin\user\objchk_win7_amd64\amd64
    echo setup - CHECKED
)
set T=C:\Temp\DAPL

if Not EXIST "%B%\dapl2-nd.dll" (
	echo Missing %B%\dapl2-nd.dll?
	exit /B 1
)

rem if Not EXIST "%T%\dat2.dll" copy /B/Y %B%\dat2.dll %T%\
rem if Not EXIST "%T%\dat2.pdb" copy /B/Y %B%\dat2.pdb %T%\
rem if Not EXIST "%T%\dapl2.dll" copy /B/Y %B%\dapl2.dll %T%\
rem if Not EXIST "%T%\dapl2.pdb" copy /B/Y %B%\dapl2.pdb %T%\
rem if Not EXIST "%T%\dtest2.exe" copy /B/Y %B%\dtest2.exe %T%\
rem if Not EXIST "%T%\dtest2.pdb" copy /B/Y %B%\dtest2.pdb %T%\

copy /B/Y %B%\dat2.dll %T%\
copy /B/Y %B%\dat2.pdb %T%\

copy /B/Y %B%\dapl2.dll %T%\
copy /B/Y %B%\dapl2.pdb %T%\

copy /B/Y %B%\dapl2-ofa-scm.dll %T%\
copy /B/Y %B%\dapl2-ofa-scm.pdb %T%\

copy /B/Y %B%\dapl2test.exe %T%\
copy /B/Y %B%\dtest2.exe %T%\
copy /B/Y %B%\dtest2.pdb %T%\
copy /B/Y %B%\dtestcm.exe %T%\
copy /B/Y %B%\dtestcm.pdb %T%\

copy /B/Y %B%\dapl2-ND.dll %T%\
copy /B/Y %B%\dapl2-ND.pdb %T%\

endlocal

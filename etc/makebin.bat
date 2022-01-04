@echo off
setlocal

rem usage:
rem   makebin src dst OSE Arch DDK_ROOT WdfCoInstaler_Ver build_type
rem
rem  src(%1)  - OpenIB src path ...\gen1\trunk
rem  dst(%2)  - full path to where binaries are copied, must exist.
rem  OSE(%3)  - (Operating System Environment) which windows version
rem            {win8,win7,wxp,wlh,wnet} representing {XP, server 2012,2008,2003}
rem  Arch(%4) - all, x86, x64, ia64
rem  DDK_ROOT - {blank == assumes %SystemDrive%\WinDDK\7600.16385.0}
rem  WdfCoInstall_ver - 5 digit WdfCoInstallerVersion # (blank == 01007} 
rem  build_type  - 'checked' or 'free', blank == free

rem makebin is designed to take an openIB src tree path and produce a folder
rem (tree) of binaries suitable for input to a WIX builder which procduces
rem an OS/Arch specific .msi installer.
rem Building a OFED release is done is 3 phases, makebin is the 2nd phase.
rem makebin is commonly run from trunk\buildrelease.bat although it can be
rem run standalone.

echo Starting makebin
echo      Src   %1
echo      Dst   %2
echo      OS    %3
echo      Arch  %4

if "%1"=="" goto usage
if "%2"=="" goto usage

if "%3"=="" goto usage
if /I "%3"=="win8" goto os_ok
if /I "%3"=="win7" goto os_ok
if /I "%3"=="wlh" goto os_ok
if /I "%3"=="wnet" goto os_ok
if /I "%3"=="wxp" goto os_ok
echo %0: Err - Invalid OS type '%3', use [win8, win7, wlh, wnet, wxp] ?
exit /B 1

:os_ok

rem Enable tracing to indicate phase for potiential debug.
set DBG=TRUE

set OSE=%3

if "%4"=="" goto usage

if /I "%4"=="ia64" (
    set ARCH_MS=%4
    goto arch_ok
)
if /I "%4"=="x86"  (
    set ARCH_MS=%4
    goto arch_ok
)
if /I "%4"=="x64"  (
    set ARCH_MS=amd64
    goto arch_ok
)
echo %0: Err - Invalid Arch type '%4', use [x86, ia64, x64, all] ?
exit /B 1

:arch_ok
set _ARCH=%4

if /I "%3"=="wxp" (
    if /I "%4"=="x86" goto os_arch_OK
    echo %0: Err - Invalid OS Arch combination '%3 %4', wxp + x86 Only!
    exit /B 1
)

:os_arch_OK

rem setup DDK root path
if /I "%5"=="" (
	set _DDK=%systemdrive%\WinDDK\7600.16385.1
) else (
	set _DDK=%5
)
if not exist "%_DDK%" (
	echo Missing file %_DDK% ?
	exit /B 1
)

set WdfCoInstaller=%_DDK%\redist\wdf
set DIFXP=%_DDK%\redist\DIFx\DIFxAPP\WixLib
set DPINST=%_DDK%\redist\DIFx\DPInst\EngMui

if /I "%6"=="" (
	set CoInstallVer=01009
) else (
	set CoInstallVer=%6
)

if /I "%7"=="checked" (
	set checked=1
	set OBJ=objchk
) else (
	set checked=0
	set OBJ=objfre
)

if not exist %1 goto usage
if not exist %2 goto usage

rem verify binaries OK

if /I "%_ARCH%"=="x64"  (
    if not exist %1\bin\kernel\%OBJ%_%OSE%_amd64\amd64 goto error1
	if not exist %1\bin\user\%OBJ%_%OSE%_amd64\amd64 goto error4
    goto bin_ok
)

if /I "%_ARCH%"=="ia64"  (
    if not exist %1\bin\kernel\%OBJ%_%OSE%_ia64\ia64 goto error2
    if not exist %1\bin\user\%OBJ%_%OSE%_ia64\ia64 goto error5
    goto bin_ok
)

rem wxp or x86

if not exist %1\bin\kernel\%OBJ%_%OSE%_x86\i386 goto error3
if not exist %1\bin\user\%OBJ%_%OSE%_x86\i386 goto error6

:bin_OK

rem Create base folders
for %%i in ( HCA net storage tools DAPL2 ) do (
	if not EXIST "%2\%%i\%ARCH_MS%"  (
		mkdir %2\%%i\%ARCH_MS%
		if ERRORLEVEL 1 (
			echo ERR: mkdir %2\%%i\%ARCH_MS%
			exit /B 1
		)
	)
)

set CORE_DRV_FRE=ibbus.sys ibbus.pdb ibiou.sys ibiou.pdb ib_iou.inf mthca.sys mthca.inf mthca.pdb mlx4_bus.sys mlx4_bus.pdb mlx4_bus.inf winverbs.sys winverbs.pdb winmad.sys winmad.pdb ndfltr.sys ndfltr.pdb

set CORE_UM_F=ibal.dll ibal.lib ibal.pdb complib.dll complib.lib complib.pdb mthcau.dll mthcau.pdb mlx4u.dll mlx4u.pdb mlx4nd.dll mlx4nd.pdb

set WV_FRE=winverbs.dll winverbs.lib winmad.dll winmad.lib libibverbs.dll libibverbs.lib libibverbs.pdb libibumad.lib libibumad.dll libibumad.pdb libibmad.lib libibmad.dll libibmad.pdb librdmacm.dll librdmacm.lib librdmacm.pdb libibnetdisc.dll libibnetdisc.pdb

set DAPL2_F=dapl2-ND.dll dapl2-ND.pdb dapl2.dll dapl2.pdb dapl2-ofa-scm.dll dapl2-ofa-scm.pdb dapl2-ofa-cma.dll dapl2-ofa-cma.pdb dapl2-ofa-ucm.dll dapl2-ofa-ucm.pdb dat2.dll dat2.lib dat2.pdb

rem
rem KERNEL MODE
rem

if /I "%OSE%" == "wxp"     goto free_wxp_drv
if /I "%_ARCH%" == "x86"   goto free_wxp_drv
if /I "%_ARCH%" == "ia64"  goto free_ia64_drv

rem Copy AMD64 Free drivers

set bin_dir=%1\bin\kernel\%OBJ%_%OSE%_amd64\amd64
set bin_dir_fre_WOW=%1\bin\user\%OBJ%_%OSE%_x86\i386

set dest_dir=%2\HCA\amd64\

if "%DBG%" == "TRUE" echo DBG: AMD64 free drivers

for %%i in ( %CORE_DRV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1> nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)

xcopy %WdfCoInstaller%\amd64\WdfCoInstaller%CoInstallVer%.dll %dest_dir% /yq 

for %%i in ( ipoib qlgcvnic ) do (
    xcopy %bin_dir%\%%i.sys %2\net\amd64\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.sys %2\net\amd64\ /yq
        exit /B 1
    )
    xcopy %bin_dir%\%%i.pdb %2\net\amd64\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.pdb %2\net\amd64\ /yq
        exit /B 1
    )
)

xcopy %bin_dir%\ibsrp.sys %2\storage\amd64\ /yq
xcopy %bin_dir%\ibsrp.pdb %2\storage\amd64\ /yq

if not exist "%bin_dir%\netipoib.inf" (
    echo "ERR: missing %bin_dir%\netipoib.inf?" 
    exit /B 1
)
xcopy %bin_dir%\netipoib.inf %2\net\amd64\ /yq

if not exist "%bin_dir%\netvnic.inf" (
    echo "ERR: missing %bin_dir%\netvnic.inf?" 
    exit /B 1
)
xcopy %bin_dir%\netvnic.inf %2\net\amd64\ /yq

if not exist "%bin_dir%\ib_srp.inf" (
    echo "ERR: missing %bin_dir%\ib_srp.inf?" 
    exit /B 1
)
xcopy %bin_dir%\ib_srp.inf %2\storage\amd64\ /yq

goto free_drv_done

:free_ia64_drv

rem Copy IA64 drivers

set bin_dir=%1\bin\kernel\%OBJ%_%OSE%_ia64\ia64
set bin_dir_fre_WOW=%1\bin\user\%OBJ%_%OSE%_x86\i386

set dest_dir=%2\HCA\ia64\

if "%DBG%" == "TRUE" echo DBG: ia64 free drivers

for %%i in ( %CORE_DRV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)
xcopy %WdfCoInstaller%\ia64\WdfCoInstaller%CoInstallVer%.dll %dest_dir% /yq 

for %%i in ( ipoib qlgcvnic ) do (
    xcopy %bin_dir%\%%i.sys %2\net\ia64\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.sys %2\net\ia64\ /yq
        exit /B 1
    )
    xcopy %bin_dir%\%%i.pdb %2\net\ia64\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.pdb %2\net\ia64\ /yq
        exit /B 1
    )
)

xcopy %bin_dir%\ibsrp.sys %2\storage\ia64\ /yq
xcopy %bin_dir%\ibsrp.pdb %2\storage\ia64\ /yq

if not exist "%bin_dir%\netipoib.inf" (
    echo "ERR: missing %bin_dir%\netipoib.inf?" 
    exit /B 1
)
xcopy %bin_dir%\netipoib.inf %2\net\ia64\ /yq
xcopy %bin_dir%\netvnic.inf %2\net\ia64\ /yq
xcopy %bin_dir%\ib_srp.inf %2\storage\ia64\ /yq

goto free_drv_done

rem Copy x86 drivers

:free_wxp_drv

if "%DBG%" == "TRUE" echo DBG: x86 free drivers

set bin_dir=%1\bin\kernel\%OBJ%_%OSE%_x86\i386
set dest_dir=%2\HCA\x86\

for %%i in ( %CORE_DRV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)
xcopy %WdfCoInstaller%\x86\WdfCoInstaller%CoInstallVer%.dll %dest_dir% /yq

for %%i in ( ipoib qlgcvnic ) do (
    xcopy %bin_dir%\%%i.sys %2\net\x86\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.sys %2\net\x86\ /yq
        exit /B 1
    )
    xcopy %bin_dir%\%%i.pdb %2\net\x86\ /yq 1> nul
    if ERRORLEVEL 1 (
        echo   ERR xcopy %bin_dir%\%%i.pdb %2\net\x86\ /yq
        exit /B 1
    )
)

xcopy %bin_dir%\ibsrp.sys %2\storage\x86\ /yq
xcopy %bin_dir%\ibsrp.pdb %2\storage\x86\ /yq

rem Use netipoib.inf without WSD support for XP32
if /I "%OSE%" == "wxp" (
    copy /A /Y %1\ulp\ipoib\kernel\netipoib-xp32.inf %2\net\x86\netipoib.inf
) else (
    if not exist "%bin_dir%\netipoib.inf" (
        echo "ERR: missing %bin_dir%\netipoib.inf?" 
        exit /B 1
    )
    xcopy %bin_dir%\netipoib.inf %2\net\x86\ /yq
)

rem allow XP SRP build & sign, WIX skips SRP for XP
rem otherwise there is too much special casing for SRP on XP.
xcopy %bin_dir%\ib_srp.inf %2\storage\x86\ /yq
xcopy %bin_dir%\netvnic.inf %2\net\x86\ /yq
xcopy %bin_dir%\ib_srp.inf %2\storage\x86\ /yq

:free_drv_done

if /I "%OSE%" == "wxp" goto wxp_free_drv
if /I "%_ARCH%" == "x86" goto wxp_free_drv
if /I "%_ARCH%" == "ia64" goto ia64_fre_dll

rem Copy Free x64 dll

set bin_dir=%1\bin\user\%OBJ%_%OSE%_amd64\amd64
set dest_dir=%2\HCA\amd64\

if "%DBG%" == "TRUE" echo DBG: copy amd64 Free dlls

for %%i in (%CORE_UM_F%) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /y
        exit /B 1
    )
)

echo xcopy winverbs User to HCA\amd64

for %%i in ( %WV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)

xcopy %bin_dir%\ibwsd.dll %2\net\amd64\ /yq
xcopy %bin_dir%\installsp.exe %2\net\amd64\ /yq 
xcopy %bin_dir%\installsp.exe %2\tools\amd64\release\ /yq

if exist "%bin_dir%\ndinstall.exe" (
    xcopy %bin_dir%\ndinstall.exe %2\net\amd64\ /yq 
    xcopy %bin_dir%\ndinstall.exe %2\HCA\amd64\ /yq
    xcopy %bin_dir%\ndinstall.exe %2\tools\amd64\release\ /yq
    xcopy %bin_dir%\ibndprov.dll %2\net\amd64\ /yq
    xcopy %bin_dir%\wvndprov.dll %2\net\amd64\ /yq
) else (
    echo %0 - missing x64 Network Direct components [wvndprov.dll ibndprov.dll,ndinstall.exe]
    exit /B 1
)

echo xcopy amd64 Free *.exe tools\amd64\release\ 
xcopy %bin_dir%\*.exe %2\tools\amd64\release\ /yq 1>nul

echo xcopy AMD64 [Winverb-apps].pdb tools\amd64\release\ 
xcopy %bin_dir%\*.pdb %2\tools\amd64\release\ /yq 1>nul
if ERRORLEVEL 1 (
	echo ERR on xcopy %bin_dir%\*.pdb %2\tools\amd64\release\ /yq
	exit /B 1
)

for %%i in ( %DAPL2_F% ) do (
    xcopy %bin_dir%\%%i %2\DAPL2\amd64\ /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %2\DAPL2\amd64\ /yq
        exit /B 1
    )
)
goto fre_dll_WOW


:ia64_fre_dll

rem Copy Free IA64 dlls

set bin_dir=%1\bin\user\%OBJ%_%OSE%_ia64\ia64
set dest_dir=%2\HCA\ia64\

if "%DBG%" == "TRUE" echo DBG: copy IA64 Free dlls

for %%i in (%CORE_UM_F%) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %%i %dest_dir% /yq
        exit /B 1
    )
)

echo xcopy winverbs: User free to HCA\ia64

for %%i in ( %WV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)

xcopy %bin_dir%\ibwsd.dll %2\net\ia64\ /yq
xcopy %bin_dir%\wvndprov.dll  %2\net\ia64\ /yq
xcopy %bin_dir%\installsp.exe %2\net\ia64\ /yq
xcopy %bin_dir%\ndinstall.exe %2\net\ia64\ /yq
xcopy %bin_dir%\ndinstall.exe %2\HCA\ia64\ /yq

echo xcopy IA64 Free *.exe tools\ia64\release\ 
xcopy %bin_dir%\*.exe %2\tools\ia64\release\ /yq 1>nul

echo xcopy IA64 [Winverb-apps].pdb tools\ia64\release\ 
xcopy %bin_dir%\*.pdb %2\tools\ia64\release\ /yq 1>nul
if ERRORLEVEL 1 (
	echo ERR on xcopy %bin_dir%\*.pdb %2\tools\ia64\release\ /yq
	exit /B 1
)

for %%i in ( %DAPL2_F% ) do (
    xcopy %bin_dir%\%%i %2\DAPL2\ia64\ /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %2\DAPL2\ia64\ /yq
        exit /B 1
    )
)
goto fre_dll_WOW


rem Copy Free x86 drivers

:wxp_free_drv

set bin_dir=%1\bin\user\%OBJ%_%OSE%_x86\i386
set dest_dir=%2\HCA\x86\

if "%DBG%" == "TRUE" echo DBG: copy x86 Free dlls

for %%i in (%CORE_UM_F%) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)

echo xcopy winverbs: User free to HCA\x86

for %%i in ( %WV_FRE% ) do (
    xcopy %bin_dir%\%%i %dest_dir% /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %dest_dir% /yq
        exit /B 1
    )
)

echo xcopy x86 free *.exe to tools\x86\release
xcopy %bin_dir%\*.exe %2\tools\x86\release\ /yq 1>nul

echo xcopy X86 Free [Winverb-apps].pdb tools\x86\release\ 
xcopy %bin_dir%\*.pdb %2\tools\x86\release\ /yq 1>nul
if ERRORLEVEL 1 (
	echo ERR on xcopy %bin_dir%\*.pdb %2\tools\x86\release\ /yq
	exit /B 1
)

for %%i in ( %DAPL2_F% ) do (
    xcopy %bin_dir%\%%i %2\DAPL2\x86\ /yq 1>nul
    if ERRORLEVEL 1 (
        echo ERR on xcopy %bin_dir%\%%i %2\DAPL2\x86\ /yq
        exit /B 1
    )
)

if exist "%bin_dir%\ndinstall.exe" (
    copy %bin_dir%\ndinstall.exe %2\HCA\x86\ /y
    copy %bin_dir%\ndinstall.exe %2\net\x86\ /y
    copy %bin_dir%\ndinstall.exe %2\tools\x86\release\ /y
    copy %bin_dir%\ibndprov.dll %2\net\x86\ /y
    copy %bin_dir%\wvndprov.dll %2\net\x86\ /y
) else (
    echo %0 - missing x86 Network Direct components [ibndprov.dll,ndinstall.exe]
)

if /I "%OSE%" == "wxp" goto mk_sym_bin

rem free x86 items

if "%DBG%" == "TRUE" echo DBG: copy x86 Free WSD

copy %bin_dir%\ibwsd.dll %2\net\x86\ /y
copy %bin_dir%\installsp.exe %2\net\x86\ /y
copy %bin_dir%\installsp.exe %2\tools\x86\release\ /y

goto mk_sym_bin

:fre_dll_WOW

rem free x86 DLLs --> WOW64 DLLs

if "%DBG%" == "TRUE" echo DBG: x86 Free dlls to WOW64

if /I "%_ARCH%" == "x64" (
    if exist "%bin_dir_fre_WOW%\ibndprov.dll" (
        copy %bin_dir_fre_WOW%\ibndprov.dll %2\net\amd64\ibndprov32.dll /y
        copy %bin_dir_fre_WOW%\wvndprov.dll %2\net\amd64\wvndprov32.dll /y
    )
    copy /B %bin_dir_fre_WOW%\ibwsd.dll %2\net\amd64\ibwsd32.dll /y
    copy /B %bin_dir_fre_WOW%\ibal.dll %2\HCA\amd64\ibal32.dll /y
    copy /B %bin_dir_fre_WOW%\ibal.lib %2\HCA\amd64\ibal32.lib /y
    copy /B %bin_dir_fre_WOW%\ibal.pdb %2\HCA\amd64\ibal32.pdb /y
    copy /B %bin_dir_fre_WOW%\complib.dll %2\HCA\amd64\cl32.dll /y
    copy /B %bin_dir_fre_WOW%\complib.lib %2\HCA\amd64\cl32.lib /y
    copy /B %bin_dir_fre_WOW%\complib.pdb %2\HCA\amd64\cl32.pdb /y
    copy /B %bin_dir_fre_WOW%\winverbs.dll %2\HCA\amd64\winverbs32.dll /y
    copy /B %bin_dir_fre_WOW%\winverbs.lib %2\HCA\amd64\winverbs32.lib /y
    copy /B %bin_dir_fre_WOW%\winverbs.pdb %2\HCA\amd64\winverbs32.pdb /y
    copy /B %bin_dir_fre_WOW%\winmad.dll %2\HCA\amd64\winmad32.dll /y
    copy /B %bin_dir_fre_WOW%\winmad.lib %2\HCA\amd64\winmad32.lib /y
    copy /B %bin_dir_fre_WOW%\winmad.pdb %2\HCA\amd64\winmad32.pdb /y
    copy /B %bin_dir_fre_WOW%\mthcau.dll %2\HCA\amd64\mthca32.dll /y
    copy /B %bin_dir_fre_WOW%\mlx4u.dll %2\HCA\amd64\mlx4u32.dll /y
    copy /B %bin_dir_fre_WOW%\mlx4nd.dll %2\HCA\amd64\mlx4nd32.dll /y

    if not exist "%bin_dir_fre_WOW%\libibnetdisc.dll" (
        echo ERR: missing %bin_dir_fre_WOW%\libibnetdisc.dll
        exit /B 1
    )
    copy /B %bin_dir_fre_WOW%\libibmad.dll %2\HCA\amd64\libibmad32.dll /y
    copy /B %bin_dir_fre_WOW%\libibumad.dll %2\HCA\amd64\libibumad32.dll /y
    copy /B %bin_dir_fre_WOW%\libibverbs.dll %2\HCA\amd64\libibverbs32.dll /y
    copy /B %bin_dir_fre_WOW%\librdmacm.dll %2\HCA\amd64\librdmacm32.dll /y
    copy /B %bin_dir_fre_WOW%\libibnetdisc.dll %2\HCA\amd64\libibnetdisc32.dll /y

    copy /B %bin_dir_fre_WOW%\dat2.dll %2\DAPL2\amd64\dat232.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2.dll %2\DAPL2\amd64\dapl232.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-scm.dll %2\DAPL2\amd64\dapl2-ofa-scm32.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-cma.dll %2\DAPL2\amd64\dapl2-ofa-cma32.dll /y 
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-ucm.dll %2\DAPL2\amd64\dapl2-ofa-ucm32.dll /y
) else (
    rem IA64
    if exist "%bin_dir_fre_WOW%\ibndprov.dll" (
        copy %bin_dir_fre_WOW%\ibndprov.dll %2\net\ia64\ibndprov32.dll /y
        copy %bin_dir_fre_WOW%\wvndprov.dll %2\net\ia64\wvndprov32.dll /y
    )
    copy /B %bin_dir_fre_WOW%\ibwsd.dll %2\net\ia64\ibwsd32.dll /y
    copy /B %bin_dir_fre_WOW%\ibal.dll %2\HCA\ia64\ibal32.dll /y
    copy /B %bin_dir_fre_WOW%\ibal.lib %2\HCA\ia64\ibal32.lib /y
    copy /B %bin_dir_fre_WOW%\ibal.pdb %2\HCA\ia64\ibal32.pdb /y
    copy /B %bin_dir_fre_WOW%\complib.dll %2\HCA\ia64\cl32.dll /y
    copy /B %bin_dir_fre_WOW%\complib.lib %2\HCA\ia64\cl32.lib /y
    copy /B %bin_dir_fre_WOW%\complib.pdb %2\HCA\ia64\cl32.pdb /y
    copy /B %bin_dir_fre_WOW%\winverbs.dll %2\HCA\ia64\winverbs32.dll /y
    copy /B %bin_dir_fre_WOW%\winverbs.lib %2\HCA\ia64\winverbs32.lib /y
    copy /B %bin_dir_fre_WOW%\winverbs.pdb %2\HCA\ia64\winverbs32.pdb /y
    copy /B %bin_dir_fre_WOW%\winmad.dll %2\HCA\ia64\winmad32.dll /y
    copy /B %bin_dir_fre_WOW%\winmad.lib %2\HCA\ia64\winmad32.lib /y
    copy /B %bin_dir_fre_WOW%\winmad.pdb %2\HCA\ia64\winmad32.pdb /y
    copy /B %bin_dir_fre_WOW%\mthcau.dll %2\HCA\ia64\mthca32.dll /y
    copy /B %bin_dir_fre_WOW%\mlx4u.dll %2\HCA\ia64\mlx4u32.dll /y
    copy /B %bin_dir_fre_WOW%\mlx4nd.dll %2\HCA\ia64\mlx4nd32.dll /y
    copy /B %bin_dir_fre_WOW%\dat2.dll %2\DAPL2\ia64\dat232.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2.dll %2\DAPL2\ia64\dapl232.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-scm.dll %2\DAPL2\ia64\dapl2-ofa-scm32.dll /y
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-cma.dll %2\DAPL2\ia64\dapl2-ofa-cma32.dll /y 
    copy /B %bin_dir_fre_WOW%\dapl2-ofa-ucm.dll %2\DAPL2\ia64\dapl2-ofa-ucm32.dll /y
)

:mk_sym_bin

rem bin\bin used to generate a symbol store in build-ofa-dist.bat.

echo 'Copy bin\objfre_%3_{%ARCH_MS%} to bin'

if /I "%_ARCH%" == "x64"  goto bin_x64
if /I "%_ARCH%" == "ia64"  goto bin_ia64

rem x86
xcopy %1\bin\kernel\%OBJ%_%3_x86 %2\bin\kernel\objfre_%3_x86\ /S /Y /Q

xcopy %1\bin\user\%OBJ%_%3_x86 %2\bin\user\objfre_%3_x86\ /S /Y /Q
goto do_dat

:bin_x64
xcopy %1\bin\kernel\%OBJ%_%3_amd64 %2\bin\kernel\objfre_%3_amd64\ /S /Y /Q

xcopy %1\bin\user\%OBJ%_%3_amd64 %2\bin\user\objfre_%3_amd64\ /S /Y /Q
goto do_dat

:bin_ia64
xcopy %1\bin\kernel\%OBJ%_%3_ia64 %2\bin\kernel\objfre_%3_ia64\ /S /Y /Q

xcopy %1\bin\user\%OBJ%_%3_ia64 %2\bin\user\objfre_%3_ia64\ /S /Y /Q

:do_dat

rem Copy DAT v2.0 header files
if "%DBG%" == "TRUE" echo DBG: [%OSE%] DAT v2.0 header files

if exist %1\ulp\dapl2\dat\include\dat (set DATINC=dat) else (set DATINC=dat2)
pushd %1\ulp\dapl2\dat\include\%DATINC%
if ERRORLEVEL 1 (
    echo %0: ERR - missing DAT files @ %1\ulp\dapl2\dat\include\%DATINC%
    exit /B 1
)
xcopy dat.h %2\DAPL2 /Y/Q  
xcopy dat_error.h %2\DAPL2 /Y/Q
xcopy dat_ib_extensions.h %2\DAPL2 /Y/Q
xcopy dat_platform_specific.h %2\DAPL2 /Y/Q
xcopy dat_redirection.h %2\DAPL2 /Y/Q
xcopy dat_registry.h %2\DAPL2 /Y/Q
xcopy dat_vendor_specific.h %2\DAPL2 /Y/Q
xcopy udat.h %2\DAPL2 /Y/Q
xcopy udat_config.h %2\DAPL2 /Y/Q
xcopy udat_redirection.h %2\DAPL2 /Y/Q
xcopy udat_vendor_specific.h %2\DAPL2 /Y/Q
popd

pushd %1\ulp\dapl2\test\dapltest\scripts
xcopy dt-svr.bat %2\DAPL2 /Y/Q
xcopy dt-cli.bat %2\DAPL2 /Y/Q
popd

rem Copy IBAL header files
if "%DBG%" == "TRUE" echo DBG: IBAL header files
if exist %1\inc (
    if exist %2\inc rmdir /S/Q %2\inc
    mkdir %2\Inc
    pushd %1\inc
    xcopy oib_ver.h %2\Inc /Y/Q
    xcopy mod_ver.def %2\Inc /Y/Q
    xcopy openib.def %2\Inc /Y/Q
    xcopy user\comp_channel.h %2\Inc /Y/Q
    xcopy user\dlist.h %2\Inc /Y/Q
    xcopy user\getopt.h %2\Inc /Y/Q
    xcopy Complib %2\Inc\Complib /I/S/Y/Q
    xcopy Iba %2\Inc\Iba /I/S/Y/Q
    xcopy User\Complib %2\Inc\Complib /I/S/Y/Q
    xcopy User\Iba %2\Inc\Iba /I/S/Y/Q
    xcopy User\linux %2\Inc\linux /I/S/Y/Q
    xcopy User\rdma %2\Inc\rdma /I/S/Y/Q
    xcopy ..\ulp\libibverbs\include\infiniband %2\Inc\infiniband /I/S/Y/Q
    xcopy ..\ulp\librdmacm\include\rdma\rdma_cma.h %2\Inc\rdma /Y/Q
    xcopy ..\ulp\librdmacm\include\rdma\rdma_verbs.h %2\Inc\rdma /Y/Q
    xcopy ..\ulp\librdmacm\include\rdma\rsocket.h %2\Inc\rdma /Y/Q
    xcopy ..\ulp\librdmacm\include\rdma\rwinsock.h %2\Inc\rdma /Y/Q
    xcopy ..\ulp\librdmacm\include\rdma\rsocksvc.h %2\Inc\rdma /Y/Q
    xcopy ..\etc\user %2\Inc\etc\user /I/S/Y/Q
    popd
)

rem WDK/WIX, Docs & IB SDK items
if "%DBG%" == "TRUE" echo DBG: WDK, WIx, Docs and SDK files

if exist %2\Misc  rmdir /Q/S %2\Misc
mkdir %2\Misc

copy /Y/A %1\Docs\Manual.htm %2\Misc\Manual.htm
copy /Y/A %1\tests\cmtest\user\cmtest_main.c %2\Misc\cmtest.c

rem copy 'Driver Install Frameworks for Applications' files so WIX makefiles
rem are not required to know the current WDK version/path.

for %%i in ( amd64 ia64 x86 ) do (
	mkdir %2\Misc\%%i
	if ERRORLEVEL 1 (
		echo ERR on mkdir %2\Misc\DIFxAPP\%%i
		exit /B 1
	)
	for %%j in ( DIFxApp.dll DIFxAppA.dll DIFxApp.wixlib ) do (
		copy /B/Y %DIFXP%\%%i\%%j  %2\Misc\%%i\%%j
		if ERRORLEVEL 1 (
			echo ERR on copy /B/Y %DIFXP%\%%i\%%j  %2\Misc\%%i\%%j
			exit /B 1
		)
	)
	copy /B/Y %DPINST%\%%i\DPInst.exe  %2\Misc\%%i\DPInst.exe
	if ERRORLEVEL 1 (
		echo ERR on copy /B/Y %DPINST%\%%i\DPInst.exe  %2\Misc\%%i\DPInst.exe
		exit /B 1
	)
)

goto end

:usage
echo makebin src dest os
echo   src	base directory.
echo   dest	directory in which to build the installable binary tree.
echo   os   Windows version [wlh, wnet, wxp]
goto end

:error1
echo %1\bin\kernel\objfre_%OSE%_amd64\amd64 missing 
goto end
:error2
echo %1\bin\kernel\objfre_%OSE%_ia64\ia64 missing 
goto end
:error3
echo %1\bin\kernel\objfre_%OSE%_x86\i386 missing 
goto end
:error4
echo %1\bin\user\objfre_%OSE%_amd64\amd64 missing 
goto end
:error5
echo %6\bin\user\objfre_%OSE%_ia64\ia64 missing 
goto end
:error6
echo %1\bin\user\objfre_%OSE%_x86\i386 missing 

:end
echo.
echo Finished OS %3
echo.

endlocal

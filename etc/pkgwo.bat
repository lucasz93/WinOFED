@echo off
setlocal
setlocal enabledelayedexpansion

if "%WDK_PATH%"=="" goto use
if "%OPENIB_REV%"=="" goto use
if "%PLATFORM_SDK_PATH%"=="" goto use

if "%1"=="chk" (
	set wo_bld=chk
	goto set_arch
)
if "%1"=="fre" (
	set wo_bld=fre
	goto set_arch
)
goto use

:set_arch
if "%2"=="x86" (
	set wo_arch=x86
	set wo_arch_dir=i386
	goto set_os
)
if "%2"=="x64" (
	set wo_arch=amd64
	set wo_arch_dir=amd64
	goto set_os
)
if "%2"=="ia64" (
	set wo_arch=ia64
	set wo_arch_dir=ia64
	goto set_os
)
goto use

:set_os
if "%3"=="win7" (
	set wo_os=win7
	goto set_files
)
if "%3"=="2003" (
	set wo_os=wnet
	goto set_files
)
if "%3"=="2008" (
	set wo_os=WLH
	goto set_files
)
if "%3"=="xp" (
	if not "%2"=="x86" goto use
	set wo_os=WXP
	set wo_arch=
	goto set_files
)
goto use

:set_files
if "%4"=="all" (
	for %%i in (dapl, ibacm, ibbus, ipoib, mlx4_bus, mlx4_hca, mthca, winmad, winverbs) do (
		call %~dp0\pkgwo %1 %2 %3 %%i
	)
	popd
	goto end
)
if "%4"=="dapl" (
	set files=dapl dat dtest
	goto package
)
if "%4"=="ibacm" (
	set files=ib_acm libibacm
	goto package
)
if "%4"=="ibbus" (
	set files=ib_bus ibbus ibal alts cmtest complib ib_read ib_send ib_write ib_limit
	goto package
)
if "%4"=="ipoib" (
	set files=ipoib nd ibwsd netipoib wvnd ibat ibnd
	goto package
)
if "%4"=="mlx4_bus" (
	set files=mlx4_bus mlx4_core
	goto package
)
if "%4"=="mlx4_hca" (
	set files=mlx4_hca mlx4u mlx4_ib mlx4_net
	goto package
)
if "%4"=="mthca" (
	set files=mthca
	goto package
)
if "%4"=="winmad" (
	rem -- Includes IB-mgmt libraries
	set files=winmad wm ^
		  libibumad libibmad libibnet ibaddr iblinkinfo ibnetdiscover ibping ^
		  ibportstate ibqueryerror ibroute ibsendtrap ibstat ^
		  ibsysstat ibtracert mcm_rereg perfquery saquery sminfo smp vendstat ^
		  opensm osmtest ibtrapgen
	goto package
)
if "%4"=="winverbs" (
	rem -- Includes OFED verbs and RDMA CM compatability libraries
	set files=winverb wv ^
		  libibverbs ibv_async ibv_dev ibv_ librdmacm rdma_
	goto package
)
goto use

:package
pushd %WINOF_PATH%
if not exist install             mkdir install
if not exist install\%3          mkdir install\%3
if not exist install\%3\%2       mkdir install\%3\%2
if not exist install\%3\%2\%1    mkdir install\%3\%2\%1
if not exist install\%3\%2\%1\%4 mkdir install\%3\%2\%1\%4

for %%i in (%files%) do (
	xcopy /D /Y bin\kernel\obj%wo_bld%_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i* install\%3\%2\%1\%4
	xcopy /D /Y bin\user\obj%wo_bld%_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*   install\%3\%2\%1\%4

	rem -- Include both free and checked versions of libraries in the package.
	rem -- The library names do not overlap.
 	if "%1"=="chk" (
		xcopy /D /Y bin\user\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.dll install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.pdb install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.exp install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objfre_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.lib install\%3\%2\%1\%4
	)
	if "%1"=="fre" (
		xcopy /D /Y bin\user\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.dll install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.pdb install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.exp install\%3\%2\%1\%4
		xcopy /D /Y bin\user\objchk_%wo_os%_%wo_arch%\%wo_arch_dir%\%%i*.lib install\%3\%2\%1\%4
	)
	
	rem -- Include 32-bit libaries with the 64-bit package.  Rename the 32-bit
	rem -- libraries from lib.dll -> lib32.dll or libd.dll -> lib32d.dll.
	rem -- complib, which changes to cl32
	rem -- mthca, which drops the 'u'
	if "%2"=="x64" (	
		if not exist install\wow64\%3\%2\chk\%4 mkdir install\wow64\%3\%2\chk\%4
		xcopy /D /Y bin\user\objchk_%wo_os%_x86\i386\%%i*.dll install\wow64\%3\%2\chk\%4
		pushd install\wow64\%3\%2\chk\%4
		for /f "usebackq" %%j in (`dir /b *d.dll`) do (
			set dll32_old=%%j
			set dll32_new=!dll32_old:~,-5!32d.dll
			if "!dll32_old!"=="complibd.dll" set dll32_new=cl32d.dll
			if "!dll32_old!"=="mthcaud.dll"  set dll32_new=mthca32d.dll
			echo F | xcopy /D /Y !dll32_old! %WINOF_PATH%\install\%3\%2\%1\%4\!dll32_new!
		)
		popd

		if not exist install\wow64\%3\%2\fre\%4 mkdir install\wow64\%3\%2\fre\%4
		xcopy /D /Y bin\user\objfre_%wo_os%_x86\i386\%%i*.dll install\wow64\%3\%2\fre\%4
		pushd install\wow64\%3\%2\fre\%4
		for /f "usebackq" %%j in (`dir /b *.dll`) do (
			set dll32_old=%%j
			set dll32_new=!dll32_old:~,-4!32.dll
			if "!dll32_old!"=="complib.dll" set dll32_new=cl32.dll
			if "!dll32_old!"=="mthcau.dll"  set dll32_new=mthca32.dll
			echo F | xcopy /D /Y !dll32_old! %WINOF_PATH%\install\%3\%2\%1\%4\!dll32_new!
		)
		popd
	)
	
	xcopy /D /Y %WDK_PATH%\redist\wdf\%wo_arch_dir%\wdf* install\%3\%2\%1\%4
	xcopy /D /Y %WDK_PATH%\redist\difx\dpinst\multilin\%wo_arch_dir%\dpinst* install\%3\%2\%1\%4
)

popd
goto end

:use
echo -
echo pkgwo - package winof
echo -
echo Separates a built WinOF tree into separate packages for installation.
echo You should build both the free and checked versions of the specified
echo package before running this batch file for the first time.
echo You must customize for your system by setting the following environment
echo variables:
echo -
echo WDK_PATH          (example set WDK_PATH=c:\winddk\6001.18001)
echo WINOF_PATH        (example set WINOF_PATH=c:\ofw\trunk)
echo OPENIB_REV        (example set OPENIB_REV=0)
echo PLATFORM_SDK_PATH (example set PLATFORM_SDK_PATH=c:\progra~1\mi2578~1)
echo -
echo Use:
echo pkgwo {chk : fre} {x86 : x64 : ia64} {xp : 2003 : 2008 : win7} {package}
echo xp only supports x86 build
echo -
echo Valid package names are:
echo all, dapl, ibacm, ibbus, ipoib, mlx4_bus, mlx4_hca, mthca, winmad, winverbs
echo -
echo Examples:
echo pkgwo chk x86 2008 winverbs  - packages 2008 checked x86 files for winverbs
echo pkgwo fre x64 win7 mlx4_bus  - packages windows 7 free x64 files for mlx4_bus
echo -
echo Packages are created under WINOF_PATH\install.
echo See docs\build.txt for additional information on building the tree.

:end
endlocal
@echo on

set DDK_HOME=\\tzachid0\c$\Winddk\3790.1830

rem if %PROCESSOR_ARCHITECTURE% == x86 (set ARCH_PATH=i386) else set ARCH_PATH=AMD64
rem use same binaries for 32 and 64 bits
set ARCH_PATH=i386

mkdir %SystemDrive%\trace
copy %DDK_HOME%\tools\tracing\%ARCH_PATH%\*.exe %SystemDrive%\trace
copy %DDK_HOME%\tools\tracing\%ARCH_PATH%\*.dll %SystemDrive%\trace

copy %DDK_HOME%\tools\tracing\i386\tracepdb.exe %SystemDrive%\trace

copy %DDK_HOME%\bin\x86\mspdb70.dll %SystemDrive%\trace
copy %DDK_HOME%\bin\x86\msvcr70.dll %SystemDrive%\trace
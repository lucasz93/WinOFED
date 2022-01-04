@echo off
rem
rem Builds the winof stack for all platforms.
rem

call %~dp0\bldwo chk x86  2008 %*
call %~dp0\bldwo fre x86  2008 %*
call %~dp0\bldwo chk x64  2008 %*
call %~dp0\bldwo fre x64  2008 %*
call %~dp0\bldwo chk x86  win7 %*
call %~dp0\bldwo fre x86  win7 %*
call %~dp0\bldwo chk x64  win7 %*
call %~dp0\bldwo fre x64  win7 %*

@echo on

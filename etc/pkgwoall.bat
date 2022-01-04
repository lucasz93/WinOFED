@echo off
rem
rem Packages the winof stack for all platforms.
rem

call %~dp0\pkgwo chk x86  2003 all
call %~dp0\pkgwo fre x86  2003 all
call %~dp0\pkgwo chk x64  2003 all
call %~dp0\pkgwo fre x64  2003 all
call %~dp0\pkgwo chk ia64 2003 all
call %~dp0\pkgwo fre ia64 2003 all
call %~dp0\pkgwo chk x86  2008 all
call %~dp0\pkgwo fre x86  2008 all
call %~dp0\pkgwo chk x64  2008 all
call %~dp0\pkgwo fre x64  2008 all
call %~dp0\pkgwo chk ia64 2008 all
call %~dp0\pkgwo fre ia64 2008 all

@echo on

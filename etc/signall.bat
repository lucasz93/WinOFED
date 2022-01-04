@echo off
setlocal

for /f "usebackq" %%i in (`dir /AD /b`) do (
	inf2cat /driver:%%i /os:server2008_x64,server2008r2_x64
	pushd %%i
	call signwo
	popd
)

@echo on
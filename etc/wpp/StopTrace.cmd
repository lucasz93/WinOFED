tracelog.exe -stop MTHCALog
tracelog.exe -stop IBALLog
rem tracelog.exe -stop SDPLOg

set TRACE_FORMAT_PREFIX=%%7!08d! %%!LEVEL! %%2!s!: %%8!04x!.%%3!04x!: %%4!s!: %%!FUNC!:

tracefmt.exe -p tmf -display -v -displayonly -nosummary  | sort > aaa
start notepad aaa

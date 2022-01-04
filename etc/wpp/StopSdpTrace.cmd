rem level=32 => Highest 4 = information, 3 = warning

tracelog -stop SdpDetailedRt

set TRACE_FORMAT_PREFIX=%%7!07d! %%2!s! %%8!04x!.%%3!04x!: %%4!s!: %%!COMPNAME! %%!FUNC! 


tracefmt.exe -seq -p tmf C:\LogFile.Etl -nosummary -hires -o result.txt



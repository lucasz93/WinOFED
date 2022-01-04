rem level=32 => Highest 4 = information, 3 = warning

tracepdb.exe -f ibal.pdb -p tmf
tracepdb.exe -f ibbus.pdb -p tmf

set TRACE_FORMAT_PREFIX=%%7!08d! %%2!s!: %%8!04x!:

tracelog -stop ALDetailedRt

tracelog -start ALDetailedRt -ls -guid #B199CE55-F8BF-4147-B119-DACD1E5987A6 -flag 0x0f00 -level 5 -rt -ft 1
tracelog -enable ALDetailedRt -guid #99DC84E3-B106-431e-88A6-4DD20C9BBDE3 -flag 0x0f00 -level 5
tracefmt.exe -rt ALDetailedRt -Displayonly -p tmf -ods

tracelog -stop ALDetailedRt 

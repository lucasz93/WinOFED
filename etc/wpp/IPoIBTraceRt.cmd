rem level=32 => Highest 4 = information, 3 = warning

tracepdb.exe -f ipoib.pdb -p tmf


set TRACE_FORMAT_PREFIX=%%7!08d! %%2!s!: %%8!04x!: 

tracelog -stop IPoIBdRt

tracelog -start IPoIBdRt -ls -guid #3F9BC73D-EB03-453a-B27B-20F9A664211A -flag 0x0fff -level 5 -rt -ft 1
tracefmt.exe -rt IPoIBdRt -Displayonly -p tmf -ods

tracelog -stop IPoIBdRt 

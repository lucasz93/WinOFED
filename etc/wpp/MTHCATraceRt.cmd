rem level=32 => Highest 4 = information, 3 = warning

tracepdb.exe -f mthca.pdb -p tmf
tracepdb.exe -f mthcau.pdb -p tmf

set TRACE_FORMAT_PREFIX=%%7!08d! %%2!s!: %%8!04x!:

tracelog -stop MTHCALogdRt

tracelog -start MTHCALogdRt -ls -guid #8BF1F640-63FE-4743-B9EF-FA38C695BFDE -flag 0x0f00 -level 5 -rt -ft 1
tracelog -enable MTHCALogdRt -guid #2C718E52-0D36-4bda-9E58-0FC601818D8F -flag 0x0f00 -level 5
tracefmt.exe -rt MTHCALogdRt -Displayonly -p tmf -ods

tracelog -stop MTHCALogdRt 

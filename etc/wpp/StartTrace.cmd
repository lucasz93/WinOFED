rem MTHCA
del c:\WinIB1.etl
del c:\WinIB2.etl
tracelog -start   MTHCALog -ls  -guid #8BF1F640-63FE-4743-B9EF-FA38C695BFDE  -flag 0x1 -level 2 -UseCPUCycle -f c:\WInIB1.etl
tracelog -enable  MTHCALog     -guid #2C718E52-0D36-4bda-9E58-0FC601818D8F     -flag 0x1 -level 2



rem IBAL
tracelog -start   IBALLog -ls   -guid #B199CE55-F8BF-4147-B119-DACD1E5987A6  -flag 0x1 -level 2 -UseCPUCycle -f c:\WInIB2.etl
tracelog -enable  IBALLog       -guid #99DC84E3-B106-431e-88A6-4DD20C9BBDE3     -flag 0x1 -level 2



rem SDP
rem tracelog -start  SDPLog -ls -guid #D6FA8A24-9457-455d-9B49-3C1E5D195558 -flag 0xffff -level 32 -UseCPUCycle
rem tracelog -enable SDPLOg     -guid #2D4C03CC-E071-48e2-BDBD-526A0D69D6C9 -flag 0xffff -level 32

rem SDP
rem tracelog -start  SDPLog -ls -guid #D6FA8A24-9457-455d-9B49-3C1E5D195558 -flag 0xffff -level 32 -UseCPUCycle
rem tracelog -enable SDPLOg     -guid #2D4C03CC-E071-48e2-BDBD-526A0D69D6C9 -flag 0xffff -level 32


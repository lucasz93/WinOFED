rem level=32 => Highest 4 = information, 3 = warning

tracelog -stop SdpDetailedRt

tracelog -start SdpDetailedRt -ls -guid #D6FA8A24-9457-455d-9B49-3C1E5D195558 -flag 0xffff -level 4 -rt -ft 1
tracelog -enable SdpDetailedRt -guid #2D4C03CC-E071-48e2-BDBD-526A0D69D6C9 -flag 0xffff -level 4
tracefmt.exe -rt SdpDetailedRt -Displayonly -p tmf -ods

tracelog -stop SdpDetailedRt


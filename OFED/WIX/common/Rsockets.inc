<Include>
    <Component Id='cRsockets'
               Guid='EB466A59-F695-45B4-B560-EB68058A309F'
               SharedDllRefCount='no' KeyPath='no'
               NeverOverwrite='no'
               Permanent='no' Transitive='no'
               Location='either'>

         <File Id="rstream.exe" DiskId="1"
               Name="rstream.exe"
               Source="..\bin\tools\$(var.ARCH)\release\rstream.exe" />
         <File Id="riostream.exe" DiskId="1"
               Name="riostream.exe"
               Source="..\bin\tools\$(var.ARCH)\release\riostream.exe" />
         <File Id="rsinstall.exe" DiskId="1"
               Name="rsinstall.exe"
               Source="..\bin\tools\$(var.ARCH)\release\rsinstall.exe" />
         <File Id="rsocksvc.exe" DiskId="1"
               Name="rsocksvc.exe"
               Source="..\bin\tools\$(var.ARCH)\release\rsocksvc.exe" />
         <File Id="rnetstat.exe" DiskId="1"
               Name="rnetstat.exe"
               Source="..\bin\tools\$(var.ARCH)\release\rnetstat.exe" />
    </Component>

    <Component Id="cRsocketsService" KeyPath="yes"
               Guid="A69357F7-0CFB-4335-985A-668A702B9F7B">

    </Component>

</Include>

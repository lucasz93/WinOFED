<Include>

      <Component Id="cDAPL2RT" Guid="02C3490D-EF11-40C9-9E8D-0BEE97082A6A">

          <File Id="dapl2test.exe" DiskId="1"
                 Name="dapl2test.exe"
                 Source="..\bin\tools\$(var.ARCH)\release\dapl2test.exe" />
          <File Id="dtest2.exe" DiskId="1"
                 Name="dtest2.exe"
                 Source="..\bin\tools\$(var.ARCH)\release\dtest2.exe" />
          <File Id="dtestx.exe" DiskId="1"
                 Name="dtestx.exe"
                 Source="..\bin\tools\$(var.ARCH)\release\dtestx.exe" />
          <File Id="dtestcm.exe" DiskId="1"
                 Name="dtestcm.exe"
                 Source="..\bin\tools\$(var.ARCH)\release\dtestcm.exe" />
          <File Id="dt_svr.bat" DiskId="1"
                Name="dt-svr.bat"
                Source="..\bin\DAPL2\dt-svr.bat" />
          <File Id="dt_cli.bat" DiskId="1"
                Name="dt-cli.bat"
                Source="..\bin\DAPL2\dt-cli.bat" />

          <File Id="dapl2.pdb" DiskId="1"
                Name="dapl2.pdb"
                Source="..\bin\DAPL2\$(var.ARCH)\dapl2.pdb" />
          <File Id="dat2.pdb" DiskId="1"
                Name="dat2.pdb"
                Source="..\bin\DAPL2\$(var.ARCH)\dat2.pdb" />
          <File Id="dat2.lib" DiskId="1"
                Name="dat2.lib"
                Source="..\bin\DAPL2\$(var.ARCH)\dat2.lib" />
      </Component>

</Include>

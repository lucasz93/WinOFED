<Include>
     <Component Id="Tools" Guid="1B89230E-5D8D-41a5-BD1F-A3E308FEE026">

      <!-- Add INSTALLDIR to the system-wide PATH evnironment var -->
      <Environment Id='UpdatePath' Name='PATH' Action='set'
                   System='yes' Part='last' Value='[INSTALLDIR]' /> 
<!--
       <File Id="qlgcvnic_config" DiskId="1" Vital="yes"
             Name="qlgcvnic_config.exe"
             Source="..\bin\tools\$(var.ARCH)\release\qlgcvnic_config.exe" />
-->
       <File Id="part_man" Name="part_man.exe"
             DiskId="1" Vital="yes"
             Source="..\bin\tools\$(var.ARCH)\release\part_man.exe" />
       <File Id="vstat" Name="vstat.exe"
             DiskId="1" Vital="yes"
             Source="..\bin\tools\$(var.ARCH)\release\vstat.exe" />
       <File Id="ttcp.exe" Name="ttcp.exe"
             DiskId="1"
             Source="..\bin\tools\$(var.ARCH)\release\ttcp.exe" />
       <File Id="cmtest.exe" Name="cmtest.exe"
             DiskId="1" Source="..\bin\tools\$(var.ARCH)\release\cmtest.exe" />
       <File Id="ib_limits.exe" DiskId="1"
             Name="ib_limits.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_limits.exe" />
       <File Id="ib_read_bw.exe" DiskId="1"
             Name="ib_read_bw.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_read_bw.exe" />
       <File Id="ib_read_lat.exe" DiskId="1"
             Name="ib_read_lat.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_read_lat.exe" />
       <File Id="ib_send_bw.exe" DiskId="1"
             Name="ib_send_bw.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_send_bw.exe" />
       <File Id="ib_send_lat.exe" DiskId="1"
             Name="ib_send_lat.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_send_lat.exe" />
       <File Id="ib_write_bw.exe" DiskId="1"
             Name="ib_write_bw.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_write_bw.exe" />
       <File Id="ib_write_lat.exe" DiskId="1"
             Name="ib_write_lat.exe"
             Source="..\bin\tools\$(var.ARCH)\release\ib_write_lat.exe" />
       <File Id="nsc" Name="nsc.exe" DiskId="1"
                Vital="yes" Source="..\bin\tools\$(var.ARCH)\release\nsc.exe" />
       <File Id="NDlist" Name="NDlist.exe" DiskId="1"
                Vital="yes" Source="..\bin\tools\$(var.ARCH)\release\NDlist.exe" />

       <?if $(var.OS) != "win8" ?>
           <File Id="installsp.exe" DiskId="1"
                 Name="installsp.exe"
                 Source="..\bin\net\$(var.ARCH)\installsp.exe" />
       <?endif?>

     </Component>
</Include>

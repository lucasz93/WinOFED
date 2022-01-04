<Include xmlns:DifxApp='http://schemas.microsoft.com/wix/DifxAppExtension' >

    <!-- RDMA SCSI over IB (aka SRP) -->

   <Directory Id="srpDir" Name="SRP">

      <Component Id="cSRP_driver_installed"
               Guid="6945E66C-ADC0-4258-8858-AE7E7DF70495" >

        <DifxApp:Driver
            DeleteFiles="yes"
            ForceInstall="no"
            Legacy="no"
            PlugAndPlayPrompt="no"
            AddRemovePrograms="no"
            Sequence="7" />

        <CreateFolder />

        <File Id="drv_ib_srp.inf" DiskId="1" Vital="yes"
              Name="IB_SRP.INF"
              Source="..\bin\storage\$(var.ARCH)\ib_srp.inf" />
        <File Id="drv_ibsrp.cat" DiskId="1" Vital="yes"
              Name="IBSRP.CAT"
              Source="..\bin\storage\$(var.ARCH)\ibsrp.cat" />
        <File Id="drv_ibsrp.sys" DiskId="1" Vital="yes"
              Name="IBSRP.SYS" 
              Source="..\bin\storage\$(var.ARCH)\ibsrp.sys" />
        <File Id="ibsrp.pdb" DiskId="1"
              Name="ibsrp.pdb"
              Source="..\bin\storage\$(var.ARCH)\ibsrp.pdb" />
      </Component>

   </Directory>

</Include>

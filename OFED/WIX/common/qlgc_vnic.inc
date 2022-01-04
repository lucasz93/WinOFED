<Include xmlns:DifxApp='http://schemas.microsoft.com/wix/DifxAppExtension' >

   <Directory Id="qlgcvnicDir" Name="qlgcvnic">

      <Component Id="cQLVNIC_driver_installed"
               Guid="730C742E-73D9-4871-A523-6FB7963EA01E" >

        <DifxApp:Driver
            DeleteFiles="yes"
            ForceInstall="no"
            Legacy="no"
            PlugAndPlayPrompt="no"
            AddRemovePrograms="no"
            Sequence="6" />

        <CreateFolder />

        <File Id="qlgcvnic.sys" DiskId="1"
              Name="qlgcvnic.sys"
              Source="..\bin\net\$(var.ARCH)\qlgcvnic.sys" />
        <File Id="qlgcvnic.pdb" DiskId="1"
              Name="qlgcvnic.pdb"
              Source="..\bin\net\$(var.ARCH)\qlgcvnic.pdb" />
        <File Id="netvnic.inf" DiskId="1"
              Name="netvnic.inf"
              Source="..\bin\net\$(var.ARCH)\netvnic.inf" />
        <File Id="netvnic.cat" DiskId="1"
              Name="netvnic.cat"
              Source="..\bin\net\$(var.ARCH)\netvnic.cat" />
      </Component>
   </Directory>
</Include>

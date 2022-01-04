<Include xmlns:DifxApp='http://schemas.microsoft.com/wix/DifxAppExtension'>

  <Directory Id="IOU" Name="IOU">
    <Component Id="cIOUdriver" Guid="77F3A3C5-0C12-4e21-A788-8B9F1BF89A01" >

      <CreateFolder />

      <DifxApp:Driver
          DeleteFiles="yes"
          ForceInstall="no"
          Legacy="no"
          PlugAndPlayPrompt="no"
          AddRemovePrograms="no"
          Sequence="5" />

      <File Id="ibiou.sys" DiskId="1"
             Name="ibiou.sys"
             Source="..\bin\HCA\$(var.ARCH)\ibiou.sys" />
      <File Id="ibiou.pdb" DiskId="1"
             Name="ibiou.pdb"
             Source="..\bin\HCA\$(var.ARCH)\ibiou.pdb" />
      <File Id="ib_iou.cat" DiskId="1"
             Name="ib_iou.cat"
             Source="..\bin\HCA\$(var.ARCH)\ib_iou.cat" />
      <File Id="ib_iou.inf" DiskId="1"
             Name="ib_iou.inf"
             Source="..\bin\HCA\$(var.ARCH)\ib_iou.inf" />

    </Component>
  </Directory>
</Include>

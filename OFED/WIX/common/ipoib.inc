<Include xmlns:DifxApp='http://schemas.microsoft.com/wix/DifxAppExtension' >

 <Directory Id="ipoibDir" Name="net">

    <Component Id="cIPoIB" Guid="18CC13CC-0D29-4B88-8F03-854CE0C6AF94" >

      <DifxApp:Driver
          DeleteFiles="yes"
          ForceInstall="no"
          Legacy="no"
          PlugAndPlayPrompt="no"
          AddRemovePrograms="no"
          Sequence="4" />

      <CreateFolder />

      <File Id="netipoib_inf" DiskId="1"
             Name="netipoib.inf"
             Source="..\bin\net\$(var.ARCH)\netipoib.inf" />
      <File Id="ipoib_cat" DiskId="1"
             Name="ipoib.cat"
             Source="..\bin\net\$(var.ARCH)\ipoib.cat" />
      <File Id="ipoib.sys" DiskId="1"
             Name="ipoib.sys"
             Source="..\bin\net\$(var.ARCH)\ipoib.sys" />
      <File Id="ipoib.pdb" DiskId="1"
             Name="ipoib.pdb"
             Source="..\bin\net\$(var.ARCH)\ipoib.pdb" />

      <?if $(var.OS) != "XP" ?>
        <File Id="ibwsd.dll" DiskId="1"
               Name="ibwsd.dll"
               Source="..\bin\net\$(var.ARCH)\ibwsd.dll" />

        <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
            <File Id="ibwsd32.dll" DiskId="1"
                 Name="ibwsd32.dll"
                 Source="..\bin\net\$(var.ARCH)\ibwsd32.dll" />

            <RemoveFile Id='rm64_ibwsd.dll' On='uninstall'
                 Property="System64Folder"
                 Name="ibwsd.dll" />
            <RemoveFile Id='rm64_ibwsd32.dll' On='uninstall'
                 Property="System64Folder" Name="ibwsd32.dll" />
            <RemoveFile Id='rm32_ibwsd32.dll' On='uninstall'
                 Property="SystemFolder" Name="ibwsd32.dll" />
        <?endif?>

      <?endif?>

      <File Id="wvndprov.dll" DiskId="1"
            Name="wvndprov.dll"
            Source="..\bin\net\$(var.ARCH)\wvndprov.dll" />

      <?if $(var.ARCHP) = "ia64" ?>
          <File Id="wvndprov32.dll" DiskId="1"
                 Name="wvndprov32.dll"
                 Source="..\bin\net\$(var.ARCH)\wvndprov32.dll" />
          <RemoveFile Id='rmw64_wvndprov.dll' On='uninstall'
                 Property="System64Folder" Name='wvndprov.dll' />
      <?else?>
          <File Id="ibndprov.dll" DiskId="1"
                Name="ibndprov.dll"
                Source="..\bin\net\$(var.ARCH)\ibndprov.dll" />
      <?endif?>

      <?if $(var.ARCHP) = "x64" ?>
          <File Id="ibndprov32.dll" DiskId="1"
                 Name="ibndprov32.dll"
                 Source="..\bin\net\$(var.ARCH)\ibndprov32.dll" />

          <File Id="wvndprov32.dll" DiskId="1"
                 Name="wvndprov32.dll"
                 Source="..\bin\net\$(var.ARCH)\wvndprov32.dll" />

          <RemoveFile Id='rm64_ibndprov.dll' On='uninstall'
                 Property="System64Folder" Name='ibndprov.dll' />
          <RemoveFile Id='rm64_wvndprov.dll' On='uninstall'
                 Property="System64Folder" Name='wvndprov.dll' />
      <?endif?>

      <!-- Orphaned install files - remove on uninstall -->

      <RemoveFile Id='rm_ipoib.sys' On='uninstall'
             Property="SystemFolder" Name="ipoib.sys" />
      <RemoveFile Id='rm32_ibwsd.dll' On='uninstall'
             Property="SystemFolder" Name='ibwsd.dll' />
      <RemoveFile Id='o64_ibndprov.dll' On='uninstall'
             Property="SystemFolder" Name='ibndprov.dll' />
      <RemoveFile Id='rm32_wvndprov.dll' On='uninstall'
             Property="SystemFolder" Name='wvndprov.dll' />

    </Component>
 </Directory>

</Include>

<Include xmlns:DifxApp='http://schemas.microsoft.com/wix/DifxAppExtension' >

  <Directory Id="dMTHCA" Name="mthca">

    <Component Id="cMthcaDriver" Guid="BE4ED061-C852-40a2-A240-F06DBBE1159F">

      <DifxApp:Driver
          DeleteFiles="yes"
          ForceInstall="no"
          Legacy="no"
          PlugAndPlayPrompt="no"
          AddRemovePrograms="no"
          Sequence="1" />

      <CreateFolder />

      <File Id="mthca.inf" DiskId="1" Vital="yes" KeyPath="yes"
             Name="mthca.inf"
             Source="..\bin\HCA\$(var.ARCH)\mthca.inf" />
      <File Id="mthca.cat" DiskId="1"
             Name="mthca.cat"
             Source="..\bin\HCA\$(var.ARCH)\mthca.cat" />
      <File Id="mthca.sys" DiskId="1"
             Name="mthca.sys"
             Source="..\bin\HCA\$(var.ARCH)\mthca.sys" />
      <File Id="mthca.pdb" DiskId="1"
             Name="mthca.pdb"
             Source="..\bin\HCA\$(var.ARCH)\mthca.pdb" />
      <File Id="mthcau.dll" DiskId="1"
             Name="mthcau.dll"
             Source="..\bin\HCA\$(var.ARCH)\mthcau.dll" />
      <File Id="mthca_wdfcoinstaller" DiskId="1"
             Name="WdfCoInstaller$(var.WDFVER).dll"
             Source="..\bin\hca\$(var.ARCH)\WdfCoInstaller$(var.WDFVER).dll" />

      <?if $(var.ARCHP) = "x64" Or $(var.ARCH) = "ia64" ?>
          <File Id="mthca32.dll" DiskId="1"
                 Name="mthca32.dll"
                 Source="..\bin\HCA\$(var.ARCH)\mthca32.dll" />

          <!-- Orphaned install files - remove on uninstall -->
          <RemoveFile Id='rm64_mthcau.dll' On='uninstall'
                 Property="System64Folder"
                 Name='mthcau.dll' />
          <RemoveFile Id='rm64_mthcaud.dll' On='uninstall'
                 Property="System64Folder"
                 Name='mthcaud.dll' />
          <RemoveFile Id='rm64_mthca.sys' On='uninstall'
                 Property="System64Folder"
                 Name='mthca.sys' />
      <?endif?>

      <!-- Sometimes Orphaned install files - remove on uninstall -->
      <RemoveFile Id='rm_mthcau.dll' On='uninstall'
             Property="SystemFolder"
             Name="mthcau.dll" />
      <RemoveFile Id='rm_mthcaud.dll' On='uninstall'
             Property="SystemFolder"
             Name='mthcaud.dll' />
      <RemoveFile Id='rm_mthca.sys' On='uninstall'
             Property="SystemFolder"
             Name="mthca.sys" />

      <?define FLTR=mth_ ?> 
      <?include $(sys.CURRENTDIR)\..\..\common\hca_filters.inc ?>

    </Component>
  </Directory>

  <Directory Id="dMLX4_bus" Name="mlx4_bus">
    <Component Id="cMLX4BusDriver" Guid="B7EFA77D-1CE7-4bd1-BCFF-077D90C4B06E" >

      <CreateFolder />

      <DifxApp:Driver
          DeleteFiles="yes"
          ForceInstall="no"
          Legacy="no"
          PlugAndPlayPrompt="no"
          AddRemovePrograms="no"
          Sequence="3" />

      <File Id="mlx4_bus.inf" DiskId="1"
             Name="mlx4_bus.inf" 
             Source="..\bin\hca\$(var.ARCH)\mlx4_bus.inf" />
      <File Id="mlx4_bus.cat" DiskId="1"
             Name="mlx4_bus.cat"
             Source="..\bin\hca\$(var.ARCH)\mlx4_bus.cat" />
      <File Id="mlx4_bus.sys" DiskId="1"
             Name="mlx4_bus.sys"
             Source="..\bin\hca\$(var.ARCH)\mlx4_bus.sys" />
      <File Id="mlx4_bus.pdb" DiskId="1"
             Name="mlx4_bus.pdb"
             Source="..\bin\hca\$(var.ARCH)\mlx4_bus.pdb" />
      <File Id="mlx2_wdfcoinstaller" DiskId="1"
             Name="WdfCoInstaller$(var.WDFVER).dll"
             Source="..\bin\hca\$(var.ARCH)\WdfCoInstaller$(var.WDFVER).dll" />

      <File Id="mlx4u.dll" DiskId="1"
             Name="mlx4u.dll"
             Source="..\bin\hca\$(var.ARCH)\mlx4u.dll" />

      <File Id="mlx4nd.dll" DiskId="1"
             Name="mlx4nd.dll"
             Source="..\bin\hca\$(var.ARCH)\mlx4nd.dll" />

      <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
          <!-- copy 32-bit binaries for 32b in x64 -->
          <File Id="mlx4u32.dll" DiskId="1"
                 Name="mlx4u32.dll"
                 Source="..\bin\hca\$(var.ARCH)\mlx4u32.dll" />
          <File Id="mlx4nd32.dll" DiskId="1"
                 Name="mlx4nd32.dll"
                 Source="..\bin\hca\$(var.ARCH)\mlx4nd32.dll" />
      <?endif?>

      <!-- Orphaned install files - remove on uninstall -->
      <RemoveFile Id='rm_mlx4_bus.sys' On='uninstall'
             Property="SystemFolder"
             Name="mlx4_bus.sys" />
      <RemoveFile Id='rm_ndinstall.exe' On='uninstall'
             Property="SystemFolder"
             Name='ndinstall.exe' />
      <RemoveFile Id='rm_mlx4nd.dll' On='uninstall'
             Property="SystemFolder"
             Name='mlx4nd.dll' />
      <RemoveFile Id='rm_mlx4u.dll' On='uninstall'
             Property="SystemFolder"
             Name='mlx4u.dll' />

      <?undef FLTR ?> 
      <?define FLTR=mlx_ ?> 
      <?include $(sys.CURRENTDIR)\..\..\common\hca_filters.inc ?>

    </Component>
  </Directory>

</Include>

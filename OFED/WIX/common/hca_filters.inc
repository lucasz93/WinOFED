<Include>

      <!-- Assumes FLTR is uiniquely set, see mlnx_drivers.inc -->

      <File Id="$(var.FLTR)complib.dll" DiskId="1"
             Name="complib.dll"
             Source="..\bin\HCA\$(var.ARCH)\complib.dll" />

      <File Id="$(var.FLTR)ibal.dll" DiskId="1"
             Name="ibal.dll"
             Source="..\bin\HCA\$(var.ARCH)\ibal.dll" />

      <File Id="$(var.FLTR)ibbus.sys" DiskId="1"
             Name="ibbus.sys"
             Source="..\bin\HCA\$(var.ARCH)\ibbus.sys" />
      <File Id="$(var.FLTR)ibbus.pdb" DiskId="1"
             Name="ibbus.pdb"
             Source="..\bin\HCA\$(var.ARCH)\ibbus.pdb" />

      <File Id="$(var.FLTR)ndfltr.sys" DiskId="1"
             Name="ndfltr.sys"
             Source="..\bin\HCA\$(var.ARCH)\ndfltr.sys" />
      <File Id="$(var.FLTR)ndfltr.pdb" DiskId="1"
             Name="ndfltr.pdb"
             Source="..\bin\HCA\$(var.ARCH)\ndfltr.pdb" />

      <File Id="$(var.FLTR)ndinstall.exe" DiskId="1"
            Name="ndinstall.exe"
            Source="..\bin\HCA\$(var.ARCH)\ndinstall.exe" />

      <File Id="$(var.FLTR)libibverbs.dll" DiskId="1"
            Name="libibverbs.dll"
            Source="..\bin\HCA\$(var.ARCH)\libibverbs.dll" />

      <File Id="$(var.FLTR)libibmad.dll" DiskId="1"
            Name="libibmad.dll"
            Source="..\bin\HCA\$(var.ARCH)\libibmad.dll" />

      <File Id="$(var.FLTR)libibumad.dll" DiskId="1"
            Name="libibumad.dll"
            Source="..\bin\HCA\$(var.ARCH)\libibumad.dll" />
 
      <File Id="$(var.FLTR)librdmacm.dll" DiskId="1"
            Name="librdmacm.dll"
            Source="..\bin\HCA\$(var.ARCH)\librdmacm.dll" />
 
      <File Id="$(var.FLTR)libibnetdisc.dll" DiskId="1"
            Name="libibnetdisc.dll"
            Source="..\bin\HCA\$(var.ARCH)\libibnetdisc.dll" />

      <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>

          <File Id="$(var.FLTR)cl32.dll" DiskId="1"
                 Name="cl32.dll"
                 Source="..\bin\HCA\$(var.ARCH)\cl32.dll" />

          <File Id="$(var.FLTR)ibal32.dll" DiskId="1"
                 Name="ibal32.dll"
                 Source="..\bin\HCA\$(var.ARCH)\ibal32.dll" />

          <File Id="$(var.FLTR)libibverbs32.dll" DiskId="1"
                Name="libibverbs32.dll"
                Source="..\bin\HCA\$(var.ARCH)\libibverbs32.dll" />

          <File Id="$(var.FLTR)libibmad32.dll" DiskId="1"
                Name="libibmad32.dll"
                Source="..\bin\HCA\$(var.ARCH)\libibmad32.dll" />

          <File Id="$(var.FLTR)libibumad32.dll" DiskId="1"
                Name="libibumad32.dll"
                Source="..\bin\HCA\$(var.ARCH)\libibumad32.dll" />
 
          <File Id="$(var.FLTR)librdmacm32.dll" DiskId="1"
                Name="librdmacm32.dll"
                Source="..\bin\HCA\$(var.ARCH)\librdmacm32.dll" />
 
          <File Id="$(var.FLTR)libibnetdisc32.dll" DiskId="1"
                Name="libibnetdisc32.dll"
                Source="..\bin\HCA\$(var.ARCH)\libibnetdisc32.dll" />


          <RemoveFile Id='$(var.FLTR)rm64_ibal.dll' On='uninstall'
                 Property="System64Folder"
                 Name="ibal.dll" />
          <RemoveFile Id='$(var.FLTR)rm64_complib.dll' On='uninstall'
                 Property="System64Folder"
                 Name="complib.dll" />
      <?endif?>

      <!--
         Orphaned install files - remove on uninstall
         x64 SystemFolder == SysWOW64, System64Folder == System32
      -->
      <RemoveFile Id='$(var.FLTR)rm_ibal.dll' On='uninstall'
             Property="SystemFolder"
             Name="ibal.dll" />
      <RemoveFile Id='$(var.FLTR)rm_complib.dll' On='uninstall'
             Property="SystemFolder"
             Name="complib.dll" />

      <RemoveFile Id='$(var.FLTR)rm_libibverbs.dll' On='uninstall'
             Property="SystemFolder" Name="libibverbs.dll" />

      <RemoveFile Id='$(var.FLTR)rm64_libibverbs.dll' On='uninstall'
             Property="System64Folder" Name="libibverbs.dll" />

      <RemoveFile Id='$(var.FLTR)rm_librdmacm.dll' On='uninstall'
             Property="SystemFolder" Name="librdmacm.dll" />

      <RemoveFile Id='$(var.FLTR)rm64_librdmacm.dll' On='uninstall'
             Property="System64Folder" Name="librdmacm.dll" />

      <RemoveFile Id='$(var.FLTR)rm_libibmad.dll' On='uninstall'
             Property="SystemFolder" Name="libibmad.dll" />

      <RemoveFile Id='$(var.FLTR)rm64_libibmad.dll' On='uninstall'
             Property="System64Folder" Name="libibmad.dll" />

      <RemoveFile Id='$(var.FLTR)rm_libibumad.dll' On='uninstall'
             Property="SystemFolder" Name="libibumad.dll" />

      <RemoveFile Id='$(var.FLTR)rm64_libibumad.dll' On='uninstall'
             Property="System64Folder" Name="libibumad.dll" />

      <RemoveFile Id='$(var.FLTR)rm_ibbus.sys' On='uninstall'
             Property="SystemFolder" Name="ibbus.sys" />

      <RemoveFile Id='$(var.FLTR)rm_ndfltr.sys' On='uninstall'
             Property="SystemFolder" Name="ndfltr.sys" />

      <RemoveFile Id='$(var.FLTR)rm_ndinstall.exe' On='uninstall'
             Property="SystemFolder" Name="ndinstall.exe" />

      <RemoveFile Id='$(var.FLTR)rm64_ibbus.sys' On='uninstall'
             Property="System64Folder" Name="ibbus.sys" />

      <?include $(sys.CURRENTDIR)\..\..\common\winverbs_drivers.inc ?> 

</Include>

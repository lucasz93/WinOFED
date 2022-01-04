<Include>

      <File Id="$(var.FLTR)winverbs.sys" DiskId="1"
            Name="winverbs.sys"
            Source="..\bin\HCA\$(var.ARCH)\winverbs.sys" />
      <File Id="$(var.FLTR)winverbs.pdb" DiskId="1"
            Name="winverbs.pdb"
            Source="..\bin\HCA\$(var.ARCH)\winverbs.pdb" />
      <File Id="$(var.FLTR)winverbs.dll" DiskId="1"
            Name="winverbs.dll"
            Source="..\bin\HCA\$(var.ARCH)\winverbs.dll" />
      <File Id="$(var.FLTR)winmad.sys" DiskId="1"
            Name="winmad.sys"
            Source="..\bin\HCA\$(var.ARCH)\winmad.sys" />
      <File Id="$(var.FLTR)winmad.pdb" DiskId="1"
            Name="winmad.pdb"
            Source="..\bin\HCA\$(var.ARCH)\winmad.pdb" />
      <File Id="$(var.FLTR)winmad.dll" DiskId="1"
            Name="winmad.dll"
            Source="..\bin\HCA\$(var.ARCH)\winmad.dll" />

      <?if $(var.ARCHP) = "x64" Or $(var.ARCHP) = "ia64" ?>
          <File Id="$(var.FLTR)winverbs32.dll" DiskId="1"
                Name="winverbs32.dll"
                Source="..\bin\HCA\$(var.ARCH)\winverbs32.dll" />
          <File Id="$(var.FLTR)winmad32.dll" DiskId="1"
                Name="winmad32.dll"
                Source="..\bin\HCA\$(var.ARCH)\winmad32.dll" />
      <?endif?>

      <RemoveFile Id='$(var.FLTR)rm_winverbs.dll' On='uninstall'
             Property="SystemFolder" Name='winverbs.dll' />
      <RemoveFile Id='$(var.FLTR)rm_winmad.dll' On='uninstall'
             Property="SystemFolder" Name='winmad.dll' />

      <RemoveFile Id='$(var.FLTR)rm64_winverbs.dll' On='uninstall'
             Property="System64Folder" Name='winverbs.dll' />
      <RemoveFile Id='$(var.FLTR)rm64_winmad.dll' On='uninstall'
             Property="System64Folder" Name='winmad.dll' />

      <RemoveFile Id='$(var.FLTR)rm32_winverbs.sys' On='uninstall'
             Property="SystemFolder" Name='winverbs.sys' />
      <RemoveFile Id='$(var.FLTR)rm32_winmad.sys' On='uninstall'
             Property="SystemFolder" Name='winmad.sys' />

      <RemoveFile Id='$(var.FLTR)rm64_winverbs.sys' On='uninstall'
             Property="System64Folder" Name='winverbs.sys' />
      <RemoveFile Id='$(var.FLTR)rm64_winmad.sys' On='uninstall'
             Property="System64Folder" Name='winmad.sys' />

</Include>

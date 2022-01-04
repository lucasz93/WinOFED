<Include>

<DirectoryRef Id="TARGETDIR">

  <Directory Id="ProgramMenuFolder">
     <Directory Id="ProgramMenuDir" Name="$(var.PRODUCT)" >
        <Component Id="WinOFshortcuts"
                   Guid="DD67D6B3-F7C5-4fa3-A249-A0D87557333E">

            <Shortcut Id="sc_manual"
                      Name="Manual"
                      Description="$(var.PID) Manual"
                      Target="[INSTALLDIR]Manual.htm"
                      WorkingDirectory="INSTALLDIR"
                      Icon="openfabrics.ico" IconIndex="0" />

            <Shortcut Id="sc_RelNotes"
                      Name="Release Notes"
                      Description="$(var.PID) Release/Install Notes"
					  Target="[INSTALLDIR]Release_notes.htm"
                      WorkingDirectory="INSTALLDIR"
                      Icon="openfabrics.ico" IconIndex="0" />

            <Shortcut Id="sc_Command_Window"
                      Name="$(var.PID) Command Window"
                      Description="Cmd Window in $(var.PID) folder"
					  Target="[INSTALLDIR]Command Window.lnk"
                      WorkingDirectory="INSTALLDIR"
                      Icon="openfabrics.ico" IconIndex="0" />

            <Shortcut Id="sc_UninstallProduct"
                      Name="Uninstall $(var.PID) $(var.WOFREL)"
		              Target="[System64Folder]msiexec.exe"
                      Arguments="/X [ProductCode]"
                      Description="Uninstalls $(var.PRODUCT)" />

            <Shortcut Id="sc_AboutProduct"
                      Name="About $(var.PID)"
                      WorkingDirectory="INSTALLDIR"
		              Target="[System64Folder]wscript.exe"
                      Arguments="//NoLogo about.vbs"
                      Description="About $(var.PID) release"
                      Icon="openfabrics.ico" IconIndex="0" />

            <RemoveFolder Id="ProgramMenuDir" On="uninstall" />

            <RegistryValue Root="HKCU" Key="Software\Microsoft\$(var.PID)"
                           Name="installed" Type="integer" Value="1"
                           KeyPath="yes" />
        </Component>
     </Directory>
  </Directory>

</DirectoryRef>

</Include>

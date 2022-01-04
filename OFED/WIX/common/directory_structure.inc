
<Include>

    <!-- Define the install directory structure -->

    <Directory Id="TARGETDIR" Name="SourceDir">
        <Directory Id="WINROOT" />
        <Directory Id="$(var.SysNative)" />

        <?if $(var.ARCHP) != "x86" ?>
           <Directory Id="SystemFolder" />
        <?endif?>

        <Directory Id="$(var.ProgramFilesNative)" Name="PFiles">
            <Directory Id="INSTALLDIR" Name="$(var.PID)">
               <Directory Id="DriversDir" Name="Drivers" />
            </Directory>
        </Directory>
    </Directory>

</Include>

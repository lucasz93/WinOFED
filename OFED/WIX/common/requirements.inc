<Include>

    <!--
        System/Machine-wide install, otherwise per-user (Administrator) only.
        All users see OpenFabrics WinOF Release Notes & Manual in Programs.
     -->
    <Property Id="ALLUSERS"> Value="1" </Property>

    <!-- Requires files sc.exe & reg.exe -->
    <Property Id="REG_EXISTS">
      <DirectorySearch Id="RegFileDir" Path="[SystemFolder]" Depth="1">
        <FileSearch Id="RegFile" Name="reg.exe" />
      </DirectorySearch>
    </Property>

    <Property Id="SC_EXISTS">
      <DirectorySearch Id="ScFileDir" Path="[SystemFolder]" Depth="1">
        <FileSearch Id="ScFile" Name="sc.exe" />
      </DirectorySearch>
    </Property>

    <Property Id="IPOIB_EXISTS">
      <DirectorySearch Id="ipoibFileDir" Path="[INSTALLDIR]\net" Depth="0">
        <FileSearch Id="ipoibFile" Name="netipoib.inf" />
      </DirectorySearch>
    </Property>

    <Property Id="DAT_CONF_EXISTS">
      <DirectorySearch Id="DCscan" Path="[WindowsVolume]\DAT" Depth="0">
        <FileSearch Id="DCFile" Name="dat.conf" />
      </DirectorySearch>
    </Property>

    <Property Id="DAT1_INSTALLED">
      <DirectorySearch Id="Dat1DS" Path="[INSTALLDIR]" Depth="0">
        <FileSearch Id="daplFS" Name="dapl.dll" />
      </DirectorySearch>
    </Property>

    <Property Id="DAT2_INSTALLED">
      <DirectorySearch Id="Dat2DS" Path="[INSTALLDIR]" Depth="0">
        <FileSearch Id="dapl2FS" Name="dapl2.dll" />
      </DirectorySearch>
    </Property>

    <Condition Message="[ProductName] requires system file reg.exe">
       REG_EXISTS
    </Condition>

    <Condition Message="[ProductName] requires system file sc.exe">
       SC_EXISTS
    </Condition>

    <Media Id="1" Cabinet="Data1.cab" EmbedCab="yes" DiskPrompt="CD-ROM #1" />
    <Property Id="DiskPrompt" Value="$(var.PID) Installation [1]" />

    <SetProperty Id="WINROOT" Value="[WindowsVolume]" Before="CostInitialize" />
    <SetProperty Id="WINTEMP" Value="[WindowsFolder]\temp\" Before="CostInitialize" />

</Include>

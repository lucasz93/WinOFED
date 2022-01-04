<Include>

    <!--> File where Custom Actions are defined -->
    <Binary Id="CA" SourceFile="..\..\CustomActions.vbs" />

    <CustomAction Id="WinOF_setup" BinaryKey="CA"
         VBScriptCall="WinOF_setup" Return="check" />

    <CustomAction Id="ChkPreviousInstall" BinaryKey="CA"
         VBScriptCall="ChkPreviousInstall" Return="check" />

    <CustomAction Id="PostDriverInstall" BinaryKey="CA"
         VBScriptCall="PostDriverInstall" Return="check" Execute="deferred" />

    <CustomAction Id="Remove_Services" BinaryKey="CA"
         VBScriptCall="Remove_Services" Return="check" />

    <CustomAction Id="Remove_IB_Devices" BinaryKey="CA"
         VBScriptCall="Remove_IB_Devices" Return="check" />

    <CustomAction Id="IB_DriverCleanup" BinaryKey="CA"
         VBScriptCall="IB_DriverCleanup" Return="check" />

    <CustomAction Id="CheckDriversOK" BinaryKey="CA"
         VBScriptCall="CheckDriversOK" Return="check" />

    <CustomAction Id="WSDStart" BinaryKey="CA"
         VBScriptCall="WSDEnable" Return="check" />

    <CustomAction Id="ND_start" BinaryKey="CA"
         VBScriptCall="ND_StartMeUp" Return="check" />

    <CustomAction Id="HammerTime" BinaryKey="CA"
         VBScriptCall="HammerTime" Return="check" />

    <CustomAction Id="BcastRegChanged" BinaryKey="CA"
         VBScriptCall="BcastRegChanged" Return="check" />

    <CustomAction Id="InstallChanged" BinaryKey="CA"
         VBScriptCall="InstallChanged" Return="check" />


    <InstallExecuteSequence>
       <!-- Always executed (Install or uninstall) -->
       <Custom Action="WinOF_setup" After="InstallInitialize" />

       <!--> Install ONLY </!-->
       <Custom Action="ChkPreviousInstall" After='WinOF_setup'>
               NOT Installed AND Not NODRV </Custom>

       <Custom Action="PostDriverInstall" After='InstallServices'>
               Not Installed AND Not NODRV </Custom>

       <Custom Action="CheckDriversOK" After="InstallFinalize">
               Not Installed AND Not NODRV </Custom>

       <?if $(var.OS) != "win8"?>
           <Custom Action="WSDStart" After="CheckDriversOK">
              ($cWinsockDirectEnabled = 3) AND Not Installed AND Not NODRV </Custom>

           <Custom Action="ND_start" After="WSDStart">
              ($cNetworkDirect = 3) AND Not Installed AND Not NODRV </Custom>
       <Custom Action="BcastRegChanged" After="ND_start">
               Not Installed</Custom>

       <?endif?>

       <!--> CHANGE ONLY </!-->
       <Custom Action="InstallChanged" After="InstallFinalize">
           Installed </Custom>

       <!--> REMOVE ONLY </!-->
       <Custom Action="Remove_Services" After="CostFinalize">
               REMOVE="ALL" AND Not NODRV </Custom>

       <Custom Action="Remove_IB_Devices" Before="MsiProcessDrivers">
               REMOVE="ALL" AND Not NODRV </Custom>

       <Custom Action="IB_DriverCleanup" After="MsiCleanupOnSuccess">
               REMOVE="ALL" AND Not NODRV </Custom>

       <Custom Action="HammerTime" After="IB_DriverCleanup">
               REMOVE="ALL" </Custom>

    </InstallExecuteSequence>

</Include>

<Include>

    <!--
       Features - Level=1 == Typical, no WSD on XP, or SRP on 32-bit XP
     -->

    <Feature Id="IBcore" Title="InfiniBand Core Modules" Level="1"
        Description="Drivers, Libraries, Subnet Management, Tools, Diags, Docs"
        Display="collapse" ConfigurableDirectory="INSTALLDIR"
        AllowAdvertise="no" InstallDefault="local" Absent="disallow">

      <?if $(var.OS) != "win8" ?>
          <ComponentRef Id="cDriverTools" />
      <?endif?>
      <ComponentRef Id="Docs" />
      <ComponentRef Id="WinOFshortcuts" />
      <ComponentRef Id="Tools" />
      <ComponentRef Id="OpenSMservice" />
      <ComponentRef Id="cOpenSMfolder" />
      <ComponentRef Id="cRsockets" />

      <?if $(var.ARCHP) = "x64" AND ($(var.OS) = "wlh" OR $(var.OS) = "win7") ?>
         <ComponentRef Id="cCertPush" />
      <?endif?>

      <?if $(var.OS) != "win8" ?>
        <Feature Id="fMxDrivers" Title="Mellanox HCA Drivers" Level="1"
           Description="Mellanox InfiniBand Host Channel Adapter Drivers"
           Display="expand" ConfigurableDirectory="INSTALLDIR"
           AllowAdvertise='no' InstallDefault='local' Absent='allow'>
          <Condition Level="200"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cMthcaDriver" />
          <ComponentRef Id="cMLX4BusDriver" />
        </Feature>
      <?endif?>

      <Feature Id="fWV" Title="OFED Verbs"
          Description="user-mode libraries + utilities"
          Display="expand" Level="1" ConfigurableDirectory="INSTALLDIR"
          AllowAdvertise="no" InstallDefault="local" Absent="allow">
          <ComponentRef Id="cWinVerbsOFED" />
          <ComponentRef Id="cWinVerbsUApps" />
      </Feature>

      <?if $(var.OS) != "win8" ?>
        <Feature Id="fIOU" Title="I/O Unit Driver"
           Description="InfiniBand I/O Unit Driver"
           Display="expand" Level="1" 
           AllowAdvertise='no' InstallDefault='local' Absent='allow'>
          <Condition Level="200"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cIOUdriver" />
        </Feature>
      <?endif?>

    </Feature>

    <?if $(var.OS) != "win8" ?>
        <Feature Id="fIPoIB" Title="IPoIB" Level="1"
            Description="Internet Protocols over InfiniBand" Display="expand"
            ConfigurableDirectory="INSTALLDIR" AllowAdvertise="no"
            InstallDefault="local" Absent="allow">
          <Condition Level="200"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cIPoIB" />
        </Feature>

        <Feature Id="fWSD" Title="WSD Enabled"
            Description="Start the Winsock Direct provider" Display="expand"
            Level="5" ConfigurableDirectory="INSTALLDIR"
            AllowAdvertise='no' InstallDefault='local' Absent='allow'>
          <!-- skip WSD on Windows XP, IPoIB is required for WSD. -->
          <Condition Level="300"><![CDATA[NODRV=1]]></Condition>
          <Condition Level="400"><![CDATA[VersionNT=501]]></Condition>
          <Condition Level="201"> IPOIB_EXISTS </Condition>
          <ComponentRef Id="cWinsockDirectEnabled" />
        </Feature>

        <Feature Id="fND" Title="Network Direct.v2 Enabled"
            Description="Start the Network Direct.v2 Provider"
            Display="expand" Level="1" ConfigurableDirectory="INSTALLDIR"
            AllowAdvertise="no" InstallDefault="local" Absent="allow">
          <Condition Level="201"> IPOIB_EXISTS </Condition>
          <Condition Level="300"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cNetworkDirect" />
        </Feature>
    <?endif?>

    <Feature Id="fOSMS" Title="OpenSM Service" Level="5"
        Description="Open Subnet Management started as a local Windows Service"
        Display="expand" ConfigurableDirectory="INSTALLDIR" AllowAdvertise="no"
        InstallDefault="local" Absent="allow">
      <ComponentRef Id="OpenSM_Service_enabled" />
    </Feature>

    <Feature Id="fRsock" Title="Rsockets Service" Level="1"
        Description="Rsockets started as a local Windows Service"
        Display="expand" ConfigurableDirectory="INSTALLDIR" AllowAdvertise="no"
        InstallDefault="local" Absent="allow">
      <ComponentRef Id="cRsocketsService" />
    </Feature>

    <Feature Id="fDAPL" Title="DAPL" Description="uDAT+uDAPL over InfiniBand"
        Display="collapse" Level="1" ConfigurableDirectory="INSTALLDIR"
        AllowAdvertise="no" InstallDefault="local" Absent="allow">
      <Condition Level="201"> IPOIB_EXISTS </Condition>

      <Feature Id="fDatConf" Title="DAT-DAPL Config File"
         Description="DAPL runtime configuration file; installed if %SystemDrive%\DAT\dat.conf does not exist."
         Display="expand" Level="1" 
         AllowAdvertise='no' InstallDefault='local' Absent='allow'>
        <Condition Level="0"> DAT_CONF_EXISTS AND Not Installed </Condition>
        <ComponentRef Id="cDatCfg" />
      </Feature>

      <Feature Id="fDatBASIC2" Title="DAT 2.0 Runtime"
         Description="DAT 2.0 runtime libraries"
         Display="expand" Level="1" 
         AllowAdvertise='no' InstallDefault='local' Absent='allow'>
        <ComponentRef Id="cDAPL2RT" />
        <ComponentRef Id="cSys32DAPL2RT" />
        <?if $(var.ARCHP) != "x86" ?>
           <ComponentRef Id="cSysWowDAPL2RT" />
        <?endif?>
      </Feature>

      <Feature Id="fDatBldKit2" Title="DAT 2.0 Build Kit"
         Description="DAT 2.0 Application Build Kit - Headers and Libraries"
         Display="expand" Level="2" 
         AllowAdvertise='no' InstallDefault='local' Absent='allow'>
         <Condition Level="201"> DAT2_INSTALLED </Condition>
         <ComponentRef Id="cDATV2" />
         <ComponentRef Id="cDatBldV2Hdrs" />
         <ComponentRef Id="cDatBldV2Libs" />
      </Feature>
    </Feature>

    <Feature Id="fIBsdk" Title="OFED SDK" Level="2"
         Description="InfiniBand Software Development Kit in %SystemDrive%\OFED_SDK"
         Display="expand" ConfigurableDirectory="INSTALLDIR"
         AllowAdvertise='no' InstallDefault='local' Absent='disallow'>
      <ComponentRef Id="cIBAL_Libs" />
      <ComponentRef Id="cIBAL_Incs" />
      <ComponentRef Id="cIBAL_Complib" />
      <ComponentRef Id="cIBAL_IBA" />
      <ComponentRef Id="cINC_user_etc" />
      <ComponentRef Id="cINC_user_ib" />
      <ComponentRef Id="cINC_linux" />
      <ComponentRef Id="cINC_linux_arpa" />
      <ComponentRef Id="cINC_linux_netinet" />
      <ComponentRef Id="cINC_linux_sys" />
      <ComponentRef Id="cINC_user_rdma" />
      <ComponentRef Id="cWDK_example" />
      <ComponentRef Id="cVS_bld_example1" />
      <ComponentRef Id="cVS_bld_example2" />
      <ComponentRef Id="cVS_bld_example3" />
    </Feature>

    <?if $(var.OS) != "win8" ?>
        <Feature Id="fSRP" Title="SRP" Level="5" Description="SCSI over RDMA"
            Display="expand" ConfigurableDirectory="INSTALLDIR"
            AllowAdvertise="no" InstallDefault="local" Absent="allow">
          <Condition Level="200"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cSRP_driver_installed" />
        </Feature>

        <Feature Id="fVNIC" Title="QLogic-VNIC"
            Description="QLogic - Virtual NIC over InfiniBand"
            Display="expand" Level="5" ConfigurableDirectory="INSTALLDIR"
            AllowAdvertise="no" InstallDefault="local" Absent="allow">
          <Condition Level="200"><![CDATA[NODRV=1]]></Condition>
          <ComponentRef Id="cQLVNIC_driver_installed" />
        </Feature>
    <?endif?>

</Include>

<Include>
    <Component Id='OpenSMservice'
               Guid='90042B4F-51B5-4720-8E54-6D366253634B'
               SharedDllRefCount='no' KeyPath='no'
               NeverOverwrite='no'
               Permanent='no' Transitive='no'
               Location='either'>

         <File Id="opensm.exe" DiskId="1"
               Name="opensm.exe"
               Source="..\bin\tools\$(var.ARCH)\release\opensm.exe"
               Vital='yes' KeyPath='yes' Hidden='no' System='no' />

         <File Id="opensm.pdb" DiskId="1"
               Name="opensm.pdb"
               Source="..\bin\tools\$(var.ARCH)\release\opensm.pdb" />

         <File Id="osmtest.exe" DiskId="1"
               Name="osmtest.exe"
               Source="..\bin\tools\$(var.ARCH)\release\osmtest.exe" />

         <File Id="ibtrapgen.exe" DiskId="1"
               Name="ibtrapgen.exe"
               Source="..\bin\tools\$(var.ARCH)\release\ibtrapgen.exe" />

         <ServiceInstall Id='osmSvcInstall' Vital='yes' Name='OpenSM'
               Description='InfiniBand Subnet Configuration and Management'
               DisplayName='InfiniBand Subnet Manager'
               Arguments='-e --service'
               ErrorControl='normal'
               Start='disabled'
               Interactive='no'
               Type='ownProcess' />

         <ServiceControl Id='osmSvcControl' Name='OpenSM'
               Stop='uninstall'
               Wait='no'
               Remove='uninstall' />
    </Component>

    <Component Id="OpenSM_Service_enabled" KeyPath="yes"
               Guid="3A148CF8-129D-4AE0-A769-5FC0E0828B5E">

         <ServiceControl Id='osmSvcControlStart' Name='OpenSM'
               Start='install'
               Stop='uninstall'
               Wait='no'
               Remove='uninstall' />

    </Component>

    <!-- create a folder for OpenSM config files -->
    <Directory Id="OpenSMDir" Name="OpenSM">
        <Component Id="cOpenSMfolder"
                   Guid="3D4B2704-05C4-4378-9D49-17580945B367">

            <CreateFolder />

            <RemoveFile Id='fOSM1' On='uninstall' Name="guid2lid" />
            <RemoveFile Id='fOSM2' On='uninstall' Name="*.*" />
            <RemoveFile Id='fOSM3' On='uninstall' Name="*" />

            <RemoveFile Id='fOsmlog' On='uninstall' Property="WINTEMP"
                        Name="osm.log" />
            <RemoveFile Id='fOsmsyslog' On='uninstall' Property="WINTEMP"
                        Name="osm.syslog" />
            <RemoveFile Id='fOpensm_subnet' On='uninstall' Property="WINTEMP"
                        Name="opensm-subnet.lst" />
        </Component>
    </Directory>

</Include>

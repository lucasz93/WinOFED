<Include>

  <Directory Id="dDATD" Name="DAT">
 
    <!-- install DAT configuration file to %SystemDrive%\DAT\ -->

    <Component Id="cDatCfg"
               Guid="54A4F997-AC33-45D8-9899-6332E40B62A8" >
      <CreateFolder />
      <File Id="_dat.conf" DiskId="1" KeyPath="yes"
             Name="dat.conf"
             Source="..\..\dat.conf" />
    </Component>

    <!-- Install DAT header files to %SystemDrive%\DAT\v2-0\DAT2 -->

    <Directory Id="v2_0" Name="v2-0">
      <Component Id="cDATV2" Guid="A3CD6CA7-309B-48D2-B375-C9A91B37A876"
                 KeyPath="yes" SharedDllRefCount="yes">
        <CreateFolder />
      </Component>

      <Directory Id="DDID2" Name="DAT2">    
        <Component Id="cDatBldV2Hdrs"
                  Guid="4D688030-53AC-4351-8999-E3AFEB38A92C" >    
          <CreateFolder />    
          <File Id="_dat.h" DiskId="1"    
                Name="dat.h"
                Source="..\bin\DAPL2\dat.h" />    

          <File Id="_dat_error.h" DiskId="1"    
                Name="dat_error.h"
                Source="..\bin\DAPL2\dat_error.h" />    
  
          <File Id="_dat_ib_extensions.h" DiskId="1"    
                Name="dat_ib_extensions.h"
                Source="..\bin\DAPL2\dat_ib_extensions.h" />    
  
          <File Id="_dat_platform_specific.h"    
                DiskId="1" Name="dat_platform_specific.h"    
                Source="..\bin\DAPL2\dat_platform_specific.h" />    
  
          <File Id="_dat_redirection.h"    
                DiskId="1" Name="dat_redirection.h"    
                Source="..\bin\DAPL2\dat_redirection.h" />    
  
          <File Id="_dat_registry.h"    
                DiskId="1" Name="dat_registry.h"    
                Source="..\bin\DAPL2\dat_registry.h" />    
  
          <File Id="_dat_vendor_specific.h"    
                DiskId="1" Name="dat_vendor_specific.h"    
                Source="..\bin\DAPL2\dat_vendor_specific.h" />    
  
          <File Id="_udat.h" DiskId="1"    
                Name="udat.h"
                Source="..\bin\DAPL2\udat.h" />    
  
          <File Id="_udat_config.h" DiskId="1"    
                Name="udat_config.h"
                Source="..\bin\DAPL2\udat_config.h" />    
  
          <File Id="_udat_redirection.h"    
                DiskId="1" Name="udat_redirection.h"    
                Source="..\bin\DAPL2\udat_redirection.h" />    
  
          <File Id="_udat_vendor_specific.h"    
                DiskId="1" Name="udat_vendor_specific.h"    
                Source="..\bin\DAPL2\udat_vendor_specific.h" />    
        </Component>
      </Directory>

      <Component Id="cDatBldV2Libs"
                  Guid="4DCE2855-1F42-4A7D-957A-44A47715C051" >    

          <File Id="_dat2.lib" DiskId="1"
                Name="dat2.lib"
                Source="..\bin\DAPL2\$(var.ARCH)\dat2.lib" />
      </Component>
    </Directory>

  </Directory>

</Include>

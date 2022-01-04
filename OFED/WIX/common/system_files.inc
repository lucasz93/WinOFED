<Include>

 <!-- Files installed in %windir%\system32 -->

 <DirectoryRef Id="$(var.SysNative)"> 

    <!-- DAPL -->

    <Component Id="cSys32DAPL2RT" Win64="$(var.Win64)"
               Guid="63AEC400-109C-4409-B9F3-5E900FA7EFCE">

      <File Id="_dapl2.dll" DiskId="1"
            Name="dapl2.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2.dll" />
 
      <File Id="_dapl2_ofa_scm.dll" DiskId="1"
            Name="dapl2-ofa-scm.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-scm.dll" />
 
      <File Id="_dapl2_ofa_cma.dll" DiskId="1"
            Name="dapl2-ofa-cma.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-cma.dll" />
 
      <File Id="_dapl2_ofa_ucm.dll" DiskId="1"
            Name="dapl2-ofa-ucm.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-ucm.dll" />
 
      <File Id="_dapl2_ND.dll" DiskId="1"
            Name="dapl2-ND.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ND.dll" />
 
      <File Id="_dapl2_ND.pdb" DiskId="1"
            Name="dapl2-ND.pdb"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ND.pdb" />
 
      <File Id="_dat2.dll" DiskId="1"
            Name="dat2.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dat2.dll" />
    </Component>


 </DirectoryRef> 

 <?if $(var.ARCHP) != "x86" ?> 

    <!-- Files installed in %windir%\sysWOW64 -->

    <DirectoryRef Id="SystemFolder"> 
       <Component Id="cSysWowDAPL2RT" Win64="no"
                  Guid="3363834C-12D7-43db-95AA-14527C3D146A">
           <File Id="_dapl32.dll" DiskId="1"
                 Name="dapl2.dll"
                 Source="..\bin\DAPL2\$(var.ARCH)\dapl232.dll" />
           <File Id="_dat32.dll" DiskId="1"
                 Name="dat2.dll"
                 Source="..\bin\DAPL2\$(var.ARCH)\dat232.dll" />

           <File Id="_dapl2_ofa_scm32.dll" DiskId="1"
                 Name="dapl2-ofa-scm.dll"
                 Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-scm32.dll" />
 
           <File Id="_dapl2_ofa_cma32.dll" DiskId="1"
                 Name="dapl2-ofa-cma.dll"
                 Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-cma32.dll" />
 

           <File Id="_dapl2_ofa_ucm32.dll" DiskId="1"
                 Name="dapl2-ofa-ucm.dll"
                 Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-ucm32.dll" />
 
       </Component>
    </DirectoryRef> 

 <?endif?>

</Include>

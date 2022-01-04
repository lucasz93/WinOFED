<Include>

    <Component Id="cDriverTools" DiskId="1"
               Guid="4B4EA649-65ED-4B0D-8717-D60481412211">
      <CreateFolder />
      <File Id="dpinst.exe"
             Name="DPInst.exe"
             Source="..\bin\misc\$(var.ARCH)\DPInst.exe" />
      <File Id="dpinst.xml"
             Name="dpinst.xml"
             Source="..\..\dpinst.xml" />
      <File Id="devman.exe"
             Name="devman.exe"
             Source="..\..\$(var.ARCHP)\devman.exe" />
    </Component>

    <?include $(sys.CURRENTDIR)\..\..\common\mlnx_drivers.inc ?>

    <?include $(sys.CURRENTDIR)\..\..\common\iou.inc ?>

</Include>

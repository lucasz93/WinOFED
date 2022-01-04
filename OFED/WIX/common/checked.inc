<Include>
    <Directory Id="dir_0" Name="Checked">
      <Directory Id="dir_1" Name="kernel">

        <Component Id="cCHK_kernel"
                   Guid="D96F299E-EEAA-44a5-BBC4-A862A158BA6B">
          <CreateFolder />

          <File Id="chk0" DiskId="1"
                Name="README.txt"
                Source="..\..\README_checked.txt" />
        </Component>

          <Directory Id="kdir_mthca" Name="mthca">
            <Component Id="cCHK_mthca"
                       Guid="CAC772A9-9B00-44ef-AB67-ED935A079869">
              <CreateFolder />
              <File Id="chk1" DiskId="1"
                    Name="mthca.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mthca.sys" />
              <File Id="cat1" DiskId="1"
                    Name="mthca.cat"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mthca.cat" />
              <File Id="sym1" DiskId="1"
                    Name="mthca.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mthca.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_ibbus" Name="bus">
            <Component Id="cCHK_ibbus"
                       Guid="10FC2995-A014-4b7b-8FBF-12529F997E9F">
              <CreateFolder />
              <File Id="chk2" DiskId="1"
                    Name="ibbus.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\ibbus.sys" />
              <File Id="sym2" DiskId="1"
                    Name="ibbus.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\ibbus.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_ibiou" Name="iou">
            <Component Id="cCHK_iou"
                       Guid="B14C5FD2-9793-47dc-BA4C-9CA1D2DE2319">
              <CreateFolder />
              <File Id="chk3" DiskId="1"
                    Name="ibiou.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\ibiou.sys" />
              <File Id="cat3" DiskId="1"
                    Name="ib_iou.cat"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\ib_iou.cat" />
              <File Id="sym3" DiskId="1"
                    Name="ibiou.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\ibiou.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_ipoib" Name="ipoib">
            <Component Id="cCHK_ipoib"
                       Guid="95E5F891-7F97-4209-AED3-AF009DA18C5D">
              <CreateFolder />
              <File Id="inf4" DiskId="1"
                    Name="netipoib.inf"
                    Source="..\bin\Chk\net\$(var.ARCH)\netipoib.inf" />
              <File Id="cat4" DiskId="1"
                    Name="ipoib.cat"
                    Source="..\bin\Chk\net\$(var.ARCH)\ipoib.cat" />
              <File Id="sym4" DiskId="1"
                    Name="ipoib.pdb"
                    Source="..\bin\Chk\net\$(var.ARCH)\ipoib.pdb" />
              <File Id="chk_ipoib1" DiskId="1" Name="ipoib.sys"
                    Source="..\bin\Chk\net\$(var.ARCH)\ipoib.sys" />
<?if $(var.OS) != "XP" ?>
              <File Id="chk_ipoib3" DiskId="1" Name="ibwsd.dll"
                    Source="..\bin\Chk\net\$(var.ARCH)\ibwsd.dll" />
              <File Id="chk_ipoib6" DiskId="1" Name="installsp.exe"
                    Source="..\bin\Chk\net\$(var.ARCH)\installsp.exe" />
<?endif?>
<?if $(var.ARCHP) != "x86" ?>
              <File Id="chk_ipoib4" DiskId="1" Name="ibwsd32.dll"
                    Source="..\bin\Chk\net\$(var.ARCH)\ibwsd32.dll" />
<?endif?>
<?if $(var.ARCHP) != "ia64" ?>
              <File Id="chk_ipoib8" DiskId="1" Name="ibndprov.dll"
                    Source="..\bin\Chk\net\$(var.ARCH)\ibndprov.dll" />
<?endif?>
<?if $(var.ARCHP) = "x64" ?>
              <File Id="chk_ipoib2" DiskId="1" Name="ibndprov32.dll"
                    Source="..\bin\Chk\net\$(var.ARCH)\ibndprov32.dll" />
<?endif?>
              <File Id="chk_ipoib5" DiskId="1" Name="ndinstall.exe"
                    Source="..\bin\Chk\net\$(var.ARCH)\ndinstall.exe" />
              <File Id="chk_ipoib7" DiskId="1" Name="wvndprov.dll"
                    Source="..\bin\Chk\net\$(var.ARCH)\wvndprov.dll" />
            </Component>
          </Directory>

          <Directory Id="kdir_vnic" Name="vnic">
            <Component Id="cCHK_vnic"
                       Guid="22E6256B-E655-4004-BCA9-44A329CBEE57">
              <CreateFolder />
              <File Id="chk5" DiskId="1"
                    Name="qlgcvnic.sys"
                    Source="..\bin\Chk\net\$(var.ARCH)\qlgcvnic.sys" />
              <File Id="cat5" DiskId="1"
                    Name="netvnic.cat"
                    Source="..\bin\Chk\net\$(var.ARCH)\netvnic.cat" />
              <File Id="sym5" DiskId="1"
                    Name="qlgcvnic.pdb"
                    Source="..\bin\Chk\net\$(var.ARCH)\qlgcvnic.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_srp" Name="srp">
            <Component Id="cCHK_srp"
                       Guid="FD6E2410-66A5-415e-9AB4-A19CFE07EC56">
              <CreateFolder />
              <File Id="chk6" DiskId="1"
                    Name="ibsrp.sys"
                    Source="..\bin\Chk\storage\$(var.ARCH)\ibsrp.sys" />
              <File Id="cat6" DiskId="1"
                    Name="ibsrp.cat"
                    Source="..\bin\Chk\storage\$(var.ARCH)\ibsrp.cat" />
              <File Id="sym6" DiskId="1"
                    Name="ibsrp.pdb"
                    Source="..\bin\Chk\storage\$(var.ARCH)\ibsrp.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_mlx4" Name="mlx4">
            <Component Id="cCHK_mlx4"
                       Guid="05EFF991-9434-4c39-93B5-61834A18F5AB">
              <CreateFolder />
              <File Id="chk7" DiskId="1"
                    Name="mlx4_bus.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_bus.sys" />
              <File Id="cat7" DiskId="1"
                    Name="mlx4_bus.cat"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_bus.cat" />
              <File Id="sym7" DiskId="1"
                    Name="mlx4_bus.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_bus.pdb" />

              <File Id="chk8" DiskId="1"
                    Name="mlx4_hca.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_hca.sys" />
              <File Id="cat8" DiskId="1"
                    Name="mlx4_hca.cat"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_hca.cat" />
              <File Id="sym8" DiskId="1"
                    Name="mlx4_hca.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4_hca.pdb" />
            </Component>
          </Directory>

          <Directory Id="kdir_winverbs" Name="winverbs">
            <Component Id="cCHK_winverbs"
                       Guid="CDEBE21F-03F8-472a-9F14-7743C41E3E69">
              <CreateFolder />
              <File Id="chk9" DiskId="1"
                    Name="winverbs.sys"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\winverbs.sys" />
              <File Id="sym9" DiskId="1"
                    Name="winverbs.pdb"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\winverbs.pdb" />
              <File Id="dll9" DiskId="1"
                    Name="winverbs.dll"
                    Source="..\bin\Chk\HCA\$(var.ARCH)\winverbs.dll" />
            </Component>
          </Directory>

      </Directory>

      <Directory Id="dir_14" Name="user">
        <Component Id="cCHK_user"
                   Guid="A4AC788B-6517-47b2-A898-0493ADC794D5">
          <CreateFolder />
          <File Id="file82" DiskId="1"
            Name="dapl2-ofa-cma.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-cma.dll" />
          <File Id="file83" DiskId="1"
            Name="dapl2-ofa-scm.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2-ofa-scm.dll" />
          <File Id="file84" DiskId="1"
            Name="dapl2.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dapl2.dll" />
          <File Id="file85" DiskId="1"
            Name="dapl2test.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\dapl2test.exe" />
          <File Id="file88" DiskId="1"
            Name="dat2.dll"
            Source="..\bin\DAPL2\$(var.ARCH)\dat2.dll" />
          <File Id="file90" DiskId="1"
            Name="dtest2.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\dtest2.exe" />
          <File Id="file91" DiskId="1"
            Name="dtestx.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\dtestx.exe" />
          <File Id="file45" DiskId="1"
            Name="complib.dll"
            Source="..\bin\Chk\HCA\$(var.ARCH)\complib.dll" />
          <File Id="file94" DiskId="1"
            Name="ibal.dll"
            Source="..\bin\Chk\HCA\$(var.ARCH)\ibal.dll" />

          <?if $(var.OS) != "XP" ?>
            <File Id="file97" DiskId="1"
              Name="ibwsd.dll"
              Source="..\bin\Chk\net\$(var.ARCH)\ibwsd.dll" />
            <File Id="file105" DiskId="1"
              Name="installsp.exe"
              Source="..\bin\Chk\net\$(var.ARCH)\installsp.exe" />
          <?endif?>

          <File Id="file69" DiskId="1"
            Name="mlx4u.dll"
            Source="..\bin\Chk\HCA\$(var.ARCH)\mlx4u.dll" />
          <File Id="file108" DiskId="1"
            Name="mthcau.dll"
            Source="..\bin\Chk\HCA\$(var.ARCH)\mthcau.dll" />
          <File Id="file110" DiskId="1"
            Name="opensm.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\opensm.exe" />
          <File Id="file115" DiskId="1"
            Name="vstat.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\vstat.exe" />
          <File Id="file116" DiskId="1"
            Name="part_man.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\part_man.exe" />
          <File Id="file117" DiskId="1"
            Name="qlgcvnic_config.exe"
            Source="..\bin\tools\$(var.ARCH)\debug\qlgcvnic_config.exe" />
        </Component>
      </Directory>
    </Directory>
</Include>

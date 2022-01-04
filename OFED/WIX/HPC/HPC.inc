<Include>
   <Directory Id="cHPC" Name="HPC">
          <Component Id="cCertPush"
                     Guid="8B3AEC30-CFED-4d90-A48E-AD2C8C55FEFD">

            <CreateFolder />

            <File Id="cert_add.bat" DiskId="1"
                     Name="cert-add.bat"
                     Source="..\..\HPC\cert-add.bat" />
            <File Id="rem_cert_add.bat" DiskId="1"
                     Name="rem-cert-add.bat"
                     Source="..\..\HPC\rem-cert-ADD.bat" />
            <File Id="OFA_cert_install.bat" DiskId="1"
                     Name="OFA-cert-install.bat"
                     Source="..\..\HPC\OFA-cert-install.bat" />
            <File Id="WinOF_install.bat" DiskId="1"
                     Name="WinOF-install.bat"
                     Source="..\..\HPC\WinOF-install.bat" />
            <File Id="readme_HPC.txt" DiskId="1"
                     Name="readme-HPC.txt"
                     Source="..\..\HPC\readme-HPC.txt" />
          </Component>
   </Directory>
</Include>


<Include>

   <UIRef Id="WixUI_FeatureTree" />

      <WixVariable Id="WixUILicenseRtf" Value="..\..\License.rtf" />
      <WixVariable Id="WixUIBannerBmp" Value="..\..\banner.bmp" />
      <WixVariable Id="WixUIDialogBmp" Value="..\..\dialog.bmp" />

      <UIRef Id="WixUI_ErrorProgressText" />
      <Icon Id="openfabrics.ico" SourceFile="..\..\openfabrics.ico" />

   <UI>
      <ProgressText Action="MsiInstallDrivers">Installing Drivers</ProgressText>
      <ProgressText Action="CheckDriversOK">Verify Driver Install</ProgressText>
      <ProgressText Action="WSDStart">Installing Network Providers</ProgressText>
      <ProgressText Action="MsiUninstallDrivers">Uninstalling Drivers</ProgressText>
   </UI>

</Include>

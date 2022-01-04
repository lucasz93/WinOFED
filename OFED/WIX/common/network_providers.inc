<Include>

  <!-- XP does not support WSD -->
  <?if $(var.OS) != "XP" ?>
      <Component Id="cWinsockDirectEnabled" KeyPath="yes"
             Guid="A761C53F-9197-441d-9BB3-67103D11BAED">
      </Component>
  <?endif?>

  <Component Id="cNetworkDirect" KeyPath="yes"
         Guid="F3BA9E15-E532-4b8c-BB0C-E8C1CC0EEF3F">
  </Component>

</Include>

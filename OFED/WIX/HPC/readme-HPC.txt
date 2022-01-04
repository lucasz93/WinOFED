[11-07-11] stan.

Files in this folder are Windows Server 2008 HPC specific.

For full HPC install details,
see Start-->All Programs-->Windows OpenFabrics-->WinOF Release Notes-->HPC Install

cert_add.bat
  Extract OFA Trusted Publisher certificate from the head-node local certificate
  store to a local file OFA_TP.cer, then insert OFA_TP.cer into remote node's
  Trusted Publisher certificate store by calling '.\rem-cert-add.bat'.

  For the example, assume \\HN\WOF is the compute node visible share name;
  head-node local name is \Program Files\Microsoft HPC Pack\Data\InstallShare\WOF

 First extract OFA TP certificate into a remote node visible share file OFA_TP.cer,
 creates \\HN\WOF\OFA_TP.cer & \\HN\WOF\rem-cert-ADD.bat

    cert-add \\HN\WOF

    Instert OFA TP certificate into a remote node's Trusted Publisher Store
    cert-add \\HN\WOF cn01 cn02 cn03 [...]

rem-cert-add.bat
  Add Trusted Publisher certificate to local node's TP store. Can be run via
  clusrun.exe or locally as a WDM provisioning template step.

Provisioning Template installation examples: (local config mods required)

Batch files are part of an entire directory copied via the template to the
node under provisioning. Template then executes
  1) OFA-cert-install.bat
  2) WinOF-install.bat

OFA-cert-install.bat
  No args wrapper for rem-cert-add.bat suitible to be run as a compute node
  provisioning template step.

WinOF-install.bat
  No args wrapper to quietly with logging, install default WinOF on local node.


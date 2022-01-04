
[04-23-09] stan
  Network Direct InfiniBand provider sources building here, no more fake builds.
  ND builds require
     MS WIndows SDK v6.1
     Server 2008 HPC SDK (defines ND_INC env var in WDK build env)
  If HPC SDK not present then all arch builds for ND are skipped, comment in .log file.
  If HPC SKD present then build for ND provider for x86 & x64, no IA64 until ND over winverbs.

[10-22-08]
  Latest MS ND source (delivered to Qlogic 10-22-08 from Microsoft). 
  ND binaries (ndinstall.exe + ndibprov.dll) generated from WinOF svn.1684 tree by Alex Estrin.
  MS ND provider source version in sync with MS ND SDK (2.0.1551.0).


[9-10-08]
  Current ND binaries @ 2.0.0.3140 built from Mellanox internal tree.


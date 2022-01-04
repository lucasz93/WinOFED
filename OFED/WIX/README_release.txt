[06-25-2013]

OFED for Windows 3.2 GA (General Availability) release is available for download @
   https://www.openfabrics.org/downloads/Windows/v3.2

Please address comments and concerns to https://bugs.openfabrics.org and/or the
Windows OpenFabrics email list ofw@lists.openfabrics.org 


OFED for windows Release Summary
--------------------------------

1) The winOFED 3.2 RC3 release is based on winOFED source svn revision 3635
   (branches\WOF3-2).

   Last OFED release (3.1) based on svn.3414.

2) New Features or Changes:

    Supported environments: (no Windows 8 support until the next release winOFED 3.3).
        Windows Server 2008 R2/HPC and Windows 7 for x64, x86.

    NetworkDirect.v2 provider
	NDlist - display NetworkDirect.v2 device & IP address

    uDAT / uDAPL 2.0.35 code base

    OpenSM version 3.3.13 (see '%windir%\temp\osm.syslog or %windir%\temp\osm.log' for runtime SM details).

    Mellanox RoCE not supported.

    Mellanox FDR HCAs suppported.

    Rsockets for Windows 1.0 supported [see rstream.exe & riostream.exe], thank you Herbert Schmitt! <Hubert.Schmitt@oce.com>
       See OFED_SDK install feature for code/build example (rstream.c).

   ***** Special Notes *****

      winOFED no longer supports:

           Mellanox Infinihost HCAs, Mellanox Connect-X HCA only.
           Vista & Server 2008 (Longhorn) operating environments; Server 2008 R2/WIndows-7 fully supported.
           Intel ia64 platforms; x64 and x86 only.


3) Bug fixes - all components.



**** Known Issues ****

If the install appears to hang, look around for popup windows requesting input which are
covered by other windows.  Such is the case on Server 2008 initial install - Answer 'yes'
to always trust the OpenFabrics Alliance as a SW publisher.


Please:
  Read the Release_notes.htm file!

  make 'sure' your Mellanox HCA firmware is recent:
      vstat.exe displays HCA firmware version & PSID.
      flint.exe (found at the Mellanox website, Windows firmware tools download package)
      displays PSID.

Thank you,

Mellanox and the OFED for Windows developers!



WDK (Windows Driver Kit) Build Environment Example [4-08-10]
-----------------------------------------------------------------

Install the Windows WDK see
  http://www.microsoft.com/whdc/Devtools/wdk/default.mspx

Why use the WDK? WDK (600.16385.0) is what's used to build the OFED for Windows
distribution.
Installing the WDK first requires burning a CD of the downloaded WDK - sigh...

Due to the problematic nature of spaces in path names, the Windows Driver Kit
does not allow spaces in pathnames; Due to the spaces in %ProgramFiles%\OFED
path, the OFED_SDK is not installed under %ProgramFiles%\OFED.
See %SystemDrive%\OFED_SDK\


Building CM test example
------------------------

Start a 'Free/Release' type WDK command window for you respective architecture
[x86,amd64,ia64]; assumed OS is Windows Server 2008 R2 (aka win7).

cd to %SystemDrive%\OFED_SDK\Samples\DDK

Set the 'OPENIB_REV' env variable to the svn version number; any # will work.
Hold the mouse-point over the file 'C:\Program Files\OFED\opensm.exe'.
The last field of the 'File Version' field is the svn revision number.

example: set OPENIB_REV=917

If building an 32-bit cmtest version on an x64 platform, edit Sources file by
adding '32' to ibal.lib and complib.lib, resulting in 'ibal32.lib' and
'complib32.lib'.
Note - executable will be appear as 'objfre_win7_x86\i386\cmtest.exe'.

build /wg

The executable will be created in a processor specific directory:

  x64 (Release/Free) example:
      %SystemDrive%\OFED_SDK\Samples\DDK\objfre_win7_amd64\amd64\cmtest.exe

  x64 (Checked/Debug)) example:
      %SystemDrive%\OFED_SDK\Samples\DDK\objchk_win7_amd64\amd64\cmtest.exe

  x86 (Release/Free) example:
      %SystemDrive%\OFED_SDK\Samples\DDK\objfre_win7_x86\i386\cmtest.exe

Executing cmtest.exe
--------------------

cmtest.exe passes messages between the server and client over a reliable IB
queue-pair connection (RC).
Connection end-points (hosts) are identified by IB port LID (hex integer, as
displayed by the vstat command).

Note: <spaces> are not allowed between command line switch and it's argument.

Server side:	cmtest -s -l0xlll -r0xrrr -m1024 -n100

Client side:	cmtest.exe -l0xlll -r0xrrr -m1024 -n100

where:
  lll == local port lid as displayed by vstat command.
  rrr == Remote port lid as displayed by vstat command;
           lll == rrr for local loopback operation.
  -m  == bytes per message
  -n  == number of messages to send.
  -c  == number of connections (default is -c1)
  cmtest -h reveals all...


Example cmtest invocation
-------------------------

cmdWin> vstat

        hca_idx=0
        uplink={BUS=UNRECOGNIZED (33)}
        vendor_id=0x02c9
        vendor_part_id=0x6278
        hw_ver=0xa0
        fw_ver=4.08.0200
        PSID=_00A0000001
        node_guid=0002:c902:0020:1d74
        num_phys_ports=2
                port=1
                port_state=PORT_ACTIVE (4)	<==== Must be active!
                link_speed=2.5Gbps (1)
                link_width=4x (2)
                rate=10
                sm_lid=0x0001
                port_lid=0x0020			<==== -l0x20
                port_lmc=0x0
                max_mtu=2048 (4)


Separate processes for server & client (different cmd windows).

server:    cmtest -s -l0x20 -r0x20 -m8192 -n400
client:    cmtest    -l0x20 -r0x20 -m8192 -n400

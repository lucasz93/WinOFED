
Visual Studio (C++) Build Environment [4-07-10]
-----------------------------------------------------------------

Install Microsoft Visual Studio 10 (C++ env)


**** WARNING - win32 application building ****

Disregard if building x64 application.

The Visual Studio default x86 calling convention is '__cdecl' (/Gd).
The 32-bit versions of ibal32.lib & complib32.lib are built using '__stdcall'
as the default calling convention (AL_API & CL_API).
Make _sure_ 'all' user-defined ibal and complib callback routines match the
callback function declaration [AL_API or CL_API calling conventions].
The VS compiler will note a C-style cast is required if the calling conventions
do not match.
The other option is to force __stdcall (/Gz) as the 'default' calling
convention for your 'win32' InfiniBand application.


Building CM test example from a cmd.exe window
----------------------------------------------


cd to %SystemDrive%\OFED_SDK\Samples\VS


Makefile Solution
-----------------

Assuming Visual Studio is installed, select a Visual Studio 2005 command
window from the start menu.

vcvarsall X64	 - sets X64 processor compilation env, X86 | IA64

nmake -f Makefile

If building a win32 application on a 64-bit platform then link with
lbal32[d].lib & complib32[d].lib.



Visual Studio Solution
----------------------

Double-click cmtest.c

Create a New Solution 'Project from Existing Code', using cmtest.c & cmtest.rc
Select a C++ console application project.

Salient Solution points:
  compile as a C program
  set additional Include path as C:\OFED_SDK\Inc
  set additional Resource Include path as C:\OFED_SDK\Inc
  Set additional Library path as C:\OFED_SDK\Lib .
  Link with additional libraries ibal.lib & complib.lib .
  If building a win32 application on a 64-bit platform then link with
  lbal32[d].lib & complib32[d].lib
  

Executing cmtest.exe
--------------------

cmtest.exe passes messages between the server and client over a reliable
IB queue-pair connection (RC).
Connection end-points (hosts) are identified by IB port LID (hex integer,
as displayed by the vstat command).

Note: <spaces> are not allowed between command line switch and it's argument.

Server side:	 cmtest -s -l0xlll -r0xrrr -m1024 -n100

Client side:	cmtest.exe -l0xlll -r0xrrr -m1024 -n100

where:
  lll == local port lid as displayed by vstat command.
  rrr == Remote port lid as displayed by vstat; lll == rrr for local loopback
         operation.
  -m  == bytes per message
  -n  == number of messages to send.
  -c  == number of connections (default is -c1)
  cmtest -h reveals all...

Example cmtest invocation: local loopback test

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
                port_lid=0x0020			<=== -l0x20
                port_lmc=0x0
                max_mtu=2048 (4)


Separate processes for server & client (different cmd windows).

server:    cmtest -s -l0x20 -r0x20 -m8192 -n400
client:    cmtest    -l0x20 -r0x20 -m8192 -n400

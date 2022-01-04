
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


Building rdma_bw test example from a cmd.exe window
----------------------------------------------


cd to %SystemDrive%\OFED_SDK\Samples\rdma_bw


Makefile Solution
-----------------

Assuming Visual Studio is installed, select a Visual Studio x64 command
window from the start menu.

nmake -f Makefile

If building a win32 application on a 64-bit platform then link with
lbal32[d].lib & complib32[d].lib.


Executing ibv_rdma_bw.exe
--------------------

  'ibv_rdma_bw -h' tells the story...

Example ibv_rdma_bw invocation: local loopback test

Separate processes for server & client (different cmd windows).

server:    ibv_rdma_bw 
client:    ibv_rdma_bw -h <ethernet-hostname>

Example using rdma_cm over IPoIB interface

server:    ibv_rdma_bw -c
client:    ibv_rdma_bw -c -h IPoIB-IF-IPv4-address


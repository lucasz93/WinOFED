
Building rstream.exe in the Visual Studio (C++) Build Environment [4-10-2013]
-----------------------------------------------------------------

Install Microsoft Visual Studio 10 (C++ env)

Select a Visual Studio command window from the start menu

x64 Win64 command window works fine.

IF x86/win32 building THEN
  The Visual Studio default x86 calling convention is '__cdecl' (/Gd).
  The 32-bit versions of ibal32.lib & complib32.lib are built using '__stdcall'
  as the default calling convention (AL_API & CL_API).
  Make _sure_ 'all' user-defined ibal and complib callback routines match the
  callback function declaration [AL_API or CL_API calling conventions].
  The VS compiler will note a C-style cast is required if the calling conventions
  do not match.
  The other option is to force __stdcall (/Gz) as the 'default' calling
  convention for your 'win32' InfiniBand application.
ENDIF


cd to %SystemDrive%\OFED_SDK\Samples\rstream


Makefile Solution
-----------------

nmake -f Makefile


Note:
  If building a win32 application on a 64-bit platform then link with
  lbal32[d].lib & complib32[d].lib.


Executing rstream.exe
--------------------

  'rstream /?' tells the story...

Example rstream invocation: Separate processes for server & client (different cmd windows).

Example using rstream over IPoIB interface

server:    rstream -p 1024
client:    rstream -p 1024 -s 10.10.4.201	# IPoIB-IPv4-address


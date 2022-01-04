
in a win7 x64 build window - build /wgcf

run setup.bat in order to copy .dll files to c:\temp\dapl\

Add to C:\DAT\dat.con
ND u2.0 nonthreadsafe default C:\Temp\DAPL\dapl2-ND.dll ri.2.0 "ND0 1" ""

To test - requires to cmd windows @ c:\temp\DAPL, see readme.txt in c:\temp\dapl\

[8-14-12] NDv1 Delayed Endpoint creation now working; buffered r/w OK, RDMA is not.

[8-15-2012] start Conversion to NDv2
	Must switch to mlx4_30\ base which involves rebuilding installer, drivers, ND2 NDA .h files....sigh

[9-01-2012] Finished/validated base mlmx4_30 install, actually start on DAPL/ND to NDv2

[9-06-12] NDv2 building, start debug

[9-13-12] dtest2 listening for a connection; MemRegister, CQ creation, QP creation and Listen working (somewhat, yet to connect).

[9-20-12] dtest2 server and client complete successfully; dtestcm fails > 1 connection - ref counts on cm_id?

[1-2-13] dapls_ib_disconnect() using a serialized event returns IO_Pending
	with following GetOverlappedResults() taking ~ 68000 milliseconds to 
	complete successfully.





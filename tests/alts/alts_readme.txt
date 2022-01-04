README:

AL Test Suite consists of a bunch of test cases to test the AL functionality.
Test case are focused for bring up of AL and some data transfer test cases.
AL test suite can be used to test individual AL exposed API's and also can
be used to debug and bring up of SHIM.

AL Test Suite consists of Kernel Mode component and a user mode component.
All the test cases are under shared/alts directory. These test case 
can be compiled for both user mode as well as for kernel mode.

1)AL Test Suite for User mode
	This consists of user mode AL test application which has all the test cases in
	usermode test application. No kernel mode component is required here. However
	this al test suite needs user mode component library.
	
	Compiling user mode AL test suite:
	a)First compile User mode Component Library.
	
	>cd ~/linuxuser/iba/complib
	>make

	b)To compile AL test suite for User mode, run make command with BUILD_USER set
	to 1 as showm below.

	>cd ~/linuxuser/iba/alts
	>make BUILD_USER

2)AL Test Suite for Kernel mode
	This consists both a user mode component and a kernel mode component. User mode
	component is needed to drive the test. Kernel mode component is a driver
	which consists of all the test cases compiled in linked to the driver.

	Compiling kernel mode AL test suite:
	a)Compile User mode Component Library.
	
	>cd ~/linuxuser/iba/complib
	>make

	b)Compile Kernel mode Component Library

	>cd ~/linux/drivers/iba/complib
	>make

	c>Compile user mode AL test suite

	>cd ~/linuxuser/iba/alts
	>make

	d) Compile kernel mode AL test Driver

	>cd ~/linux/drivers/iba/alts
	>make


3)Running the test:
If you would like to test KAL, then you need to install kernel mode
component of AL test suite as shown below.

>cd ~/linux/drivers/iba/alts
>insmod altsdrv.o

Running specific tests are as shown below.
	>./alts -tc=XXXXX [-um|-km]

	tc-> stands for test case to run.
	-um -> user mode test
	-km -> kernel mode test

	XXXX can be any one of the following.
	OpenClose	
	QueryCAAttribute
	ModifyCAAttribute
	AllocDeallocPD
	AllocDeallocRDD
	CreateDestroyAV
	QueryAndModifyAV
	CreateDestroyQP
	QueryAndModifyQP
	CreateAndDestroyCQ
	QueryAndModifyCQ
	CreateAndDestroyEEC
	QueryAndModifyEEC
	AttachMultiCast
	RegisterMemRegion
	RegisterVarMemRegions
	ReregisterHca
	RegisterPhyMemRegion
	CreateMemWindow
	RegisterSharedMemRegion
	MultiSend
	RegisterPnP
	MadTests
	MadQuery
	CmTests

	To run OpenClose test case in user mode
	>./alts -tc=OpenClose -um

	To run OpenClose test case in kernel mode
	>./alts -tc=OpenClose -km	

	OR
	>./alts -tc=OpenClose

	Default is kernel mode.


To see the results:
All the log messages by default goes to /var/log/messages file. 
Grep for "failed" if any test case has failed you should see
that in this file.



	


	

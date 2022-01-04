#
# Edit .\sources. if you want to add a new source
# file to this component.  This file merely indirects to the real make file
# that is shared by all the driver components of the OpenIB Windows project.
#

# If ND SDK installed then Defeat IA64 builds
# Otherwise, default all builds.

DDK_BLOCK_ON_IA64=1
!IFNDEF ND_SDK_PATH
!MESSAGE Skipping ibndprov.dll build: ND_SD_PATH not set.
DDK_BLOCK_ON_X86=1
DDK_BLOCK_ON_AMD64=1
!ENDIF

!INCLUDE ..\..\..\inc\openib.def

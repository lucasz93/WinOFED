#
# Edit .\sources. if you want to add a new source
# file to this component.  This file merely indirects to the real make file
# that is shared by all the driver components of the OpenIB Windows project.
#

# Never build for IA64.
DDK_BLOCK_ON_IA64=1

# If ND SDK is missing, defeat all builds.
!IFNDEF ND_SDK_PATH
!MESSAGE Skipping mxl4nd.dll build: ND_SD_PATH not set.
DDK_BLOCK_ON_X86=1
DDK_BLOCK_ON_AMD64=1
!ENDIF

!INCLUDE ..\..\..\..\inc\openib.def

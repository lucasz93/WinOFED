#
#*********************************************************************
# 
# NMAKE Options (passed as macro) 
#
# Select a Visual Studio command window from the start menu
# vcvarsall X64	 - sets X64 processor compilation env, X86 | IA64
#
#*********************************************************************/


#*********************************************************************
# 
# Dot Directives
#
#*********************************************************************/

.SUFFIXES :		# clear the .SUFFIXES list
.SUFFIXES : .c 		# initialize .SUFFIXES list


#*********************************************************************
# 
# Macros
#
#*********************************************************************/

SRC=cmtest

LIB_PATH=C:\OFED_SDK\Lib
INC_PATH=C:\OFED_SDK\Inc

IB_LIBS=ibal.lib complib.lib

DEFS= /D_WIN64 /D_X64_ /D_AMD64_

ARCH=x64

OBJS =	$(SRC).obj
EXEC = $(SRC).exe

#
# Compiler 
#

CC = cl

INC_FLAGS = /I $(INC_PATH)

UNUSED_CL=/Zp1 /Od
UNUSED_LINKER= /DEBUG /incremental:no

CC_FLAGS= /nologo /Gy /W3 /Gm- \
	/GR- /GF  /O2 /Oi /Oy- /D_CRT_SECURE_NO_WARNINGS \
	$(DEFS) $(INC_FLAGS)

#
# Linker
#

LINK = link

LIBS = 	ws2_32.lib advapi32.lib User32.lib

LINK_FLAGS = /nologo /subsystem:console /machine:$(ARCH) $(LIBS) \
		/libpath:$(LIB_PATH) $(IB_LIBS)

#
# System Utilities
#

RM = del /Q


#*********************************************************************
# Inference Rules
#
#*********************************************************************/

.c.obj:
	$(CC) $(CC_FLAGS) /Fo$@ /c $< 


#*********************************************************************
#
# Description Blocks
#
#*********************************************************************/

all : $(EXEC)


$(EXEC) : $(OBJS) $0
	$(LINK) $(LINK_FLAGS) /out:$(EXEC) $(OBJS)

clean: 
	-$(RM) *.exe *.pdb *.obj

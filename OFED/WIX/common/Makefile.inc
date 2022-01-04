
# Common Makefile include for WIX Installer (.msi) build

# Inputs args:
#
# S == WIX src & obj filename: .wxs & .wixobj
# A == arch {x86,ia64,x64}
# P == Installer (.msi) 'complete' filename
# optional: DFXP == Path to DifxApp files setup in makebin.bat

!ifndef DFXP
DFXP=..\bin\Misc\$(A)
!endif

# WIX 3.5 tools
L=..\..\WIX_tools\wix35-binaries

DFX=difxapp_$(A).wixlib

# Since makebin.bat knows correct WDK version, makebin copies the DIFX APP files
# to the bin\Misc\arch tree; this eliminates Makefiles having to know about
# WDK versions.  see DFXP=..\bin\Misc\arch

full: clean $(P).msi

clean:
	@del /q $(S).wixobj 2>nul
	@del /q $(P).wixpdb 2>nul
	@del /q $(P).msi 2>nul
	@del /q/f DIFxA*.* 2>nul

MySetup: $(P).msi

# .dlls need to be in the current folder
$(DFX) : $(L)\$(DFX) $(DFXP)\DIFxApp.dll $(DFXP)\DIFxAppA.dll
	@copy /B/Y $(DFXP)\DIFxApp.dll .
	@copy /B/Y $(DFXP)\DIFxAppA.dll .
	@copy /B/Y $(L)\$(DFX) .

$(S).wixobj: $(S).wxs

$(P).msi: $(S).wixobj $(DFX)
	@echo --
	@echo Building $(P).msi
	$(L)\light.exe -nologo -ext WixUiExtension -ext WixDifxAppExtension -out $(P).msi $(S).wixobj $(DFX)
	@del /q $(S).wixobj 2>nul
	@del /q/f DIFxA*.* 2>nul
	@del /q $(P).wixpdb 2>nul


###################
# makefile inference rules
#
.SUFFIXES: .wxs .wixobj

.wxs.wixobj::
	@$(L)\candle.exe -nologo -arch $(A) -ext WixDifxAppExtension  -trace -v $<


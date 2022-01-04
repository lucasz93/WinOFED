# Fake like something useful is being built. 
# Recreate standard build obj{fre|chk}_xxx_xxx\ folders from cached binaries.
# files then copied to the $(ND_TARGET*)
#
# When ND sources hit svn, this hack will be removed.
# recreate dir structure and nd binaries are obj*\ folders tend to get
# removed; don't want to lose the ONLY copies of the ND binaries we have.
#
# ND on ia64 is not supported [8-07-08]. Building the ia64\ folders is a
# blantant hack to keep the build happy; WIX installer does not use the
# ia64 .exe or .dll. If someone knows how to force the build to skip ia64,
# please enlighten.

Custom_target:
!if "$(BUILD_PASS)" == "PASS2" || "$(BUILD_PASS)" == "ALL"

!endif

!INCLUDE ..\..\..\inc\mod_ver.def





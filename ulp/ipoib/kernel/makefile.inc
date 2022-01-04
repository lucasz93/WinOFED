
# Transform .inx file to .inf file adding date + major,min & svn.version stamp
# Output .inf file is copied to the $(INF_TARGET) folder (commonly where .sys file resides).

_LNG=$(LANGUAGE)

!IF !DEFINED(_INX)
_INX=.
!ENDIF

STAMP=stampinf -a $(_BUILDARCH)

!INCLUDE ..\..\..\inc\mod_ver.def

$(INF_TARGET) : $(_INX)\$(INF_NAME).inx
    copy $(_INX)\$(@B).inx $@
    $(STAMP) -f $@ -d * -v $(IB_MAJORVERSION).$(IB_MINORVERSION).$(OPENIB_REV)

mofcomp: mlx4_hca.bmf

mlx4_hca.bmf: hca.mof
        mofcomp -B:$(OBJ_PATH)\$O\mlx4_hca.bmf hca.mof
        wmimofck $(OBJ_PATH)\$O\mlx4_hca.bmf


# Transform .inx file to .inf file adding date + major,min & svn.version stamp
# Output .inf file is copied to the $(INF_TARGET) folder (commonly where .sys file resides).

_LNG=$(LANGUAGE)

!IF !DEFINED(_INX)
_INX=.
!ENDIF

STAMP=stampinf -a $(_BUILDARCH) -k $(KMDF_VERSION_MAJOR).$(KMDF_VERSION_MINOR)

!INCLUDE ..\..\..\..\inc\mod_ver.def

$(INF_TARGET) : $(_INX)\$(INF_NAME).inx
    copy $(_INX)\$(@B).inx $@
    $(STAMP) -f $@ -d * -v $(IB_MAJORVERSION).$(IB_MINORVERSION).$(IB_BUILDVERSION).$(OPENIB_REV)

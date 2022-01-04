# Vendor umad

VENDOR_INC= $(WINIBHOME)\ulp\libibmad\include; \
	$(WINIBHOME)\ulp\libibumad\include;

VENDOR_LIBS=$(TARGETPATH)\*\osmv_openib.lib \
	$(LIBPATH)\*\libibmad.lib \
	$(LIBPATH)\*\libibumad.lib

VENDOR_IF=OSM_VENDOR_INTF_OPENIB


#ifndef HCI_PCI_H
#define HCI_PCI_H


NTSTATUS
hca_reset(
	IN		DEVICE_OBJECT* const		pDevObj, int is_tavor );

NTSTATUS
hca_enable_pci(
	IN		DEVICE_OBJECT* const		pDevObj,
	OUT 	PBUS_INTERFACE_STANDARD phcaBusIfc,
	OUT 	PCI_COMMON_CONFIG*	pHcaConfig
	);

void hca_disable_pci(
	IN		PBUS_INTERFACE_STANDARD	phcaBusIfc);

NTSTATUS
	hca_tune_pci(
		IN				DEVICE_OBJECT* const		pDevObj,
		OUT 			uplink_info_t *p_uplink_info );

#endif

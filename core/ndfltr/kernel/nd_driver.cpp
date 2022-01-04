/*
 * Copyright (c) 2008 Intel Corporation. All rights reserved.
 * Portions (c) Microsoft Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE ANd
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "precomp.h"

#include <initguid.h>
#include <rdma\verbs.h>
#include <iba\ib_cm_ifc.h>
#include <iba\ibat_ifc.h>

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ND_RDMA_DEVICE, NdRdmaDeviceGetContext);

WDFDEVICE                           ControlDevice;
IBAT_IFC                            IbatInterface;


static EVT_WDF_DRIVER_DEVICE_ADD            NdRdmaDeviceAdd;
static EVT_WDF_OBJECT_CONTEXT_CLEANUP       NdRdmaDeviceCleanup;
static EVT_WDF_DEVICE_D0_ENTRY              NdPowerD0Entry;
static EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED NdPowerD0Exit;
static EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL   NdIoDeviceControl;
static EVT_WDF_DEVICE_FILE_CREATE           NdFileCreate;
static EVT_WDF_FILE_CLEANUP                 NdFileCleanup;

static FN_REQUEST_HANDLER NdInvalidRequest;
static FN_REQUEST_HANDLER NdProviderQueryAddressList;
static FN_REQUEST_HANDLER NdProviderResolveAddress;

#define ND_DISPATCH_COUNT(name_) \
    ((UCHAR)(FIELD_SIZE(ND_DISPATCH_TABLE,##name_)/sizeof(FN_REQUEST_HANDLER*)))

#define ND_DISPATCH_OFFSET(name_) \
    ((UCHAR)(FIELD_OFFSET(ND_DISPATCH_TABLE,##name_)/sizeof(FN_REQUEST_HANDLER*))+1)

#define ND_DISPATCH_INDEX(name_) \
    MAKEWORD(ND_DISPATCH_OFFSET(name_),ND_DISPATCH_COUNT(name_))

struct ND_DISPATCH_TABLE {
    FN_REQUEST_HANDLER* NdProvider[IOCTL_ND_PROVIDER_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdAdapter[IOCTL_ND_ADAPTER_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdPd[IOCTL_ND_PD_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdCq[IOCTL_ND_CQ_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdMr[IOCTL_ND_MR_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdMw[IOCTL_ND_MW_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdSrq[IOCTL_ND_SRQ_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdConnector[IOCTL_ND_CONNECTOR_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdListener[IOCTL_ND_LISTENER_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdQp[IOCTL_ND_QP_MAX_OPERATION];
    FN_REQUEST_HANDLER* NdVirtualPartition[IOCTL_NDV_PARTITION_MAX_OPERATION];
};

static const struct ND_IOCTL_DISPATCH {
    WORD Index[ND_RESOURCE_TYPE_COUNT];
    FN_REQUEST_HANDLER* Table[1];
    ND_DISPATCH_TABLE Dispatch;
} IoctlDispatch = {
    { ND_DISPATCH_INDEX(NdProvider),
    ND_DISPATCH_INDEX(NdAdapter),
    ND_DISPATCH_INDEX(NdPd),
    ND_DISPATCH_INDEX(NdCq),
    ND_DISPATCH_INDEX(NdMr),
    ND_DISPATCH_INDEX(NdMw),
    ND_DISPATCH_INDEX(NdSrq),
    ND_DISPATCH_INDEX(NdConnector),
    ND_DISPATCH_INDEX(NdListener),
    ND_DISPATCH_INDEX(NdQp),
    ND_DISPATCH_INDEX(NdVirtualPartition) },

    { NULL },

    {
        { ND_DRIVER::InitProvider,
        NdProviderBindFile,
        ND_PROVIDER::QueryAddressList,
        ND_PROVIDER::ResolveAddress },

        { NdAdapterOpen,
        NdAdapterClose,
        ND_ADAPTER::Query,
        ND_ADAPTER::QueryAddressList },

        { NdPdCreate,
        NdPdFree },

        { NdCqCreate,
        NdCqFree,
        NdCqCancelIo,
        NdCqGetAffinity,
        NdCqModify,
        NdCqNotify },

        { NdMrCreate,
        NdMrFree,
        NdMrCancelIo,
        NdMrRegister,
        NdMrDeregister,
        NdMrRegisterPages },

        { NdMwCreate,
        NdMwFree },

        { NdSrqCreate,
        NdSrqFree,
        NdSrqCancelIo,
        NdSrqGetAffinity,
        NdSrqModify,
        NdSrqNotify },

        { NdConnectorCreate,
        NdEpFree,
        NdEpCancelIo,
        NdEpBind,
        NdEpConnect,
        NdEpCompleteConnect,
        NdEpAccept,
        NdEpReject,
        NdEpGetReadLimits,
        NdEpGetPrivateData,
        NdEpGetPeerAddress,
        NdEpGetAddress,
        NdEpNotifyDisconnect,
        NdEpDisconnect },

        { NdListenerCreate,
        NdEpFree,
        NdEpCancelIo,
        NdEpBind,
        NdEpListen,
        NdEpGetAddress,
        NdEpGetConnectionRequest },

        { NdQpCreate,
        NdQpCreateWithSrq,
        NdQpFree,
        NdQpFlush },

        { ND_PARTITION::ResolveAdapterId,
        ND_DRIVER::CreatePartition,
        ND_PARTITION::Free,
        ND_PARTITION::BindAddress,
        ND_PARTITION::UnbindAddress,
        ND_PARTITION::BindLuid }
    }
};


static
void
NdInvalidRequest(
    __in ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool /*InternalDeviceControl*/
    )
{
    WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
}


void* operator new(size_t, void* pObj)
{
    return pObj;
}


ND_DRIVER::ND_DRIVER()
    : m_Partition0(0)
{
    InitializeListHead(&m_DevList);
    KeInitializeGuardedMutex(&m_Lock);
}


ND_DRIVER::~ND_DRIVER()
{
    m_ProvTable.Iterate(DisposeFunctor<ND_PROVIDER>());
    m_PartitionTable.Iterate(DisposeFunctor<ND_PARTITION>());
}


ND_RDMA_DEVICE*
ND_DRIVER::GetRdmaDevice(
    UINT64 AdapterId
    )
{
    ND_RDMA_DEVICE    *curDev, *dev = NULL;
    LIST_ENTRY        *entry;

    KeAcquireGuardedMutex(&m_Lock);
    for (entry = m_DevList.Flink; entry != &m_DevList; entry = entry->Flink) {
        curDev = CONTAINING_RECORD(entry, ND_RDMA_DEVICE, Entry);
        // TODO: Need to check against possible MACs somehow.
        if (curDev->Interface.Verbs.guid == AdapterId) {
            InterlockedIncrement(&curDev->Ref);
            dev = curDev;
            break;
        }
    }
    KeReleaseGuardedMutex(&m_Lock);
    return dev;
}


ND_PROVIDER* ND_DRIVER::GetProvider(UINT64 Handle)
{
    m_ProvTable.LockShared();
    ND_PROVIDER* prov = m_ProvTable.At(Handle);
    if (prov != NULL) {
        prov->AddRef();
    }
    m_ProvTable.Unlock();
    return prov;
}


ND_PARTITION* ND_DRIVER::GetPartition(UINT64 Handle, bool InternalDeviceControl)
{
    if (InternalDeviceControl == false) {
        m_Partition0.AddRef();
        return &m_Partition0;
    }

    m_PartitionTable.LockShared();
    ND_PARTITION* part = m_PartitionTable.At(Handle);
    if (part != NULL) {
        part->AddRef();
    }
    m_PartitionTable.Unlock();
    return part;
}


NTSTATUS ND_DRIVER::FreePartition(UINT64 Handle)
{
    NTSTATUS status;
    m_PartitionTable.LockExclusive();
    ND_PARTITION* part = m_PartitionTable.At(Handle);
    if (part == NULL) {
        status = STATUS_INVALID_HANDLE;
    } else if (part->Busy()) {
        status = STATUS_DEVICE_BUSY;
    } else {
        m_PartitionTable.Erase(Handle);
        status = STATUS_SUCCESS;
    }
    m_PartitionTable.Unlock();

    if (NT_SUCCESS(status)) {
        part->Dispose();
    }
    return status;
}


//static
void
ND_DRIVER::InitProvider(
    ND_PROVIDER* pProvider,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    if (pProvider != NULL) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
        return;
    }

    WDFFILEOBJECT file = WdfRequestGetFileObject(Request);
    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());

    ND_HANDLE* in;
    size_t inLen;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        &inLen
        );
    if (!NT_SUCCESS(status)) {
        goto err;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    ND_PARTITION* part = driver->GetPartition(in->Handle, InternalDeviceControl);
    if (part == NULL) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto err;
    }

    driver->m_ProvTable.LockExclusive();
    UINT64 hProv = driver->m_ProvTable.Reserve();
    if (hProv == 0) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto unlock;
    }

    ND_PROVIDER *prov;
    status = ND_PROVIDER::Create(part, &prov);
    if (!NT_SUCCESS(status)) {
        driver->m_ProvTable.Free(hProv);
        goto unlock;
    }

    status = prov->Bind(hProv, file);
    if (!NT_SUCCESS(status)) {
        prov->Dispose();
        driver->m_ProvTable.Free(hProv);
    } else {
        driver->m_ProvTable.Set(hProv, prov);
    }

unlock:
    driver->m_ProvTable.Unlock();
    part->Release();

err:
    WdfRequestComplete(Request, status);
}


//static
void
ND_DRIVER::CreatePartition(
    ND_PROVIDER* /*pProvider*/,
    WDFREQUEST Request,
    bool InternalDeviceControl
    )
{
    if (InternalDeviceControl == false) {
        WdfRequestComplete(Request, STATUS_INVALID_DEVICE_REQUEST);
        return;
    }

    NDV_PARTITION_CREATE* in;
    NTSTATUS status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(*in),
        reinterpret_cast<VOID**>(&in),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    if (in->Version != ND_IOCTL_VERSION) {
        status = STATUS_INVALID_DEVICE_REQUEST;
        goto out;
    }

    if (in->MmioType != NdPartitionKernelVirtual) {
        status = STATUS_NOT_SUPPORTED;
        goto out;
    }

    if (in->XmitCap != 0) {
        status = STATUS_NOT_SUPPORTED;
        goto out;
    }

    UINT64* out;
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(*out),
        reinterpret_cast<VOID**>(&out),
        NULL
        );
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    ND_PARTITION* part;
    status = ND_PARTITION::Create(in->AdapterId, &part);
    if (!NT_SUCCESS(status)) {
        goto out;
    }

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    driver->m_PartitionTable.LockExclusive();
    *out = driver->m_PartitionTable.Insert(part);
    driver->m_PartitionTable.Unlock();

    if (*out == 0) {
        part->Dispose();
        status = STATUS_INSUFFICIENT_RESOURCES;
    } else {
        WdfRequestSetInformation(Request, sizeof(*out));
    }

out:
    WdfRequestComplete(Request, status);
}


ND_RDMA_DEVICE*
NdRdmaDeviceGet(
    UINT64 AdapterId
    )
{
    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    return driver->GetRdmaDevice(AdapterId);
}


void
NdRdmaDevicePut(
    ND_RDMA_DEVICE *pDevice
    )
{
    if (InterlockedDecrement(&pDevice->Ref) == 0) {
        KeSetEvent(&pDevice->Event, 0, FALSE);
    }
}


static
ND_PROVIDER*
NdGetProvider(
    WDFFILEOBJECT File
    )
{
    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    return driver->GetProvider(ND_PROVIDER::HandleFromFile(File));
}


void
NdCompleteRequests(
    WDFQUEUE Queue,
    NTSTATUS ReqStatus
    )
{
    WDFREQUEST    request;
    NTSTATUS    status;

    status = WdfIoQueueRetrieveNextRequest(Queue, &request);

    while (NT_SUCCESS(status)) {
        WdfRequestComplete(request, ReqStatus);
        status = WdfIoQueueRetrieveNextRequest(Queue, &request);
    }
}


void
NdFlushQueue(
    WDFQUEUE Queue,
    NTSTATUS ReqStatus
    )
{
    //TODO: Don't think we need the locking here...
    WdfObjectAcquireLock(Queue);
    NdCompleteRequests(Queue, ReqStatus);
    WdfObjectReleaseLock(Queue);
}


void
NdCompleteRequestsWithInformation(
    WDFQUEUE Queue,
    NTSTATUS ReqStatus
    )
{
    WDFREQUEST  request;
    NTSTATUS    status;
    VOID        *out;
    size_t      outlen;

    status = WdfIoQueueRetrieveNextRequest(Queue, &request);

    while (NT_SUCCESS(status)) {
        outlen = 0;
        WdfRequestRetrieveOutputBuffer(request, 0, &out, &outlen);
        WdfRequestCompleteWithInformation(request, ReqStatus, outlen);
        status = WdfIoQueueRetrieveNextRequest(Queue, &request);
    }
}


static
VOID
IoctlHandler(
    WDFREQUEST Request,
    ULONG IoControlCode,
    bool InternalDeviceControl
    )
{
    ND_PROVIDER* prov = NULL;

    ULONG res = ND_RESOURCE_FROM_CTL_CODE(IoControlCode);
    ULONG op = ND_OPERATION_FROM_CTRL_CODE(IoControlCode);

    if (res != NdVirtualPartition) {
        prov = NdGetProvider(WdfRequestGetFileObject(Request));
        if (prov == NULL) {
            if (IoControlCode != IOCTL_ND_PROVIDER_INIT) {
                WdfRequestComplete(Request, STATUS_INVALID_DEVICE_STATE);
            } else {
                ND_DRIVER::InitProvider(NULL, Request, InternalDeviceControl);
            }
            return;
        }
    }

    WORD index = IoctlDispatch.Index[res];
    if (op < HIBYTE(index)) {
        IoctlDispatch.Table[LOBYTE(index) + op](prov, Request, InternalDeviceControl);
    } else {
        NdInvalidRequest(NULL, Request, InternalDeviceControl);
    }

    if (prov != NULL) {
        prov->Release();
    }
}


static
VOID
NdDeviceControl(
    WDFQUEUE,
    WDFREQUEST Request,
    size_t,
    size_t,
    ULONG IoControlCode
    )
{
    IoctlHandler(Request, IoControlCode, false);
}


static
VOID
NdInternalDeviceControl(
    WDFQUEUE,
    WDFREQUEST Request,
    size_t,
    size_t,
    ULONG IoControlCode
    )
{
    IoctlHandler(Request, IoControlCode, true);
}


static VOID
NdFileCreate(
    WDFDEVICE,
    WDFREQUEST Request,
    WDFFILEOBJECT
    )
{
    WdfRequestComplete(Request, STATUS_SUCCESS);
}


void ND_DRIVER::FreeProvider(WDFFILEOBJECT FileObject)
{
    m_ProvTable.LockExclusive();
    UINT64 hProv = ND_PROVIDER::HandleFromFile(FileObject);
    ND_PROVIDER* prov = m_ProvTable.At(hProv);
    if (prov != NULL && prov->Unbind(FileObject) == 0 ) {
        m_ProvTable.Erase(hProv);
        m_ProvTable.Unlock();
        prov->Dispose();
    } else {
        m_ProvTable.Unlock();
    }
}


static
VOID
NdFileCleanup(
    WDFFILEOBJECT FileObject
    )
{
    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    driver->FreeProvider(FileObject);
}


VOID
NdCreateControlDevice(
    WDFDRIVER Driver
    )
{
    PWDFDEVICE_INIT         pInit;
    WDF_FILEOBJECT_CONFIG   fileConfig;
    WDF_OBJECT_ATTRIBUTES   attr;
    WDF_IO_QUEUE_CONFIG     ioConfig;
    NTSTATUS                status;
    WDFQUEUE                queue;
    DECLARE_CONST_UNICODE_STRING(name, L"\\Device\\Ndfltr");
    DECLARE_CONST_UNICODE_STRING(symlink, ND_DOS_DEVICE_NAME);

    pInit = WdfControlDeviceInitAllocate(
        Driver,
        &SDDL_DEVOBJ_SYS_ALL_ADM_RWX_WORLD_RW_RES_R
        );
    if (pInit == NULL) {
        return;
    }

    WdfDeviceInitSetExclusive(pInit, FALSE);
    status = WdfDeviceInitAssignName(pInit, &name);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(pInit);
        return;
    }

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,
        NdFileCreate,
        WDF_NO_EVENT_CALLBACK,
        NdFileCleanup
        );
    //
    // We will use FsContext to store our app context.  This allows us to have
    // multiple files associated with a single app context.
    //
    fileConfig.FileObjectClass = WdfFileObjectWdfCanUseFsContext2;

    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    attr.SynchronizationScope = WdfSynchronizationScopeNone;
    WdfDeviceInitSetFileObjectConfig(pInit, &fileConfig, &attr);

    WDF_OBJECT_ATTRIBUTES_INIT(&attr);
    status = WdfDeviceCreate(&pInit, &attr, &ControlDevice);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(pInit);
        return;
    }

    status = WdfDeviceCreateSymbolicLink(ControlDevice, &symlink);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(ControlDevice);
        ControlDevice = NULL;
        return;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioConfig, WdfIoQueueDispatchParallel);
    ioConfig.PowerManaged = WdfFalse;
    ioConfig.EvtIoDeviceControl = NdDeviceControl;
    ioConfig.EvtIoInternalDeviceControl = NdInternalDeviceControl;
    status = WdfIoQueueCreate(
        ControlDevice,
        &ioConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
        );
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(ControlDevice);
        ControlDevice = NULL;
        return;
    }

    WdfControlFinishInitializing(ControlDevice);
}


NTSTATUS ND_DRIVER::AddDevice(ND_RDMA_DEVICE* pDev)
{
    KeAcquireGuardedMutex(&m_Lock);
    BOOLEAN create = IsListEmpty(&m_DevList);
    InsertTailList(&m_DevList, &pDev->Entry);
    KeReleaseGuardedMutex(&m_Lock);

    if (create) {
        NTSTATUS status = WdfFdoQueryForInterface(
            pDev->WdfDev,
            &GUID_IBAT_INTERFACE,
            (PINTERFACE) &IbatInterface,
            sizeof(IbatInterface),
            IBAT_INTERFACE_VERSION,
            NULL
            );
        if (!NT_SUCCESS(status)) {
            return status;
        }
        //
        // We are in the same device stack, so no need to hold on to the interface.
        //
        IbatInterface.InterfaceHeader.InterfaceDereference(
            IbatInterface.InterfaceHeader.Context
            );
        NdCreateControlDevice(WdfGetDriver());
    }
    return STATUS_SUCCESS;
}


static
NTSTATUS
NdPowerD0Entry(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE
    )
{
    ND_RDMA_DEVICE  *dev;
    NTSTATUS        status;

    dev = NdRdmaDeviceGetContext(Device);
    if (dev->hDevice != NULL) {
        return STATUS_SUCCESS;
    }

    status = WdfFdoQueryForInterface(
        Device,
        &GUID_INFINIBAND_INTERFACE_CM,
        (PINTERFACE) &dev->CmInterface,
        sizeof(dev->CmInterface),
        INFINIBAND_INTERFACE_CM_VERSION,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }
    //
    // We are in the same device stack, so no need to hold on to the interface.
    //
    dev->CmInterface.InterfaceHeader.InterfaceDereference(
        dev->CmInterface.InterfaceHeader.Context
        );

    status = WdfFdoQueryForInterface(
        Device,
        &GUID_RDMA_INTERFACE_VERBS,
        (PINTERFACE) &dev->Interface,
        sizeof(dev->Interface),
        RDMA_INTERFACE_VERBS_VERSION,
        NULL
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }
    //
    // We are in the same device stack, so no need to hold on to the interface.
    //
    dev->Interface.InterfaceHeader.InterfaceDereference(
        dev->Interface.InterfaceHeader.Context
        );

    dev->hDevice = reinterpret_cast<ib_ca_handle_t>(dev->Interface.Verbs.p_hca_obj);

    ib_ca_attr_t* attr = dev->QueryCaAttributes();
    if (attr == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    dev->Info.InfoVersion =                 ND_VERSION_2;
    dev->Info.VendorId =                    static_cast<UINT16>(attr->vend_id);
    dev->Info.DeviceId =                    static_cast<UINT16>(attr->dev_id);
    dev->Info.AdapterId =                   attr->ca_guid;
    dev->Info.MaxRegistrationSize =         static_cast<SIZE_T>(attr->init_region_size);
    dev->Info.MaxWindowSize =               static_cast<SIZE_T>(attr->init_region_size);
    dev->Info.MaxInitiatorSge =             attr->max_sges;
    dev->Info.MaxReceiveSge =               attr->max_sges;
    dev->Info.MaxReadSge =                  attr->max_sges;
    if (attr->num_ports == 0) {
        dev->Info.MaxTransferLength =       0;
    } else {
        dev->Info.MaxTransferLength =       static_cast<ULONG>(attr->p_port_attr->max_msg_size);
    }
    dev->Info.MaxInlineDataSize =           0;
    dev->Info.MaxInboundReadLimit =         attr->max_qp_resp_res;
    dev->Info.MaxOutboundReadLimit =        attr->max_qp_init_depth;
    dev->Info.MaxReceiveQueueDepth =        attr->max_wrs;
    dev->Info.MaxInitiatorQueueDepth =      attr->max_wrs;
    dev->Info.MaxSharedReceiveQueueDepth =  attr->max_srq_wrs;
    dev->Info.MaxCompletionQueueDepth =     attr->max_cqes;
    dev->Info.InlineRequestThreshold =      172;
    dev->Info.LargeRequestThreshold =       65536;
    dev->Info.MaxCallerData =               IB_REQ_CM_RDMA_PDATA_SIZE;
    dev->Info.MaxCalleeData =               IB_REJ_PDATA_SIZE;
    dev->Info.AdapterFlags =                ND_ADAPTER_FLAG_IN_ORDER_DMA_SUPPORTED |
                                            ND_ADAPTER_FLAG_MULTI_ENGINE_SUPPORTED |
                                            ND_ADAPTER_FLAG_LOOPBACK_CONNECTIONS_SUPPORTED;

    ExFreePoolWithTag(attr, ND_RDMA_DEVICE_POOL_TAG);

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    return driver->AddDevice(dev);
}


static
NTSTATUS
NdRdmaDeviceAdd(
    WDFDRIVER,
    PWDFDEVICE_INIT DeviceInit
    )
{
    WDF_OBJECT_ATTRIBUTES           attr;
    WDF_PNPPOWER_EVENT_CALLBACKS    power;
    WDFDEVICE                       dev;
    ND_RDMA_DEVICE                  *pDev;
    NTSTATUS                        status;

    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, ND_RDMA_DEVICE);
    attr.EvtCleanupCallback = NdRdmaDeviceCleanup;

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);
    power.EvtDeviceD0EntryPostInterruptsEnabled = NdPowerD0Entry;
    power.EvtDeviceD0ExitPreInterruptsDisabled = NdPowerD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &power);

    status = WdfDeviceCreate(&DeviceInit, &attr, &dev);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    pDev = NdRdmaDeviceGetContext(dev);
    pDev->Ref = 1;
    pDev->WdfDev = dev;
    KeInitializeEvent(&pDev->Event, NotificationEvent, FALSE);
    InitializeListHead(&pDev->Entry);

    return STATUS_SUCCESS;
}


void ND_DRIVER::RemoveHandler(ND_RDMA_DEVICE* pDev)
{
    KeAcquireGuardedMutex(&m_Lock);
    RemoveEntryList(&pDev->Entry);
    BOOLEAN destroy = IsListEmpty(&m_DevList);
    KeReleaseGuardedMutex(&m_Lock);

    m_ProvTable.LockExclusive();
    m_ProvTable.Iterate(RemoveDeviceFunctor<ND_PROVIDER>(pDev));
    m_ProvTable.Unlock();

    if (destroy == TRUE && ControlDevice != NULL) {
        WdfObjectDelete(ControlDevice);
        ControlDevice = NULL;
    }
}


static
NTSTATUS
NdPowerD0Exit(
    WDFDEVICE Device,
    WDF_POWER_DEVICE_STATE TargetState
    )
{
    if (TargetState == WdfPowerDeviceD3Final) {
        NdRdmaDeviceCleanup(Device);
    }

    return STATUS_SUCCESS;
}


static
VOID
NdRdmaDeviceCleanup(
    WDFOBJECT Device
    )
{
    ND_RDMA_DEVICE* dev = NdRdmaDeviceGetContext(Device);

    if (dev->hDevice == NULL) {
        return;
    }

    ND_DRIVER* driver = NdDriverGetContext(WdfGetDriver());
    driver->RemoveHandler(dev);

    if (InterlockedDecrement(&dev->Ref) > 0) {
        KeWaitForSingleObject(&dev->Event, Executive, KernelMode, FALSE, NULL);
    }

    dev->hDevice = NULL;
}


EVT_WDF_DRIVER_UNLOAD NdDriverUnload;

VOID
NdDriverUnload(
    IN WDFDRIVER Driver
    )
{
    ND_DRIVER* driver = NdDriverGetContext(Driver);
    driver->ND_DRIVER::~ND_DRIVER();
    //ProvTable.Dispose();
}


EXTERN_C
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT DriverObject,
    PUNICODE_STRING RegistryPath
    )
{
    WDF_OBJECT_ATTRIBUTES   attr;
    WDF_DRIVER_CONFIG       config;
    NTSTATUS                status;
    WDFDRIVER               driver;

    ControlDevice = NULL;

    ND_ENDPOINT::SetStartingPsn(reinterpret_cast<ULONG>(DriverObject));
    IbatInterface.InterfaceHeader.InterfaceDereference = WdfDeviceInterfaceDereferenceNoOp;

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, ND_DRIVER);

    WDF_DRIVER_CONFIG_INIT(&config, NdRdmaDeviceAdd);
    config.EvtDriverUnload = NdDriverUnload;
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attr,
        &config,
        &driver
        );
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ND_DRIVER* pDriver = NdDriverGetContext(driver);
    new(pDriver) ND_DRIVER();

    return STATUS_SUCCESS;
}


ib_ca_attr_t* ND_RDMA_DEVICE::QueryCaAttributes()
{
    UINT32 size = 0;
    ib_api_status_t ibStatus = Interface.Verbs.query_ca(hDevice, NULL, &size, NULL);
    if (ibStatus != IB_INSUFFICIENT_MEMORY) {
        return NULL;
    }

    ib_ca_attr_t* attr = reinterpret_cast<ib_ca_attr_t*>(
        ExAllocatePoolWithTag(PagedPool, size, ND_RDMA_DEVICE_POOL_TAG)
        );
    if (attr == NULL) {
        return NULL;
    }

    ibStatus = Interface.Verbs.query_ca(hDevice, attr, &size, NULL);
    if (ibStatus != IB_SUCCESS) {
        ExFreePoolWithTag(attr, ND_RDMA_DEVICE_POOL_TAG);
        return NULL;
    }

    return attr;
}

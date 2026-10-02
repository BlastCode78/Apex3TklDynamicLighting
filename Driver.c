#include "Apex3TklDynamicLighting.h"
#include "LampArrayDescriptor.h"

#define LAMP_ARRAY_ATTRIBUTES_REPORT_ID 1
#define LAMP_ATTRIBUTES_REQUEST_REPORT_ID 2
#define LAMP_ATTRIBUTES_RESPONSE_REPORT_ID 3
#define LAMP_MULTI_UPDATE_REPORT_ID 4
#define LAMP_RANGE_UPDATE_REPORT_ID 5
#define LAMP_ARRAY_CONTROL_REPORT_ID 6
#define LAMP_UPDATE_COMPLETE 0x01

#define LAMP_ARRAY_KIND_KEYBOARD 1
#define LAMP_PURPOSE_ILLUMINATION 0x10
#define STEELSERIES_ZONE_COLOR_COMMAND 0x21
#define STEELSERIES_ALL_ZONES_MASK 0xFF
#define STEELSERIES_ZONE_COUNT 8
#define STEELSERIES_REPORT_SIZE 65

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, EvtDeviceAdd)
#pragma alloc_text(PAGE, EvtDeviceCleanup)
#endif

static
VOID
CompleteVhfOperation(
    _In_ VHFOPERATIONHANDLE Operation,
    _In_ NTSTATUS Status
)
{
    NTSTATUS completeStatus = VhfAsyncOperationComplete(Operation, Status);
    if (!NT_SUCCESS(completeStatus))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "Apex3TklDynamicLighting: VhfAsyncOperationComplete=0x%08X\n",
            completeStatus);
    }
}

static
NTSTATUS
QueueZoneColorWrite(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ VHFOPERATIONHANDLE Operation
)
{
    WDF_WORKITEM_CONFIG config;
    WDF_WORKITEM_CONFIG_INIT(&config, EvtZoneWriteWorkItem);
    // The parent device uses the default dispatch-level execution setting;
    // keep this PASSIVE_LEVEL worker independent of automatic serialization.
    config.AutomaticSerialization = FALSE;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ZONE_WRITE_CONTEXT);
    attributes.ParentObject = DeviceContext->Device;

    WDFWORKITEM workItem;
    NTSTATUS status = WdfWorkItemCreate(&config, &attributes, &workItem);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    PZONE_WRITE_CONTEXT workContext = ZoneWriteGetContext(workItem);
    RtlZeroMemory(workContext, sizeof(*workContext));
    workContext->Device = DeviceContext->Device;
    workContext->Operation = Operation;
    workContext->Report[0] = 0; // HID report ID
    workContext->Report[1] = STEELSERIES_ZONE_COLOR_COMMAND;
    workContext->Report[2] = STEELSERIES_ALL_ZONES_MASK;

    WdfSpinLockAcquire(DeviceContext->StateLock);
    for (ULONG zone = 0; zone < STEELSERIES_ZONE_COUNT; ++zone)
    {
        ULONG offset = 3 + (zone * 3);
        BOOLEAN enabled = DeviceContext->Colors[zone][3] != 0;
        workContext->Report[offset + 0] = enabled ? DeviceContext->Colors[zone][0] : 0;
        workContext->Report[offset + 1] = enabled ? DeviceContext->Colors[zone][1] : 0;
        workContext->Report[offset + 2] = enabled ? DeviceContext->Colors[zone][2] : 0;
    }
    WdfSpinLockRelease(DeviceContext->StateLock);

    WdfWorkItemEnqueue(workItem);
    return STATUS_SUCCESS;
}

VOID
EvtZoneWriteWorkItem(
    _In_ WDFWORKITEM WorkItem
)
{
    PZONE_WRITE_CONTEXT context = ZoneWriteGetContext(WorkItem);
    WDF_MEMORY_DESCRIPTOR reportDescriptor;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
        &reportDescriptor, context->Report, sizeof(context->Report));

    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(2));

    ULONG_PTR colorBytesTransferred = 0;
    NTSTATUS colorStatus = WdfIoTargetSendWriteSynchronously(
        WdfDeviceGetIoTarget(context->Device),
        NULL,
        &reportDescriptor,
        NULL,
        &options,
        &colorBytesTransferred);

    UCHAR brightnessReport[STEELSERIES_REPORT_SIZE] = { 0 };
    brightnessReport[0] = 0; // HID report ID
    brightnessReport[1] = 0x23; // SteelSeries zone-controller brightness command
    brightnessReport[2] = 0x10; // Full output range; per-lamp RGB carries color/intensity.

    NTSTATUS brightnessStatus = colorStatus;
    ULONG_PTR brightnessBytesTransferred = 0;
    if (NT_SUCCESS(colorStatus))
    {
        WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(
            &reportDescriptor, brightnessReport, sizeof(brightnessReport));
        brightnessStatus = WdfIoTargetSendWriteSynchronously(
            WdfDeviceGetIoTarget(context->Device),
            NULL,
            &reportDescriptor,
            NULL,
            &options,
            &brightnessBytesTransferred);
    }

    NTSTATUS status = NT_SUCCESS(colorStatus) ? brightnessStatus : colorStatus;

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "Apex3TklDynamicLighting: physical zone write=0x%08X/%Iu brightness=0x%08X/%Iu\n",
        colorStatus, colorBytesTransferred, brightnessStatus, brightnessBytesTransferred);
    CompleteVhfOperation(context->Operation, status);
    WdfObjectDelete(WorkItem);
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, EvtDeviceAdd);

    NTSTATUS status = WdfDriverCreate(
        DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES,
        &config, WDF_NO_HANDLE);

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "Apex3TklDynamicLighting: DriverEntry WdfDriverCreate=0x%08X\n", status);
    return status;
}

NTSTATUS
EvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    UNREFERENCED_PARAMETER(Driver);
    PAGED_CODE();

    WdfFdoInitSetFilter(DeviceInit);

    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);
    deviceAttributes.EvtCleanupCallback = EvtDeviceCleanup;

    WDFDEVICE device;
    NTSTATUS status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "Apex3TklDynamicLighting: WdfDeviceCreate=0x%08X\n", status);
        return status;
    }

    PDEVICE_CONTEXT deviceContext = DeviceGetContext(device);
    RtlZeroMemory(deviceContext, sizeof(*deviceContext));
    deviceContext->Device = device;
    deviceContext->AutonomousMode = TRUE;

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject = device;
    status = WdfSpinLockCreate(&lockAttributes, &deviceContext->StateLock);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "Apex3TklDynamicLighting: WdfSpinLockCreate=0x%08X\n", status);
        return status;
    }

    VHF_CONFIG vhfConfig;
    VHF_CONFIG_INIT(
        &vhfConfig,
        WdfDeviceWdmGetDeviceObject(device),
        g_LampArrayReportDescriptorLength,
        g_LampArrayReportDescriptor);
    vhfConfig.VhfClientContext = deviceContext;
    vhfConfig.EvtVhfAsyncOperationGetFeature = EvtVhfGetFeature;
    vhfConfig.EvtVhfAsyncOperationSetFeature = EvtVhfSetFeature;

    status = VhfCreate(&vhfConfig, &deviceContext->VhfHandle);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "Apex3TklDynamicLighting: VhfCreate=0x%08X\n", status);
        return status;
    }

    status = VhfStart(deviceContext->VhfHandle);
    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
            "Apex3TklDynamicLighting: VhfStart=0x%08X\n", status);
        VhfDelete(deviceContext->VhfHandle, TRUE);
        deviceContext->VhfHandle = NULL;
        return status;
    }

    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "Apex3TklDynamicLighting: filter started; LampArray VHF started\n");
    return STATUS_SUCCESS;
}

NTSTATUS
EvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST RawResources,
    _In_ WDFCMRESLIST TranslatedResources
)
{
    UNREFERENCED_PARAMETER(Device);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
        "Apex3TklDynamicLighting: EvtDevicePrepareHardware raw=%lu translated=%lu\n",
        WdfCmResourceListGetCount(RawResources),
        WdfCmResourceListGetCount(TranslatedResources));
    return STATUS_SUCCESS;
}

VOID
EvtDeviceCleanup(
    _In_ WDFOBJECT Object
)
{
    PAGED_CODE();
    PDEVICE_CONTEXT deviceContext = DeviceGetContext((WDFDEVICE)Object);
    if (deviceContext->VhfHandle != NULL)
    {
        VhfDelete(deviceContext->VhfHandle, TRUE);
        deviceContext->VhfHandle = NULL;
    }
}

VOID
EvtVhfGetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET HidTransferPacket
)
{
    UNREFERENCED_PARAMETER(VhfOperationContext);
    PDEVICE_CONTEXT context = (PDEVICE_CONTEXT)VhfClientContext;
    PUCHAR buffer = HidTransferPacket->reportBuffer;
    ULONG length = HidTransferPacket->reportBufferLen;
    NTSTATUS status = STATUS_SUCCESS;

    if (buffer == NULL)
    {
        status = STATUS_INVALID_PARAMETER;
    }
    else if (HidTransferPacket->reportId == LAMP_ARRAY_ATTRIBUTES_REPORT_ID)
    {
        // HID report buffers include the report-ID byte, as required by HID.
        if (length < 23) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            RtlZeroMemory(buffer, 23);
            buffer[0] = LAMP_ARRAY_ATTRIBUTES_REPORT_ID;
            buffer[1] = (UCHAR)(APEX_LAMP_COUNT & 0xFF);
            buffer[2] = (UCHAR)(APEX_LAMP_COUNT >> 8);
            // Geometry and update timing remain zero until measured. Identify
            // the physical device type so Windows can classify the LampArray.
            buffer[15] = LAMP_ARRAY_KIND_KEYBOARD;
        }
    }
    else if (HidTransferPacket->reportId == LAMP_ATTRIBUTES_RESPONSE_REPORT_ID)
    {
        // LampAttributesResponse: ID, LampId, five ULONGs, five bytes,
        // and a 16-bit InputBinding (total 30 bytes including report ID).
        if (length < 30) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            RtlZeroMemory(buffer, 30);
            buffer[0] = LAMP_ATTRIBUTES_RESPONSE_REPORT_ID;
            WdfSpinLockAcquire(context->StateLock);
            USHORT lampId = context->RequestedLampId;
            context->RequestedLampId = (USHORT)((lampId + 1) % APEX_LAMP_COUNT);
            WdfSpinLockRelease(context->StateLock);
            buffer[1] = (UCHAR)(lampId & 0xFF);
            buffer[2] = (UCHAR)(lampId >> 8);
            // Geometry and update latency remain zero until measured. The
            // purpose occupies bytes 15..18; latency follows at 19..22.
            buffer[15] = 0x01; // LampPurposeControl (keyboard lighting)
            buffer[23] = 0xFF; // Red levels
            buffer[24] = 0xFF; // Green levels
            buffer[25] = 0xFF; // Blue levels
            buffer[26] = 1;    // Intensity level
            buffer[27] = 1;    // Programmable
            // InputBinding (bytes 28..29) stays zero: these zones do not map
            // one-to-one to individual key usages.
        }
    }
    else
    {
        status = STATUS_NOT_SUPPORTED;
    }

    CompleteVhfOperation(VhfOperationHandle, status);
}

VOID
EvtVhfSetFeature(
    _In_ PVOID VhfClientContext,
    _In_ VHFOPERATIONHANDLE VhfOperationHandle,
    _In_opt_ PVOID VhfOperationContext,
    _In_ PHID_XFER_PACKET HidTransferPacket
)
{
    UNREFERENCED_PARAMETER(VhfOperationContext);
    PDEVICE_CONTEXT context = (PDEVICE_CONTEXT)VhfClientContext;
    PUCHAR report = HidTransferPacket->reportBuffer;
    ULONG reportLength = HidTransferPacket->reportBufferLen;
    PUCHAR data = NULL;
    ULONG length = 0;
    NTSTATUS status = STATUS_SUCCESS;

    if (report == NULL || reportLength < 1)
    {
        status = STATUS_INVALID_PARAMETER;
        CompleteVhfOperation(VhfOperationHandle, status);
        return;
    }

    if (report[0] != HidTransferPacket->reportId)
    {
        CompleteVhfOperation(VhfOperationHandle, STATUS_INVALID_PARAMETER);
        return;
    }

    data = report + 1;
    length = reportLength - 1;

    switch (HidTransferPacket->reportId)
    {
    case LAMP_ATTRIBUTES_REQUEST_REPORT_ID:
        if (length < 2) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            USHORT lampId = (USHORT)data[0] | ((USHORT)data[1] << 8);
            WdfSpinLockAcquire(context->StateLock);
            context->RequestedLampId = lampId < APEX_LAMP_COUNT ? lampId : 0;
            WdfSpinLockRelease(context->StateLock);
        }
        break;

    case LAMP_MULTI_UPDATE_REPORT_ID:
        if (length < 50) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            UCHAR lampCount = data[0];
            UCHAR flags = data[1];
            if (lampCount > APEX_LAMP_COUNT) lampCount = APEX_LAMP_COUNT;
            BOOLEAN updateComplete = (flags & LAMP_UPDATE_COMPLETE) != 0;
            WdfSpinLockAcquire(context->StateLock);
            for (UCHAR i = 0; i < lampCount; ++i)
            {
                USHORT lampId = (USHORT)data[2 + (i * 2)] |
                    ((USHORT)data[3 + (i * 2)] << 8);
                if (lampId < APEX_LAMP_COUNT)
                {
                    RtlCopyMemory(context->Colors[lampId],
                        &data[18 + (i * 4)], 4);
                }
            }
            if (updateComplete) context->AutonomousMode = FALSE;
            WdfSpinLockRelease(context->StateLock);

            if (updateComplete)
            {
                status = QueueZoneColorWrite(context, VhfOperationHandle);
                if (!NT_SUCCESS(status))
                {
                    CompleteVhfOperation(VhfOperationHandle, status);
                    return;
                }
                return; // The passive-level work item completes this VHF operation.
            }
        }
        break;

    case LAMP_RANGE_UPDATE_REPORT_ID:
        if (length < 9) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            UCHAR flags = data[0];
            USHORT start = (USHORT)data[1] | ((USHORT)data[2] << 8);
            USHORT end = (USHORT)data[3] | ((USHORT)data[4] << 8);
            if (start < APEX_LAMP_COUNT && end < APEX_LAMP_COUNT && start <= end)
            {
                BOOLEAN updateComplete = (flags & LAMP_UPDATE_COMPLETE) != 0;
                WdfSpinLockAcquire(context->StateLock);
                for (USHORT i = start; i <= end; ++i)
                    RtlCopyMemory(context->Colors[i], &data[5], 4);
                if (updateComplete) context->AutonomousMode = FALSE;
                WdfSpinLockRelease(context->StateLock);

                if (updateComplete)
                {
                    status = QueueZoneColorWrite(context, VhfOperationHandle);
                    if (!NT_SUCCESS(status))
                    {
                        CompleteVhfOperation(VhfOperationHandle, status);
                        return;
                    }
                    return; // The passive-level work item completes this VHF operation.
                }
            }
        }
        break;

    case LAMP_ARRAY_CONTROL_REPORT_ID:
        if (length < 1) status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            WdfSpinLockAcquire(context->StateLock);
            context->AutonomousMode = (data[0] != 0);
            WdfSpinLockRelease(context->StateLock);
        }
        break;

    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }

    CompleteVhfOperation(VhfOperationHandle, status);
}

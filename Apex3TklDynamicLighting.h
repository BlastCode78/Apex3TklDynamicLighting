#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>
#include <hidclass.h>
#include <vhf.h>

#define APEX_LAMP_COUNT 8

typedef struct _DEVICE_CONTEXT
{
    WDFDEVICE Device;
    VHFHANDLE VhfHandle;
    WDFSPINLOCK StateLock;

    USHORT RequestedLampId;

    BOOLEAN AutonomousMode;

    UCHAR Colors[APEX_LAMP_COUNT][4];

} DEVICE_CONTEXT, * PDEVICE_CONTEXT;

typedef struct _ZONE_WRITE_CONTEXT
{
    WDFDEVICE Device;
    VHFOPERATIONHANDLE Operation;
    UCHAR Report[65];
} ZONE_WRITE_CONTEXT, *PZONE_WRITE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    DEVICE_CONTEXT,
    DeviceGetContext
);

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(
    ZONE_WRITE_CONTEXT,
    ZoneWriteGetContext
);

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD EvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP EvtDeviceCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE EvtDevicePrepareHardware;

EVT_VHF_ASYNC_OPERATION EvtVhfGetFeature;
EVT_VHF_ASYNC_OPERATION EvtVhfSetFeature;
EVT_VHF_ASYNC_OPERATION EvtVhfWriteReport;
EVT_WDF_WORKITEM EvtZoneWriteWorkItem;

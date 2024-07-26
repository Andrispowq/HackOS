#include "device.h"
#include "lib/memory.h"
#include "lib/stdio.h"

Device* devices[MAX_DEVICE_COUNT];

void InitialiseDevices()
{
    memset(devices, 0, sizeof(Device*) * MAX_DEVICE_COUNT);
}

void RegisterDevice(Device* device)
{
    for(uint32_t i = 0; i < MAX_DEVICE_COUNT; i++)
    {
        if(devices[i] == nullptr)
        {
            devices[i] = device;
            return;
        }
    }

    kprintf("KERNEL ERROR: Too many devices added!\n");
    asm("hlt");
}

void UnregisterDevice(Device* device)
{
    for(uint32_t i = 0; i < MAX_DEVICE_COUNT; i++)
    {
        if(devices[i] == device)
        {
            devices[i] = nullptr;
            return;
        }
    }

    kprintf("KERNEL ERROR: Can't find the device!\n");
    asm("hlt");
}
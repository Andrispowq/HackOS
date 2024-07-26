#ifndef DEV_H
#define DEV_H

// USB Limits
#define USB_STRING_SIZE                 127

// USB Speeds
#define USB_FULL_SPEED                  0x00
#define USB_LOW_SPEED                   0x01
#define USB_HIGH_SPEED                  0x02

#include "req.h"
#include "desc.h"

// USB Endpoint
typedef struct USBEndpoint
{
    USBEndpDesc desc;
    uint32_t toggle;
} UsbEndpoint;

// USB Transfer
typedef struct USBTransfer
{
    USBEndpoint *endp;
    USBDevReq *req;
    void* data;
    uint32_t len;
    bool complete;
    bool success;
} UsbTransfer;

class USBDevice
{
public:
    void hcControl(USBTransfer* t);
    void hcIntr(USBTransfer* t);

    void drvPoll();

public:
    USBDevice* parent;
    USBDevice* next;
    void* hc;
    void* drv;

    uint32_t port;
    uint32_t speed;
    uint32_t addr;
    uint32_t maxPacketSize;

    USBEndpoint endp;
    USBIntfDesc intfDesc;
};

#endif
#ifndef DESC_H
#define DESC_H

#include "lib/stdint.h"

// USB Base Descriptor Types
#define USB_DESC_DEVICE                 0x01
#define USB_DESC_CONF                   0x02
#define USB_DESC_STRING                 0x03
#define USB_DESC_INTF                   0x04
#define USB_DESC_ENDP                   0x05

// USB HID Descriptor Types
#define USB_DESC_HID                    0x21
#define USB_DESC_REPORT                 0x22
#define USB_DESC_PHYSICAL               0x23

// USB HUB Descriptor Types
#define USB_DESC_HUB                    0x29

struct USBDeviceDesc
{
    uint8_t len;
    uint8_t type;
    uint16_t usbVer;
    uint8_t devClass;
    uint8_t devSubClass;
    uint8_t devProtocol;
    uint8_t maxPacketSize;
    uint16_t vendorId;
    uint16_t productId;
    uint16_t deviceVer;
    uint8_t vendorStr;
    uint8_t productStr;
    uint8_t serialStr;
    uint8_t confCount;
} __attribute__((packed));

struct USBConfDesc
{
    uint8_t len;
    uint8_t type;
    uint16_t totalLen;
    uint8_t intfCount;
    uint8_t confValue;
    uint8_t confStr;
    uint8_t attributes;
    uint8_t maxPower;
} __attribute__((packed));

struct USBStringDesc
{
    uint8_t len;
    uint8_t type;
    uint16_t str[];
} __attribute__((packed));

struct USBIntfDesc
{
    uint8_t len;
    uint8_t type;
    uint8_t intfIndex;
    uint8_t altSetting;
    uint8_t endpCount;
    uint8_t intfClass;
    uint8_t intfSubClass;
    uint8_t intfProtocol;
    uint8_t intfStr;
} __attribute__((packed));

struct USBEndpDesc
{
    uint8_t len;
    uint8_t type;
    uint8_t addr;
    uint8_t attributes;
    uint16_t maxPacketSize;
    uint8_t interval;
} __attribute__((packed));

struct USBHidDesc
{
    uint8_t len;
    uint8_t type;
    uint16_t hidVer;
    uint8_t countryCode;
    uint8_t descCount;
    uint8_t descType;
    uint16_t descLen;
} __attribute__((packed));

struct USBHubDesc
{
    uint8_t len;
    uint8_t type;
    uint8_t portCount;
    uint16_t chars;
    uint8_t portPowerTime;
    uint8_t current;
} __attribute__((packed));

// Hub Characteristics
#define HUB_POWER_MASK                  0x03        // Logical Power Switching Mode
#define HUB_POWER_GLOBAL                0x00
#define HUB_POWER_INDIVIDUAL            0x01
#define HUB_COMPOUND                    0x04        // Part of a Compound Device
#define HUB_CURRENT_MASK                0x18        // Over-current Protection Mode
#define HUB_TT_TTI_MASK                 0x60        // TT Think Time
#define HUB_PORT_INDICATORS             0x80        // Port Indicators

#endif
#ifndef UHCI_CONTROLLER_H
#define UHCI_CONTROLLER_H

#include "drivers/usb/controller.h"
#include "drivers/usb/dev.h"

#include "lib/data_structures/link.h"

class UHCIController : public Controller
{
public:
    UHCIController() {}
    ~UHCIController() {}

    virtual void Poll() override {}

public:
    uint32_t IOAddress;
    uint32_t* frameList;
};

// Queue Head
struct UHCI_QH
{
    volatile uint32_t head;
    volatile uint32_t element;

    // internal fields
    USBTransfer* transfer;
    Link qhLink;
    uint32_t tdHead;
    uint32_t active;
    uint8_t pad[24];
};

// Transfer Descriptor
struct UHCI_TD
{
    volatile uint32_t link;
    volatile uint32_t cs;
    volatile uint32_t token;
    volatile uint32_t buffer;

    // internal fields
    uint32_t tdNext;
    uint8_t active;
    uint8_t pad[11];
} __attribute__((packed));

#endif
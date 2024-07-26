#include "madt.h"

#include "lib/stdio.h"
#include "lib/memory.h"

namespace ACPI
{
    static uint8_t lapic_ids[256] = { 0 };
    static uint8_t cores = 0;
    static uint64_t lapic_ptr = 0;
    static uint64_t ioapic_ptr = 0;

    void MADT::DetectCores()
    {
        lapic_ptr = _madt.lapicAddress;

        MADTEntryRecordHeader* header = _madt.entries;
        uint64_t end = (uint64_t)&_madt + _madt.header.Length;

        for(MADTEntryRecordHeader* h = header; (uint64_t)h < end;)
        {
            switch (h->EntryType)
            {
            case 0:
            {
                MADTEntryRecordHeaderLAPIC* he = (MADTEntryRecordHeaderLAPIC*)h;
                
                if(he->Flags & 0x1)
                    lapic_ids[cores++] = he->APICID;

                break;
            }
            
            case 1:
            {
                MADTEntryRecordHeaderIOAPIC* he = (MADTEntryRecordHeaderIOAPIC*)h;
                ioapic_ptr = he->APICAddress;
                break;
            }
            
            case 5:
            {
                MADTEntryRecordHeaderLAPICAddressOverride* he = (MADTEntryRecordHeaderLAPICAddressOverride*)h;
                lapic_ptr = he->LAPICAddressPhysical;
                break;
            }

            default:
                break;
            }

            h = (MADTEntryRecordHeader*)((uint64_t)h + h->RecordLength);
        }

        kprintf("Found %d cores\n", cores);
        kprintf("Local APIC address: 0x%x\n", lapic_ptr);
        kprintf("I/O APIC address: 0x%x\n\n", ioapic_ptr);
    }
};

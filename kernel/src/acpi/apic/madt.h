#ifndef MADT_H
#define MADT_H

#include "acpi/system_table.h"

#define APIC_LOW 2
#define LEVEL_TRIGGERED 8

namespace ACPI
{
    struct MADTEntryRecordHeader
    {
        uint8_t EntryType;
        uint8_t RecordLength;        
    } __attribute__((packed));

    struct MADTEntryRecordHeaderLAPIC
    {
        MADTEntryRecordHeader header;

        uint8_t ProcessorID;
        uint8_t APICID;
        uint32_t Flags;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderIOAPIC
    {
        MADTEntryRecordHeader header;

        uint8_t APICID;
        uint8_t Reserved;
        uint32_t APICAddress;
        uint32_t GlobalSystemInterruptBase;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderIOAPICInterruptSourceOverride
    {
        MADTEntryRecordHeader header;

        uint8_t BusSource;
        uint8_t IRQSource;
        uint32_t GlobalSystemInterrupt;
        uint16_t Flags;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderIOAPICNMISource
    {
        MADTEntryRecordHeader header;

        uint8_t NMISource;
        uint8_t Reserved;
        uint16_t Flags;
        uint32_t GlobalSystemInterrupt;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderLAPICNMISource
    {
        MADTEntryRecordHeader header;

        uint8_t ProcessorID;
        uint16_t Flags;
        uint8_t LINTNumber;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderLAPICAddressOverride
    {
        MADTEntryRecordHeader header;

        uint16_t Reserved;
        uint64_t LAPICAddressPhysical;
    } __attribute__((packed));

    struct MADTEntryRecordHeaderLAPICX2
    {
        MADTEntryRecordHeader header;

        uint16_t Reserved;
        uint32_t LAPICX2ID;
        uint32_t Flags;
        uint32_t ACPIID;
    } __attribute__((packed));

    struct MADTHeader
    {
        SDTHeader header;

        uint32_t lapicAddress;
        uint32_t Flags;

        MADTEntryRecordHeader entries[0];
    } __attribute__((packed));

    class MADT
    {
    public:
        MADTHeader GetMADT() { return _madt; }
        bool is_valid();

        void DetectCores();

    private:
        MADTHeader _madt;
    };
};

#endif
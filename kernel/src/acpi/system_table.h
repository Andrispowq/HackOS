#ifndef SYSTEM_TABLE_H
#define SYSTEM_TABLE_H

#include "lib/stdint.h"

typedef struct sdt_header
{
    char Signature[4];
    uint32_t Length;
    uint8_t Revision;
    uint8_t Checksum;
    uint8_t OEMID[6];
    uint8_t OEMTableID[8];
    uint32_t OEMRevision;
    uint32_t CreatorID;
    uint32_t CreatorRevision;
} SDTHeader;

void* FindTable(SDTHeader* sdtHeader, char* signature);

#endif
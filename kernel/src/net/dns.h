#pragma once
#include "hackos/network.h"

namespace Net
{
void InvalidateResolve();
unsigned BuildDns(uint8_t* data, const char* name, uint16_t id);
// False means malformed/unrelated. A true result carries a terminal resolver state.
bool ParseDns(const uint8_t* data, unsigned size, uint16_t id, const char* name, HackosResolve& result);
uint64_t ResolveControl(uint64_t pid, uint64_t operation, HackosResolve& record);
void CancelResolve(uint64_t pid);
void PollResolve(bool ready, bool link, const uint8_t* mac, const uint8_t* ip);
void ReceiveResolve(const uint8_t* frame, unsigned length, const uint8_t* mac, const uint8_t* ip);
}

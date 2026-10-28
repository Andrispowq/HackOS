#pragma once

#include "hackos/abi.h"

class Process;

uint64_t UserWindowCreate(const HackosWindowSpec* source);
uint64_t UserWindowBlit(uint64_t handle, const HackosWindowRect* source, const uint32_t* pixels);
uint64_t UserWindowPoll(uint64_t handle);
uint64_t UserWindowClose(uint64_t handle);
void ReleaseUserWindows(Process* process);

extern uint64_t user_windows_live;

uint64_t UserWindowState(uint64_t handle, HackosWindowState* destination);
uint64_t UserWindowEvent(uint64_t handle, HackosWindowEvent* destination);
uint64_t UserWindowMode(uint64_t handle, uint64_t operation);

uint32_t UserWindowReadiness(uint64_t handle);
uint64_t UserWindowFrame(uint64_t handle, uint64_t operation, HackosWindowFrame* destination);
uint64_t UserWindowUpload(uint64_t handle, const HackosWindowUpload* source, const uint32_t* pixels);

uint64_t UserWindowInput(uint64_t handle, uint64_t operation, HackosWindowInputState* destination);
uint64_t UserWindowEvent2(uint64_t handle, HackosWindowEvent2* destination);
uint64_t UserWindowCloseReply(uint64_t handle, uint64_t accept);
uint64_t UserClipboard(uint64_t operation, void* buffer, uint64_t length);
void ClearSessionClipboard(uint64_t session);

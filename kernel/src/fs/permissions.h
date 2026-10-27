#pragma once
#include "hackos/abi.h"

struct FilePermissions
{
    uint32_t uid = 0, gid = 0, mode = 0755;
};

bool FileAccess(const FilePermissions& permissions, const HackosCredentials& credentials, unsigned requested,
                bool directory);
bool PathAccess(const char* path, const HackosCredentials& credentials, unsigned requested, bool parent = false);

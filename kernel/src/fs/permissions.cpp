#include "permissions.h"
#include "filesystem.h"
#include "lib/string.h"

bool FileAccess(const FilePermissions& permissions, const HackosCredentials& credentials, unsigned requested,
                bool directory)
{
    if (credentials.groupCount > HACKOS_MAX_GROUPS || requested > 7)
    {
        return false;
    }

    if (!credentials.uid)
    {
        return !(requested & 1) || directory || (permissions.mode & 0111);
    }

    unsigned shift = 0;
    if (credentials.uid == permissions.uid)
    {
        shift = 6;
    }
    else
    {
        bool group = credentials.gid == permissions.gid;
        for (unsigned i = 0; i < credentials.groupCount; ++i)
        {
            group = group || credentials.groups[i] == permissions.gid;
        }

        if (group)
        {
            shift = 3;
        }
    }

    return ((permissions.mode >> shift) & requested) == requested;
}

bool PathAccess(const char* path, const HackosCredentials& credentials, unsigned requested, bool parent)
{
    if (!path || path[0] != '~' || !filesystems[0])
    {
        return false;
    }

    char prefix[256] = "~";
    unsigned length = strlen((char*)path);
    if (length >= sizeof(prefix))
    {
        return false;
    }

    if (parent)
    {
        while (length > 1 && path[length - 1] != '/')
        {
            --length;
        }

        if (length > 1)
        {
            --length;
        }
    }

    for (unsigned i = 1; i <= length; ++i)
    {
        if (i == length || path[i] == '/')
        {
            prefix[i] = 0;
            ActiveFile* file = nullptr;
            if (filesystems[0]->Lookup(prefix, file))
            {
                return false;
            }

            bool directory = file->GetAttributes() & DIRECTORY;
            bool allowed = (i == length || directory) &&
                           FileAccess(file->GetPermissions(), credentials, i == length ? requested : 1, directory);
            filesystems[0]->CloseFile(file);
            if (!allowed)
            {
                return false;
            }
        }

        prefix[i] = path[i];
    }

    return true;
}

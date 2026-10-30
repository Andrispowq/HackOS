#include "resolve.h"

int main(int argc, char** argv)
{
    if (!Arguments(argc, 2, "nslookup <hostname>")) return 2;
    if (Equal(argv[1], "--help") || Equal(argv[1], "-h"))
    {
        Print("Usage: nslookup <hostname>\nIPv4 DNS through 10.0.2.3; 5-second deadline.\n");
        return 0;
    }
    HackosResolve result = {};
    if (!ResolveHost(argv[1], result)) return 1;
    ReportOutput out;
    out.Format("Server: 10.0.2.3\nName: %s\nAddress: %u.%u.%u.%u\nTTL: %u seconds\n", result.name,
               result.address[0], result.address[1], result.address[2], result.address[3], result.ttl);
    return out.ok ? 0 : 1;
}

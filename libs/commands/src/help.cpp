#include "command.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "help"))
    {
        return 2;
    }

    Print("Commands: help, clear, echo [-n] [text], time [--date|--time|--ticks], ps [aux], kill <pid>\n");
    Print("Terminal: terminal get [key], terminal set <key> <value>, terminal reset; terminal --help.\n");
    Print("Files: ls [-la] [path], wd, cat [path], read [path]. Shell: cd <path>, history.\n");
    Print("Files: xxd <file>, mkdir <path>, rm <file>, mv <source> <destination>.\n");
    Print("Filters: grep [-Finvclq] <pattern> [file ...], wc [-lwc] [file ...].\n");
    Print("Filters: head/tail [-n N|-c N] [file ...], tee [-a] [file ...], more [-n lines] [file ...].\n");
    Print("Filters read stdin by default. more: Space pages, Enter advances, q quits. Use --help.\n");
    Print("Permissions: chmod <octal-mode> <path>, chown <user> <group> <path> (root).\n");
    Print("Editor: edit <file>; Ctrl+S saves, Ctrl+X exits without saving.\n");
    Print("Programs: <name> [args], ./<path>, /usr/bin/<name>; append & for background.\n");
    Print("Identity: id, whoami, users, groups. Accounts persist across boots.\n");
    Print("Root: sysinfo [full|cpu|memory|disk|pci|usb|ps2|display|acpi]; sysinfo --help.\n");
    Print("Root: useradd <name> [group], groupadd <name>, usermod <user> <group>.\n");
    Print("Root: runas <user> <command> [args], shutdown, restart. Kill: own tasks or root.\n");
    Print("Jobs: jobs, fg [job], bg [job], wait [job], kill %job. Ctrl+Z stops; Ctrl+C interrupts.\n");
    Print("Streams: a | b; < input, > output, >> append, 2> errors, 2>&1. Up to four stages.\n");
    Print("cat copies stdin when no path is given; Ctrl+D ends a terminal stream read.\n");
    Print("Tab completes commands and paths; Up/Down recalls history.\n");
    Print("Session: logout. Root: passwd [user] sets a login password.\n");
    return 0;
}

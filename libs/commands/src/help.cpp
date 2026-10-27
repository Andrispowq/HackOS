#include "command.h"

int main(int argc, char**)
{
    if (!Arguments(argc, 1, "help")) return 2;

    Print("Commands: help, clear, print [text|tick|time|date], ps [aux], kill <pid>\n");
    Print("Files: ls [path], wd, cat <path>, read <path>. Shell: cd <path>, history.\n");
    Print("Programs: <name> [args], ./<path>, /usr/bin/<name>; append & for background.\n");
    Print("Power: shutdown, restart. File readers own and close their handles.\n");
    return 0;
}

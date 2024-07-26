#ifndef SHELL_H
#define SHELL_H

#include "lib/string.h"
#include "lib/memory.h"
#include "lib/stdio.h"

#define INVALID_MODE -1
#define COMMAND_MODE 0
#define CALCULATOR_MODE 1

static int state = COMMAND_MODE;

void InitialiseShell();
void shell_command(char* input);

#endif

#ifndef FUNCTION_H
#define FUNCTION_H

#include "kernel_interface/syscall.h"

#define UNUSED(x) (void)(x)

#define ASSERT(x) if(!(x)) kprintf("ERROR: assertion check failed, in file: %s, at line: %d\n", __FILE__, __LINE__);

#endif
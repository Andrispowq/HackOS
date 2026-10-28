#ifndef PAGING_H
#define PAGING_H

#include "libc/stdint.h"

/* Identity-map 0..4 GiB with 2 MiB pages in reserved low memory. */
int InitPaging(void);

#endif

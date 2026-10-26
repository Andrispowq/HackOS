#pragma once
// Compile-time symbol isolation only. Production algorithms are not replaced.
// Keep the kernel's libc-like symbols away from the host/sanitizer runtime.
#define memcpy hackos_memcpy
#define memset hackos_memset
#define memcmp hackos_memcmp
#define strlen hackos_strlen
#define strcmp hackos_strcmp
#define strncmp hackos_strncmp
#define strcpy hackos_strcpy
#define fopen hackos_fopen
#define fread hackos_fread
#define fwrite hackos_fwrite
#define fclose hackos_fclose
#define free hackos_free
#define alloc hackos_alloc

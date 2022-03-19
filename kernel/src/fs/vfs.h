#ifndef VFS_H
#define VFS_H

#define O_CREAT 0x1
#define O_READ 0x2
#define O_WRITE 0x4

void* fopen(const char* path, int flags);
int fread(void* buffer, int size, int n, void* file);
int fwrite(void* buffer, int size, int n, void* file);
void fclose(void* file);

#endif
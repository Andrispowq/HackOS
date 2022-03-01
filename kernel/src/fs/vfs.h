#ifndef VFS_H
#define VFS_H

#define O_CREAT 1

void* fopen(const char* path, int flags);
int fread(void* buffer, int size, int n, void* file);
int fwrite(void* buffer, int size, int n, void* file);
void fclose(void* file);

#endif
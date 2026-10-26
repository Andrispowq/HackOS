// This translation unit alone uses hosted headers; it is compiled without rename.h.
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void* test_alloc(size_t n) { return std::malloc(n); }
extern "C" void test_free(void* p) { std::free(p); }
extern "C" void test_copy(void* d, const void* s, size_t n) { std::memcpy(d, s, n); }
extern "C" void test_fill(void* d, int c, size_t n) { std::memset(d, c, n); }
extern "C" int test_equal(const void* a, const void* b, size_t n) { return std::memcmp(a,b,n)==0; }
extern "C" int test_name_equal(const char* a, const char* b) { return std::strcmp(a,b)==0; }
extern "C" void test_abort(const char* message) {
    std::fprintf(stderr, "TEST ADAPTER ERROR: %s\n", message);
    std::exit(2);
}
extern "C" void* test_disk_load(size_t* size) {
    const char* path = std::getenv("HACKOS_FAT_IMAGE");
    FILE* f = path ? std::fopen(path, "rb") : nullptr;
    if (!f) test_abort("FAT fixture missing");
    std::fseek(f, 0, SEEK_END);
    long length = std::ftell(f);
    if (length <= 0) test_abort("invalid FAT fixture size");
    *size = static_cast<size_t>(length);
    std::rewind(f);
    void* data = std::malloc(*size);
    if (!data || std::fread(data, 1, *size, f) != *size) test_abort("reading FAT fixture");
    std::fclose(f);
    return data;
}
extern "C" void test_disk_save(const void* data, size_t size) {
    const char* path = std::getenv("HACKOS_FAT_RESULT");
    FILE* f = path ? std::fopen(path, "wb") : nullptr;
    if (!f || std::fwrite(data, 1, size, f) != size) test_abort("writing FAT result");
    std::fclose(f);
}

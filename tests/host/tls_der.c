/* Exercise the production bounded leaf-extension parser on truncated/mutated DER. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../libs/tls/src/engine.c"

int main(int argc, char** argv)
{
    assert(argc == 2);
    FILE* file = fopen(argv[1], "rb");
    assert(file);
    unsigned char original[8192];
    size_t size = fread(original, 1, sizeof(original), file);
    fclose(file);
    assert(size > 0 && size < sizeof(original));
    Validator* v = calloc(1, sizeof(*v));
    assert(v);
    v->numeric = 1;
    v->ip[0] = 127;
    v->ip[3] = 1;
    memcpy(v->leaf, original, size);
    v->used = size;
    assert(Leaf(v) == 0);
    v->ip[3] = 2;
    assert(Leaf(v) == BR_ERR_X509_BAD_SERVER_NAME);
    v->ip[3] = 1;
    for (size_t length = 0; length < size; ++length)
    {
        v->used = length;
        assert(Leaf(v) != 0);
    }
    v->used = size;
    for (size_t position = 0; position < size; ++position)
    {
        for (unsigned value = 0; value < 256; value += 17)
        {
            v->leaf[position] = value;
            (void)Leaf(v);
        }
        v->leaf[position] = original[position];
    }
    free(v);
    puts("PASS TLS leaf DER truncations and mutations (ASan/UBSan)");
    return 0;
}

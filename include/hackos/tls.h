#pragma once
#include "hackos/types.h"
extern "C"
{
    size_t hackos_tls_size();
    int hackos_tls_start(void* context, const char* host, const uint8_t* ip, uint32_t days, uint32_t seconds,
                         const uint8_t* seed);
    unsigned hackos_tls_state(void* context);
    int hackos_tls_error(void* context);
    unsigned char* hackos_tls_buffer(void* context, unsigned operation, size_t* size);
    void hackos_tls_ack(void* context, unsigned operation, size_t size);
    void hackos_tls_flush(void* context);
    void hackos_tls_close(void* context);
}
// BearSSL engine state bits, kept behind this freestanding adapter.
constexpr unsigned TLS_CLOSED = 1, TLS_SEND_RECORD = 2, TLS_RECV_RECORD = 4, TLS_SEND_APP = 8, TLS_RECV_APP = 16;

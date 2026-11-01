#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "bearssl.h"
size_t hackos_tls_size(void);
int hackos_tls_start(void*, const char*, const unsigned char*, uint32_t, uint32_t, const unsigned char*);
unsigned hackos_tls_state(void*);
int hackos_tls_error(void*);
unsigned char* hackos_tls_buffer(void*, unsigned, size_t*);
void hackos_tls_ack(void*, unsigned, size_t);
void hackos_tls_flush(void*);
void hackos_tls_close(void*);

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        return 2;
    }
    void* engine = calloc(1, hackos_tls_size());
    unsigned char seed[32], ip[4];
    for (unsigned i = 0; i < 32; ++i)
    {
        seed[i] = i + 1;
    }
    int numeric = inet_pton(AF_INET, argv[2], ip) == 1;
    if (!hackos_tls_start(engine, argv[2], numeric ? ip : NULL, atoi(argv[3]), 0, seed))
    {
        return 3;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct timeval timeout = {5, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_in peer = {
        .sin_family = AF_INET, .sin_port = htons(atoi(argv[1])), .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    if (connect(fd, (struct sockaddr*)&peer, sizeof(peer)))
    {
        return 4;
    }
    int sent = 0, received = 0, result = 1;
    for (unsigned step = 0; step < 20000; ++step)
    {
        unsigned state = hackos_tls_state(engine);
        if (state & BR_SSL_CLOSED)
        {
            fprintf(stderr, "TLS error=%d received=%d\n", hackos_tls_error(engine), received);
            result = hackos_tls_error(engine) || !received;
            break;
        }
        unsigned op = state & BR_SSL_SENDREC              ? BR_SSL_SENDREC
                      : state & BR_SSL_RECVAPP            ? BR_SSL_RECVAPP
                      : !sent && (state & BR_SSL_SENDAPP) ? BR_SSL_SENDAPP
                                                          : BR_SSL_RECVREC;
        size_t size;
        unsigned char* data = hackos_tls_buffer(engine, op, &size);
        if (!data || !size)
        {
            break;
        }
        if (size > 73)
        {
            size = 73;
        }
        if (op == BR_SSL_SENDREC)
        {
            ssize_t count = send(fd, data, size, 0);
            if (count <= 0)
            {
                break;
            }
            hackos_tls_ack(engine, op, count);
        }
        else if (op == BR_SSL_RECVREC)
        {
            ssize_t count = recv(fd, data, size, 0);
            if (count <= 0)
            {
                break;
            }
            hackos_tls_ack(engine, op, count);
        }
        else if (op == BR_SSL_SENDAPP)
        {
            const char* request = "GET / HTTP/1.1\r\nHost: demo.test\r\nConnection: close\r\n\r\n";
            size = strlen(request);
            memcpy(data, request, size);
            hackos_tls_ack(engine, op, size);
            hackos_tls_flush(engine);
            sent = 1;
        }
        else
        {
            fwrite(data, 1, size, stdout);
            received += size;
            hackos_tls_ack(engine, op, size);
            hackos_tls_close(engine);
        }
    }
    close(fd);
    free(engine);
    return result;
}

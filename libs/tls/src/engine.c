#include "bearssl.h"
#ifndef HACKOS_TLS_ROOTS
#define HACKOS_TLS_ROOTS "roots.h"
#endif
#include HACKOS_TLS_ROOTS

/* The upstream verifier handles signatures, names, dates, basic constraints and
 * key usage. This wrapper also checks leaf EKU and IPv4 subjectAltName, with
 * bounded certificate storage. Numeric IP identities never use DNS/CN matching. */
typedef struct
{
    const br_x509_class* vtable;
    br_x509_minimal_context minimal;
    unsigned char leaf[8192], ip[4];
    size_t used;
    unsigned certificates, total, error;
    int numeric;
} Validator;

typedef struct
{
    br_ssl_client_context client;
    Validator validator;
    unsigned char buffers[BR_SSL_BUFSIZE_BIDI];
} Engine;

typedef struct
{
    const unsigned char* data;
    size_t size;
} Der;

static int Take(Der* input, unsigned* tag, Der* value)
{
    if (input->size < 2)
    {
        return 0;
    }
    *tag = *input->data++;
    --input->size;
    unsigned length = *input->data++;
    --input->size;
    if (length & 128)
    {
        unsigned n = length & 127;
        if (!n || n > 3 || input->size < n || !input->data[0])
        {
            return 0;
        }
        length = 0;
        while (n--)
        {
            length = (length << 8) | *input->data++;
            --input->size;
        }
        if (length < 128)
        {
            return 0;
        }
    }
    if (length > input->size)
    {
        return 0;
    }
    value->data = input->data;
    value->size = length;
    input->data += length;
    input->size -= length;
    return 1;
}

static int Sequence(Der* input, unsigned expected, Der* value)
{
    unsigned tag;
    return Take(input, &tag, value) && tag == expected;
}

static unsigned Leaf(Validator* validator)
{
    Der data = {validator->leaf, validator->used}, outer, tbs, field;
    unsigned tag;
    int ipFound = 0, sanSeen = 0, ekuSeen = 0;
    if (!Sequence(&data, 0x30, &outer) || data.size || !Sequence(&outer, 0x30, &tbs))
    {
        return BR_ERR_X509_BAD_TAG_VALUE;
    }
    while (tbs.size)
    {
        if (!Take(&tbs, &tag, &field))
        {
            return BR_ERR_X509_BAD_TAG_VALUE;
        }
        if (tag != 0xa3)
        {
            continue;
        }
        Der extensions;
        if (!Sequence(&field, 0x30, &extensions) || field.size)
        {
            return BR_ERR_X509_BAD_TAG_VALUE;
        }
        while (extensions.size)
        {
            Der ext, oid, content;
            if (!Sequence(&extensions, 0x30, &ext) || !Sequence(&ext, 6, &oid))
            {
                return BR_ERR_X509_BAD_TAG_VALUE;
            }
            if (ext.size && ext.data[0] == 1)
            {
                if (!Sequence(&ext, 1, &content))
                {
                    return BR_ERR_X509_BAD_TAG_VALUE;
                }
            }
            if (!Sequence(&ext, 4, &content) || ext.size)
            {
                return BR_ERR_X509_BAD_TAG_VALUE;
            }
            if (oid.size != 3 || oid.data[0] != 0x55 || oid.data[1] != 0x1d)
            {
                continue;
            }
            if (oid.data[2] == 17)
            {
                Der names, name;
                if (sanSeen++ || !Sequence(&content, 0x30, &names) || content.size)
                {
                    return BR_ERR_X509_BAD_TAG_VALUE;
                }
                while (names.size)
                {
                    if (!Take(&names, &tag, &name))
                    {
                        return BR_ERR_X509_BAD_TAG_VALUE;
                    }
                    if (tag == 0x87 && name.size == 4 && !memcmp(name.data, validator->ip, 4))
                    {
                        ipFound = 1;
                    }
                }
            }
            else if (oid.data[2] == 37)
            {
                Der purposes, purpose;
                int server = 0;
                static const unsigned char serverAuth[] = {0x2b, 6, 1, 5, 5, 7, 3, 1};
                static const unsigned char anyUsage[] = {0x55, 0x1d, 0x25, 0};
                if (ekuSeen++ || !Sequence(&content, 0x30, &purposes) || content.size)
                {
                    return BR_ERR_X509_BAD_TAG_VALUE;
                }
                while (purposes.size)
                {
                    if (!Sequence(&purposes, 6, &purpose))
                    {
                        return BR_ERR_X509_BAD_TAG_VALUE;
                    }
                    if ((purpose.size == sizeof(serverAuth) && !memcmp(purpose.data, serverAuth, sizeof(serverAuth))) ||
                        (purpose.size == sizeof(anyUsage) && !memcmp(purpose.data, anyUsage, sizeof(anyUsage))))
                    {
                        server = 1;
                    }
                }
                if (!server)
                {
                    return BR_ERR_X509_FORBIDDEN_KEY_USAGE;
                }
            }
        }
    }
    return validator->numeric && !ipFound ? BR_ERR_X509_BAD_SERVER_NAME : 0;
}

static void Chain(const br_x509_class** context, const char* name)
{
    Validator* v = (Validator*)context;
    v->used = v->certificates = v->total = v->error = 0;
    v->minimal.vtable->start_chain(&v->minimal.vtable, v->numeric ? NULL : name);
}

static void Certificate(const br_x509_class** context, uint32_t size)
{
    Validator* v = (Validator*)context;
    if (++v->certificates > 8 || size > 8192 || size > 65536 - v->total)
    {
        v->error = BR_ERR_X509_LIMIT_EXCEEDED;
    }
    else
    {
        v->total += size;
    }
    v->minimal.vtable->start_cert(&v->minimal.vtable, size);
}

static void Append(const br_x509_class** context, const unsigned char* bytes, size_t size)
{
    Validator* v = (Validator*)context;
    if (v->certificates == 1 && !v->error)
    {
        if (size > sizeof(v->leaf) - v->used)
        {
            v->error = BR_ERR_X509_LIMIT_EXCEEDED;
        }
        else
        {
            memcpy(v->leaf + v->used, bytes, size);
            v->used += size;
        }
    }
    v->minimal.vtable->append(&v->minimal.vtable, bytes, size);
}

static void EndCertificate(const br_x509_class** context)
{
    Validator* v = (Validator*)context;
    v->minimal.vtable->end_cert(&v->minimal.vtable);
    if (v->certificates == 1 && !v->error)
    {
        v->error = Leaf(v);
    }
}

static unsigned EndChain(const br_x509_class** context)
{
    Validator* v = (Validator*)context;
    unsigned error = v->minimal.vtable->end_chain(&v->minimal.vtable);
    return error ? error : v->error;
}

static const br_x509_pkey* Key(const br_x509_class* const* context, unsigned* usages)
{
    const Validator* v = (const Validator*)context;
    if (v->error)
    {
        return NULL;
    }
    return v->minimal.vtable->get_pkey(&v->minimal.vtable, usages);
}

static const br_x509_class validatorClass = {sizeof(Validator), Chain,    Certificate, Append,
                                             EndCertificate,    EndChain, Key};

size_t hackos_tls_size(void)
{
    return sizeof(Engine);
}

int hackos_tls_start(void* context, const char* host, const unsigned char* ip, uint32_t days, uint32_t seconds,
                     const unsigned char* seed)
{
    Engine* e = context;
    memset(e, 0, sizeof(*e));
    static const uint16_t suites[] = {
        BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,       BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
        BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384,       BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384,
        BR_TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256, BR_TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256};
    br_ssl_client_zero(&e->client);
    br_ssl_engine_set_versions(&e->client.eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(&e->client.eng, suites, sizeof(suites) / sizeof(suites[0]));
    br_ssl_engine_set_ec(&e->client.eng, &br_ec_all_m31);
    br_ssl_engine_set_rsavrfy(&e->client.eng, br_rsa_i31_pkcs1_vrfy);
    br_ssl_engine_set_ecdsa(&e->client.eng, br_ecdsa_i31_vrfy_asn1);
    br_ssl_engine_set_hash(&e->client.eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(&e->client.eng, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_hash(&e->client.eng, br_sha512_ID, &br_sha512_vtable);
    br_ssl_engine_set_prf_sha256(&e->client.eng, br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(&e->client.eng, br_tls12_sha384_prf);
    br_ssl_engine_add_flags(&e->client.eng, BR_OPT_NO_RENEGOTIATION);
    static const char* protocols[] = {"http/1.1"};
    br_ssl_engine_set_protocol_names(&e->client.eng, protocols, 1);
    br_ssl_engine_set_default_aes_gcm(&e->client.eng);
    br_ssl_engine_set_default_chapol(&e->client.eng);
    Validator* v = &e->validator;
    v->vtable = &validatorClass;
    v->numeric = ip != NULL;
    if (ip)
    {
        memcpy(v->ip, ip, 4);
    }
    br_x509_minimal_init(&v->minimal, &br_sha256_vtable, trust_anchors,
                         sizeof(trust_anchors) / sizeof(trust_anchors[0]));
    br_x509_minimal_set_rsa(&v->minimal, br_rsa_i31_pkcs1_vrfy);
    br_x509_minimal_set_ecdsa(&v->minimal, &br_ec_all_m31, br_ecdsa_i31_vrfy_asn1);
    br_x509_minimal_set_hash(&v->minimal, br_sha256_ID, &br_sha256_vtable);
    br_x509_minimal_set_hash(&v->minimal, br_sha384_ID, &br_sha384_vtable);
    br_x509_minimal_set_hash(&v->minimal, br_sha512_ID, &br_sha512_vtable);
    br_x509_minimal_set_minrsa(&v->minimal, 256);
    br_x509_minimal_set_time(&v->minimal, days, seconds);
    br_ssl_engine_set_x509(&e->client.eng, &v->vtable);
    br_ssl_engine_set_buffer(&e->client.eng, e->buffers, sizeof(e->buffers), 1);
    br_ssl_engine_inject_entropy(&e->client.eng, seed, 32);
    return br_ssl_client_reset(&e->client, ip ? NULL : host, 0);
}

unsigned hackos_tls_state(void* context)
{
    return br_ssl_engine_current_state(&((Engine*)context)->client.eng);
}

int hackos_tls_error(void* context)
{
    return br_ssl_engine_last_error(&((Engine*)context)->client.eng);
}

unsigned char* hackos_tls_buffer(void* context, unsigned op, size_t* size)
{
    br_ssl_engine_context* e = &((Engine*)context)->client.eng;
    switch (op)
    {
    case BR_SSL_SENDREC:
        return br_ssl_engine_sendrec_buf(e, size);
    case BR_SSL_RECVREC:
        return br_ssl_engine_recvrec_buf(e, size);
    case BR_SSL_SENDAPP:
        return br_ssl_engine_sendapp_buf(e, size);
    case BR_SSL_RECVAPP:
        return br_ssl_engine_recvapp_buf(e, size);
    default:
        *size = 0;
        return NULL;
    }
}

void hackos_tls_ack(void* context, unsigned op, size_t size)
{
    br_ssl_engine_context* e = &((Engine*)context)->client.eng;
    switch (op)
    {
    case BR_SSL_SENDREC:
        br_ssl_engine_sendrec_ack(e, size);
        break;
    case BR_SSL_RECVREC:
        br_ssl_engine_recvrec_ack(e, size);
        break;
    case BR_SSL_SENDAPP:
        br_ssl_engine_sendapp_ack(e, size);
        break;
    case BR_SSL_RECVAPP:
        br_ssl_engine_recvapp_ack(e, size);
        break;
    }
}

void hackos_tls_flush(void* context)
{
    br_ssl_engine_flush(&((Engine*)context)->client.eng, 0);
}

void hackos_tls_close(void* context)
{
    br_ssl_engine_close(&((Engine*)context)->client.eng);
}

const char* hackos_tls_error_message(int error)
{
    switch (error)
    {
    case BR_ERR_X509_EXPIRED:
        return "TLS: certificate expired or not yet valid; check the clock.";
    case BR_ERR_X509_BAD_SERVER_NAME:
        return "TLS: certificate does not match this hostname.";
    case BR_ERR_X509_NOT_TRUSTED:
        return "TLS: certificate chain is not trusted.";
    case BR_ERR_BAD_VERSION:
        return "TLS: server requires an unsupported protocol version.";
    default:
        return "TLS: handshake/certificate or record verification failed.";
    }
}

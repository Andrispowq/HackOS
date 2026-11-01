# Userspace TLS

The immutable upstream subset in `vendor/bearssl` is from
<https://www.bearssl.org/git/BearSSL>, commit
`7bea48e5e850ab4cafbe68d3765cdaba13a86d6f` (2026-04-06). Its MIT license is retained.
Only upstream `inc/`, `src/` and `LICENSE.txt` are included; do not reformat them.
`src/engine.c`, `src/memory.c`, the freestanding header adapters and this Makefile
are HackOS integration code. `sources.json` pins provenance and trust-bundle hash.

`roots.pem` is curl's Mozilla-derived CA bundle dated 2026-10-20, downloaded from
<https://curl.se/ca/cacert.pem>. Its original metadata is kept; the bundle and generated anchors are MPL 2.0
(`ROOTS-LICENSE.txt`). The PEM conversion does not carry Firefox's additional
external CA/domain constraints; this is not the full browser trust-policy engine.
`src/roots.h` is generated with Python's `cryptography` package:

```
python3 tools/tls_roots.py libs/tls/roots.pem libs/tls/src/roots.h
```

Normal builds use these checked-in inputs, with no downloads or host CA lookup.
Updating trust requires explicitly replacing the bundle, regenerating the header,
updating `sources.json`, reviewing the anchor changes and rerunning TLS tests.
Test private keys/CA roots are generated under ignored `tests/out`; test roots are
linked only into copied test binaries and never into production images.

ALPN advertises only HTTP/1.1, and renegotiation is explicitly disabled.
Certificate date/name/trust failures have distinct user-facing diagnostics.

The profile requires TLS 1.2 with ECDHE_RSA/ECDHE_ECDSA and AES-128/256-GCM or
ChaCha20-Poly1305. SHA-256/384/512 certificate signatures are supported; RSA moduli
must be at least 256 bytes, using BearSSL's byte-length minimum. There is no TLS 1.0/1.1/1.3, CBC, static RSA key
exchange, SHA-1/MD5 certificate signatures, resumption, renegotiation,
client certificates, OCSP/CRL or certificate fetching/path building. The peer
must send a chain to a bundled trust anchor. Upstream's minimal X.509 verifier
checks signatures, names, dates, basic constraints and key usage. The adapter
also enforces leaf serverAuth/anyExtendedKeyUsage when EKU exists and exact IPv4
SAN matching; numeric addresses never use DNS/CN matching or SNI. DNS names use
BearSSL's DNS SAN/CN/wildcard matching and SNI. Intermediate EKU restrictions and
revocation are not implemented by this minimal verifier.

Each session uses fixed engine/record buffers and an 8 KiB leaf certificate
buffer, with at most eight certificates, 8 KiB each and 64 KiB total. System RNG,
clock and CPU crypto autodetection are disabled in the library. The userspace
adapter provides bounded CPUID-checked RDRAND entropy and explicit RTC UTC dates.
Entropy/time/allocation errors fail closed. Sessions are wiped before release.
The browser pumps at most eight TCP records per turn and preserves its existing
10-second inactivity/120-second total-load limits. HTTP also has a 30-second
request deadline across redirect hops. No insecure override exists.

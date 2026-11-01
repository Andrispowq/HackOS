"""Ephemeral test CA and server fixtures. Never included in production trust."""
from datetime import datetime, timezone
from pathlib import Path
import ipaddress
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.x509.oid import NameOID, ExtendedKeyUsageOID


def fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    begin, end = datetime(2025, 1, 1), datetime(2035, 1, 1)
    def name(value):
        return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, value)])
    def issue(subject, key, issuer, issuer_key, ca=False, names=None, before=begin, after=end, client=False):
        b = (x509.CertificateBuilder().subject_name(name(subject)).issuer_name(issuer.subject if issuer else name(subject))
             .public_key(key.public_key()).serial_number(x509.random_serial_number())
             .not_valid_before(before).not_valid_after(after)
             .add_extension(x509.BasicConstraints(ca=ca, path_length=1 if ca else None), critical=True)
             .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False, key_encipherment=not ca,
                            data_encipherment=False, key_agreement=False, key_cert_sign=ca, crl_sign=ca,
                            encipher_only=False, decipher_only=False), critical=True))
        if not ca:
            b = b.add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH if client else ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
        if names is not None:
            b = b.add_extension(x509.SubjectAlternativeName(names), critical=False)
        return b.sign(issuer_key or key, hashes.SHA256())
    root_key = ec.generate_private_key(ec.SECP256R1())
    root = issue('HackOS ephemeral TEST root', root_key, None, None, ca=True)
    intermediate_key = ec.generate_private_key(ec.SECP256R1())
    intermediate = issue('HackOS TEST intermediate', intermediate_key, root, root_key, ca=True)
    root_path = directory/'root.pem'
    root_path.write_bytes(root.public_bytes(serialization.Encoding.PEM))
    names = [x509.DNSName('demo.test'), x509.DNSName('localhost'), x509.IPAddress(ipaddress.ip_address('10.0.2.2')), x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]
    cases = {}
    for variant in ('valid', 'rsa', 'wrong-name', 'expired', 'future', 'untrusted', 'client-only', 'dns-ip'):
        key = rsa.generate_private_key(65537, 2048) if variant == 'rsa' else ec.generate_private_key(ec.SECP256R1())
        args = {'names': names}
        if variant == 'wrong-name': args['names'] = [x509.DNSName('wrong.test')]
        if variant == 'dns-ip': args['names'] = [x509.DNSName('127.0.0.1'), x509.DNSName('10.0.2.2')]
        if variant == 'expired': args.update(before=datetime(2020,1,1), after=datetime(2021,1,1))
        if variant == 'future': args.update(before=datetime(2040,1,1), after=datetime(2041,1,1))
        if variant == 'client-only': args['client'] = True
        cert = issue('demo.test', key, None if variant == 'untrusted' else intermediate,
                     None if variant == 'untrusted' else intermediate_key, **args)
        chain = cert.public_bytes(serialization.Encoding.PEM)
        if variant != 'untrusted': chain += intermediate.public_bytes(serialization.Encoding.PEM)
        certpath, keypath = directory/(variant+'.pem'), directory/(variant+'.key')
        certpath.write_bytes(chain)
        if variant == 'valid': (directory/'leaf.der').write_bytes(cert.public_bytes(serialization.Encoding.DER))
        keypath.write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        keypath.chmod(0o600)
        cases[variant] = (certpath,keypath)
    return root_path,cases

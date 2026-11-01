#!/usr/bin/env python3
"""TLS certificate failures, exact downloads and HTTPS browser on disposable images.
Only copied test binaries trust the ephemeral test CA; production anchors stay intact.
"""
import argparse
import contextlib
import os
from pathlib import Path
import shutil
import ssl
import threading
import sys
from common import ROOT, OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_browser import ThreadedServer, SlowHandler, test as browser_test
from tls_fixtures import fixtures
from http_demo import BINARY, Handler
sys.path.insert(0,str(ROOT/'tools'))
from tls_roots import generate

@contextlib.contextmanager
def server(cert=None):
    with ThreadedServer(('127.0.0.1',0), SlowHandler) as instance:
        instance.requests=[]
        if cert:
            instance.context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            instance.context.minimum_version=instance.context.maximum_version=ssl.TLSVersion.TLSv1_2
            instance.context.load_cert_chain(*cert)
        thread=threading.Thread(target=instance.serve_forever,daemon=True);thread.start()
        try: yield instance
        finally: instance.shutdown();thread.join(2)

def trusted_image(image, directory, root):
    directory.mkdir(parents=True,exist_ok=True)
    shutil.copy2(image,directory/'HackOS.img')
    shutil.copy2(image.parent/'kernel.elf',directory/'kernel.elf')
    source=directory/'source'
    shutil.rmtree(source,ignore_errors=True)
    shutil.copytree(image.parent/'source',source)
    generate(root,source/'libs/tls/src/roots.h')
    for lib in ('tls','browser','commands'):
        result=command(['make','-j2','-C',source/'libs'/lib, 'bin/http' if lib=='commands' else 'all'],timeout=300)
        (directory/(lib+'-build.log')).write_text(result.stdout)
        require_success(result,'build TLS test '+lib)
    for name,path in [('HTTP','libs/commands/bin/http'),('BROWSER','libs/browser/bin/browser')]:
        require_success(command(['mcopy','-o','-i',filesystem(directory/'HackOS.img'),source/path,'::/USR/BIN/'+name]),'install isolated TLS test binary')
    return directory/'HackOS.img'

def test(firmware,image,timeout,internet=False):
    directory=OUT/('https-'+firmware);directory.mkdir(parents=True,exist_ok=True)
    root,cases=fixtures(directory/'certificates')
    patched=trusted_image(image,directory/'trusted-build',root)
    disposable=directory/'HackOS.img';shutil.copy2(patched,disposable)
    with contextlib.ExitStack() as stack:
        servers={name:stack.enter_context(server(cert)) for name,cert in cases.items()}
        def url(name,path='/'):
            return f'https://10.0.2.2:{servers[name].server_address[1]}{path}'
        with boot(directory,disposable,firmware,'https',timeout,network=True) as g:
            g.fresh=True;g.setup();g.enter('root','New password: ');g.password(ADMIN_PASSWORD,'Action: ');g.login('root',ADMIN_PASSWORD)
            g.shell('http '+url('valid'),'Hello from HackOS HTTP!')
            g.shell('runas guest http '+url('rsa','/chunked'),'Hello chunked!')
            g.shell('http -o /tmp/tls-binary '+url('valid','/binary'),'Saved 256 bytes.')
            g.shell('echo untouched > /tmp/tls-keep','$ ')
            for name in ('wrong-name','expired','future','untrusted','client-only','dns-ip'):
                before=len(servers[name].requests)
                g.shell('http -o /tmp/tls-keep '+url(name),'TLS: handshake/certificate or record verification failed')
                assert len(servers[name].requests)==before,'HTTP sent before certificate acceptance'
                g.shell('cat /tmp/tls-keep','untouched')
            g.shell('http '+url('valid','/redirect-relative'),'Hello from HackOS HTTP!')
            g.screen('https')
        for name,expected in [('tls-binary',BINARY),('tls-keep',b'untouched\n')]:
            local=directory/name
            require_success(command(['mcopy','-o','-i',filesystem(disposable),'::/TMP/'+name,local]),'check HTTPS bytes')
            assert local.read_bytes()==expected
        # Use production anchors against the same private CA: it must be rejected.
        production=directory/'production.img';shutil.copy2(image,production)
        with boot(directory,production,firmware,'untrusted-production',timeout,network=True) as g:
            g.fresh=True;g.setup();g.enter('root','New password: ');g.password(ADMIN_PASSWORD,'Action: ');g.login('root',ADMIN_PASSWORD)
            g.shell('http '+url('valid'),'TLS: handshake/certificate or record verification failed')
            if internet:
                g.shell('http https://example.com/', 'HTTP status 200')
    # Home intentionally remains the local plain-HTTP demo. HTTPS navigation uses
    # 8443, while Home/Back exercise switching between both secure and plain pages.
    with ThreadedServer(('127.0.0.1',8080),Handler) as plain:
        thread=threading.Thread(target=plain.serve_forever,daemon=True);thread.start()
        try: browser_test(firmware,patched,timeout,cases['valid'])
        finally: plain.shutdown();thread.join(2)
    print('PASS',firmware,'HTTPS: verified chains/IP identities, rejection before HTTP/output, binary bytes, production-root isolation and browser',flush=True)

def public_browser(firmware, image, timeout):
    directory=OUT/('https-public-'+firmware);directory.mkdir(parents=True,exist_ok=True)
    disposable=directory/'HackOS.img';shutil.copy2(image,disposable)
    with boot(directory,disposable,firmware,'public-browser',timeout,network=True) as g:
        g.fresh=True;g.setup();g.enter('root','New password: ');g.password(ADMIN_PASSWORD,'Action: ');g.login('root',ADMIN_PASSWORD)
        g.enter('browser https://example.com/','BROWSER_LOADED')
        g.screen('example-com')
        offset=len(g.output())
        g.qmp.key('f4',alt=True)
        g.wait('$ ',offset)
    print('PASS',firmware,'production Browser https://example.com/ and close',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',choices=('bios','uefi'),required=True)
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--timeout',type=int,default=180)
    p.add_argument('--internet',action='store_true',help='Also test example.com using production trust roots')
    p.add_argument('--public-browser-only',action='store_true',help='Only smoke-test production Browser against example.com (Internet required)')
    a=p.parse_args()
    if a.public_browser_only: public_browser(a.firmware,a.image.resolve(),a.timeout)
    else: test(a.firmware,a.image.resolve(),a.timeout,a.internet)

#!/usr/bin/env python3
"""ASan/UBSan interoperability and identity/chain/date rejection for real TLS adapter."""
import concurrent.futures
import os
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import threading
from datetime import date
from tls_fixtures import fixtures
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from tls_roots import generate
OUT = ROOT/'tests/out/tls'
FLAGS = ['-std=c99','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-ffunction-sections','-fdata-sections',
         '-DBR_USE_URANDOM=0','-DBR_USE_GETENTROPY=0','-DBR_USE_WIN32_RAND=0','-DBR_USE_UNIX_TIME=0','-DBR_USE_WIN32_TIME=0',
         '-DBR_RDRAND=0','-DBR_AES_X86NI=0','-DBR_SSE2=0','-DBR_POWER8=0']

def build(root):
    generate(root,OUT/'test_roots.h')
    vendor=ROOT/'libs/tls/vendor/bearssl'
    flags=FLAGS+['-I'+str(vendor/'inc'),'-I'+str(vendor/'src'),'-DHACKOS_TLS_ROOTS="'+str(OUT/'test_roots.h')+'"']
    sources=sorted((vendor/'src').rglob('*.c'))+[ROOT/'libs/tls/src/engine.c', ROOT/'tests/host/tls_client.c']
    def compile(item):
        i,source=item
        target=OUT/f'{i}.o'
        subprocess.run([os.environ.get('CC','gcc'),*flags,'-c',str(source),'-o',str(target)],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        return target
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        objects=list(pool.map(compile,enumerate(sources)))
    binary=OUT/'tls-client'
    subprocess.run(['gcc','-fsanitize=address,undefined','-Wl,--gc-sections',*map(str,objects),'-o',str(binary)],check=True)
    der=OUT/'tls-der'
    subprocess.run(['gcc',*flags,'-c',str(ROOT/'tests/host/tls_der.c'),'-o',str(OUT/'der.o')],check=True)
    subprocess.run(['gcc','-fsanitize=address,undefined','-Wl,--gc-sections',*map(str,objects[:-2]),str(OUT/'der.o'),'-o',str(der)],check=True)
    subprocess.run([str(der),str(OUT/'leaf.der')],check=True,timeout=30)
    return binary

def check(binary,cert,key,host,valid,version=ssl.TLSVersion.TLSv1_2):
    server=socket.socket();server.bind(('127.0.0.1',0));server.listen()
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version=context.maximum_version=version
    context.load_cert_chain(cert,key)
    sni=[]
    context.set_servername_callback(lambda sock,name,ctx:sni.append(name))
    def serve():
        try:
            peer,_=server.accept();peer.settimeout(5)
            with context.wrap_socket(peer,server_side=True) as secure:
                secure.recv(2048)
                secure.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok')
                secure.unwrap().close()
        except (OSError,ssl.SSLError): pass
    thread=threading.Thread(target=serve,daemon=True);thread.start()
    result=subprocess.run([str(binary),str(server.getsockname()[1]),host,str(date(2026,11,1).toordinal()+365)],capture_output=True,timeout=15)
    server.close();thread.join(6)
    assert (result.returncode==0)==valid,(cert.name,host,result.returncode,result.stdout,result.stderr)
    assert b'AddressSanitizer' not in result.stderr and b'runtime error:' not in result.stderr,result.stderr
    if valid:
        assert result.stdout.endswith(b'ok'),result.stdout
        assert sni==([None] if host=='127.0.0.1' else [host]),sni
    print('PASS',cert.stem,host,version.name,'accepted' if valid else 'rejected',flush=True)

if __name__=='__main__':
    root,cases=fixtures(OUT)
    binary=build(root)
    for variant,(cert,key) in cases.items():
        check(binary,cert,key,'127.0.0.1',variant in ('valid','rsa'))
    check(binary,*cases['valid'],'demo.test',True)
    check(binary,*cases['valid'],'wrong.test',False)
    check(binary,*cases['valid'],'demo.test',False,ssl.TLSVersion.TLSv1_3)

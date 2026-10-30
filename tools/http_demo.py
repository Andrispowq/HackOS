#!/usr/bin/env python3
"""Loopback-only HTTP demo and deterministic response fixtures; no filesystem serving."""
import argparse
import socketserver

WELCOME = b'Hello from HackOS HTTP!\nFetched using TCP streams and HTTP/1.1.\nTry /chunked, /large or /binary.\n'
LARGE = b'0123456789abcdef' * 512
BINARY = bytes(range(256))


HOME_HTML = b"""<!doctype html><html><head><title>HackOS Web</title>
<style>hidden style text</style></head><body>
<h1>Welcome to the HackOS web</h1>
<p>A small browser, built inside a small operating system.</p>
<p>This page arrived over <strong>real TCP and HTTP</strong>. Try a link below.</p>
<h2>Explore</h2><ul>
<li><a href="guide.html">Read the browser guide</a></li>
<li><a href="long.html">Open a long page and scroll</a></li>
<li><a href="../missing">Try a missing page</a></li>
<li><a href="https://example.com/">Try an unsupported HTTPS link</a></li>
</ul><h2>Simple HTML, readable text</h2>
<p>Paragraphs wrap to the window width. <b>Bold text</b>, headings, lists,
line breaks<br>and entities such as &lt;html&gt; &amp; &quot;quotes&quot; work.</p>
<pre>GET /browser/ HTTP/1.1
Host: 10.0.2.2:8080

Hello from HackOS!</pre>
<p>No scripts, stylesheets, images or tracking requests are loaded.</p>
<script>THIS SCRIPT MUST NEVER APPEAR</script></body></html>"""
GUIDE_HTML = b"""<h1>Browser guide</h1><p>You followed a relative link.</p>
<ol><li>Enter an HTTP URL and press Enter or Go.</li>
<li>Click links, or focus the page and use Left/Right then Enter.</li>
<li>Use the wheel, arrow keys, Page Up or Page Down to scroll.</li>
<li>Use Back to return, Reload to fetch again, or Stop to cancel.</li></ol>
<p><a href="./">Return to the home page</a></p>
<p><a href="long.html">Read the long page</a></p>
<h2>Limits</h2><p>Pages are limited to 32 KiB and addresses to 255 bytes.
This is a text-only HTML viewer. HTTPS and automatic redirects are not supported.</p>"""
LONG_HTML = (b'<h1>A longer journey</h1><p><a href="./">Back to home</a></p>' +
             b''.join(f'<h2>Section {i}</h2><p>Scrolling through a real page in HackOS. '
                      f'This is paragraph {i}. Resize the window to see the text wrap.</p>'.encode()
                      for i in range(1, 41)) + b'<p>END OF LONG PAGE</p>')

def response(target, host):
    path = target.split('?', 1)[0]
    if path == '/chunked':
        return (b'HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n'
                b'6;demo="yes"\r\nHello \r\n8\r\nchunked!\r\n1\r\n\n\r\n0\r\nX-Demo: complete\r\n\r\n')
    if path == '/close':
        return b'HTTP/1.0 200 OK\r\n\r\nClose-delimited response.\n'
    if path == '/empty':
        return b'HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n'
    if path == '/redirect':
        return b'HTTP/1.1 302 Found\r\nLocation: /\r\nContent-Length: 0\r\n\r\n'
    if path == '/bad':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\nxx'
    if path == '/truncated':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort'
    if path == '/compressed':
        return b'HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 1\r\n\r\nx'
    if path == '/oversize':
        return b'HTTP/1.1 200 OK\r\nContent-Length: 32769\r\n\r\n'
    html = path in ('/browser/', '/browser/guide.html', '/browser/long.html')
    if html:
        body = {'/browser/': HOME_HTML, '/browser/guide.html': GUIDE_HTML, '/browser/long.html': LONG_HTML}[path]
    elif path == '/controls':
        body = b'safe\x1b[31m\x00\xff\n'
    elif path == '/large':
        body = LARGE
    elif path == '/binary':
        body = BINARY
    elif path == '/boundary':
        body = b'x' * 32768
    elif path == '/host':
        body = b'Host: ' + host.encode('ascii') + b'\nTarget: ' + target.encode('ascii') + b'\n'
    elif path in ('/', '/info'):
        body = WELCOME
    else:
        return b'HTTP/1.1 404 Not Found\r\nContent-Length: 8\r\n\r\nmissing\n'
    prefix = b'HTTP/1.1 103 Early Hints\r\nLink: </large>\r\n\r\n' if path == '/info' else b''
    return prefix + b'HTTP/1.1 200 OK\r\nContent-Type: ' + (b'text/html' if html else b'text/plain') + b'\r\nConnection: close\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body


class Handler(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(10)
        try:
            data = b''
            while b'\r\n\r\n' not in data and len(data) < 2048:
                part = self.request.recv(min(512, 2048 - len(data)))
                if not part:
                    return
                data += part
            if b'\r\n\r\n' not in data:
                return
            lines = data.split(b'\r\n')
            method, target, version = lines[0].decode('ascii').split(' ')
            headers = {}
            for line in lines[1:]:
                if not line:
                    break
                key, value = line.decode('ascii').split(':', 1)
                headers[key.lower()] = value.strip()
            if method != 'GET' or version != 'HTTP/1.1' or not target.startswith('/') or 'host' not in headers:
                return
            if hasattr(self.server, 'requests'):
                self.server.requests.append((target, headers))
            message = response(target, headers['host'])
            # Independent writes exercise arbitrary TCP/HTTP segmentation.
            at = 0
            for size in (1, 2, 7, 31, 127):
                self.request.sendall(message[at:at + size])
                at += size
                if at >= len(message):
                    return
            while at < len(message):
                self.request.sendall(message[at:at + 512])
                at += 512
        except (OSError, ValueError, UnicodeError):
            return


class Server(socketserver.TCPServer):
    allow_reuse_address = True


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8080)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error('port must be between 1024 and 65535')
    with Server(('127.0.0.1', args.port), Handler) as server:
        print(f'HTTP demo: http http://10.0.2.2:{args.port}/', flush=True)
        server.serve_forever()

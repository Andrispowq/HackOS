#!/usr/bin/env python3
"""Browser input/rendering/navigation over a real loopback HTTP connection."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import socketserver
import threading
import time
from common import OUT, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD
from http_demo import Handler, Server


class SlowHandler(Handler):
    def handle(self):
        if b'/slow ' in self.request.recv(2048, socket.MSG_PEEK):  # MSG_PEEK: normal handler still receives the request.
            try:
                self.request.recv(2048)
                self.request.sendall(b'HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n')
                time.sleep(5)
            except OSError:
                pass
        else:
            super().handle()


class ThreadedServer(socketserver.ThreadingMixIn, Server):
    daemon_threads = True


def test(firmware, image, timeout):
    directory = OUT / ('browser-' + firmware)
    directory.mkdir(parents=True, exist_ok=True)
    disposable = directory / 'HackOS.img'
    shutil.copy2(image, disposable)
    built = image.parent
    with ThreadedServer(('127.0.0.1', 8080), SlowHandler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            with boot(directory, disposable, firmware, 'browser', timeout, network=True) as g:
                g.fresh = True
                g.setup()
                g.enter('root', 'New password: ')
                g.password(ADMIN_PASSWORD, 'Action: ')
                g.login('root', ADMIN_PASSWORD)
                g.enter('browser', 'BROWSER_LOADED')
                sequence = 0

                def inspect(checks=''):
                    nonlocal sequence
                    sequence += 1
                    g.qmp.call('stop')
                    script = directory / f'inspect-{sequence}.gdb'
                    script.write_text('set pagination off\nset confirm off\ntarget remote ' + str(g.gdb) +
                                      '\nadd-symbol-file ' + str(built / 'source/libs/browser/bin/browser') + '\npython\nimport gdb, struct, time, json\n' + '''
bp = gdb.Breakpoint('ReapExitedProcesses', type=gdb.BP_HARDWARE_BREAKPOINT)
bp.silent = True
deadline = time.monotonic() + 60
while time.monotonic() < deadline:
    gdb.execute('continue', to_string=True)
    w = gdb.parse_and_eval('KernelWindowManager->focused')
    p = gdb.parse_and_eval('ready_queue')
    while int(p) and (not int(w) or int(p['pid']) != int(w['owner'])): p = p['next']
    if int(p) and int(p['state']) == 6 and int(w['events']['count']) == 0 and int(gdb.parse_and_eval('mouseEvents.count')) == 0:
        break
else: raise AssertionError('Browser did not drain input and block')
def read(expression, length):
    expression = expression.replace('document.', '$doc->')
    address = int(gdb.parse_and_eval('(unsigned long)&' + expression))
    result = b''
    while length:
        leaf = p['userSpace']['leaf']['entries'][(address - 0x400000000000) // 4096]
        physical = (int(leaf['address']) << 12) + (address & 4095)
        size = min(length, 4096 - (address & 4095))
        result += bytes(gdb.selected_inferior().read_memory(physical, size))
        address += size
        length -= size
    return result
gdb.execute('set $doc = (Hackos::Html::Document*)' + str(struct.unpack('<Q', read('document', 8))[0]))
def u(expression): return struct.unpack('<I', read(expression, 4))[0]
def small(expression): return struct.unpack('<H', read(expression, 2))[0]
def text(expression): return read(expression, 256).split(b'\\0', 1)[0].decode('ascii')
def field(name): return 'view.widgets[' + str(u(name)) + ']'
def page(): return read('document.glyphs', u('document.count') * 4)[::4].decode('ascii')
''' + checks + '''
positions = {}
for i in range(u('view.count')):
    f = 'view.widgets[' + str(i) + ']'
    r = f + '.bounds'
    positions[text(f + '.text')] = [int(w['startX']) + u(r + '.x') + u(r + '.width') // 2,
                                    int(w['startY']) + 30 + u(r + '.y') + u(r + '.height') // 2]
for name in ['addressField', 'pageWidget']:
    r = field(name) + '.bounds'
    positions[name] = [int(w['startX']) + u(r + '.x') + 30, int(w['startY']) + 30 + u(r + '.y') + 12]
r = field('pageWidget') + '.bounds'
count = u('document.count')
glyphs = read('document.glyphs', count * 4)
columns = read('document.columns', count * 2)
rows = read('document.rows', count * 2)
top = u('top')
for i in range(count):
    link = struct.unpack_from('<H', glyphs, i * 4 + 2)[0]
    row = struct.unpack_from('<H', rows, i * 2)[0]
    if link and row >= top and 'link:' + str(link) not in positions:
        positions['link:' + str(link)] = [int(w['startX']) + u(r + '.x') + 8 + struct.unpack_from('<H', columns, i * 2)[0] * 8 + 3,
                                         int(w['startY']) + 30 + u(r + '.y') + 8 + (row-top) * 20 + 8]
print('BROWSER_POSITIONS=' + json.dumps(positions))
bp.delete()
end
detach
quit
''')
                    result = command([os.environ.get('GDB', 'gdb'), '--batch', '--nx', '-q', built / 'kernel.elf', '-x', script], timeout=90)
                    (directory / f'inspect-{sequence}.log').write_text(result.stdout)
                    require_success(result, 'Browser inspection')
                    positions = json.loads(next(line.split('=', 1)[1] for line in result.stdout.splitlines() if line.startswith('BROWSER_POSITIONS=')))
                    g.qmp.call('cont')
                    return positions

                def key(code, shift=False, control=False, alt=False):
                    keys = (['ctrl'] if control else []) + (['alt'] if alt else []) + (['shift'] if shift else []) + [code]
                    g.qmp.call('input-send-event', {'events': [
                        {'type': 'key', 'data': {'down': down, 'key': {'type': 'qcode', 'data': k}}}
                        for down, codes in [(True, keys), (False, list(reversed(keys)))] for k in codes]})
                    time.sleep(.06)
                g.qmp.key = key
                mouse = [0, 0]

                def click(name):
                    x, y = inspect()[name]
                    while mouse != [x, y]:
                        dx, dy = max(-100, min(100, x-mouse[0])), max(-100, min(100, y-mouse[1]))
                        g.qmp.call('input-send-event', {'events': [
                            {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                            {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
                        mouse[0] += dx
                        mouse[1] += dy
                        time.sleep(.04)
                    for down in (True, False):
                        g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                        time.sleep(.1)

                def load(action, marker='BROWSER_LOADED'):
                    offset = len(g.output())
                    action()
                    g.wait(marker, offset)

                def address(url):
                    click('addressField')
                    key('a', control=True)
                    g.qmp.type(url)
                    key('ret')

                inspect("assert 'Welcome to the HackOS web' in page()\nassert 'MUST NEVER' not in page()\nassert u('document.linkCount') == 4")
                g.screen('home')
                load(lambda: click('link:1'))
                inspect("assert text('current').endswith('/browser/guide.html')\nassert 'You followed a relative link.' in page()\nassert u('historyCount') == 1")
                g.screen('guide')
                load(lambda: click('Back'))
                inspect("assert text('current').endswith('/browser/')\nassert u('historyCount') == 0")
                load(lambda: click('Reload'))
                # Keyboard link selection, activation, and scroll to the final paragraph.
                key('right')
                key('right')
                load(lambda: key('ret'))
                inspect("assert text('current').endswith('/browser/long.html')")
                key('end')
                inspect("assert u('top') > 40\nassert 'END OF LONG PAGE' in page()")
                g.screen('scrolled')
                key('f10', alt=True)
                inspect("assert u('view.width') > 1080")
                key('f10', alt=True)
                inspect("assert u('view.width') == 1080")
                load(lambda: click('Home'))
                load(lambda: address('http://10.0.2.2:8080/missing'), 'BROWSER_ERROR')
                inspect("assert text('current').endswith('/browser/')\nassert '404' in text(field('statusLabel') + '.text')\nassert 'Welcome' in page()")
                address('https://example.com/')
                inspect("assert 'HTTPS' in text(field('statusLabel') + '.text')\nassert 'Welcome' in page()")
                address('http://10.0.2.2:8080/slow')
                click('Stop')
                inspect("assert 'stopped' in text(field('statusLabel') + '.text')\nassert 'Welcome' in page()")
                load(lambda: click('Home'))
                address('http://10.0.2.2:8080/slow')
                offset = len(g.output())
                key('f4', alt=True)
                g.wait('$ ', offset)
                g.shell('echo browser-closed', 'browser-closed')
                # A second process can immediately allocate a socket and load a page.
                g.enter('browser', 'BROWSER_LOADED')
                inspect("assert 'Welcome' in page()")
                offset = len(g.output())
                key('f4', alt=True)
                g.wait('$ ', offset)
            print(f'PASS {firmware}: browser HTTP, HTML, links, Back/Reload, address entry, scrolling/resize, errors, cancel/close and restart')
        finally:
            server.shutdown()
            thread.join(2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), default='bios')
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--timeout', type=int, default=180)
    args = parser.parse_args()
    test(args.firmware, args.image.resolve(), args.timeout)

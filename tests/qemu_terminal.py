#!/usr/bin/env python3
"""Terminal appearance, real glyph pixels, ordinary-user control and ABI bounds."""
import argparse
import shutil
import sys
import time
from common import ROOT, OUT, Report, command, require_success
from qemu_accounts import boot, ADMIN_PASSWORD, filesystem
from qemu_shell import build_probe
from session import provision_imports


def test(firmware, timeout):
    directory = OUT / ('qemu-terminal-' + firmware)
    report = Report('qemu-terminal-' + firmware, directory)
    built = OUT / ('build-' + firmware)
    image = directory / 'HackOS.img'

    def pixels(name, height, foreground, background):
        data = (directory / (name + '.ppm')).read_bytes()
        magic, dimensions, maximum, rgb = data.split(b'\n', 3)
        assert magic == b'P6' and maximum == b'255'
        width, screen_height = map(int, dimensions.split())
        assert len(rgb) == width * screen_height * 3
        font = (ROOT / 'util/fonts/zap-light16.psf').read_bytes()
        assert font[:2] == b'\x36\x04' and font[3] == 16
        cell_width = height // 2
        for y in range(height):
            for x in range(cell_width * 5):
                ch = b'Ag  Z'[x // cell_width]
                bits = font[4 + ch * 16 + y * 16 // height]
                expected = foreground if bits & (0x80 >> ((x % cell_width) * 8 // cell_width)) else background
                offset = (y * width + x) * 3
                assert rgb[offset:offset + 3] == bytes(expected), (name, x, y, rgb[offset:offset + 3], expected)
        return width, screen_height

    def run():
        shutil.copy2(built / 'HackOS.img', image)
        probe = build_probe(directory, built / 'source', extra=('terminal_settings_probe.cpp', 'TERMSET'))
        require_success(command(['mcopy', '-i', filesystem(image), probe, '::/USR/BIN/TERMSET']), 'install terminal probe')
        provision_imports(image, firmware)
        with boot(directory, image, firmware, 'terminal', timeout) as g:
            g.fresh = True
            g.setup()
            g.enter('root', 'New password: ')
            g.password(ADMIN_PASSWORD, 'Action: ')
            g.login('root', ADMIN_PASSWORD)
            g.shell('termset', 'TERMINAL_ABI_OK')
            g.shell('termset background &', 'TERMINAL_BACKGROUND_DENIED')
            g.shell('wait', '$ ')
            g.enter('passwd guest', 'New password: ')
            g.password('GuestPass123', 'Password saved.')
            g.logout()
            g.login('guest', 'GuestPass123')
            g.shell('termset', 'TERMINAL_ABI_OK')
            g.shell('termset background &', 'TERMINAL_BACKGROUND_DENIED')
            g.shell('wait', '$ ')
            g.shell('terminal --help', 'Usage: terminal')
            g.shell('terminal get | cat > /tmp/defaults', '$ ')
            g.shell('terminal get font_size | wc -c > /tmp/count', '$ ')
            # Move the hardware cursor away from the glyph comparison area.
            for _ in range(3):
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'rel', 'data': {'axis': 'x', 'value': 100}},
                    {'type': 'rel', 'data': {'axis': 'y', 'value': 100}}]})
                time.sleep(0.06)
            g.shell('terminal set font_size 32', '$ ')
            g.shell('terminal set bg_col 0x80402080', '$ ')
            g.shell('terminal set font_col 0xFFFFFF80', '$ ')
            g.shell('terminal get font_col > /tmp/rgba', '$ ')
            g.shell('terminal get rows > /tmp/rows', '$ ')
            g.shell('terminal get columns > /tmp/columns', '$ ')
            g.shell('terminal set font_size 64 &', 'foreground terminal required')
            g.shell('wait', '$ ')
            g.shell('terminal get font_size > /tmp/size', '$ ')
            g.enter('termset paint', 'TERMINAL_PAINT_READY')
            g.screen('large-alpha')
            width, height = pixels('large-alpha', 32, (160, 144, 136), (64, 32, 16))
            mark = len(g.output())
            g.qmp.key('q')
            g.wait('$ ', mark)
            g.shell('terminal set font_size 8', '$ ')
            g.enter('termset paint', 'TERMINAL_PAINT_READY')
            g.screen('small-alpha')
            pixels('small-alpha', 8, (160, 144, 136), (64, 32, 16))
            mark = len(g.output())
            g.qmp.key('q')
            g.wait('$ ', mark)
            g.shell('terminal set font_size 30', '$ ')
            g.shell('terminal set bg_col 0x101820ff', '$ ')
            g.shell('terminal set font_col 0xe8eef2ff', '$ ')
            g.enter('termset paint', 'TERMINAL_PAINT_READY')
            g.screen('theme')
            pixels('theme', 30, (232, 238, 242), (16, 24, 32))
            mark = len(g.output())
            g.qmp.key('q')
            g.wait('$ ', mark)
            g.shell('terminal get | more', 'font_size = 30')
            # Edit at the new cell size, then check this history after reset.
            mark = len(g.output())
            g.qmp.type('echo editedx')
            g.qmp.key('backspace')
            g.qmp.type('\n')
            g.wait('\nedited\n', mark)
            g.wait('$ ', mark)
            g.shell('terminal set font_col 00000000', '$ ')
            g.shell('window &', 'Window demo ready.')
            g.screen('window-native-font')
            data = (directory / 'window-native-font.ppm').read_bytes().split(b'\n', 3)[3]
            font = (ROOT / 'util/fonts/zap-light16.psf').read_bytes()
            for y in range(16):
                for x in range(48):
                    bits = font[4 + b'Window'[x // 8] * 16 + y]
                    offset = ((208 + y) * width + 210 + x) * 3
                    pixel = data[offset:offset + 3]
                    if bits & (0x80 >> (x % 8)):
                        assert pixel == b'\xff\xff\xff', ('window glyph', x, y, pixel)
                    else:
                        assert pixel in (b'\x36\x6b\x96', b'\x66\x66\x66'), ('window background', x, y, pixel)
            # Focus the window with the pointer at (300, 300), then close it.
            for down in (True, False):
                g.qmp.call('input-send-event', {'events': [
                    {'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
                time.sleep(0.1)
            mark = len(g.output())
            g.qmp.key('q')
            g.wait('Window demo closed.', mark)
            g.shell('wait', '$ ')
            g.shell('terminal reset', '$ ')
            g.shell('terminal get > /tmp/reset', '$ ')
            g.shell('history', 'echo edited')
            g.screen('reset')

        def read(name):
            return require_success(command(['mtype', '-i', filesystem(image), '::/TMP/' + name]), name)

        defaults = f'font_size = 16\nbg_col = 0x505050ff\nfont_col = 0x00ffffff\ncolumns = {width // 8}\nrows = {height // 16}\n'
        assert read('defaults') == read('reset') == defaults
        assert read('count') == '3\n' and read('rgba') == '0xffffff80\n'
        assert read('rows') == str(height // 32) + '\n' and read('columns') == str(width // 16) + '\n'
        assert read('size') == '32\n'
        return 'Root/guest ABI and CLI bounds; foreground enforcement; pipe/file bytes; 8/30/32px glyphs and alpha verified against framebuffer pixels.'

    report.check('terminal', run)
    return report.finish()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--firmware', choices=('bios', 'uefi'), required=True)
    parser.add_argument('--timeout', type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))

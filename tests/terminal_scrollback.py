"""End-to-end terminal history, viewport and clipboard checks in a desktop session."""
import time


def run(g, inspect, key, shell, mouse, built):
    prefix = "gdb.execute('add-symbol-file " + str(built / 'source/libs/term/bin/term') + "', to_string=True)\n" + '''
controller = next(p for p in processes() if int(p['pid']) == number('terminals[0].controller'))
def readTerm(expr):
    item = gdb.parse_and_eval(expr)
    address, size = int(item.address), item.type.sizeof
    data = b''
    while size:
        leaf = controller['userSpace']['leaf']['entries'][(address - 0x400000000000) // 4096]
        amount = min(size, 4096 - (address & 4095))
        data += bytes(gdb.selected_inferior().read_memory((int(leaf['address']) << 12) + (address & 4095), amount))
        address += amount
        size -= amount
    return data
def value(expr): return int.from_bytes(readTerm(expr), 'little')
'''

    def check(code):
        return inspect(prefix + code)

    shell('cat /tmp/scrollback.txt', 'SCROLL0599')
    check("""assert number('terminals[0].history.count') == 512
assert value('viewport.image.liveLine') > 512
assert value('viewport.image.firstLine') == value('viewport.image.liveLine')
assert b'SCROLL0599' in readTerm('viewport.image.cells')""")
    key('home', shift=True)
    check("""assert value('viewport.image.firstLine') == value('viewport.image.oldestLine')
assert value('viewport.image.liveLine') - value('viewport.image.firstLine') == 512
assert b'SCROLL0599' not in readTerm('viewport.image.cells')""")
    key('pgdn', shift=True)
    check("assert value('viewport.image.firstLine') == value('viewport.image.oldestLine') + value('viewport.image.state.rows')")
    key('end', shift=True)
    check("assert value('viewport.image.firstLine') == value('viewport.image.liveLine')")
    key('pgup', shift=True)
    check("assert value('viewport.image.firstLine') < value('viewport.image.liveLine')")
    key('esc')
    shell('clear', '$ ')
    shell('echo COPY_ME', 'COPY_ME')
    check("assert number('terminals[0].history.count') == 0\nassert b'COPY_ME' in readTerm('viewport.image.cells')")
    # The clear command leaves the prompt at row zero; echo's output is row one.
    coordinates = check("""assert readTerm('viewport.image.cells')[90:97] == b'COPY_ME'
w = gdb.parse_and_eval('KernelWindowManager->focused').dereference()
print('TERM_COORDS', int(w['startX']), int(w['startY']))""")
    import re
    match = re.search(r'TERM_COORDS (\d+) (\d+)', coordinates)
    x, y = int(match[1]), int(match[2])

    def move(tx, ty):
        while mouse != [tx, ty]:
            dx, dy = max(-100, min(100, tx - mouse[0])), max(-100, min(100, ty - mouse[1]))
            g.qmp.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
            mouse[0] += dx
            mouse[1] += dy
            time.sleep(0.04)

    move(x + 2, y + 30 + 16 + 2)
    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
    time.sleep(0.15)
    move(x + 6 * 8 + 2, y + 30 + 16 + 2)
    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
    check("assert value('viewport.selected') == 1\nassert value('viewport.anchor') == 90\nassert value('viewport.cursor') == 96")
    key('c', shift=True, control=True)
    check("""assert number('clipboardLength') == 7
assert bytes(gdb.selected_inferior().read_memory(gdb.parse_and_eval('clipboardData').address, 7)) == b'COPY_ME'
assert readTerm('notice').startswith(b'Copied 7 bytes')""")
    g.screen('terminal-selection-copy')
    key('f10', alt=True)
    check("assert value('viewport.selected') == 0\nassert value('viewport.image.state.columns') == 160")
    key('f10', alt=True)
    check("assert value('viewport.image.state.columns') == 90")
    g.qmp.type('echo ')
    key('v', shift=True, control=True)
    check("assert readTerm('notice').startswith(b'Pasted text')\nassert b'echo COPY_ME' in readTerm('viewport.image.cells')[180:270]")
    mark = len(g.output())
    key('ret')
    g.wait('COPY_ME', mark)
    g.wait('$ ', mark)
    g.screen('terminal-paste')
    shell('cat /tmp/scrollback.txt', 'SCROLL0599')
    check("assert b'SCROLL0599' in readTerm('viewport.image.cells')\nassert value('viewport.image.firstLine') == value('viewport.image.liveLine')")
    move(x + 20, y + 80)
    for down in (True, False):
        g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'wheel-up', 'down': down}}]})
    check("assert value('viewport.image.liveLine') - value('viewport.image.firstLine') == 3, 'wheel offset: ' + str(value('viewport.image.liveLine') - value('viewport.image.firstLine'))")
    key('home', shift=True)
    check("assert value('viewport.image.firstLine') == value('viewport.image.oldestLine')")
    g.screen('terminal-scrollback')
    shell('echo LIVE_AGAIN', 'LIVE_AGAIN')
    check("assert value('viewport.image.firstLine') == value('viewport.image.liveLine')")

    shell('terminal set font_size 32', '$ ')
    check("assert number('terminals[0].appearance.height') == 32\nassert value('appearance.fontSize') == 32\nassert value('viewport.image.state.columns') == 45\nassert value('viewport.image.state.rows') == 13")
    shell('terminal set bg_col 203040ff', '$ ')
    shell('terminal set font_col a0e0ffff', '$ ')
    check("assert value('appearance.bgColour') == 0x203040ff\nassert value('appearance.fontColour') == 0xa0e0ffff")
    shell('terminal set font_size 64 &', 'foreground terminal required')
    check("assert number('terminals[0].appearance.height') == 32")
    g.screen('terminal-appearance')
    key('esc', alt=True)
    key('ret')
    inspect("assert number('terminals[1].controller') != 0\nassert number('terminals[0].appearance.height') == 32\nassert number('terminals[1].appearance.height') == 16\nassert number('terminals[1].appearance.background') == 0x101b25ff")
    key('f4', alt=True)
    move(x + 100, y + 15)
    for down in (True, False):
        g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(0.1)
    check("assert number('KernelWindowManager->focused->owner') == number('terminals[0].controller')")
    shell('terminal set font_size default', '$ ')
    check("assert value('appearance.fontSize') == 16\nassert value('appearance.bgColour') == 0x203040ff")
    shell('terminal reset', '$ ')
    check("assert value('appearance.fontSize') == 16\nassert value('appearance.bgColour') == 0x101b25ff\nassert value('appearance.fontColour') == 0xe5eef5ff")

    shell('cat /tmp/ansi.txt', 'ANSI_END')
    check("""assert readTerm('viewport.image.cells')[:14] == b'RED GREEN BLUE'
assert value('viewport.styles[0]') == 2
assert value('viewport.styles[4]') == 3
assert value('viewport.styles[10]') == 5
assert value('viewport.styles[90]') == 1024
assert value('viewport.styles[95]') == 2048
assert value('viewport.styles[101]') == 4096
assert readTerm('viewport.image.cells')[180:189] == b'CURSOR_OK'
assert value('viewport.styles[270]') == 0""")
    g.screen('terminal-ansi-colours')
    key('f10', alt=True)
    check("assert value('viewport.image.state.columns') == 160\nassert value('viewport.styles[0]') == 2\nassert value('viewport.styles[160]') == 1024")
    key('f10', alt=True)
    check("assert value('viewport.image.state.columns') == 90\nassert value('viewport.styles[90]') == 1024")
    move(x + 2, y + 32)
    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': True}}]})
    time.sleep(0.15)
    move(x + 18, y + 32)
    g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': False}}]})
    check("assert value('viewport.selected') == 1\nassert value('viewport.cursor') == 2")
    key('c', shift=True, control=True)
    inspect("assert number('clipboardLength') == 3\nassert bytes(gdb.selected_inferior().read_memory(gdb.parse_and_eval('clipboardData').address, 3)) == b'RED'")
    shell('cat /tmp/ansiscroll.txt', 'ROW0599')
    check("assert b'ROW0599' in readTerm('viewport.image.cells')")
    key('home', shift=True)
    check("""assert value('viewport.image.firstLine') == value('viewport.image.oldestLine')
assert readTerm('viewport.image.cells').startswith(b'ROW')
assert value('viewport.styles[0]') == 2
assert value('viewport.styles[90]') == 2""")
    g.screen('terminal-ansi-scrollback')
    key('end', shift=True)

    # Measure a real typed character, then force an ordinary full repaint and
    # compare every client pixel with the retained result.
    shell('clear', '$ ')
    check("assert value('viewport.image.liveLine') == 0\nassert readTerm('viewport.image.cells').startswith(b'root@root:~$ ')\nassert readTerm('viewport.image.cells')[90:] == b' ' * (9600 - 90)")
    def paint_state(after=0):
        import json
        output = check("""import hashlib
w = gdb.parse_and_eval('KernelWindowManager->focused').dereference()
assert int(w['owner']) == number('terminals[0].controller') and not int(w['transaction'])
assert value('damage.valid')
assert readTerm('damage.painted.image.cells') == readTerm('viewport.image.cells')
assert value('damage.painted.image.state.x') == value('viewport.image.state.x')
assert value('terminal_paint_frames') > """ + str(after) + """
front = w['front']
frame = bytes(gdb.selected_inferior().read_memory(front['address'], int(front['pitch']) * int(front['height']) * 4))
print('TERMINAL_PAINT ' + json.dumps(dict(frames=value('terminal_paint_frames'), full=value('terminal_paint_full_frames'),
    pixels=value('terminal_paint_pixels'), uploads=value('terminal_paint_uploads'), last=value('terminal_last_pixels'),
    checksum=hashlib.sha256(frame).hexdigest())))""")
        return json.loads(next(line[15:] for line in output.splitlines() if line.startswith('TERMINAL_PAINT ')))
    before = paint_state()
    key('a')
    after = paint_state(before['frames'])
    assert after['full'] == before['full'], (before, after)
    assert 0 < after['pixels'] - before['pixels'] < 720 * 432 // 16, (before, after)
    check("""address = number('(unsigned long)&damage.valid')
leaf = controller['userSpace']['leaf']['entries'][(address - 0x400000000000) // 4096]
gdb.selected_inferior().write_memory((int(leaf['address']) << 12) + (address & 4095), bytes(1))
gdb.execute('set terminals[0].revision = terminals[0].revision + 1')""")
    full = paint_state(after['frames'])
    assert full['full'] == after['full'] + 1 and full['checksum'] == after['checksum'], (after, full)
    import json
    (g.directory / 'terminal-damage.json').write_text(json.dumps({'before': before, 'typed': after, 'forced_full': full}, indent=2) + '\n')
    key('backspace')

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

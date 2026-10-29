"""Sustained mixed desktop workload used by qemu_desktop --stress-cycles."""
import json
import time


def run(g, inspect, key, click, shell, mouse, cycles):
    def parsed(checks):
        output = inspect(checks)
        return json.loads(next(line[13:] for line in output.splitlines() if line.startswith('STRESS_STATE ')))

    def checkpoint(expected=None):
        checks = '''ps = processes()
assert len(ps) == 3, [(p['name'].string(), int(p['state'])) for p in ps]
assert number('user_windows_live') == 2
assert all(number('terminals[' + str(i) + '].controller') == 0 for i in range(8))
assert not number('display.dirty')
assert all(int(p['state']) != 0 for p in ps if p['name'].string() != 'idle')
wm = gdb.parse_and_eval('KernelWindowManager')
assert all(not int(wm['windows']['ptr'][i]['transaction']) for i in range(2))
heap = gdb.parse_and_eval('(HeapSegmentHeader*)heapStart')
heap_bytes = heap_blocks = 0
while int(heap):
    if not int(heap['free']):
        heap_bytes += int(heap['length'])
        heap_blocks += 1
    heap = heap['next']
stats = {name: number(name) for name in ['user_windows_live', 'window_surface_pages', 'user_pages_live', 'user_files_live', 'user_streams_live', 'user_pipes_live']}
stats.update(processes=len(ps), heap_bytes=heap_bytes, heap_blocks=heap_blocks,
             physical_used=number('PageFrameAllocator::s_Allocator->usedMemory'),
             descriptors=sum(bool(int(p['descriptors'][i])) for p in ps for i in range(16)),
             file_handles=sum(bool(int(p['files'][i])) for p in ps for i in range(8)))
'''
        if expected is not None:
            checks += 'assert stats == ' + repr(expected) + ', stats\n'
        return parsed(checks + "print('STRESS_STATE ' + json.dumps(stats))")

    def move(x, y, typing=False):
        while mouse != [x, y]:
            dx, dy = max(-50, min(50, x - mouse[0])), max(-50, min(50, y - mouse[1]))
            g.qmp.call('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
            mouse[0] += dx
            mouse[1] += dy
            if typing:
                key('a')
            time.sleep(0.02)

    def button(down):
        g.qmp.call('input-send-event', {'events': [{'type': 'btn', 'data': {'button': 'left', 'down': down}}]})

    def focus(title):
        for attempt in range(17):
            current = parsed("w = gdb.parse_and_eval('KernelWindowManager->focused')\nprint('STRESS_STATE ' + json.dumps(w['title'].string() if int(w) else ''))")
            if current == title or (title == 'Terminal' and current.startswith('Terminal ')):
                return
            key('tab', alt=True)
        raise AssertionError('Cannot focus ' + title)

    def settled_editor(close=False):
        checks = """w = gdb.parse_and_eval('KernelWindowManager->focused')
assert w['title'].string() == 'Text editor'
p = next(p for p in processes() if int(p['pid']) == int(w['owner']))
assert int(p['state']) == 6 and not int(w['events']['count'])
assert not number('mouseEvents.count') and not int(w['transaction'])
assert not int(w['modifiers']) and all(int(w['pressedKeys'][i]) == 0 for i in range(4))
"""
        if close:
            checks += "assert int(w['closePending']) and int(w['closeDelivered'])\n"
        inspect(checks)

    def geometry():
        return parsed('''w = gdb.parse_and_eval('KernelWindowManager->focused')
assert w['title'].string() == 'Text editor'
print('STRESS_STATE ' + json.dumps([int(w[f]) for f in ['startX', 'startY', 'sizeX', 'sizeY']]))''')

    # Drop the initial terminal; the stable baseline contains only desktop + login.
    key('f4', alt=True)
    checkpoint()
    baseline = None
    records = []
    started = time.monotonic()
    for cycle in range(cycles + 1):
        click(50, 1000)
        inspect("assert number('terminals[0].controller') != 0\nassert 'root@root:~$ ' in grid(0)")
        shell('echo stress | tr a-z A-Z > /tmp/stress', '$ ')
        shell('cat /tmp/stress', 'STRESS')
        for x, marker in [(600, 'FILES_READY'), (750, 'SETTINGS_READY'), (665, 'EDITOR_READY')]:
            mark = len(g.output())
            click(x, 1000)
            g.wait(marker, mark)
        inspect("assert number('user_windows_live') == 6")
        click(300, 250)
        g.qmp.type('mixed workload\n')
        x, y, w, h = geometry()
        move(x + 140, y + 12)
        button(True)
        move(x + 220, y + 52, typing=True)
        button(False)
        inspect("assert not number('KernelWindowManager->capture')")
        moved = geometry()
        assert moved == [x + 80, y + 40, w, h], moved
        x, y, w, h = moved
        move(x + w - 4, y + h - 4)
        button(True)
        move(x + w + 36, y + h + 16, typing=True)
        button(False)
        inspect("assert not number('KernelWindowManager->capture')")
        assert geometry() == [x, y, w + 40, h + 20]
        # Overflow while dragging must cancel capture and recover held input.
        x, y, w, h = geometry()
        move(x + 140, y + 12)
        button(True)
        g.qmp.call('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': True, 'key': {'type': 'qcode', 'data': 'shift'}}}]})
        inspect("gdb.execute('set mouseEvents.overflow = true')")
        button(False)
        g.qmp.call('input-send-event', {'events': [
            {'type': 'key', 'data': {'down': False, 'key': {'type': 'qcode', 'data': 'shift'}}}]})
        inspect("""w = gdb.parse_and_eval('KernelWindowManager->focused')
assert not number('KernelWindowManager->capture')
assert not int(w['mouseButtons']) and not int(w['modifiers'])
assert all(int(w['pressedKeys'][i]) == 0 for i in range(4))""")
        key('b')
        key('f10', alt=True)
        key('f10', alt=True)
        settled_editor()
        if cycle % 2:
            key('esc', alt=True, control=True)
        else:
            key('f4', alt=True)
            settled_editor(close=True)
            key('tab', shift=True)  # Cancel -> Discard through supported dialog navigation.
            settled_editor(close=True)
            key('ret')
        inspect("assert number('user_windows_live') == 5")
        for title in ('Settings', 'Files'):
            focus(title)
            key('f4', alt=True)
        inspect("assert number('user_windows_live') == 3")
        # Saturate the endpoint limit, reject the ninth controller, then reclaim all.
        for count in range(2, 9):
            click(50, 1000)
            inspect("assert sum(number('terminals[' + str(i) + '].controller') != 0 for i in range(8)) == " + str(count))
        mark = len(g.output())
        click(50, 1000)
        g.wait('exited 1', mark)
        inspect("assert number('user_windows_live') == 10")
        for count in range(7, -1, -1):
            focus('Terminal')
            key('f4', alt=True)
            inspect("assert sum(number('terminals[' + str(i) + '].controller') != 0 for i in range(8)) == " + str(count))
        state = checkpoint(baseline)
        if baseline is None:
            baseline = state  # Warm caches and the heap before measuring repeatability.
        records.append({'cycle': cycle, 'warmup': cycle == 0, 'resources': state,
                        'elapsed_seconds': round(time.monotonic() - started, 3)})
        (g.directory / 'stress-resources.json').write_text(json.dumps(records, indent=2) + '\n')
        print('STRESS_CYCLE ' + str(cycle) + ' resource baseline restored', flush=True)
    g.screen('stress-final')
    return 'Mixed desktop stress: ' + str(cycles) + ' measured cycles plus warmup; resource baseline restored each time.'

#!/usr/bin/env python3
"""Exercise owned userspace windows, rendering and lifecycle in QEMU."""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import threading
from common import ROOT, OUT, Report, command, require_success
from qemu_shell import QMP, build_probe


def test(firmware, timeout):
    directory = OUT/("qemu-windows-"+firmware)
    report = Report("qemu-windows-"+firmware, directory)
    built = OUT/("build-"+firmware)
    console = directory/"console.log"
    console.write_text("")

    def run():
        programs = [build_probe(directory, built/"source", extra=("window_probe.cpp", "WINPROBE"))]
        symbols = require_success(command([os.environ.get("CROSS_COMPILE", "x86_64-elf-") + "nm", directory/"WINPROBE"]), "probe symbols")
        gate = int(re.search(r"^([0-9a-f]+) [A-Za-z] event_gate$", symbols, re.M)[1], 16)
        image = directory/"HackOS.img"
        shutil.copy2(built/"HackOS.img", image)
        fs_image = str(image) + ("@@"+str(334*512) if firmware == "bios" else "")
        for program in programs:
            require_success(command(["mcopy", "-o", "-i", fs_image, program, "::/USR/BIN/"+program.name]), "install test program in disposable image")
        with tempfile.TemporaryDirectory(prefix="hackos-windows-") as temporary:
            temporary = Path(temporary)
            qmp_path, gdb_path = temporary/"qmp", temporary/"gdb"
            args = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64",
                    "-m", "1G", "-smp", "cores=4,threads=1,sockets=1", "-display", "none",
                    "-monitor", "none", "-serial", "none", "-net", "none", "-no-reboot", "-snapshot",
                    "-drive", f"file={image},format=raw", "-usb", "-device", "usb-ehci,id=ehci",
                    "-device", "qemu-xhci,id=xhci", "-qmp", f"unix:{qmp_path},server=on,wait=off",
                    "-gdb", f"unix:{gdb_path},server=on,wait=off", "-debugcon", f"file:{console}",
                    "-global", "isa-debugcon.iobase=0xe9", "-d", "guest_errors", "-D", str(directory/"guest.log")]
            if firmware == "uefi":
                ovmf = ROOT/"boot/UEFI/OVMFbin"
                variables = directory/"OVMF_VARS.fd"
                shutil.copy2(ovmf/"OVMF_VARS-pure-efi.fd", variables)
                args += ["-drive", f"if=pflash,format=raw,unit=0,file={ovmf/'OVMF_CODE-pure-efi.fd'},readonly=on",
                         "-drive", f"if=pflash,format=raw,unit=1,file={variables}"]
            with (directory/"qemu.log").open("w") as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
                qmp = None
                try:
                    def wait_for(predicate, description, allowance=None):
                        deadline = time.monotonic() + (timeout if allowance is None else allowance)
                        while time.monotonic() < deadline:
                            if process.poll() is not None:
                                raise AssertionError("QEMU exited before " + description)
                            text = output()
                            if "PROBE_FAIL" in text or "Exception:" in text:
                                raise AssertionError("Guest reported a failure; see console.log")
                            if predicate(): return
                            time.sleep(0.05)
                        raise AssertionError("Timed out waiting for " + description + "; see console.log")
                    def output(): return console.read_text(errors="replace")
                    def wait_text(text, offset=0, allowance=None):
                        wait_for(lambda: text in output()[offset:], repr(text), allowance)
                    wait_for(qmp_path.exists, "QMP socket")
                    qmp = QMP(qmp_path)
                    wait_text("root@root:~$ ")

                    qmp.call("stop")
                    boot_script = directory/"shell-entry.gdb"
                    boot_script.write_text("set pagination off\nset confirm off\ntarget remote " + str(gdb_path) + "\npython\nimport gdb\n" + """
bp = gdb.Breakpoint("syscall_handler", type=gdb.BP_HARDWARE_BREAKPOINT)
gdb.execute("continue")
r = gdb.parse_and_eval("r").dereference()
p = gdb.parse_and_eval("current_process")
assert int(p["pid"]) == 2 and int(p["systemShell"]) and int(p["userSpace"])
assert int(r["cs"]) == 0x1b and int(r["ss"]) == 0x23
assert int(gdb.parse_and_eval("$cr3")) == int(p["pageTable"]["pml4"])
print("SHELL_CPL3_OK")
bp.delete()
""" + "end\ndetach\nquit\n")
                    result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q", built/"kernel.elf", "-x", boot_script], timeout=20)
                    (directory/"shell-entry.log").write_text(result.stdout)
                    require_success(result, "inspect shell privilege")
                    assert "SHELL_CPL3_OK" in result.stdout, result.stdout
                    qmp.call("cont")

                    def enter(text, expected):
                        offset = len(output())
                        qmp.type(text + "\n")
                        wait_text(expected, offset)
                        wait_text("root@root:~$ ", offset)

                    def inspect(name, assertions):
                        qmp.call("stop")
                        script = directory/(name + ".gdb")
                        script.write_text("set pagination off\nset confirm off\ntarget remote " + str(gdb_path) + "\npython\nimport gdb\n" + """
bp = gdb.Breakpoint("ReapExitedProcesses()", type=gdb.BP_HARDWARE_BREAKPOINT)
# Allow input dispatch, deferred scheduling, then zombie reclamation/redraw.
gdb.execute("continue")
gdb.execute("continue")
gdb.execute("continue")
# Inspect a completed composition, not a transient dirty flag between user
# uploads. An overflow recovery can start a second repaint after the first log.
bp.delete()
if int(gdb.parse_and_eval("display.dirty")):
    flush = gdb.Breakpoint("Display::DrawBackbuffer()", type=gdb.BP_HARDWARE_BREAKPOINT)
    gdb.execute("continue")
    flush.delete()
    gdb.execute("finish")
assert not int(gdb.parse_and_eval("display.dirty")), "Display did not flush its frame"
display = gdb.parse_and_eval("display")
fb = display["framebuffer"]
back = display["backbuffer"]
""" + assertions + "\nprint('WINDOW_STATE_OK')\nend\ndetach\nquit\n")
                        result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q",
                                          built/"kernel.elf", "-x", script], timeout=30)
                        (directory/(name + ".log")).write_text(result.stdout)
                        require_success(result, "inspect window state")
                        assert "WINDOW_STATE_OK" in result.stdout, result.stdout
                        qmp.call("cont")

                    mouse_x = 0
                    mouse_y = 0

                    def move(x, y):
                        nonlocal mouse_x, mouse_y
                        while mouse_x != x or mouse_y != y:
                            dx = max(-100, min(100, x - mouse_x))
                            dy = max(-100, min(100, y - mouse_y))
                            qmp.call("input-send-event", {"events": [
                                {"type": "rel", "data": {"axis": "x", "value": dx}},
                                {"type": "rel", "data": {"axis": "y", "value": dy}}]})
                            mouse_x += dx
                            mouse_y += dy
                            time.sleep(0.06)

                    def button(down):
                        qmp.call("input-send-event", {"events": [
                            {"type": "btn", "data": {"button": "left", "down": down}}]})
                        time.sleep(0.15)

                    def click(x, y):
                        move(x, y)
                        button(True)
                        button(False)

                    def wheel(direction):
                        for down in (True, False):
                            qmp.call("input-send-event", {"events": [
                                {"type": "btn", "data": {"button": "wheel-" + direction, "down": down}}]})
                        time.sleep(0.15)

                    def snapshot(name):
                        qmp.call("stop")
                        qmp.call("screendump", {"filename": str(directory/(name + ".ppm"))})
                        qmp.call("cont")

                    def launch():
                        offset = len(output())
                        qmp.type("window\n")
                        wait_text("Window demo ready.", offset)
                        return offset

                    offset = launch()
                    inspect("draw", """
assert int(gdb.parse_and_eval("user_windows_live")) == 1, "Missing owned window"
assert int(fb["address"][245 * int(fb["pitch"]) + 215]) == 0xffff00ff, "Missing magenta client pixels"
assert int(back["address"][245 * int(back["pitch"]) + 215]) != 0xffff00ff, "Window overwrote console backbuffer"
p = gdb.parse_and_eval("ready_queue")
while int(p) and p["name"].string() != "~/USR/BIN/WINDOW":
    p = p["next"]
assert int(p) and int(p["userSpace"]), "Demo lacks private address space"
bp = gdb.Breakpoint("syscall_handler", type=gdb.BP_HARDWARE_BREAKPOINT)
bp.condition = "current_process->pid == " + str(int(p["pid"]))
gdb.execute("continue")
frame = gdb.parse_and_eval("r")
assert int(frame["cs"]) & 3 == 3, "Window calls did not originate at CPL3"
bp.delete()
""")
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"window.ppm")})
                    qmp.call("cont")
                    # Drag without crossing a window border, then resize the client.
                    move(230, 215)
                    button(True)
                    move(310, 265)
                    button(False)
                    wait_text("WINDOW_MOVE 280 250", offset)
                    move(476, 446)
                    button(True)
                    move(516, 476)
                    button(False)
                    wait_text("WINDOW_RESIZE 240 200", offset)
                    wait_text("WINDOW_PAINT 240 200", offset)
                    inspect("geometry", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["startX"]) == 280 and int(w["startY"]) == 250
assert int(w["sizeX"]) == 240 and int(w["sizeY"]) == 230
assert int(gdb.parse_and_eval("KernelWindowManager->capture")) == 0
""")
                    key_offset = len(output())
                    qmp.type("a")
                    wait_text("WINDOW_KEY 97 0", key_offset)
                    wait_text("WINDOW_PAINT 240 200", key_offset)
                    inspect("key-paint", """
assert int(fb["address"][295 * int(fb["pitch"]) + 295]) == (0xff000000 | (97 * 0x12345 & 0xffffff))
""")
                    key_offset = len(output())
                    click(330, 330)
                    wait_text("WINDOW_MOUSE 50 50", key_offset)
                    wait_text("WINDOW_PAINT 240 200", key_offset)
                    inspect("mouse-paint", """
assert int(fb["address"][330 * int(fb["pitch"]) + 330]) == 0xffffffff
""")
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"interactive.ppm")})
                    qmp.call("cont")
                    # A captured client release outside the window must arrive.
                    move(335, 335)
                    button(True)
                    move(600, 550)
                    button(False)
                    inspect("capture-release", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["mouseButtons"]) == 0
assert int(w["mouseX"]) == 320 and int(w["mouseY"]) == 270
assert int(gdb.parse_and_eval("KernelWindowManager->capture")) == 0
""")
                    wheel_offset = len(output())
                    move(350, 350)
                    wheel("up")
                    wait_text("WINDOW_WHEEL -1 0", wheel_offset)
                    wheel("down")
                    wait_text("WINDOW_WHEEL 1 0", wheel_offset)
                    inspect("wheel-device", """
assert int(gdb.parse_and_eval("mouseDevice")) == 3
""")
                    mode_offset = len(output())
                    paint_started = time.monotonic()
                    click(481, 265)
                    moving = threading.Event()
                    moving.set()
                    motion_errors = []

                    def keep_moving():
                        try:
                            while moving.is_set():
                                for dx in (1, -1):
                                    qmp.call("input-send-event", {"events": [
                                        {"type": "rel", "data": {"axis": "x", "value": dx}}]})
                                    time.sleep(0.005)
                        except Exception as error:
                            motion_errors.append(error)

                    motion = threading.Thread(target=keep_moving)
                    motion.start()
                    try:
                        wait_text("WINDOW_MODE 2 0", mode_offset)
                        wait_text("WINDOW_PAINT 1918 1020", output().index("WINDOW_MODE 2 0", mode_offset))
                    finally:
                        moving.clear()
                        motion.join(timeout=5)
                    assert not motion.is_alive() and not motion_errors, motion_errors
                    (directory/"maximized-seconds.txt").write_text(str(time.monotonic() - paint_started) + "\n")
                    inspect("maximized", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["maximised"]) and not int(w["minimised"])
assert int(w["startX"]) == 1 and int(w["startY"]) == 1
assert int(w["sizeX"]) == 1918 and int(w["sizeY"]) == 1050
assert int(w["drawArea"]["address"][900 * 1918 + 1500]) != 0
assert int(fb["address"][931 * int(fb["pitch"]) + 1501]) == int(w["drawArea"]["address"][900 * 1918 + 1500])
""")
                    snapshot("maximized")
                    mode_offset = len(output())
                    click(1856, 16)
                    wait_text("WINDOW_MODE 3 0", mode_offset)
                    inspect("minimized", """
assert int(gdb.parse_and_eval("KernelWindowManager->focused")) == 0
w = gdb.parse_and_eval("KernelWindowManager->windows.ptr[0]")
assert int(w["minimised"]) and int(w["maximised"])
assert int(fb["address"][931 * int(fb["pitch"]) + 1501]) == int(back["address"][931 * int(back["pitch"]) + 1501])
""")
                    snapshot("minimized")
                    wheel_offset = len(output())
                    wheel("up")
                    click(60, 1066)
                    wait_text("WINDOW_MODE 2 0", wheel_offset)
                    assert "WINDOW_WHEEL" not in output()[wheel_offset:]
                    wait_text("WINDOW_PAINT 1918 1020", output().index("WINDOW_MODE 2 0", wheel_offset))
                    mode_offset = len(output())
                    qmp.type("r")
                    wait_text("WINDOW_MODE 0 0", mode_offset)
                    wait_text("WINDOW_PAINT 240 200", output().index("WINDOW_MODE 0 0", mode_offset))
                    inspect("normal-restored", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert not int(w["minimised"]) and not int(w["maximised"])
assert int(w["startX"]) == 280 and int(w["startY"]) == 250
assert int(w["sizeX"]) == 240 and int(w["sizeY"]) == 230
""")
                    snapshot("normal-restored")
                    # Oversize and undersize drags are bounded by the ABI limits.
                    move(516, 476)
                    button(True)
                    move(900, 800)
                    button(False)
                    wait_text("WINDOW_RESIZE 320 240", offset)
                    wait_text("WINDOW_PAINT 320 240", offset)
                    inspect("large-client-paint", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["drawArea"]["address"][220 * 320 + 300]) == (0xff000060 | (300 * 255 // 320) << 16 | (220 * 255 // 240) << 8)
""")
                    move(596, 516)
                    button(True)
                    move(290, 290)
                    button(False)
                    wait_text("WINDOW_RESIZE 96 30", offset)
                    inspect("resize-limits", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["sizeX"]) == 96 and int(w["sizeY"]) == 60
""")
                    move(290, 265)
                    button(True)
                    move(0, 0)
                    button(False)
                    inspect("drag-limits", """
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["startX"]) == 1 and int(w["startY"]) == 1
""")
                    qmp.type("q")
                    wait_text("Window demo closed.", offset)
                    wait_text("root@root:~$ ", offset)
                    inspect("restore", """
assert int(gdb.parse_and_eval("user_windows_live")) == 0
assert int(fb["address"][245 * int(fb["pitch"]) + 215]) == int(back["address"][245 * int(back["pitch"]) + 215])
""")

                    offset = launch()
                    qmp.key("c", control=True)
                    wait_text("^C\nroot@root:~$ ", offset)

                    offset = launch()
                    click(385, 215)
                    wait_text("Window demo closed.", offset)
                    wait_text("root@root:~$ ", offset)

                    # Two independent owners: focus raises a window and routes keys
                    # without leaking them to the shell or the unfocused owner.
                    enter("window &", "Window demo ready.")
                    click(20, 900)
                    inspect("desktop-focus", """
assert int(gdb.parse_and_eval("KernelWindowManager->focused")) == 0
""")
                    enter("window second &", "Window demo ready.")
                    click(250, 300)
                    inspect("focus-first", """
manager = gdb.parse_and_eval("KernelWindowManager")
assert int(manager["windows"]["_size"]) == 2
assert int(manager["focused"]["startX"]) == 200
assert int(manager["windows"]["ptr"][1]) == int(manager["focused"])
assert int(manager["windows"]["ptr"][0]["owner"]) != int(manager["focused"]["owner"])
""")
                    key_offset = len(output())
                    qmp.type("b")
                    wait_text("WINDOW_KEY 98 0", key_offset)
                    wait_text("WINDOW_PAINT 200 170", key_offset)
                    inspect("key-isolation", """
manager = gdb.parse_and_eval("KernelWindowManager")
other = manager["windows"]["ptr"][0]
active = manager["focused"]
assert int(other["drawArea"]["address"][15 * 200 + 15]) == 0xffff00ff
assert int(active["drawArea"]["address"][15 * 200 + 15]) == (0xff000000 | (98 * 0x12345 & 0xffffff))
""")
                    assert output()[key_offset:].count("WINDOW_KEY 98 0") == 1
                    wheel_offset = len(output())
                    move(500, 330)
                    wheel("up")
                    wait_text("WINDOW_WHEEL -1 0", wheel_offset)
                    wait_text("WINDOW_PAINT 200 170", output().index("WINDOW_WHEEL -1 0", wheel_offset))
                    inspect("wheel-isolation", """
manager = gdb.parse_and_eval("KernelWindowManager")
assert int(manager["focused"]["startX"]) == 200
other = manager["windows"]["ptr"][0]
assert int(other["startX"]) == 450
assert int(other["drawArea"]["address"][15 * 200 + 15]) != 0xffff00ff
""")
                    mode_offset = len(output())
                    qmp.type("m")
                    wait_text("WINDOW_MODE 1 0", mode_offset)
                    inspect("minimize-focus-fallback", """
assert int(gdb.parse_and_eval("KernelWindowManager->focused->startX")) == 450
""")
                    click(60, 1066)
                    wait_text("WINDOW_MODE 0 0", mode_offset)
                    inspect("restore-focus", """
assert int(gdb.parse_and_eval("KernelWindowManager->focused->startX")) == 200
""")
                    key_offset = len(output())
                    qmp.type("q")
                    wait_text("Window demo closed.", key_offset)
                    inspect("focus-fallback", """
assert int(gdb.parse_and_eval("KernelWindowManager->focused->startX")) == 450
""")
                    qmp.key("c", control=True)
                    inspect("background-interrupt", """
assert int(gdb.parse_and_eval("user_windows_live")) == 0
assert int(gdb.parse_and_eval("KernelWindowManager->focused")) == 0
assert int(gdb.parse_and_eval("terminalOwner")) == 2
""")
                    enter("print focus_returned", "\nfocus_returned\n")

                    offset = len(output())
                    qmp.type("./USR/BIN/WINPROBE events\n")
                    wait_text("WINDOW_EVENTS_READY", offset)
                    qmp.type("x" * 100)
                    inspect("event-overflow", """
import struct
w = gdb.parse_and_eval("KernelWindowManager->focused")
assert int(w["events"]["count"]) == 64
assert int(w["events"]["overflow"]) == 1
p = gdb.parse_and_eval("ready_queue")
while int(p) and int(p["pid"]) != int(w["owner"]):
    p = p["next"]
va = """ + str(gate) + """
entry = p["userSpace"]["leaf"]["entries"][(va - 0x400000000000) // 4096]
physical = (int(entry["address"]) << 12) + (va & 4095)
gdb.selected_inferior().write_memory(physical, struct.pack("<Q", 1))
""")
                    wait_text("WINDOW_EVENTS_OK", offset)
                    wait_text("root@root:~$ ", offset)

                    offset = len(output())
                    qmp.type("./USR/BIN/WINPROBE hold\n")
                    wait_text("WINDOW_HELD", offset)
                    qmp.type("x" * 100)
                    qmp.key("c", control=True)
                    wait_text("^C\nroot@root:~$ ", offset)

                    enter("./USR/BIN/WINPROBE", "WINDOW_API_OK")
                    enter("./USR/BIN/WINPROBE fault", "Program exited with status 142.")
                    enter("./USR/BIN/WINPROBE minimized &", "WINDOW_HELD")
                    click(20, 900)
                    enter("ps", "WINPROBE")
                    pid = re.findall(r"Process: ~/USR/BIN/WINPROBE \((\d+)\) ALIVE", output())[-1]
                    enter("kill " + pid, "Task stopped.")
                    inspect("cleanup", """
assert int(gdb.parse_and_eval("user_windows_live")) == 0
assert int(gdb.parse_and_eval("KernelWindowManager->windows._size")) == 0
assert int(gdb.parse_and_eval("tasks_reaped")) == 14
p = gdb.parse_and_eval("ready_queue")
labels = []
while int(p):
    labels.append(p["name"].string())
    if int(p["systemShell"]):
        assert int(gdb.parse_and_eval("user_pages_live")) == int(p["userSpace"]["ownedCount"])
    assert all(int(h) == 0 for h in [p["windows"][i] for i in range(4)])
    p = p["next"]
assert labels == ["idle", "~/USR/BIN/SHELL"]
assert int(fb["address"][245 * int(fb["pitch"]) + 215]) == int(back["address"][245 * int(back["pitch"]) + 215])
""")
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"closed.ppm")})
                    qmp.call("cont")
                    qmp.type("shutdown\n")
                    process.wait(timeout=timeout)
                    assert process.returncode == 0
                    return "CPL3 window demo, pixel composition/restoration, Q and PS/2 close, bounded drag/resize, maximize/minimize/restore geometry and focus, signed wheel routing, minimized-owner cleanup, client mouse capture/release, focus/z-order and key isolation, event overflow recovery, background Ctrl-C, checked pointers/rectangles/events, per-process quota, foreign/stale handles, exit/fault/kill cleanup and shutdown."
                finally:
                    if qmp: qmp.close()
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    report.check("userspace_windows", run)
    return report.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=("bios", "uefi"), required=True)
    parser.add_argument("--timeout", type=int, default=60, help="maximum wait per milestone")
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))

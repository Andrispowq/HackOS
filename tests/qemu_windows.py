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
                    def wait_for(predicate, description):
                        deadline = time.monotonic() + timeout
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
                    def wait_text(text, offset=0):
                        wait_for(lambda: text in output()[offset:], repr(text))
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
# Two idle passes ensure the preceding pass reaped zombies and flushed drawing.
gdb.execute("continue")
gdb.execute("continue")
bp.delete()
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

                    def launch():
                        offset = len(output())
                        qmp.type("./USR/BIN/WINDOW\n")
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
                    # Use actual PS/2 mouse packets to activate the title-bar X.
                    for dx, dy in [(100, 100), (100, 100), (100, 0), (85, 15)]:
                        qmp.call("input-send-event", {"events": [
                            {"type": "rel", "data": {"axis": "x", "value": dx}},
                            {"type": "rel", "data": {"axis": "y", "value": dy}}]})
                        time.sleep(0.1)
                    qmp.call("input-send-event", {"events": [
                        {"type": "btn", "data": {"button": "left", "down": True}}]})
                    time.sleep(0.1)
                    qmp.call("input-send-event", {"events": [
                        {"type": "rel", "data": {"axis": "x", "value": 1}}]})
                    wait_text("Window demo closed.", offset)
                    wait_text("root@root:~$ ", offset)
                    qmp.call("input-send-event", {"events": [
                        {"type": "btn", "data": {"button": "left", "down": False}},
                        {"type": "rel", "data": {"axis": "x", "value": -1}}]})

                    enter("./USR/BIN/WINPROBE", "WINDOW_API_OK")
                    enter("./USR/BIN/WINPROBE fault", "Program exited with status 142.")
                    enter("./USR/BIN/WINPROBE hold &", "WINDOW_HELD")
                    enter("ps", "WINPROBE")
                    pid = re.findall(r"Process: ~/USR/BIN/WINPROBE \((\d+)\) ALIVE", output())[-1]
                    enter("kill " + pid, "Task stopped.")
                    inspect("cleanup", """
assert int(gdb.parse_and_eval("user_windows_live")) == 0
assert int(gdb.parse_and_eval("KernelWindowManager->windows._size")) == 0
assert int(gdb.parse_and_eval("tasks_reaped")) == 7
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
                    return "CPL3 window demo, pixel composition/restoration, Q and PS/2 close, Ctrl-C, checked pointers/rectangles, per-process quota, foreign/stale handles, exit/fault/kill cleanup and shutdown."
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

#!/usr/bin/env python3
"""Exercise the real PS/2 shell, libc ABI, preemption and task exit in QEMU."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from common import ROOT, OUT, Report, command, require_success


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(10)
        self.socket.connect(str(path))
        self.stream = self.socket.makefile("rwb", buffering=0)
        self.stream.readline()
        self.call("qmp_capabilities")

    def call(self, execute, arguments=None):
        request = {"execute": execute}
        if arguments is not None:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request).encode() + b"\n")
        while True:
            raw = self.stream.readline()
            if not raw:
                raise RuntimeError("QMP disconnected")
            result = json.loads(raw)
            if "error" in result:
                raise RuntimeError(str(result["error"]))
            if "return" in result:
                return result["return"]

    def key(self, key, shift=False):
        keys = (["shift"] if shift else []) + [key]
        self.call("send-key", {"keys": [{"type": "qcode", "data": k} for k in keys], "hold-time": 1})
        time.sleep(0.025)

    def type(self, text):
        punctuation = {" ": "spc", ".": "dot", "/": "slash", "-": "minus",
                       "\n": "ret", "\b": "backspace", "_": "minus", "%": "5", "~": "grave_accent"}
        for ch in text:
            self.key(punctuation.get(ch, ch.lower()), ch.isupper() or ch in "_%~")

    def close(self):
        self.stream.close()
        self.socket.close()


def build_probe(directory, source, waiting=False):
    cross = os.environ.get("CROSS_COMPILE", "x86_64-elf-")
    obj, asm, program = (directory/n for n in (("wait.o", "context.o", "WAITPROBE") if waiting else ("probe.o", "context.o", "ABIPROBE")))
    symbols = require_success(command([cross+"nm", source/"kernel/bin/kernel.elf"]), "read kernel symbols")
    address = re.search(r"^([0-9a-f]+) [A-Za-z] scheduler_switches$", symbols, re.MULTILINE)
    if not address: raise RuntimeError("Scheduler counter symbol missing")
    commands = [
        [cross+"g++", "-std=gnu++17", "-O0", "-g", "-ffreestanding", "-mno-red-zone",
         "-fno-pie", "-fno-stack-protector", "-fno-exceptions", "-fno-rtti",
         "-I", ROOT/"libs/libc/src", "-I", ROOT/"common/include", "-I", ROOT/"include", "-c", ROOT/("tests/guest/wait_probe.cpp" if waiting else "tests/guest/abi_probe.cpp"), "-o", obj],
        ["nasm", "-f", "elf64", ROOT/"tests/guest/context_probe.asm", "-o", asm],
        [cross+"ld", "-T", ROOT/"libs/test/userspace.ld", "-static", "-nostdlib",
         "--defsym=guest_switches=0x"+address[1],
         "-o", program, obj, asm, source/"libs/libc/bin/libc.a"]]
    log = []
    for args in commands:
        result = command(args)
        log.append(" ".join(map(str, args))+"\n"+result.stdout)
        (directory/(program.name.lower()+"-build.log")).write_text("\n".join(log))
        require_success(result, "build guest ABI probe")
    return program


def test(firmware, timeout):
    directory = OUT/("qemu-shell-"+firmware)
    report = Report("qemu-shell-"+firmware, directory)
    built = OUT/("build-"+firmware)
    console = directory/"console.log"
    console.write_text("")

    def run():
        programs = [build_probe(directory, built/"source", waiting) for waiting in (False, True)]
        image = directory/"HackOS.img"
        shutil.copy2(built/"HackOS.img", image)
        fs_image = str(image) + ("@@"+str(334*512) if firmware == "bios" else "")
        for program in programs:
            require_success(command(["mcopy", "-o", "-i", fs_image, program, "::/USR/BIN/"+program.name]), "install test program in disposable image")
        with tempfile.TemporaryDirectory(prefix="hackos-shell-") as temporary:
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

                    def enter(text, expected):
                        offset = len(output())
                        qmp.type(text+"\n")
                        wait_text(expected, offset)
                        wait_text("$ ", offset)

                    enter("helx\bp", "Commands: help")
                    enter("print Hello_100%", "\nHello_100%\n")
                    enter("print", "\nroot@root:")
                    enter("print tick", "Ticks: ")
                    first_tick = int(re.findall(r"Ticks: (\d+)", output())[-1])
                    enter("ps", "kernel shell (2) ALIVE")
                    enter("ls", "USR/")
                    enter("cd USR", "root@root:~/USR$ ")
                    enter("open HELLO.TXT", "File opened.")
                    enter("read", "This is an example file")
                    enter("close", "root@root:~/USR$ ")
                    enter("close", "root@root:~/USR$ ")
                    enter("read", "No open file.")
                    enter("cd ..", "root@root:~$ ")
                    enter("x"*255 + "\b"*256, "\nroot@root:~$ ")
                    enter("x"*256, "Command too long")
                    enter("print recovered", "\nrecovered\n")
                    enter("./MISSING", "Cannot load program:")
                    for iteration in range(3):
                        offset = len(output())
                        qmp.type("./USR/BIN/ABIPROBE\n")
                        wait_text("PROBE_CONTEXT_BEGIN", offset)
                        wait_text("PROBE_OK", offset)
                        assert "PROBE_FAIL" not in output()[offset:], "Guest ABI/context check failed"
                        assert "PROBE_FLOAT=1.25000000" in output()[offset:], "Floating syscall argument/alignment failed"
                        enter("ps", "kernel shell (2) ALIVE")
                    enter("./USR/BIN/WAITPROBE", "WAIT_PROBE_READY")
                    enter("./USR/BIN/ABIPROBE", "A program is already running.")
                    enter("ps", "WAITPROBE")
                    pid = re.findall(r"Process: ~/USR/BIN/WAITPROBE \((\d+)\) ALIVE", output())[-1]
                    enter("kill 1", "Cannot stop the idle or shell task.")
                    enter("kill 2", "Cannot stop the idle or shell task.")
                    enter("kill 99999", "No running task with that PID.")
                    enter("kill " + pid, "Task stopped.")
                    enter("print tick", "Ticks: ")
                    assert int(re.findall(r"Ticks: (\d+)", output())[-1]) > first_tick, "Clock did not advance"
                    assert "Exception:" not in output(), "Kernel exception during shell test"
                    # Allow idle to reap the final exited thread, then inspect real queue ownership.
                    time.sleep(0.1)
                    qmp.call("stop")
                    script = directory/"state.gdb"
                    script.write_text("set pagination off\nset confirm off\ntarget remote "+str(gdb_path)+"\n"+'''python
import gdb
# Stop after the shell flushes its dirty backbuffer, not mid-copy.
bp = gdb.Breakpoint("SleepFor(unsigned int)", type=gdb.BP_HARDWARE_BREAKPOINT)
assert bp.locations, "Shell refresh milestone missing"
gdb.execute("continue")
assert int(gdb.parse_and_eval("$pc")) in [location.address for location in bp.locations]
bp.delete()
switches = int(gdb.parse_and_eval("scheduler_switches"))
reaped = int(gdb.parse_and_eval("tasks_reaped"))
assert switches > 20, "No sustained task switches"
assert reaped == 5, "Missing/duplicate task reclamation: " + str(reaped)
p = gdb.parse_and_eval("ready_queue")
labels = []
while int(p):
    assert len(labels) < 3, "Leaked task or queue cycle"
    assert int(p["state"]) == 0
    assert int(p["pageTable"]) == int(gdb.parse_and_eval("&KernelDirectory"))
    labels.append(p["name"].string())
    p = p["next"]
assert labels == ["idle", "kernel shell"], labels
print("SHELL_REGRESSION_OK switches=%d reaped=%d" % (switches, reaped))
end
detach
quit
''')
                    env = os.environ.copy(); env["DEBUGINFOD_URLS"] = ""
                    result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q",
                                      built/"kernel.elf", "-x", script], env=env, timeout=20)
                    (directory/"gdb.log").write_text(result.stdout)
                    require_success(result, "inspect scheduler state")
                    assert "SHELL_REGRESSION_OK" in result.stdout
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"screen.ppm")})
                    return "PS/2 editing, command bounds, literal %, FAT reads, three ABI/context programs, normal return and syscall exit, non-current termination, deferred reclamation; " + result.stdout
                finally:
                    if qmp: qmp.close()
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill(); process.wait()
    report.check("interactive_shell_and_tasking", run)
    return report.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=("bios", "uefi"), required=True)
    parser.add_argument("--timeout", type=int, default=60, help="maximum wait per milestone")
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))

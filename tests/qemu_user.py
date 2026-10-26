#!/usr/bin/env python3
"""Verify real CPL3 execution, isolation, preemption, faults and reclamation."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
from common import ROOT, OUT, Report, command, require_success
from qemu_shell import QMP


def build_probes(directory):
    cross = os.environ.get("CROSS_COMPILE", "x86_64-elf-")
    objects = []
    for name, source in (("entry", "common/src/x86_64/syscall_entry.asm"),
                         ("context", "tests/guest/context_probe.asm")):
        obj = directory/(name+".o")
        require_success(command(["nasm", "-f", "elf64", ROOT/source, "-o", obj]), "assemble " + name)
        objects.append(obj)
    programs = []
    for variant in range(-1, 9):
        name = "UPROBE" if variant == -1 else "UFAULT"+str(variant)
        source = "ring3_probe.cpp" if variant == -1 else "ring3_fault.cpp"
        obj, elf = directory/(name+".o"), directory/name
        args = [cross+"g++", "-std=gnu++17", "-O2", "-g", "-ffreestanding", "-mno-red-zone",
                "-mcmodel=large", "-fno-pie", "-fno-stack-protector", "-fno-omit-frame-pointer",
                "-fno-exceptions", "-fno-rtti", "-I", ROOT/"include", "-DFAULT="+str(variant),
                "-c", ROOT/"tests/guest"/source, "-o", obj]
        result = command(args)
        (directory/(name+"-compile.log")).write_text(result.stdout)
        require_success(result, "compile " + name)
        require_success(command([cross+"ld", "-T", ROOT/"libs/ring3/userspace.ld", "-static",
                                 "-nostdlib", "-o", elf, obj, *objects]), "link " + name)
        programs.append(elf)
    symbols = require_success(command([cross+"nm", programs[0]]), "read user symbols")
    addresses = {name: int(re.search(r"^([0-9a-f]+) [A-Za-z] " + name + "$", symbols, re.M)[1], 16)
                 for name in ("release_gate", "private_value")}
    # These are valid ELF headers with policy violations; none may create a task.
    original = programs[0].read_bytes()
    phoff = struct.unpack_from("<Q", original, 32)[0]
    for name, field, value, fmt in (("BADWX", 4, 7, "I"),
                                   ("BADADDR", 16, 0x100000, "Q"),
                                   ("BADSIZE", 40, 0x200000, "Q")):
        data = bytearray(original)
        struct.pack_into("<"+fmt, data, phoff+field, value)
        path = directory/name; path.write_bytes(data); programs.append(path)
    return programs, addresses


def test(firmware, timeout):
    directory = OUT/("qemu-user-"+firmware)
    report = Report("qemu-user-"+firmware, directory)
    built = OUT/("build-"+firmware)
    console = directory/"console.log"
    console.write_text("")

    def run():
        programs, addresses = build_probes(directory)
        image = directory/"HackOS.img"
        shutil.copy2(built/"HackOS.img", image)
        fs_image = str(image) + ("@@"+str(334*512) if firmware == "bios" else "")
        for p in programs:
            require_success(command(["mcopy", "-o", "-i", fs_image, p, "::/USR/BIN/"+p.name]), "install " + p.name)
        with tempfile.TemporaryDirectory(prefix="hackos-user-") as temporary:
            temporary = Path(temporary)
            qmp_path, gdb_path = temporary/"qmp", temporary/"gdb"
            args = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64",
                    "-m", "1G", "-smp", "cores=4,threads=1,sockets=1", "-display", "none",
                    "-monitor", "none", "-serial", "none", "-net", "none", "-no-reboot", "-snapshot",
                    "-drive", f"file={image},format=raw", "-usb", "-device", "usb-ehci,id=ehci",
                    "-device", "qemu-xhci,id=xhci", "-qmp", f"unix:{qmp_path},server=on,wait=off",
                    "-gdb", f"unix:{gdb_path},server=on,wait=off", "-debugcon", f"file:{console}",
                    "-global", "isa-debugcon.iobase=0xe9", "-d", "guest_errors,cpu_reset", "-D", str(directory/"guest.log")]
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
                    def output(): return console.read_text(errors="replace")
                    def wait(predicate, description):
                        deadline = time.monotonic() + timeout
                        while time.monotonic() < deadline:
                            assert process.poll() is None, "QEMU exited: " + description
                            text = output()
                            assert "PROBE_FAIL" not in text and "Exception:" not in text, "Guest failure; see console.log"
                            if predicate(): return
                            time.sleep(0.05)
                        raise AssertionError("Timed out: " + description + "; see console.log")
                    def enter(text, expected):
                        offset = len(output()); qmp.type(text+"\n")
                        wait(lambda: expected in output()[offset:], expected)
                    def inspect(name, body):
                        qmp.call("stop")
                        script = directory/(name+".gdb")
                        script.write_text("set pagination off\nset confirm off\ntarget remote " + str(gdb_path) + "\npython\nimport gdb, struct\n" + body + "\nprint('USER_CHECK_OK')\nend\ndetach\nquit\n")
                        env = os.environ.copy(); env["DEBUGINFOD_URLS"] = ""
                        result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q", built/"kernel.elf", "-x", script], env=env, timeout=20)
                        (directory/(name+".log")).write_text(result.stdout)
                        require_success(result, name)
                        assert "USER_CHECK_OK" in result.stdout, result.stdout
                        qmp.call("cont")
                    wait(qmp_path.exists, "QMP")
                    qmp = QMP(qmp_path)
                    wait(lambda: "root@root:~$ " in output(), "shell prompt")
                    enter("user USR/BIN/RING3", "exited 0; preemptions")
                    assert "RING3: hello from isolated userspace" in output()
                    assert "RING3: finished; requesting exit" in output()
                    # Two copies at the same virtual addresses must coexist.
                    for n in range(2):
                        enter("user USR/BIN/UPROBE &", "RING3_PROBE_READY")
                    inspect("isolation", '''
base = 0x400000000000
p = gdb.parse_and_eval("ready_queue")
users = []
while int(p):
    if int(p["userSpace"]) and not int(p["systemShell"]): users.append(p.dereference())
    p = p["next"]
assert len(users) == 2, "Concurrent user processes missing"
roots = [int(p["pageTable"]["pml4"]) for p in users]
assert roots[0] != roots[1] and all(r != int(gdb.parse_and_eval("KernelDirectory.pml4")) for r in roots)
inf = gdb.selected_inferior()
def translation(p, va):
    entry = p["userSpace"]["leaf"]["entries"][(va-base)//4096]
    assert int(entry["present"]) and int(entry["user_access"])
    return (int(entry["address"]) << 12) + (va & 4095)
for p in users:
    root = p["pageTable"]["pml4"]
    assert not int(root["entries"][0]["user_access"])
    assert not int(root["entries"][32]["user_access"])
    assert int(root["entries"][128]["user_access"])
    leaf = p["userSpace"]["leaf"]["entries"]
    assert int(leaf[0]["present"]) and not int(leaf[0]["writeable"]) and not int(leaf[0]["execution_disabled"])
    assert not int(leaf[507]["present"]), "Missing user stack guard"
    assert int(leaf[511]["writeable"]) and int(leaf[511]["execution_disabled"])
    assert int(p["userPreemptions"]) > 2, "No repeated ring-3 timer preemption"
    frame = p["rsp"].cast(gdb.lookup_type("Registers").pointer()).dereference()
    assert int(frame["cs"]) == 0x1b and int(frame["ss"]) == 0x23
    assert base <= int(frame["rip"]) < base+0x200000
    assert base+0x1fc000 <= int(frame["rsp"]) < base+0x200000
    print("USER_STATE pid=%d cr3=%x preemptions=%d" % (int(p["pid"]), int(root), int(p["userPreemptions"])))
for va in (base, base+0x1ff000, ''' + str(addresses['private_value']) + '''):
    assert translation(users[0], va) != translation(users[1], va), "Shared user frame"
for p in users:
    value = struct.unpack("<Q", inf.read_memory(translation(p, ''' + str(addresses['private_value']) + '''), 8))[0]
    assert value == 0x12345678
    inf.write_memory(translation(p, ''' + str(addresses['release_gate']) + '''), struct.pack("<Q", 1))
''')
                    wait(lambda: output().count("RING3_PROBE_OK") == 2, "both isolated programs completed")
                    wait(lambda: len(re.findall(r"exited 0; preemptions (\d+)", output())) == 3, "three successful exits")
                    assert all(int(n) > 2 for n in re.findall(r"exited 0; preemptions (\d+)", output()))
                    for index, vector in enumerate((14, 14, 14, 14, 13, 13, 13, 6, 14)):
                        enter("user USR/BIN/UFAULT"+str(index), "fault " + str(vector) + " error")
                    for name in ("BADWX", "BADADDR", "BADSIZE"):
                        offset = len(output()); qmp.type("user USR/BIN/"+name+"\n")
                        wait(lambda: "Rejected user program:" in output()[offset:] or "Cannot load user program:" in output()[offset:], "reject "+name)
                    enter("print ring3_survived", "\nring3_survived\n")
                    time.sleep(0.1)
                    inspect("reclamation", '''
bp = gdb.Breakpoint("ReapExitedProcesses()", type=gdb.BP_HARDWARE_BREAKPOINT)
gdb.execute("continue")
assert int(gdb.parse_and_eval("$pc")) in [l.address for l in bp.locations]
bp.delete()
assert int(gdb.parse_and_eval("tasks_reaped")) == 12, "User task stacks not reclaimed"

p = gdb.parse_and_eval("ready_queue")
labels = []
while int(p):
    assert len(labels) < 3
    if int(p["systemShell"]):
        assert int(gdb.parse_and_eval("user_pages_live")) == int(p["userSpace"]["ownedCount"])
    else:
        assert not int(p["userSpace"])
    labels.append(p["name"].string())
    p = p["next"]
assert labels == ["idle", "~/USR/BIN/SHELL"]
assert int(gdb.parse_and_eval("$cr3")) == int(gdb.parse_and_eval("KernelDirectory.pml4"))
print("USER_RECLAIM_OK")
''')
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"screen.ppm")})
                    return "CPL3 print/exit, same-VA private processes, register/FPU state across preemption, checked user copies, denied legacy syscalls, nine contained protection faults, malformed ELF rejection, and complete user frame/table/stack reclamation."
                finally:
                    if qmp: qmp.close()
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait()
    report.check("isolated_ring3", run)
    return report.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=("bios", "uefi"), required=True)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    sys.exit(test(args.firmware, args.timeout))

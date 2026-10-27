#!/usr/bin/env python3
"""Exercise the ring-3 PS/2 shell, safe ABI, preemption, cleanup and shutdown in QEMU."""
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
from session import setup_and_login
from common import ROOT, OUT, Report, command, require_success


class QMP:
    def __init__(self, path):
        self.events = []
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
            if "event" in result: self.events.append(result)
            if "error" in result:
                raise RuntimeError(str(result["error"]))
            if "return" in result:
                return result["return"]

    def key(self, key, shift=False, control=False):
        keys = (["ctrl"] if control else []) + (["shift"] if shift else []) + [key]
        self.call("send-key", {"keys": [{"type": "qcode", "data": k} for k in keys], "hold-time": 1})
        time.sleep(0.025)

    def type(self, text):
        punctuation = {" ": "spc", ".": "dot", "/": "slash", "-": "minus",
                       "\n": "ret", "\b": "backspace", "_": "minus", "%": "5", "~": "grave_accent", "&": "7", '"': "apostrophe", "'": "apostrophe", "\\": "backslash"}
        for ch in text:
            self.key(punctuation.get(ch, ch.lower()), ch.isupper() or ch in "_%~&\"")

    def close(self):
        self.stream.close()
        self.socket.close()


def build_probe(directory, source, waiting=False, extra=None):
    cross = os.environ.get("CROSS_COMPILE", "x86_64-elf-")
    obj, asm, program = (directory/n for n in (("wait.o", "context.o", "WAITPROBE") if waiting else ("probe.o", "context.o", "ABIPROBE")))
    guest_source = "wait_probe.cpp" if waiting else "abi_probe.cpp"
    if extra:
        guest_source, name = extra
        obj, program = directory/(name+".o"), directory/name
    commands = [
        [cross+"g++", "-std=gnu++17", "-O0", "-g", "-ffreestanding", "-mno-red-zone",
         "-mcmodel=large", "-fno-pie", "-fno-stack-protector", "-fno-exceptions", "-fno-rtti",
         "-I", ROOT/"libs/libc/src", "-I", ROOT/"common/include", "-I", ROOT/"include", "-c", ROOT/"tests/guest"/guest_source, "-o", obj],
        ["nasm", "-f", "elf64", ROOT/"tests/guest/context_probe.asm", "-o", asm],
        [cross+"ld", "-T", ROOT/"libs/ring3/userspace.ld", "-static", "-nostdlib",
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
        programs += [build_probe(directory,built/"source",extra=extra)
                     for extra in (("args_probe.cpp","ARGPROBE"),("wait_test.cpp","WAITTEST"),("terminal_probe.cpp","TERMPROBE"),("credential_probe.cpp","CREDPROBE"))]
        symbols = require_success(command([os.environ.get("CROSS_COMPILE","x86_64-elf-")+"nm",directory/"ARGPROBE"]), "argument probe symbols")
        gate = int(re.search(r"^([0-9a-f]+) [A-Za-z] release_gate$", symbols, re.M)[1],16)
        image = directory/"HackOS.img"
        shutil.copy2(built/"HackOS.img", image)
        fs_image = str(image) + ("@@"+str(846*512) if firmware == "bios" else "")
        for program in programs:
            require_success(command(["mcopy", "-o", "-i", fs_image, program, "::/USR/BIN/"+program.name]), "install test program in disposable image")
        fixture = directory/"listing.txt"
        fixture.write_bytes(b"abc")
        require_success(command(["mmd", "-i", fs_image, "::/LSCHECK", "::/LSCHECK/SUBDIR"]), "listing directory")
        for name in ("VISIBLE.TXT", "SECRET.TXT", ".DOTFILE"):
            require_success(command(["mcopy", "-i", fs_image, fixture, "::/LSCHECK/" + name]), "listing fixture")
        require_success(command(["mattrib", "-i", fs_image, "+h", "::/LSCHECK/SECRET.TXT"]), "hidden fixture")
        require_success(command(["mattrib", "-i", fs_image, "+r", "::/LSCHECK/VISIBLE.TXT"]), "readonly fixture")
        with tempfile.TemporaryDirectory(prefix="hackos-shell-") as temporary:
            temporary = Path(temporary)
            qmp_path, gdb_path = temporary/"qmp", temporary/"gdb"
            args = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64,+rdrand",
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
                    setup_and_login(qmp, output, wait_for)

                    qmp.call("stop")
                    boot_script = directory/"shell-entry.gdb"
                    boot_script.write_text("set pagination off\nset confirm off\ntarget remote " + str(gdb_path) + "\npython\nimport gdb\n" + """
bp = gdb.Breakpoint("syscall_handler", type=gdb.BP_HARDWARE_BREAKPOINT)
gdb.execute("continue")
r = gdb.parse_and_eval("r").dereference()
p = gdb.parse_and_eval("current_process")
assert int(p["pid"]) == 3 and int(p["systemShell"]) and int(p["userSpace"])
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
                        qmp.type(text+"\n")
                        wait_text(expected, offset)
                        wait_text("$ ", offset)

                    enter("helx\bp", "Commands: help")
                    enter("print Hello_100%", "\nHello_100%\n")
                    enter("print", "\nroot@root:")
                    enter("print tick", "Ticks: ")
                    first_tick = int(re.findall(r"Ticks: (\d+)", output())[-1])
                    enter("ps", "~/USR/BIN/SHELL (3)")
                    enter("ps aux", "~/USR/BIN/SHELL (3)")
                    assert re.search(r"Started user process \d+: ~/USR/BIN/PS", output())
                    assert re.search(r"Process: ~/USR/BIN/PS \(\d+\) ALIVE", output())
                    enter("clear", "root@root:~$ ")
                    enter("print QUIET_OK", "\nQUIET_OK\n")
                    qmp.call("stop")
                    quiet = directory/"quiet-console.gdb"
                    quiet.write_text("set pagination off\nset confirm off\ntarget remote " + str(gdb_path) + "\npython\nimport gdb\n"
                                     "assert int(gdb.parse_and_eval('display.console.current_y')) == 2, 'Lifecycle boilerplate reached the console'\n"
                                     "assert int(gdb.parse_and_eval('display.console.current_x')) == 13\n"
                                     "print('QUIET_CONSOLE_OK')\nend\ndetach\nquit\n")
                    result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q", built/"kernel.elf", "-x", quiet], timeout=20)
                    (directory/"quiet-console.log").write_text(result.stdout)
                    require_success(result, "quiet graphical console")
                    assert "QUIET_CONSOLE_OK" in result.stdout
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"quiet-console.ppm")})
                    qmp.call("cont")
                    enter('  "ps"   "aux"  ', "~/USR/BIN/SHELL (3)")
                    enter("ps invalid", "Usage: ps [aux]")
                    enter("ps aux extra", "Usage: ps [aux]")
                    enter('ps ""', "Usage: ps [aux]")
                    enter('print "two  words" escaped\\ space "" end', "\ntwo  words escaped space  end\n")
                    enter("print tick extra", "\ntick extra\n")
                    enter('print "unfinished', "Invalid arguments")
                    enter("print trailing\\", "Invalid arguments")
                    enter("print " + "a " * 16, "Invalid arguments")
                    enter("   ", "\nroot@root:")
                    enter("missing argument", "Unknown command: missing\n")
                    enter("kill", "Usage: kill <pid>")
                    enter("kill 1 extra", "Usage: kill <pid>")
                    enter('kill ""', "Invalid PID.")
                    enter("kill 18446744073709551616", "Invalid PID.")
                    enter("user", "Usage: user <path>")
                    enter('user ""', "Usage: user <path>")
                    enter("history &", "Shell built-ins cannot run in the background.")
                    enter("shutdown now", "Usage: shutdown")
                    enter("restart now", "Usage: restart")
                    enter("cat", "Usage: cat <path>")
                    enter('cd ""', "Path must not be empty.")
                    enter("ls extra", "Cannot list directory.")
                    enter("ls", "USR/")
                    listing_offset = len(output())
                    enter("ls /LSCHECK", "VISIBLE.TXT")
                    listing = output()[listing_offset:]
                    assert "SECRET.TXT" not in listing and ".DOTFILE" not in listing
                    assert "\nVISIBLE.TXT\n" in listing and "\nSUBDIR/\n" in listing
                    for flags in ("-a", "-la", "-al", "-l -a", "-a -l"):
                        listing_offset = len(output())
                        enter("ls " + flags + " /LSCHECK", "SECRET.TXT")
                        listing = output()[listing_offset:]
                        assert ".DOTFILE" in listing and "SUBDIR/" in listing
                        if "l" in flags:
                            assert "TYPE ATTR       BYTES NAME" in listing
                            assert re.search(r"^-[ ]+r--a[ ]+3 VISIBLE.TXT$", listing, re.M)
                            assert re.search(r"^-[ ]+-h-a[ ]+3 SECRET.TXT$", listing, re.M)
                    listing_offset = len(output())
                    enter("ls -l /LSCHECK", "VISIBLE.TXT")
                    assert "SECRET.TXT" not in output()[listing_offset:]
                    enter("ls -- /LSCHECK", "VISIBLE.TXT")
                    enter("ls /LSCHECK -la", "SECRET.TXT")
                    enter("ls -z", "Usage: ls")
                    enter("ls --bad", "Usage: ls")
                    enter("ls /LSCHECK /USR", "Usage: ls")
                    enter('ls ""', "Usage: ls")
                    enter('cd "USR"', "root@root:~/USR$ ")
                    enter("wd", "\n~/USR\n")
                    enter('cat "HELLO.TXT"', "This is an example file")
                    enter("read HELLO.TXT", "This is an example file")
                    enter("cat HELLO.TXT extra", "Usage: cat <path>")
                    enter("read", "Usage: cat <path>")
                    enter("/usr/bin/print explicit_path", "\nexplicit_path\n")
                    enter("../usr/bin/print relative_path", "\nrelative_path\n")
                    enter("cat MISSING", "File not found.")
                    enter("cd ..", "root@root:~$ ")
                    enter("x"*255 + "\b"*256, "\nroot@root:~$ ")
                    enter("x"*256, "Command too long")
                    enter("print recovered", "\nrecovered\n")
                    enter("print history_one", "\nhistory_one\n")
                    enter("print history_two", "\nhistory_two\n")
                    offset = len(output())
                    qmp.type("print draft")
                    qmp.key("up"); qmp.key("up"); qmp.key("down"); qmp.key("down")
                    qmp.type("\n")
                    wait_text("\ndraft\n",offset)
                    wait_text("root@root:~$ ",offset)
                    offset = len(output()); qmp.key("up"); qmp.type("\n")
                    wait_text("\ndraft\n",offset)
                    wait_text("root@root:~$ ",offset)
                    offset = len(output()); qmp.type("print should_not_run"); qmp.key("c",control=True)
                    wait_text("^C\nroot@root:~$ ",offset)
                    offset = len(output()); qmp.key("up"); qmp.type("\n")
                    wait_text("\ndraft\n",offset) # cancelled partial input was not stored
                    wait_text("root@root:~$ ",offset)
                    offset = len(output())
                    qmp.type('./USR/BIN/ARGPROBE alpha "two words" "" escaped\\ space "&"\n')
                    wait_text("ARGS_READY",offset)
                    qmp.type("print queued\n")
                    assert "root@root:" not in output()[offset:], "Prompt returned while foreground child was alive"
                    assert "queued" not in output()[offset:], "Shell consumed input while waiting"
                    qmp.call("stop")
                    script = directory/"foreground.gdb"
                    script.write_text("set pagination off\nset confirm off\ntarget remote "+str(gdb_path)+"\npython\nimport gdb, struct\n" + """
# The child can print READY before its parent has returned from spawn.
# Observe an idle pass after the parent actually blocks, not that launch race.
bp = gdb.Breakpoint("ReapExitedProcesses()", type=gdb.BP_HARDWARE_BREAKPOINT)
bp.condition = "ready_queue->next->state == 3"
gdb.execute("continue")
bp.delete()
p = gdb.parse_and_eval("ready_queue")
shell = child = None
while int(p):
    if int(p["systemShell"]): shell = p.dereference()
    if p["name"].string() == "~/USR/BIN/ARGPROBE": child = p.dereference()
    p = p["next"]
assert shell is not None and child is not None, "Missing shell or child"
assert int(shell["state"]) == 3 and int(shell["waitingFor"]) == int(child["pid"]), "Foreground parent did not block for its child"
assert int(child["parentPid"]) == int(shell["pid"])
va = """ + str(gate) + """
entry = child["userSpace"]["leaf"]["entries"][(va-0x400000000000)//4096]
physical = (int(entry["address"]) << 12) + (va & 4095)
gdb.selected_inferior().write_memory(physical,struct.pack("<Q",1))
print("FOREGROUND_WAIT_OK")
""" + "end\ndetach\nquit\n")
                    result = command([os.environ.get("GDB","gdb"),"--batch","--nx","-q",built/"kernel.elf","-x",script],timeout=20)
                    (directory/"foreground.log").write_text(result.stdout)
                    require_success(result,"inspect blocked foreground parent")
                    assert "FOREGROUND_WAIT_OK" in result.stdout, result.stdout
                    qmp.call("cont")
                    wait_text("ARGS_DONE",offset)
                    wait_text("Program exited with status 7.",offset)
                    wait_text("root@root:~$ ",offset)
                    assert "\nqueued\n" not in output()[offset:], "Child typeahead leaked into shell commands"
                    enter("print reclaimed", "\nreclaimed\n")
                    tail = output()[offset:]
                    assert tail.index("ARGS_DONE") < tail.index("Program exited with status 7.") < tail.index("root@root:")
                    enter("./USR/BIN/WAITTEST", "WAIT_TEST_OK")
                    enter('./USR/BIN/ARGPROBE "unfinished', "Invalid arguments")
                    enter("./MISSING", "Cannot load user program:")
                    enter("cat USR/HELLO.TXT", "This is an example file")
                    for iteration in range(3):
                        offset = len(output())
                        qmp.type("./USR/BIN/ABIPROBE\n")
                        wait_text("PROBE_CONTEXT_BEGIN", offset)
                        wait_text("PROBE_OK", offset)
                        wait_text("root@root:~$ ", offset)
                        assert "PROBE_FAIL" not in output()[offset:], "Guest ABI/context check failed"
                        enter("ps", "~/USR/BIN/SHELL (3)")
                    enter("id", "uid=0(root) gid=0 groups=0")
                    enter("whoami", "\nroot\n")
                    enter("users", "guest uid=1000 gid=1000")
                    enter("groups", "users gid=1000")
                    enter("groupadd staff", "Group created.")
                    enter("useradd alice staff", "User created. Use passwd to enable login.")
                    enter("usermod alice users", "Group added")
                    enter("runas alice id", "uid=1001(alice) gid=1001 groups=1001,1000")
                    enter("runas alice whoami", "\nalice\n")
                    offset = len(output()); qmp.type("runas guest id &\n")
                    wait_text("$ ", offset)
                    # Background output may interleave with the prompt. Wait
                    # for both the launcher and its child to succeed by PID.
                    for name in ("RUNAS", "ID"):
                        pattern = r"Started user process (\d+): ~/USR/BIN/" + name + r"\n"
                        wait_for(lambda: re.search(pattern, output()[offset:]), "background " + name)
                        task = re.search(pattern, output()[offset:])[1]
                        wait_text("User process " + task + " exited 0;", offset)
                    enter("runas guest useradd intruder", "Permission denied: root required.")
                    enter("runas guest groupadd intruder", "Permission denied: root required.")
                    enter("runas guest usermod guest root", "Permission denied: root required.")
                    enter("runas guest runas root id", "Permission denied: root required.")
                    enter("runas guest shutdown", "Permission denied: root required.")
                    enter("runas guest restart", "Permission denied: root required.")
                    enter("runas guest ps", "uid=1000 gid=1000 ppid=")
                    enter("useradd alice", "Account operation failed.")
                    enter("groupadd staff", "Account operation failed.")
                    enter('useradd ""', "Account operation failed.")
                    enter("runas missing id", "Unknown user.")
                    enter("runas guest", "Usage: runas")
                    enter("./USR/BIN/CREDPROBE", "CREDENTIALS_OK")
                    offset = len(output()); qmp.type("runas guest input\n")
                    wait_text("input> ", offset)
                    qmp.type("hello\n")
                    wait_text("You typed: hello\ninput> ", offset)
                    qmp.key("c", control=True)
                    wait_text("^C\nroot@root:~$ ", offset)
                    enter("./USR/BIN/WAITPROBE &", "WAIT_PROBE_READY")
                    enter("user USR/BIN/ABIPROBE " + "a " * 15, "PROBE_OK")
                    enter("ps", "WAITPROBE")
                    pid = re.findall(r"Process: ~/USR/BIN/WAITPROBE \((\d+)\) ALIVE", output())[-1]
                    # Input goes to the foreground program, while a background
                    # task remains alive and cannot consume the keyboard queue.
                    offset = len(output()); qmp.type("./USR/BIN/INPUT\n")
                    wait_text("input> ",offset)
                    qmp.type("hellx\bo\n")
                    wait_text("You typed: hello\ninput> ",offset)
                    assert "root@root:" not in output()[offset:]
                    qmp.type("exit\n"); wait_text("root@root:~$ ",offset)
                    offset = len(output()); qmp.type("./USR/BIN/INPUT\n")
                    wait_text("input> ",offset)
                    qmp.type("unfinished"); qmp.key("c",control=True)
                    wait_text("^C\nroot@root:~$ ",offset)
                    for iteration in range(2):
                        offset = len(output()); qmp.type("./USR/BIN/TERMPROBE\n")
                        wait_text("TERMINAL_SPIN_READY",offset)
                        if iteration == 0: qmp.type("x"*260+"\n") # full queue cannot mask Ctrl-C
                        qmp.key("c",control=True)
                        wait_text("^C\nroot@root:~$ ",offset)
                        assert "Unknown command:" not in output()[offset:], "Interrupted typeahead executed in shell"
                    enter("ps", "WAITPROBE")
                    enter("history", './USR/BIN/ARGPROBE alpha "two words" "" escaped\\ space "&"')
                    enter("kill 1", "Cannot stop the idle or shell task")
                    enter("kill 2", "Cannot stop the idle or shell task")
                    enter("kill 99999", "No running task with that PID.")
                    enter("kill " + pid, "Task stopped.")
                    enter("wd", "\n~\n")
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
bp = gdb.Breakpoint("ReapExitedProcesses()", type=gdb.BP_HARDWARE_BREAKPOINT)
assert bp.locations, "Shell refresh milestone missing"
gdb.execute("continue")
assert int(gdb.parse_and_eval("$pc")) in [location.address for location in bp.locations]
bp.delete()
switches = int(gdb.parse_and_eval("scheduler_switches"))
reaped = int(gdb.parse_and_eval("tasks_reaped"))
assert switches > 20, "No sustained task switches"
assert reaped == EXPECTED_REAPED, "Missing/duplicate task reclamation: " + str(reaped)
p = gdb.parse_and_eval("ready_queue")
labels = []
while int(p):
    assert len(labels) < 4, "Leaked task or queue cycle"
    assert int(p["state"]) == (3 if int(p["authenticationAgent"]) else 0)
    if int(p["pid"]) == 3:
        assert int(p["userSpace"]) and int(p["systemShell"])
        assert int(p["pageTable"]) != int(gdb.parse_and_eval("&KernelDirectory"))
        assert int(gdb.parse_and_eval("user_pages_live")) == int(p["userSpace"]["ownedCount"]) + int(gdb.parse_and_eval("ready_queue->next->userSpace->ownedCount"))
    else:
        assert not int(p["userSpace"]) or int(p["authenticationAgent"])
    labels.append(p["name"].string())
    p = p["next"]
assert labels == ["idle", "~/USR/BIN/LOGIN", "~/USR/BIN/SHELL"], labels
assert int(gdb.parse_and_eval("user_files_live")) == 0
assert int(gdb.parse_and_eval("terminalOwner")) == 3
assert int(gdb.parse_and_eval("pendingInterrupt")) == 0
print("SHELL_REGRESSION_OK switches=%d reaped=%d" % (switches, reaped))
end
detach
quit
''')
                    started = re.findall(r"Started user process (\d+):", output())
                    assert len(started) == len(set(started)), "Duplicate process IDs"
                    script.write_text(script.read_text().replace("EXPECTED_REAPED", str(len(started) - 2)))
                    env = os.environ.copy(); env["DEBUGINFOD_URLS"] = ""
                    result = command([os.environ.get("GDB", "gdb"), "--batch", "--nx", "-q",
                                      built/"kernel.elf", "-x", script], env=env, timeout=20)
                    (directory/"gdb.log").write_text(result.stdout)
                    require_success(result, "inspect scheduler state")
                    assert "SHELL_REGRESSION_OK" in result.stdout
                    qmp.call("stop")
                    qmp.call("screendump", {"filename": str(directory/"screen.ppm")})
                    qmp.call("cont")
                    offset = len(output())
                    qmp.type("shutdown\n")
                    process.wait(timeout=timeout)
                    assert process.returncode == 0, "QEMU did not power off cleanly"
                    assert "Stopping the CPU. Bye!" in output()[offset:]
                    for raw in qmp.stream:
                        event = json.loads(raw)
                        if "event" in event: qmp.events.append(event)
                    (directory/"qmp-events.json").write_text(json.dumps(qmp.events, indent=2))
                    assert any(e.get("event") == "SHUTDOWN" and e["data"].get("guest") and
                               e["data"].get("reason") == "guest-shutdown" for e in qmp.events), "No guest ACPI shutdown event"
                    return "Auto-started CPL3 shell, fixed history recall/draft/cancel, foreground input and backspace, Ctrl-C of input and busy programs with full queue, quoted/empty/escaped argv, blocking foreground wait and exit status, late/repeated/invalid waits and orphan cleanup, PS/2 editing, safe file handles and bad pointers, register preservation, concurrent programs, exit/kill resource cleanup, and real guest ACPI shutdown; " + result.stdout
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

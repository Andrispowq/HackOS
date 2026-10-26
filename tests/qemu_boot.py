#!/usr/bin/env python3
"""Observe real kernel milestones through QEMU's debugger, without guest hooks."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from common import ROOT, OUT, Report, command, require_success
from run import validate_elf, assemble_loaders


def boot(firmware, image, elf, timeout=60, self_test=False, variant="success"):
    name = "qemu-harness/"+variant if self_test else "qemu-" + firmware
    directory = OUT / name
    report = Report(name, directory)
    case = "debugger_probe" if self_test else "kernel_boot"
    if not image.is_file() or not elf.is_file():
        report.add(case, "blocked", "Fresh image/debug ELF unavailable. Build failures must be resolved before full boot testing.")
        return report.finish()

    def run():
        validate_elf(elf.read_bytes())
        debugger = shutil.which(os.environ.get("GDB", "gdb"))
        if not debugger:
            raise RuntimeError("GDB missing; use the Linux test container")
        with tempfile.TemporaryDirectory(prefix="hackos-qemu-") as temporary:
            socket = Path(temporary)/"gdb.sock"
            args = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64",
                    "-m", "1G", "-smp", "cores=4,threads=1,sockets=1", "-display", "none",
                    "-monitor", "none", "-serial", "none", "-net", "none", "-no-reboot",
                    "-snapshot", "-drive", f"file={image},format=raw", "-usb",
                    "-device", "usb-ehci,id=ehci", "-device", "qemu-xhci,id=xhci",
                    "-S", "-gdb", f"unix:{socket},server=on,wait=off",
                    "-d", "guest_errors,cpu_reset", "-D", str(directory/"guest.log")]
            if firmware == "uefi":
                ovmf = ROOT/"boot/UEFI/OVMFbin"
                variables = directory/"OVMF_VARS.fd"
                shutil.copy2(ovmf/"OVMF_VARS-pure-efi.fd", variables)
                args += ["-drive",f"if=pflash,format=raw,unit=0,file={ovmf/'OVMF_CODE-pure-efi.fd'},readonly=on",
                         "-drive",f"if=pflash,format=raw,unit=1,file={variables}"]
            script = directory/"milestones.gdb"
            script.write_text('''set pagination off
set confirm off
set breakpoint pending off
set remotetimeout 10
target remote ''' + str(socket) + '''
python
import gdb
for symbol in ["kernel_main", "InitialiseIRQ()", "timer_callback"]:
    bp = gdb.Breakpoint(symbol, type=gdb.BP_HARDWARE_BREAKPOINT)
    if not bp.locations:
        raise gdb.GdbError("Unresolved milestone: " + symbol)
    addresses = [location.address for location in bp.locations]
    gdb.execute("continue")
    pc = int(gdb.parse_and_eval("$pc"))
    if pc not in addresses:
        raise gdb.GdbError("Stopped somewhere other than milestone: " + symbol)
    print("HACKOS_MILESTONE=" + symbol, flush=True)
    bp.delete()
end
info registers
disconnect
quit
''')
            env = os.environ.copy()
            env["DEBUGINFOD_URLS"] = ""
            with (directory/"qemu.log").open("w") as log:
                process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic()+10
                    while not socket.exists():
                        if process.poll() is not None:
                            raise RuntimeError(f"QEMU exited {process.returncode}; see qemu.log")
                        if time.monotonic() > deadline:
                            raise RuntimeError("QEMU debugger socket did not appear")
                        time.sleep(0.05)
                    try:
                        result = command([debugger,"--batch","--nx","-q",elf,"-x",script],timeout=timeout,env=env)
                    except subprocess.TimeoutExpired as exc:
                        output = exc.stdout or b""
                        (directory/"gdb.log").write_text(output.decode(errors="replace") if isinstance(output,bytes) else output)
                        raise AssertionError(f"Kernel did not reach all milestones in {timeout}s; see gdb.log and guest.log") from exc
                    (directory/"gdb.log").write_text(result.stdout)
                    assert result.returncode == 0, f"GDB exited {result.returncode}; see gdb.log"
                    for milestone in ("kernel_main","InitialiseIRQ()","timer_callback"):
                        assert "HACKOS_MILESTONE="+milestone in result.stdout, f"Missing milestone {milestone}"
                    return ("Debugger runner reached test-only labels; no kernel/IRQ coverage." if self_test else
                            "Reached kernel entry, IRQ initialization, and the timer interrupt callback.")
                finally:
                    if process.poll() is None:
                        process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
    report.check(case, run)
    return report.finish()


def harness_test():
    directory = OUT/"qemu-harness"
    report = Report("qemu-harness",directory)
    def scenario(stalled):
        assemble_loaders(directory)
        obj, elf, payload, image = [directory/name for name in ("probe.o","probe.elf","probe.bin","probe.img")]
        require_success(command(["nasm","-f","elf64",*(["-DHANG"] if stalled else []),ROOT/"tests/fixtures/debugger_probe.asm","-o",obj]),"assemble debugger probe")
        require_success(command([os.environ.get("TEST_LD","ld"),"-m","elf_x86_64","-Ttext","0x8000","-e","kernel_main","-o",elf,obj]),"link debugger probe")
        require_success(command([os.environ.get("TEST_OBJCOPY","objcopy"),"-O","binary",elf,payload]),"extract probe")
        with image.open("wb") as f:
            f.truncate((334+93750)*512)
            for offset,path in [(0,directory/"mbr.bin"),(512,directory/"bootloader.bin"),(1024,payload)]:
                f.seek(offset); f.write(path.read_bytes())
        variant = "timeout" if stalled else "success"
        status = boot("bios",image,elf,timeout=5,self_test=True,variant=variant)
        if stalled:
            result = json.loads((directory/variant/"results.json").read_text())[0]
            assert status == 1 and result["status"] == "fail" and "did not reach all milestones" in result["detail"], "Idle probe was not rejected as a timeout"
        else:
            assert status == 0, "Debugger did not observe all test-only labels"
    report.check("debugger_observes_milestones",lambda:scenario(False))
    report.check("idle_guest_fails",lambda:scenario(True))
    return report.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", choices=("bios","uefi"), default="bios")
    parser.add_argument("--self-test", action="store_true", help="test debugger orchestration using a synthetic payload")
    parser.add_argument("--image", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    if args.self_test:
        sys.exit(harness_test())
    directory = OUT/("build-"+args.firmware)
    sys.exit(boot(args.firmware, (args.image or directory/"HackOS.img").resolve(),
                  (args.elf or directory/"kernel.elf").resolve(), args.timeout))

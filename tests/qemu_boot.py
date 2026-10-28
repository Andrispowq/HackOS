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


def boot(firmware, image, elf, timeout=60, self_test=False, variant="success", report_name=None):
    name = report_name or ("qemu-harness/"+variant if self_test else "qemu-" + firmware)
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
            args = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-cpu", "qemu64,+rdrand",
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
import gdb, struct
for symbol in ["kernel_main", "InitialiseIRQ()", "timer_callback"]:
    bp = gdb.Breakpoint("*kernel_main" if symbol == "kernel_main" else symbol, type=gdb.BP_HARDWARE_BREAKPOINT)
    if not bp.locations:
        raise gdb.GdbError("Unresolved milestone: " + symbol)
    addresses = [location.address for location in bp.locations]
    gdb.execute("continue")
    pc = int(gdb.parse_and_eval("$pc"))
    if pc not in addresses:
        raise gdb.GdbError("Stopped somewhere other than milestone: " + symbol)
    print("HACKOS_MILESTONE=" + symbol, flush=True)
    if symbol == "kernel_main" and not ''' + repr(self_test) + ''':
        data = open(''' + repr(str(elf)) + ''', 'rb').read()
        offset = struct.unpack_from('<Q', data, 32)[0]
        stride, count = struct.unpack_from('<HH', data, 54)
        for i in range(count):
            kind, flags, file, virtual, physical, size, memory, alignment = struct.unpack_from('<IIQQQQQQ', data, offset + i * stride)
            if kind == 1:
                expected = data[file:file + size] + bytes(memory - size)
                actual = bytes(gdb.selected_inferior().read_memory(virtual, memory))
                assert actual == expected, 'Loader corrupted ELF segment at ' + hex(virtual)
        print('HACKOS_MILESTONE=elf_segments', flush=True)
        # Decode the wire format independently of the kernel's debug types.
        read = lambda address, size: bytes(gdb.selected_inferior().read_memory(address, size))
        handoff = int(gdb.parse_and_eval("$rdi"))
        assert handoff and handoff % 8 == 0, 'Unaligned boot information'
        fields = struct.unpack('<QIIIIQQQQBBB5x', read(handoff, 64))
        pixels, width, height, bpp, pitch, font, mmap, stride, rsdp, count, revision, bios = fields
        assert bios == ''' + repr(firmware == "bios") + ''', 'Wrong boot path flag'
        assert width > 0 and height > 0 and bpp == 32 and pitch >= width, "Invalid framebuffer geometry"
        assert pixels and pixels % 4 == 0, "Invalid framebuffer address"
        read(pixels + (height - 1) * pitch * 4, width * 4)
        header, glyphs = struct.unpack('<QQ', read(font, 16))
        magic, mode, charsize = struct.unpack('<HBB', read(header, 4))
        assert magic == 0x0436 and charsize == 16, "Invalid PSF1 font"
        expected_font = open(''' + repr(str(ROOT / 'util/fonts/zap-light16.psf')) + ''', 'rb').read()
        assert read(header, 4) == expected_font[:4], 'Font header differs'
        glyph_size = charsize * (512 if mode & 1 else 256)
        assert read(glyphs, glyph_size) == expected_font[4:4 + glyph_size], 'Font glyphs differ'
        assert mmap and mmap % 8 == 0 and stride == 24 and count > 0, "Invalid memory map shape"
        entries = list(struct.iter_unpack('<QQII', read(mmap, count * stride)))
        usable = (1,) if bios else (7,)
        assert any(size and kind in usable for address, size, kind, attributes in entries), "No usable memory"
        assert all(address + size <= 2**64 for address, size, kind, attributes in entries), "Memory extent overflow"
        acpi = read(rsdp, 20)
        assert acpi[:8] == b'RSD PTR ' and sum(acpi) % 256 == 0, 'Invalid ACPI RSDP'
        assert revision == acpi[15], 'RSDP revision differs from handoff'
        print('HACKOS_MILESTONE=boot_information', flush=True)

    if symbol == "timer_callback" and not ''' + repr(self_test) + ''':
        ticks = int(gdb.parse_and_eval("tick"))
        for repeat in range(3):
            gdb.execute("continue")
            if int(gdb.parse_and_eval("$pc")) not in addresses:
                raise gdb.GdbError("Stopped outside repeated timer callback")
            now = int(gdb.parse_and_eval("tick"))
            if now <= ticks:
                raise gdb.GdbError("Timer did not return and advance")
            ticks = now
        print("HACKOS_MILESTONE=repeated_timer", flush=True)
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
                    for milestone in ("kernel_main","InitialiseIRQ()","timer_callback", *(() if self_test else ("repeated_timer", "elf_segments", "boot_information"))):
                        assert "HACKOS_MILESTONE="+milestone in result.stdout, f"Missing milestone {milestone}"
                    return ("Debugger runner reached test-only labels; no kernel/IRQ coverage." if self_test else
                            "Reached kernel entry, IRQ initialization, and repeated timer interrupts.")
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
            f.truncate((846+93750)*512)
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

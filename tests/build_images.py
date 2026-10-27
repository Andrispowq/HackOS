#!/usr/bin/env python3
"""Build each production component and independently check the packaged image."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from common import ROOT, OUT, Report, command, require_success
from run import validate_elf


def build(firmware):
    directory = OUT / ("build-" + firmware)
    report = Report("build-" + firmware, directory)
    work = directory / "source"
    # Remove only this harness's disposable checkout, never the real build tree.
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir()
    for stale in ("HackOS.img", "kernel.elf"):
        (directory / stale).unlink(missing_ok=True)
    try:
        files = require_success(command(["git", "-c", f"safe.directory={ROOT}", "ls-files", "--cached", "--others", "--exclude-standard", "-z"]), "list source inputs").split("\0")
        for name in filter(None, files):
            path = Path(name)
            if not (ROOT / path).is_file():
                continue  # Tracked files may have been deleted in the working tree.
            if path.parts[0] in ("tests", ".github", "docs") or name.endswith(".img"):
                continue
            destination = work / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / path, destination)
        cross = os.environ.get("CROSS_COMPILE", "x86_64-elf-")
        for tool in ("gcc", "g++", "ld", "objcopy", "ar"):
            if not shutil.which(cross + tool):
                raise RuntimeError(f"Missing {cross + tool}; use the toolchain container")
        env = os.environ.copy()
        env.pop("MAKEFLAGS", None)
        versions = "\n".join(command([cross + t, "--version"]).stdout for t in ("gcc", "ld"))
        (directory / "toolchain.txt").write_text(versions)
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as exc:
        report.add("build_setup", "error", str(exc))
        return report.finish()

    def make(name, path, targets, extra=()):
        def run():
            args = ["make", "-j1", "-k", "-C", work/path, *targets,
                    f"CROSS_COMPILE={cross}", f"CC={cross}gcc", f"CXX={cross}g++", f"LD={cross}ld", *extra]
            result = command(args, cwd=work, env=env, timeout=300)
            (directory / (name + "-output.log")).write_text(result.stdout)
            assert result.returncode == 0, f"{' '.join(map(str, args))}\n{result.stdout}"
            return result.stdout
        report.check(name, run)

    if firmware == "bios":
        make("mbr", "boot/BIOS/mbr", ["mbr"])
        make("stage1", "boot/BIOS/stage_1", ["bootloader"])
        make("stage2", "boot/BIOS/stage_2", ["second_stage"])
    else:
        make("uefi_loader", "boot/UEFI/gnu-efi", ["lib", "gnuefi", "bootloader"],
             ["ARCH=x86_64", f"CROSS_COMPILE={cross}"])
    make("kernel", "kernel", ["kernel"])
    make("userspace_library", "libs/libc", ["all"])
    make("command_programs", "libs/commands", ["all"])
    make("userspace_shell", "libs/shell", ["all"])
    make("login_program", "libs/login", ["all"])
    make("userspace_test", "libs/test", ["all"])
    make("input_program", "libs/input", ["all"])
    make("window_program", "libs/window", ["all"])
    make("ring3_program", "libs/ring3", ["all"])

    if any(r["status"] != "pass" for r in report.results):
        report.add("image", "blocked", "Required components did not build; no stale binaries are used.")
        return report.finish()

    def package():
        kernel = (work/"bin/kernel.elf").read_bytes()
        validate_elf(kernel)
        # Large-model .lbss/.ldata must remain in their aligned load segments.
        # The kernel rejects shared pages rather than merging R/W/X permissions.
        for path in (work / "libs/commands/bin").iterdir():
            data = path.read_bytes()
            validate_elf(data)
            phoff = struct.unpack_from('<Q', data, 32)[0]
            phnum = struct.unpack_from('<H', data, 56)[0]
            pages = set()
            for i in range(phnum):
                kind, flags, _, address, _, _, size, _ = struct.unpack_from('<IIQQQQQQ', data, phoff + i * 56)
                if kind != 1 or not size:
                    continue
                assert 0x400000000000 <= address < address + size <= 0x400000100000, path
                assert flags & 4 and not flags & ~7 and flags & 3 != 3, path
                segment = set(range(address // 4096, (address + size + 4095) // 4096))
                assert not pages & segment, f'{path.name}: load segments share a page'
                pages |= segment
            assert len(pages) + 4 <= 64, f'{path.name}: exceeds private image/stack page limit'
        if firmware == "bios":
            for name, limit in [("mbr.bin",512),("bootloader.bin",512),
                                ("second_stage.bin",32768),("kernel.elf",393216)]:
                size = (work/"bin"/name).stat().st_size
                assert 0 < size <= limit, f"{name}: {size} bytes exceeds its {limit}-byte slot"
            assert (work/"util/fonts/zap-light16.psf").stat().st_size <= 6144
        result = command(["make", "-j1", "buildimg", f"TARGET_BIOS={'true' if firmware=='bios' else 'false'}"],
                         cwd=work, env=env, timeout=120)
        (directory/"image-output.log").write_text(result.stdout)
        require_success(result, "package image")
        image = work/"bin/HackOS.img"
        if firmware == "bios":
            data = image.read_bytes()
            assert len(data) == (846+93750)*512, "BIOS image extent mismatch"
            for start, path in [(0,"bin/mbr.bin"),(1,"bin/bootloader.bin"),(2,"bin/second_stage.bin"),
                                (66,"util/fonts/zap-light16.psf"),(78,"bin/kernel.elf")]:
                payload = (work/path).read_bytes()
                assert data[start*512:start*512+len(payload)] == payload, f"wrong payload at LBA {start}"
            fs_image = str(image) + "@@" + str(846*512)
        else:
            fs_image = str(image)
            efi = (work/"boot/UEFI/gnu-efi/x86_64/bootloader/main.efi").read_bytes()
            assert efi[:2] == b"MZ", "UEFI payload lacks DOS header"
            pe = struct.unpack_from("<I", efi, 0x3c)[0]
            assert efi[pe:pe+4] == b"PE\0\0" and struct.unpack_from("<H",efi,pe+4)[0] == 0x8664
            for name in ("/kernel.elf", "/EFI/BOOT/BOOTX64.EFI", "/startup.nsh"):
                require_success(command(["mdir","-a","-i",fs_image,"::"+name]), "image file " + name)
        for name in ("/UTIL/FONTS/zap-light16.psf", "/USR/BIN/usertest.elf", "/USR/BIN/test", "/USR/BIN/shell", "/USR/BIN/login", "/SYSTEM/ACCT0.DAT", "/SYSTEM/ACCT1.DAT", "/USR/BIN/ring3", "/USR/BIN/input", "/USR/BIN/window", *["/USR/BIN/" + name for name in ("help", "clear", "terminal", "echo", "time", "sysinfo", "ps", "kill", "ls", "wd", "cat", "read", "grep", "wc", "head", "tail", "tee", "more", "sort", "uniq", "cut", "tr", "shutdown", "restart", "id", "whoami", "users", "groups", "useradd", "groupadd", "usermod", "runas", "passwd", "xxd", "mkdir", "rm", "mv", "edit", "chmod", "chown")]):
            require_success(command(["mdir","-a","-i",fs_image,"::"+name]), "image file " + name)
        shutil.copy2(image, directory/"HackOS.img")
        shutil.copy2(work/"kernel/bin/kernel.elf", directory/"kernel.elf")
        return "Payload formats, slot limits, image offsets, and packaged inputs checked."
    report.check("image", package)
    return report.finish()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", required=True, choices=("bios", "uefi"))
    sys.exit(build(parser.parse_args().firmware))

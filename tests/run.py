#!/usr/bin/env python3
"""Run real production code under host checks and a BIOS loader-chain probe."""
import argparse
import json
import os
from pathlib import Path
import platform
import shutil
import struct
import subprocess
import sys
import time
from common import ROOT, OUT, Report, command, require_success

SOURCES = [
    "kernel/src/lib/memory.cpp", "kernel/src/lib/string.cpp",
    "common/src/bitmap.cpp", "kernel/src/memory/memory_map.cpp",
    "kernel/src/drivers/screen/framebuffer.cpp", "kernel/src/fs/filesystem.cpp",
    "kernel/src/fs/vfs.cpp", "kernel/src/fs/fat32/fat32.cpp",
    "kernel/src/fs/fat32/fat32driver.cpp", "kernel/src/fs/storage/journal.cpp", "kernel/src/fs/permissions.cpp",
    "boot/BIOS/stage_2/src/libc/memory.c", "boot/BIOS/stage_2/src/libc/string.c",
    "libs/libc/src/string.cpp",
    "common/src/split.c", "kernel/src/proc/user/address_space.cpp", "kernel/src/proc/user/accounts.cpp", "kernel/src/proc/user/account_store.cpp", "kernel/src/security/crypto.cpp",
    *[f"tests/host/{s}.cpp" for s in ("adapters", "main", "library_tests", "graphics_tests", "fs_tests", "elf_tests", "bios_tests", "user_tests", "input_tests", "file_abi_provider", "file_abi_consumer", "syscall_tests", "split_tests", "user_address_tests", "argument_tests", "history_tests", "event_queue_tests", "mouse_packet_tests", "path_tests", "text_buffer_tests", "account_tests", "account_store_tests")],
]


def llvm_tool(name):
    configured = os.environ.get(name.upper().replace("-", "_"))
    if configured:
        return configured
    found = shutil.which(name)
    if not found and sys.platform == "darwin":
        r = command(["xcrun", "--find", name])
        if r.returncode == 0:
            found = r.stdout.strip()
    if not found:
        raise RuntimeError(f"{name} missing (install LLVM or set its environment variable)")
    return found


def compile_host(directory, sanitizers):
    compiler = os.environ.get("CXX", "clang++")
    triple = require_success(command([compiler, "-dumpmachine"]), "compiler target").strip()
    if not triple.startswith(("x86_64", "amd64")):
        raise RuntimeError("Host assembly requires x86-64; use the linux/amd64 test container or an x86-64 compiler/runtime.")
    log = []
    objects = []
    flags = ["-std=c++17", "-g", "-O0", "-fno-builtin", "-fno-omit-frame-pointer",
             "-fsanitize="+sanitizers, "-fno-sanitize-recover=all",
             "-fprofile-instr-generate", "-fcoverage-mapping"]
    probe = directory/"runtime-probe.cpp"
    probe.write_text("int main() { return 0; }\n")
    require_success(command([compiler,*flags,probe,"-o",directory/"runtime-probe"]),"compile sanitizer preflight")
    probe_env = os.environ.copy()
    probe_env.update(ASAN_OPTIONS="detect_leaks=0",LLVM_PROFILE_FILE=str(directory/"runtime-probe.profraw"))
    result = command([directory/"runtime-probe"],env=probe_env)
    (directory/"runtime-probe.log").write_text(result.stdout)
    require_success(result,"sanitizer runtime preflight (even an empty program must run)")
    # Compile the shared runtime independently with each consumer's symbol ABI.
    variants = [(source, None) for source in [*SOURCES, "tests/host/runtime.cpp"]]
    variants += [("common/src/"+name+".cpp", variant)
                 for variant in ("kernel", "user") for name in ("memory_ops", "string_ops")]
    for source, variant in variants:
        obj = directory / ((variant + "_" if variant else "") + source.replace("/", "_") + ".o")
        args = [compiler, *flags]
        if source.endswith(".c"):
            args = [arg for arg in args if arg!="-std=c++17"] + ["-x","c","-std=c11"]
        if not source.endswith("/runtime.cpp"):
            rename = "bios_rename.h" if source.startswith("boot/BIOS/") or source.endswith("/bios_tests.cpp") else (
                "user_rename.h" if variant == "user" or source.startswith("libs/libc/") or source.endswith("/user_tests.cpp") else "rename.h")
            args += ["-include", ROOT/"tests/host"/rename, "-I", ROOT/"kernel/src", "-I", ROOT/"tests/host", "-I", ROOT/"common/include", "-I", ROOT/"include"]
            if source.startswith("libs/libc/"):
                args += ["-I",ROOT/"libs/libc/src"]
        args += ["-c", ROOT/source, "-o", obj]
        r = command(args, timeout=120)
        log.append(" ".join(map(str,args))+"\n"+r.stdout)
        (directory/"compile.log").write_text("\n".join(log))
        require_success(r, f"compile {source}")
        objects.append(obj)
    assembly_sources = [(prefix,folder,s) for prefix,folder in [("","common/src/x86_64"),("hackos_user_","common/src/x86_64")]
                        for s in ("_memory","_string")]
    for prefix,folder,source in assembly_sources:
        obj = directory/(prefix+source+".o")
        args = ["nasm", "-f", "macho64" if sys.platform=="darwin" else "elf64"]
        symbol_prefix = ("_" if sys.platform=="darwin" else "") + prefix
        if symbol_prefix:
            args += ["--prefix", symbol_prefix]
        args += [ROOT/f"{folder}/{source}.asm", "-o", obj]
        require_success(command(args), f"assemble {source}")
        objects.append(obj)
    binary = directory/"host-tests"
    r = command([compiler, *flags, *objects, "-o", binary], timeout=120)
    (directory/"link.log").write_text(r.stdout)
    require_success(r, "host test link")
    (directory/"toolchain.txt").write_text(command([compiler,"--version"]).stdout+"\n"+triple)
    return binary


def fat_fixture(directory):
    image = directory/"fat-fixture.img"
    sys.path.insert(0, str(ROOT / "tools"))
    from storage_image import format_volume
    format_volume(image)
    require_success(command(["mmd","-i",image,"::/TESTS"]), "create fixture directory")
    for name, data in [("HELLO.TXT", b"Hello HackOS\n"), ("PATTERN.BIN",bytes((i*17+3)%256 for i in range(8193))),
                       ("Long file name.txt",b"Long name\n"),("EMPTY.TXT",b"")]:
        source = directory/name
        source.write_bytes(data)
        require_success(command(["mcopy","-i",image,source,f"::/TESTS/{name}"]), "copy fixture")
    require_success(command(["mmd", "-i", image, "::/SYSTEM"]), "create account fixture directory")
    source = directory / "accounts.dat"
    source.write_bytes(bytes(8192))
    for slot in (0, 1):
        require_success(command(["mcopy", "-i", image, source, f"::/SYSTEM/ACCT{slot}.DAT"]), "account fixture")
    sys.path.insert(0, str(ROOT / "tools"))
    from storage_image import provision
    provision(image, initialize=True)
    return image


def coverage(binary, directory):
    profiles = sorted((directory/"profiles").glob("*.profraw"))
    if not profiles:
        raise RuntimeError("No completed test profiles; coverage cannot be reported")
    profdata = directory/"coverage.profdata"
    require_success(command([llvm_tool("llvm-profdata"),"merge","-sparse",*profiles,"-o",profdata]),"merge coverage")
    args = [llvm_tool("llvm-cov"), "report", binary, f"-instr-profile={profdata}",
            "-ignore-filename-regex=(/tests/|/usr/|/Library/)"]
    summary = require_success(command(args),"coverage report")
    (directory/"coverage-summary.txt").write_text(summary)
    for format_, filename in [("text","coverage.json"),("lcov","coverage.lcov")]:
        exported = require_success(command([args[0],"export",*args[2:],f"-format={format_}"],timeout=120),"coverage export")
        (directory/filename).write_text(exported)
    return summary


def host(args):
    directory = OUT/("host" if args.sanitizers=="address,undefined" else "host-ubsan")
    # Reports and coverage always describe this invocation, including setup failures.
    shutil.rmtree(directory,ignore_errors=True)
    directory.mkdir(parents=True,exist_ok=True)
    report = Report("host",directory)
    (directory/"profiles").mkdir()
    try:
        binary = compile_host(directory,args.sanitizers)
        fixture = fat_fixture(directory)
        listing_env = os.environ.copy()
        listing_env["LLVM_PROFILE_FILE"] = str(directory/"listing.profraw")
        listing_env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        listing = require_success(command([binary,"--list"],env=listing_env),"list host cases").splitlines()
    except (RuntimeError,OSError,subprocess.TimeoutExpired) as exc:
        report.add("host_setup","error",str(exc))
        return report.finish()
    for name in listing:
        if args.case and args.case not in name:
            continue
        env = os.environ.copy()
        # Leak ownership is not covered by these hardware/allocator adapters.
        env.update(ASAN_OPTIONS="detect_leaks=0:halt_on_error=1", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace="+("1" if args.sanitizers=="address,undefined" else "0"),
                   LLVM_PROFILE_FILE=str(directory/"profiles"/(name+"-%p.profraw")),
                   HACKOS_FAT_IMAGE=str(fixture), HACKOS_FAT_RESULT=str(directory/(name+".img")))
        started = time.monotonic()
        try:
            result = command([binary,name],timeout=15,env=env)
            detail = f"exit={result.returncode}\n"+result.stdout
            ok = result.returncode==0
            persisted = {"fat_rewrite_and_flush":("HELLO.TXT","Changed data\n"),
                         "fat_create_and_flush":("NEW.TXT","Created data\n"),
                         "fat_long_name_create_and_flush":("Another long filename.txt","Long create\n"),
                         "fat_shrink_and_flush":("HELLO.TXT","Hello")}
            if ok and name in persisted:
                filename, expected = persisted[name]
                read = command(["mtype","-i",env["HACKOS_FAT_RESULT"],"::/TESTS/"+filename])
                ok = read.returncode==0 and read.stdout==expected
                detail += "\nIndependent mtype read:\n"+read.stdout
            if ok and name == "fat_checked_mutations_survive_remount":
                read = command(["mtype", "-i", env["HACKOS_FAT_RESULT"], "::/WORK/RESULT.TXT"])
                removed = command(["mdir", "-i", env["HACKOS_FAT_RESULT"], "::/Moved long file.txt"])
                empty = command(["mtype", "-i", env["HACKOS_FAT_RESULT"], "::/WORK/EMPTY"])
                ok = read.returncode == 0 and read.stdout == "Saved across a remount\n" and removed.returncode != 0 and empty.returncode == 0 and empty.stdout == ""
                detail += "\nIndependent mtools mutation checks:\n" + read.stdout + removed.stdout + empty.stdout
            if ok and name=="fat_delete_and_flush":
                read = command(["mdir","-i",env["HACKOS_FAT_RESULT"],"::/TESTS/HELLO.TXT"])
                intact = command(["mtype","-i",env["HACKOS_FAT_RESULT"],"::/TESTS/Long file name.txt"])
                ok = read.returncode!=0 and intact.returncode==0 and intact.stdout=="Long name\n"
                detail += "\nIndependent deletion and neighboring file check:\n"+read.stdout+intact.stdout
            report.add(name,"pass" if ok else "fail",detail,time.monotonic()-started)
        except subprocess.TimeoutExpired as exc:
            report.add(name,"fail",f"Test exceeded 15 seconds: {exc}",time.monotonic()-started)
    report.check("coverage_export",lambda:coverage(binary,directory))
    return report.finish()


def assemble_loaders(directory):
    directory.mkdir(parents=True,exist_ok=True)
    for component, output in [("mbr","mbr.bin"),("stage_1","bootloader.bin")]:
        require_success(command(["nasm","-f","bin","boot_loader.asm","-o",directory/output],
                                cwd=ROOT/"boot/BIOS"/component), f"assemble {component}")


def validate_elf(data):
    assert len(data)>=64,"ELF header truncated"
    assert data[:7]==b"\x7fELF\x02\x01\x01","not little-endian ELF64 v1"
    h=struct.unpack_from("<HHIQQQIHHHHHH",data,16)
    kind,machine,version,entry,phoff,_,_,ehsize,phsize,phnum,*_=h
    assert (kind,machine,version)==(2,62,1),"not x86-64 ET_EXEC v1"
    assert ehsize==64 and phsize==56,"unexpected ELF header sizes"
    assert phnum and phoff+phnum*phsize<=len(data),"program headers outside file"
    executable_entry=False
    for i in range(phnum):
        ptype,flags,offset,vaddr,paddr,filesz,memsz,align=struct.unpack_from("<IIQQQQQQ",data,phoff+i*phsize)
        if ptype==1:
            assert filesz<=memsz and offset+filesz<=len(data),"invalid load segment"
            executable_entry |= bool(flags&1 and vaddr<=entry<vaddr+memsz)
    assert executable_entry,"entry outside executable load segments"


def images(args):
    directory=OUT/"images"; report=Report("images",directory)
    try:
        assemble_loaders(directory)
    except (RuntimeError,OSError,subprocess.TimeoutExpired) as exc:
        report.add("assemble_loaders","error",str(exc)); return report.finish()
    for name in ("mbr.bin","bootloader.bin"):
        def sector(name=name):
            data=(directory/name).read_bytes()
            assert len(data)==512,f"{name}: {len(data)} bytes, expected 512"
            assert data[510:]==b"\x55\xaa",f"{name}: invalid signature"
        report.check(name.replace(".","_"),sector)
    def partitions():
        data=(directory/"mbr.bin").read_bytes()
        assert data[446]==0x80,"boot partition inactive"
        assert struct.unpack_from("<II",data,454)==(1,845),"boot partition extent mismatch"
        assert data[466]==0x0b,"data partition is not FAT32"
        assert struct.unpack_from("<II",data,470)==(846,93750),"data partition extent mismatch"
    report.check("bios_partition_contract",partitions)
    def font():
        data=(ROOT/"util/fonts/zap-light16.psf").read_bytes()
        assert data[:2]==b"\x36\x04","bad PSF1 magic"
        assert len(data)>=4+(512 if data[2]&1 else 256)*data[3],"glyph data truncated"
        assert len(data)<=12*512,"font exceeds BIOS slot"
    report.check("font_asset",font)
    report.check("usertest_elf",lambda:validate_elf((ROOT/"util/usertest/usertest.elf").read_bytes()))
    return report.finish()


def bios_chain(args):
    directory=OUT/"bios-chain"; report=Report("bios-chain",directory)
    def run():
        assemble_loaders(directory)
        probe=directory/"probe.bin"
        require_success(command(["nasm","-f","bin",ROOT/"tests/fixtures/bios_probe.asm","-o",probe]),"assemble probe")
        image=directory/"probe.img"
        with image.open("wb") as f:
            f.truncate((846+93750)*512)
            for offset,path in [(0,directory/"mbr.bin"),(512,directory/"bootloader.bin"),(1024,probe)]:
                f.seek(offset); f.write(path.read_bytes())
        result=command(["qemu-system-x86_64","-machine","q35","-accel","tcg","-cpu","qemu64,+rdrand",
                        "-m","128M","-display","none","-monitor","none","-serial","none","-net","none",
                        "-no-reboot","-snapshot","-drive",f"file={image},format=raw",
                        "-device","isa-debug-exit,iobase=0xf4,iosize=0x04"],timeout=30)
        assert result.returncode==33,f"Expected probe exit 33, got {result.returncode}\n{result.stdout}"
        return "Real MBR + stage 1 reached a test-only payload at 0x8000. This is not a kernel boot."
    report.check("bios_mbr_stage1_handoff",run)
    return report.finish()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("suite",choices=("host","images","bios-chain","all"))
    parser.add_argument("--case",help="host case substring (full run is required in CI)")
    parser.add_argument("--sanitizers",choices=("address,undefined","undefined"),default="address,undefined",
                        help="use undefined alone only where address sanitizers cannot run; CI always uses both")
    args=parser.parse_args()
    status=0
    for name,run in [("host",host),("images",images),("bios-chain",bios_chain)]:
        if args.suite in (name,"all"):
            status |= run(args)
    return status

if __name__=="__main__":
    sys.exit(main())

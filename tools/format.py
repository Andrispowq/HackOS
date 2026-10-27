#!/usr/bin/env python3
"""Check (or --write) project-owned C/C++ style without touching vendored code."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ROOTS = ("kernel/", "common/", "libs/", "include/", "tests/", "util/", "boot/BIOS/")
LOADER = "boot/UEFI/gnu-efi/bootloader/main.c"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="apply formatting instead of checking")
    args = parser.parse_args()
    formatter = os.environ.get("CLANG_FORMAT") or shutil.which("clang-format")
    if not formatter:
        parser.error("clang-format 18.1.8 is required; set CLANG_FORMAT to its executable")
    version = subprocess.check_output([formatter, "--version"], text=True)
    if "version 18.1.8" not in version:
        parser.error("use clang-format 18.1.8 for reproducible formatting")
    names = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=ROOT
    ).decode().split("\0")
    files = sorted({name for name in names if Path(name).suffix in (".c", ".cpp", ".h", ".hpp")
                    and (ROOT / name).is_file()
                    and (name.startswith(ROOTS) or name == LOADER)})
    modes = {name: (ROOT / name).stat().st_mode for name in files}
    try:
        flags = ["-i"] if args.write else ["--dry-run", "--Werror"]
        subprocess.run([formatter, *flags, *files], cwd=ROOT, check=True)
    finally:
        if args.write:
            for name, mode in modes.items():
                (ROOT / name).chmod(mode)
    print(f"{'Formatted' if args.write else 'Checked'} {len(files)} project-owned C/C++ files.")


if __name__ == "__main__":
    main()

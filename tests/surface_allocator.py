#!/usr/bin/env python3
"""Sanitize the production surface-page allocator using an explicit bitmap fixture."""
import os
from common import ROOT, OUT, Report, command, require_success


def test():
    directory = OUT / "surface-allocator"
    report = Report("surface-allocator", directory)

    def run():
        binary = directory / "surface-allocator"
        # Hardware halt instructions are the only substituted code; allocator
        # functions are compiled unchanged. This permits a native ARM sanitizer.
        source = (ROOT / "kernel/src/arch/x86_64/paging/page_frame_allocator.cpp").read_text()
        assert source.count('asm("cli; hlt")') == 1 and source.count('asm("hlt")') == 1
        source = source.replace('asm("cli; hlt")', '__builtin_trap()').replace('asm("hlt")', '__builtin_trap()')
        adapted = directory / "page_frame_allocator.cpp"
        adapted.write_text(source)
        result = command([
            os.environ.get("SURFACE_CXX", "g++"), "-std=c++17", "-g", "-O1",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
            "-I", ROOT / "kernel/src", "-I", ROOT / "common/include", "-I", ROOT / "include",
            ROOT / "tests/host/surface_allocator_standalone.cpp",
            "-I", ROOT / "kernel/src/arch/x86_64/paging", adapted,
            ROOT / "common/src/bitmap.cpp", "-o", binary])
        (directory / "compile.log").write_text(result.stdout)
        require_success(result, "compile surface allocator")
        result = command([binary])
        (directory / "run.log").write_text(result.stdout)
        require_success(result, "surface ownership, exhaustion and fragmentation")
        return "Real bitmap and allocator: bounds, exact exhaustion, rollback, fragmentation and release accounting."

    report.check("surface_pages", run)
    return report.finish()


if __name__ == "__main__":
    raise SystemExit(test())

#!/usr/bin/env python3
"""Run the production keyboard decoders and line-buffer host cases with native sanitizers."""
import os
from common import ROOT, OUT, Report, command, require_success


def test():
    directory = OUT / 'gui-keyboard'
    report = Report('gui-keyboard', directory)
    binary = directory / 'keyboard-tests'
    adapter = directory / 'adapter.cpp'
    adapter.write_text('#include "lib/stdint.h"\nuint8_t heap_init;\n')
    result = command([os.environ.get('GUI_CXX', 'g++'), '-std=c++17', '-g', '-O1',
                      '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                      '-I', ROOT / 'kernel/src', '-I', ROOT / 'include', '-I', ROOT / 'common/include',
                      ROOT / 'tests/host/input_tests.cpp', ROOT / 'tests/host/main.cpp',
                      ROOT / 'tests/host/runtime.cpp', adapter, '-o', binary])
    (directory / 'compile.log').write_text(result.stdout)
    require_success(result, 'compile keyboard checks')
    cases = require_success(command([binary, '--list']), 'list keyboard checks').splitlines()
    for case in cases:
        report.check(case, lambda case=case: require_success(command([binary, case]), case))
    return report.finish()


if __name__ == '__main__':
    raise SystemExit(test())

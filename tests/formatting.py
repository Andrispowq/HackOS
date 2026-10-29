#!/usr/bin/env python3
"""Sanitizer checks of the actual freestanding printf formatter."""
import os
from common import ROOT, OUT, Report, command, require_success


def test():
    directory = OUT / 'formatting'
    report = Report('formatting', directory)

    def run():
        compiler = os.environ.get('GUI_CXX', 'g++')
        flags = ['-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                 '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
        obj, binary = directory / 'stdio.o', directory / 'formatting-tests'
        result = command([compiler, *flags, '-I', ROOT / 'include', '-I', ROOT / 'common/include',
                          '-Dsnprintf=hackos_snprintf', '-Dvsnprintf=hackos_vsnprintf', '-Dprintf=hackos_printf',
                          '-c', ROOT / 'libs/libc/src/stdio.cpp', '-o', obj])
        (directory / 'compile.log').write_text(result.stdout)
        require_success(result, 'compile freestanding formatter')
        result = command([compiler, *flags, ROOT / 'tests/host/formatting_standalone.cpp', obj, '-o', binary])
        (directory / 'link.log').write_text(result.stdout)
        require_success(result, 'compile formatting checks')
        return require_success(command([binary], timeout=120), 'formatting sanitizer checks')

    report.check('padding_precision_and_bounds', run)
    return report.finish()


if __name__ == '__main__':
    raise SystemExit(test())

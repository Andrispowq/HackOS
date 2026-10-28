#!/usr/bin/env python3
"""Native sanitizer checks of the real userspace GUI core."""
import os
from common import ROOT, OUT, Report, command, require_success

def test():
    directory = OUT / 'gui-library'
    report = Report('gui-library', directory)
    def run():
        binary = directory / 'gui-tests'
        compile = command([os.environ.get('GUI_CXX', 'g++'), '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                           '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-I', ROOT / 'include',
                           ROOT / 'libs/gui/src/gui.cpp', ROOT / 'libs/gui/src/window.cpp',
                           ROOT / 'tests/host/gui_standalone.cpp', ROOT / 'tests/host/gui_platform.cpp', '-o', binary])
        (directory / 'compile.log').write_text(compile.stdout)
        require_success(compile, 'compile GUI checks')
        return require_success(command([binary], timeout=120), 'GUI sanitizer checks')
    report.check('drawing_layout_widgets_dialogs', run)
    return report.finish()

if __name__ == '__main__':
    raise SystemExit(test())

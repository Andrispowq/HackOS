#!/usr/bin/env python3
"""Native ASan/UBSan checks of renderer, console and bounded input queues."""
import os
from common import ROOT, OUT, Report, command, require_success


def test():
    directory = OUT / 'graphics-sanitizers'
    report = Report('graphics-sanitizers', directory)
    compiler = os.environ.get('GUI_CXX', 'g++')
    flags = ['-std=c++17', '-O1', '-g', '-fno-builtin', '-fno-omit-frame-pointer',
             '-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    sources = ['common/src/memory_ops.cpp', 'kernel/src/drivers/screen/framebuffer.cpp',
               'kernel/src/drivers/screen/console.cpp', 'tests/host/main.cpp',
               'tests/host/graphics_tests.cpp', 'tests/host/console_tests.cpp',
               'tests/host/event_queue_tests.cpp', 'tests/host/terminal_history_tests.cpp', 'tests/host/runtime.cpp']
    binary = directory / 'graphics-tests'

    def build():
        objects = []
        for source in sources:
            obj = directory / (source.replace('/', '_') + '.o')
            args = [compiler, *flags, '-I', ROOT / 'include', '-I', ROOT / 'common/include',
                    '-I', ROOT / 'kernel/src', '-I', ROOT / 'tests/host']
            if source != 'tests/host/runtime.cpp':
                args += ['-include', ROOT / 'tests/host/rename.h']
            result = command([*args, '-c', ROOT / source, '-o', obj])
            (directory / (obj.name + '.log')).write_text(result.stdout)
            require_success(result, 'compile ' + source)
            objects.append(obj)
        # Explicit adapters for the assembly byte primitives on non-x86 hosts.
        # The x86 host suite separately links the real assembly implementations.
        adapter = directory / 'adapter.cpp'
        adapter.write_text("""#include <cstring>
unsigned char heap_init = 1;
extern "C" void* __memcpy(void* d, const void* s, size_t n)
{
    return std::memcpy(d, s, n);
}
extern "C" void* __memset(void* d, int c, size_t n)
{
    return std::memset(d, c, n);
}
extern "C" int __memcmp(void* a, const void* b, size_t n)
{
    return std::memcmp(a, b, n);
}
""")
        require_success(command([compiler, *flags, adapter, *objects, '-o', binary]), 'link renderer checks')
        return 'Real renderer, console and queues; hosted adapters replace x86 byte-copy primitives and test I/O.'

    report.check('build', build)
    if any(result['status'] != 'pass' for result in report.results):
        return report.finish()
    names = require_success(command([binary, '--list']), 'list renderer checks').splitlines()
    for name in names:
        report.check(name, lambda name=name: require_success(command([binary, name], timeout=30), name))
    return report.finish()


if __name__ == '__main__':
    raise SystemExit(test())
